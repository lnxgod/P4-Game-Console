// SPDX-License-Identifier: MIT

#include "platform_usb_host/platform_usb_host.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "tusb.h"

enum {
    PLATFORM_TUH_RHPORT = 1,
    PLATFORM_TUH_TASK_CORE = 1,
    PLATFORM_TUH_TASK_STACK_BYTES = 12 * 1024,
    PLATFORM_TUH_TASK_PRIORITY = 8,
    PLATFORM_TUH_EVENT_READY = 1U << 0,
    PLATFORM_TUH_EVENT_FAILED = 1U << 1,
    PLATFORM_TUH_EVENT_STOPPED = 1U << 2,
    PLATFORM_TUH_POLL_TIMEOUT_MS = 20,
};

static const char *const TAG = "platform_usb_host";
static portMUX_TYPE s_model_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_usb_host_model_t s_model;
static bool s_model_initialized;
static volatile bool s_stop_requested;
static EventGroupHandle_t s_events;
static TaskHandle_t s_host_task;
static usb_phy_handle_t s_phy;
static bool s_tinyusb_initialized;
static esp_err_t s_start_result;
static esp_err_t s_stop_result;

static esp_err_t status_to_esp(platform_usb_status_t status)
{
    switch (status) {
    case PLATFORM_USB_STATUS_OK:
        return ESP_OK;
    case PLATFORM_USB_STATUS_INVALID_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case PLATFORM_USB_STATUS_CLASS_BUSY:
        return ESP_ERR_INVALID_STATE;
    case PLATFORM_USB_STATUS_STALE_LEASE:
        return ESP_ERR_INVALID_STATE;
    case PLATFORM_USB_STATUS_FIXTURE_REQUIRED:
        return ESP_ERR_NOT_ALLOWED;
    case PLATFORM_USB_STATUS_LIMIT_EXCEEDED:
        return ESP_ERR_INVALID_SIZE;
    case PLATFORM_USB_STATUS_INVALID_STATE:
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static void ensure_model_initialized_locked(void)
{
    if (!s_model_initialized) {
        platform_usb_host_model_init(&s_model);
        s_model_initialized = true;
    }
}

static platform_usb_status_t complete_start(bool success)
{
    platform_usb_status_t status;
    taskENTER_CRITICAL(&s_model_lock);
    status = platform_usb_host_model_complete_start(&s_model, success);
    taskEXIT_CRITICAL(&s_model_lock);
    return status;
}

static platform_usb_status_t complete_root_enable(bool success)
{
    platform_usb_status_t status;
    taskENTER_CRITICAL(&s_model_lock);
    status = platform_usb_host_model_complete_root_port_enable(
        &s_model, success);
    taskEXIT_CRITICAL(&s_model_lock);
    return status;
}

static platform_usb_status_t complete_quiesce(bool success)
{
    platform_usb_status_t status;
    taskENTER_CRITICAL(&s_model_lock);
    status = platform_usb_host_model_complete_quiesce(&s_model, success);
    taskEXIT_CRITICAL(&s_model_lock);
    return status;
}

static void mark_fault(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    (void)platform_usb_host_model_mark_fault(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
}

static void tinyusb_host_task(void *argument)
{
    (void)argument;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_HOST,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,
    };
    s_start_result = usb_new_phy(&phy_config, &s_phy);
    if (s_start_result == ESP_OK) {
        const tusb_rhport_init_t host_init = {
            .role = TUSB_ROLE_HOST,
            .speed = TUSB_SPEED_AUTO,
        };
        if (!tusb_init(PLATFORM_TUH_RHPORT, &host_init)) {
            s_start_result = ESP_FAIL;
        } else {
            s_tinyusb_initialized = true;
        }
    }
    if (s_start_result != ESP_OK) {
        s_stop_result = ESP_OK;
        if (s_tinyusb_initialized) {
            if (!tusb_deinit(PLATFORM_TUH_RHPORT)) {
                s_stop_result = ESP_FAIL;
            } else {
                s_tinyusb_initialized = false;
            }
        }
        if (s_stop_result == ESP_OK && s_phy != NULL) {
            s_stop_result = usb_del_phy(s_phy);
            if (s_stop_result == ESP_OK) {
                s_phy = NULL;
            }
        }
        xEventGroupSetBits(
            s_events, PLATFORM_TUH_EVENT_FAILED | PLATFORM_TUH_EVENT_STOPPED);
        vTaskDelete(NULL);
        return;
    }
    xEventGroupSetBits(s_events, PLATFORM_TUH_EVENT_READY);

    while (!s_stop_requested) {
        tuh_task_ext(PLATFORM_TUH_POLL_TIMEOUT_MS, false);
        /* A periodic HID endpoint can keep TinyUSB's queue continuously
         * populated. A one-tick block guarantees idle/watchdog service. */
        vTaskDelay(1U);
    }
    s_stop_result = ESP_OK;
    if (s_tinyusb_initialized) {
        if (!tusb_deinit(PLATFORM_TUH_RHPORT)) {
            s_stop_result = ESP_FAIL;
        } else {
            s_tinyusb_initialized = false;
        }
    }
    if (s_stop_result == ESP_OK && s_phy != NULL) {
        s_stop_result = usb_del_phy(s_phy);
        if (s_stop_result == ESP_OK) {
            s_phy = NULL;
        }
    }
    xEventGroupSetBits(s_events, PLATFORM_TUH_EVENT_STOPPED);
    vTaskDelete(NULL);
}

esp_err_t platform_usb_host_start(
    const platform_usb_fixture_evidence_t *fixture_evidence)
{
    if (fixture_evidence != NULL) {
        ESP_LOGE(TAG,
                 "USB_HOST_BLOCKED reason=waveshare-powered-test-requires-null-fixture");
        return ESP_ERR_INVALID_ARG;
    }

    platform_usb_status_t status;
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    status = platform_usb_host_model_begin_integrated_start(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(status);
    }

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        (void)complete_start(false);
        return ESP_ERR_NO_MEM;
    }
    s_stop_requested = false;
    s_host_task = NULL;
    s_phy = NULL;
    s_tinyusb_initialized = false;
    s_start_result = ESP_FAIL;
    s_stop_result = ESP_OK;
    if (complete_start(true) != PLATFORM_USB_STATUS_OK) {
        vEventGroupDelete(s_events);
        s_events = NULL;
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "USB_HOST_READY controller=p4-hs stack=tinyusb root_speed=high "
             "root_port_enabled=0 topology=waveshare-h2-powered-hub "
             "physical_vbus_source=external firmware_vbus_source=0");
    return ESP_OK;
}

esp_err_t platform_usb_host_enable_root_port(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t begin_status =
        platform_usb_host_model_begin_root_port_enable(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (begin_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(begin_status);
    }

    xEventGroupClearBits(s_events,
                         PLATFORM_TUH_EVENT_READY |
                         PLATFORM_TUH_EVENT_FAILED |
                         PLATFORM_TUH_EVENT_STOPPED);
    s_stop_requested = false;
    s_start_result = ESP_FAIL;
    s_stop_result = ESP_OK;
    esp_err_t result = ESP_OK;
    const BaseType_t created = xTaskCreatePinnedToCore(
        tinyusb_host_task, "p4_tinyusb_host",
        PLATFORM_TUH_TASK_STACK_BYTES, NULL,
        PLATFORM_TUH_TASK_PRIORITY, &s_host_task,
        PLATFORM_TUH_TASK_CORE);
    if (created != pdPASS) {
        result = ESP_ERR_NO_MEM;
    } else {
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, PLATFORM_TUH_EVENT_READY | PLATFORM_TUH_EVENT_FAILED,
            pdFALSE, pdFALSE, pdMS_TO_TICKS(2000U));
        if ((bits & PLATFORM_TUH_EVENT_READY) != 0U) {
            result = ESP_OK;
        } else if ((bits & PLATFORM_TUH_EVENT_FAILED) != 0U) {
            result = s_start_result;
            s_host_task = NULL;
        } else {
            s_stop_requested = true;
            result = ESP_ERR_TIMEOUT;
        }
    }

    if (result != ESP_OK) {
        if (s_host_task != NULL) {
            const EventBits_t stopped = xEventGroupWaitBits(
                s_events, PLATFORM_TUH_EVENT_STOPPED, pdFALSE, pdTRUE,
                pdMS_TO_TICKS(1000U));
            if ((stopped & PLATFORM_TUH_EVENT_STOPPED) != 0U) {
                s_host_task = NULL;
            }
        }
    }

    const platform_usb_status_t completion =
        complete_root_enable(result == ESP_OK);
    if (completion != PLATFORM_USB_STATUS_OK) {
        mark_fault();
        return status_to_esp(completion);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "USB_HOST_ROOT_PORT_ENABLE_FAILED code=%s",
                 esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG,
             "USB_HOST_ROOT_PORT_ENABLED stack=tinyusb root_speed=high "
             "hub_tt=enabled class_leases_present=1");
    return ESP_OK;
}

esp_err_t platform_usb_host_quiesce(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t begin_status =
        platform_usb_host_model_begin_quiesce(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (begin_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(begin_status);
    }

    esp_err_t result = ESP_OK;
    s_stop_requested = true;
    if (s_host_task != NULL) {
        const EventBits_t bits = xEventGroupWaitBits(
            s_events, PLATFORM_TUH_EVENT_STOPPED, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(1000U));
        if ((bits & PLATFORM_TUH_EVENT_STOPPED) == 0U) {
            result = ESP_ERR_TIMEOUT;
        } else {
            s_host_task = NULL;
        }
    }
    if (result == ESP_OK) {
        result = s_stop_result;
    }

    const platform_usb_status_t completion =
        complete_quiesce(result == ESP_OK);
    if (completion != PLATFORM_USB_STATUS_OK) {
        mark_fault();
        return status_to_esp(completion);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "USB_HOST_FAULT stage=tinyusb-quiesce error=%s resources=retained",
                 esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG,
             "USB_HOST_QUIESCING root_port_enabled=0 input=neutral "
             "firmware_vbus_source=0");
    return ESP_OK;
}

esp_err_t platform_usb_host_stop(TickType_t timeout_ticks)
{
    if (timeout_ticks == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    platform_usb_host_info_t info;
    esp_err_t result = platform_usb_host_get_info(&info);
    if (result != ESP_OK) {
        return result;
    }
    if (info.state == PLATFORM_USB_HOST_READY) {
        result = platform_usb_host_quiesce();
        if (result != ESP_OK) {
            return result;
        }
    }

    taskENTER_CRITICAL(&s_model_lock);
    const platform_usb_status_t begin_status =
        platform_usb_host_model_begin_stop(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (begin_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(begin_status);
    }

    if (s_events != NULL) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    taskENTER_CRITICAL(&s_model_lock);
    const platform_usb_status_t completion =
        platform_usb_host_model_complete_stop(&s_model, true);
    taskEXIT_CRITICAL(&s_model_lock);
    if (completion != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(completion);
    }
    ESP_LOGI(TAG, "USB_HOST_STOPPED stack=tinyusb");
    return ESP_OK;
}

esp_err_t platform_usb_host_class_acquire(platform_usb_class_t class_id,
                                          platform_usb_class_lease_t *lease)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t status =
        platform_usb_host_model_acquire(&s_model, class_id, lease);
    taskEXIT_CRITICAL(&s_model_lock);
    return status_to_esp(status);
}

esp_err_t platform_usb_host_class_release(platform_usb_class_lease_t *lease)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t status =
        platform_usb_host_model_release(&s_model, lease);
    taskEXIT_CRITICAL(&s_model_lock);
    return status_to_esp(status);
}

esp_err_t platform_usb_host_get_info(platform_usb_host_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    *info = (platform_usb_host_info_t){
        .generation = s_model.generation,
        .class_lease_mask = s_model.lease_mask,
        .state = (platform_usb_host_state_t)s_model.state,
        .root_port_enabled = s_model.root_port_enabled != 0U,
    };
    taskEXIT_CRITICAL(&s_model_lock);
    return ESP_OK;
}
