#include "platform_usb_host/platform_usb_host.h"

#include <stdbool.h>
#include <string.h>

#include "sdkconfig.h"
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
#include "driver/gpio.h"
#endif
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#define PLATFORM_USB_HOST_DAEMON_STACK_BYTES 4096U
#define PLATFORM_USB_HOST_DAEMON_PRIORITY 5U
#define PLATFORM_USB_HOST_HS_PERIPHERAL_MAP (1U << 0)
#define PLATFORM_USB_HOST_EVENT_ALL_FREE (1U << 0)
#define PLATFORM_USB_HOST_EVENT_DAEMON_STOPPED (1U << 1)

static const char *const TAG = "platform_usb_host";

static portMUX_TYPE s_model_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_usb_host_model_t s_model;
static bool s_model_initialized;
static bool s_stop_requested;
static EventGroupHandle_t s_events;
static TaskHandle_t s_daemon_task;

#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
enum {
    PLATFORM_USB_HOST_OLIMEX_HUB_RESET_GPIO = 21,
    PLATFORM_USB_HOST_OLIMEX_RESET_ASSERT_MS = 20,
    PLATFORM_USB_HOST_OLIMEX_ENUMERATION_DELAY_MS = 100,
};

static bool s_integrated_hub_gpio_configured;

static esp_err_t integrated_hub_hold_reset(void)
{
    const gpio_config_t configuration = {
        .pin_bit_mask = UINT64_C(1) <<
            PLATFORM_USB_HOST_OLIMEX_HUB_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_set_level(
        PLATFORM_USB_HOST_OLIMEX_HUB_RESET_GPIO, 0U);
    if (result == ESP_OK && !s_integrated_hub_gpio_configured) {
        result = gpio_config(&configuration);
        if (result == ESP_OK) {
            s_integrated_hub_gpio_configured = true;
        }
    }
    if (result == ESP_OK) {
        result = gpio_set_level(
            PLATFORM_USB_HOST_OLIMEX_HUB_RESET_GPIO, 0U);
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(
            PLATFORM_USB_HOST_OLIMEX_RESET_ASSERT_MS));
    }
    return result;
}

static esp_err_t integrated_hub_release_reset(void)
{
    if (!s_integrated_hub_gpio_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t result = gpio_set_level(
        PLATFORM_USB_HOST_OLIMEX_HUB_RESET_GPIO, 1U);
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(
            PLATFORM_USB_HOST_OLIMEX_ENUMERATION_DELAY_MS));
    }
    return result;
}
#endif

/*
 * Keep the complete post-authorization host implementation in build-only,
 * electrically inert images. A preprocessor `return false` here lets O3 and
 * section GC remove usb_host_install(), the daemon, and their USB API graph,
 * defeating the purpose of a production-shaped link proof.
 *
 * The byte is const flash data but volatile at the read site, so an out-of-line
 * check cannot be constant-folded. In the unauthorized configuration it is
 * zero and the expected record below is deliberately impossible to pass
 * platform_usb_fixture_evidence_validate(): zero current limit, false required
 * properties, and no SHA-256. Therefore the normal validation rejects the
 * record before this independent build gate, while the build gate itself also
 * remains false.
 */
#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B && \
    CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED
static const volatile uint8_t s_build_authorization_gate = 1U;
static const platform_usb_fixture_evidence_t s_build_expected_evidence = {
    .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
    .size = (uint16_t)sizeof(platform_usb_fixture_evidence_t),
    .current_limit_ma = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMIT_MA,
    .externally_powered_vbus = CONFIG_PLATFORM_USB_HOST_FIXTURE_EXTERNAL_VBUS,
    .current_limited = CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMITED,
    .backfeed_blocked = CONFIG_PLATFORM_USB_HOST_FIXTURE_BACKFEED_BLOCKED,
    .common_ground = CONFIG_PLATFORM_USB_HOST_FIXTURE_COMMON_GROUND,
    .data_pair_direct = CONFIG_PLATFORM_USB_HOST_FIXTURE_DATA_PAIR_DIRECT,
    .source_role_compliant =
        CONFIG_PLATFORM_USB_HOST_FIXTURE_SOURCE_ROLE_COMPLIANT,
    .overcurrent_fault_visible =
        CONFIG_PLATFORM_USB_HOST_FIXTURE_FAULT_VISIBLE,
    .board_path_reviewed = CONFIG_PLATFORM_USB_HOST_BOARD_PATH_REVIEWED,
    .evidence_id = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_ID,
    .evidence_sha256 = CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_SHA256,
};
#elif !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static const volatile uint8_t s_build_authorization_gate = 0U;
static const platform_usb_fixture_evidence_t s_build_expected_evidence = {
    .version = PLATFORM_USB_FIXTURE_EVIDENCE_VERSION,
    .size = (uint16_t)sizeof(platform_usb_fixture_evidence_t),
    .current_limit_ma = 0U,
    .externally_powered_vbus = false,
    .current_limited = false,
    .backfeed_blocked = false,
    .common_ground = false,
    .data_pair_direct = false,
    .source_role_compliant = false,
    .overcurrent_fault_visible = false,
    .board_path_reviewed = false,
    .evidence_id = "UNAUTHORIZED",
    .evidence_sha256 = "",
};
#endif

#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static bool __attribute__((noinline)) build_authorization_enabled(void)
{
    return s_build_authorization_gate == 1U;
}
#endif

static void ensure_model_initialized_locked(void)
{
    if (!s_model_initialized) {
        platform_usb_host_model_init(&s_model);
        s_model_initialized = true;
    }
}

static esp_err_t status_to_esp(platform_usb_status_t status)
{
    switch (status) {
    case PLATFORM_USB_STATUS_OK:
        return ESP_OK;
    case PLATFORM_USB_STATUS_INVALID_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case PLATFORM_USB_STATUS_LIMIT_EXCEEDED:
        return ESP_ERR_NO_MEM;
    case PLATFORM_USB_STATUS_FIXTURE_REQUIRED:
    case PLATFORM_USB_STATUS_INVALID_STATE:
    case PLATFORM_USB_STATUS_CLASS_BUSY:
    case PLATFORM_USB_STATUS_STALE_LEASE:
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static bool daemon_stop_requested(void)
{
    bool requested;
    taskENTER_CRITICAL(&s_model_lock);
    requested = s_stop_requested;
    taskEXIT_CRITICAL(&s_model_lock);
    return requested;
}

static void usb_daemon_task(void *argument)
{
    (void)argument;
    while (!daemon_stop_requested()) {
        uint32_t event_flags = 0;
        const esp_err_t result =
            usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0U) {
            xEventGroupSetBits(s_events, PLATFORM_USB_HOST_EVENT_ALL_FREE);
        }
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT &&
            result != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "USB_DAEMON_ERROR code=%s", esp_err_to_name(result));
        }
    }

    xEventGroupSetBits(s_events, PLATFORM_USB_HOST_EVENT_DAEMON_STOPPED);
    vTaskDelete(NULL);
}

static void request_daemon_stop(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    s_stop_requested = true;
    taskEXIT_CRITICAL(&s_model_lock);
    (void)usb_host_lib_unblock();
}

static bool wait_for_bits(EventBits_t bits, TickType_t timeout_ticks)
{
    const EventBits_t observed =
        xEventGroupWaitBits(s_events, bits, pdFALSE, pdTRUE, timeout_ticks);
    return (observed & bits) == bits;
}

static void complete_start(bool success)
{
    taskENTER_CRITICAL(&s_model_lock);
    (void)platform_usb_host_model_complete_start(&s_model, success);
    taskEXIT_CRITICAL(&s_model_lock);
}

static platform_usb_status_t complete_root_port_enable(bool success)
{
    platform_usb_status_t status;
    taskENTER_CRITICAL(&s_model_lock);
    status = platform_usb_host_model_complete_root_port_enable(&s_model,
                                                               success);
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

#if !CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
static bool fixture_matches_build_authorization(
    const platform_usb_fixture_evidence_t *evidence)
{
    return build_authorization_enabled() && evidence != NULL &&
           strcmp(evidence->evidence_id,
                  s_build_expected_evidence.evidence_id) == 0 &&
           strcmp(evidence->evidence_sha256,
                  s_build_expected_evidence.evidence_sha256) == 0 &&
           evidence->current_limit_ma ==
               s_build_expected_evidence.current_limit_ma &&
           evidence->externally_powered_vbus ==
               s_build_expected_evidence.externally_powered_vbus &&
           evidence->current_limited ==
               s_build_expected_evidence.current_limited &&
           evidence->backfeed_blocked ==
               s_build_expected_evidence.backfeed_blocked &&
           evidence->common_ground ==
               s_build_expected_evidence.common_ground &&
           evidence->data_pair_direct ==
               s_build_expected_evidence.data_pair_direct &&
           evidence->source_role_compliant ==
               s_build_expected_evidence.source_role_compliant &&
           evidence->overcurrent_fault_visible ==
               s_build_expected_evidence.overcurrent_fault_visible &&
           evidence->board_path_reviewed ==
               s_build_expected_evidence.board_path_reviewed;
}
#endif

esp_err_t platform_usb_host_start(
    const platform_usb_fixture_evidence_t *fixture_evidence)
{
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    if (fixture_evidence != NULL) {
        ESP_LOGE(TAG,
                 "USB_HOST_BLOCKED reason=integrated-hub-requires-null-fixture");
        return ESP_ERR_INVALID_ARG;
    }
#else
    const platform_usb_status_t fixture_status =
        platform_usb_fixture_evidence_validate(fixture_evidence);
    if (fixture_status != PLATFORM_USB_STATUS_OK) {
        ESP_LOGE(TAG, "USB_HOST_BLOCKED reason=%s",
                 platform_usb_status_name(fixture_status));
        return status_to_esp(fixture_status);
    }
    if (!fixture_matches_build_authorization(fixture_evidence)) {
        ESP_LOGE(TAG,
                 "USB_HOST_BLOCKED reason=build-authorization-or-evidence-mismatch");
        return ESP_ERR_INVALID_STATE;
    }
#endif

    platform_usb_status_t model_status;
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    model_status =
        platform_usb_host_model_begin_integrated_start(&s_model);
#else
    model_status =
        platform_usb_host_model_begin_start(&s_model, fixture_evidence);
#endif
    taskEXIT_CRITICAL(&s_model_lock);
    if (model_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(model_status);
    }

    esp_err_t result = ESP_OK;
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    result = integrated_hub_hold_reset();
    if (result != ESP_OK) {
        complete_start(false);
        ESP_LOGE(TAG, "USB_HOST_START_FAILED stage=hub-reset error=%s",
                 esp_err_to_name(result));
        return result;
    }
#endif

    s_events = xEventGroupCreate();
    if (s_events == NULL) {
        complete_start(false);
        return ESP_ERR_NO_MEM;
    }

    taskENTER_CRITICAL(&s_model_lock);
    s_stop_requested = false;
    taskEXIT_CRITICAL(&s_model_lock);

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = true,
        .intr_flags = 0,
        .enum_filter_cb = NULL,
        .fifo_settings_custom = {0},
        .peripheral_map = PLATFORM_USB_HOST_HS_PERIPHERAL_MAP,
    };
    result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        vEventGroupDelete(s_events);
        s_events = NULL;
        complete_start(false);
        return result;
    }

    if (xTaskCreate(usb_daemon_task, "p4_usb_daemon",
                    PLATFORM_USB_HOST_DAEMON_STACK_BYTES, NULL,
                    PLATFORM_USB_HOST_DAEMON_PRIORITY, &s_daemon_task) != pdPASS) {
        const esp_err_t uninstall_result = usb_host_uninstall();
        if (uninstall_result == ESP_OK) {
            vEventGroupDelete(s_events);
            s_events = NULL;
            complete_start(false);
            return ESP_ERR_NO_MEM;
        }
        mark_fault();
        ESP_LOGE(TAG,
                 "USB_HOST_FAULT stage=task-create rollback=%s resources=retained",
                 esp_err_to_name(uninstall_result));
        return uninstall_result;
    }

    complete_start(true);
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    ESP_LOGI(TAG,
             "USB_HOST_READY controller=p4-hs peripheral=0 "
             "root_port_enabled=0 topology=olimex-fe1.1s-powered-hub "
             "hub_reset_gpio=21 hub_reset=asserted");
#else
    ESP_LOGI(TAG,
             "USB_HOST_READY controller=p4-hs peripheral=0 root_port_enabled=0 "
             "fixture=%s limit_ma=%u",
             fixture_evidence->evidence_id,
             (unsigned)fixture_evidence->current_limit_ma);
#endif
    return ESP_OK;
}

esp_err_t platform_usb_host_enable_root_port(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t model_status =
        platform_usb_host_model_begin_root_port_enable(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (model_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(model_status);
    }

    esp_err_t result = usb_host_lib_set_root_port_power(true);
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    if (result == ESP_OK) {
        result = integrated_hub_release_reset();
        if (result != ESP_OK) {
            const esp_err_t rollback =
                usb_host_lib_set_root_port_power(false);
            if (rollback != ESP_OK) {
                mark_fault();
                ESP_LOGE(TAG,
                         "USB_HOST_FAULT stage=hub-release-rollback "
                         "release=%s rollback=%s resources=retained",
                         esp_err_to_name(result), esp_err_to_name(rollback));
                return rollback;
            }
        }
    }
#endif
    const platform_usb_status_t completion_status =
        complete_root_port_enable(result == ESP_OK);
    if (completion_status != PLATFORM_USB_STATUS_OK) {
        mark_fault();
        ESP_LOGE(TAG,
                 "USB_HOST_FAULT stage=root-port-enable-model "
                 "resources=retained");
        return status_to_esp(completion_status);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "USB_HOST_ROOT_PORT_ENABLE_FAILED code=%s "
                 "root_port_enabled=0",
                 esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG,
             "USB_HOST_ROOT_PORT_ENABLED class_leases_present=1 "
             "integrated_hub=%u",
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
             1U
#else
             0U
#endif
    );
    return ESP_OK;
}

esp_err_t platform_usb_host_quiesce(void)
{
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    const platform_usb_status_t model_status =
        platform_usb_host_model_begin_quiesce(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (model_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(model_status);
    }

    esp_err_t result = ESP_OK;
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
    result = integrated_hub_hold_reset();
#endif
    const esp_err_t root_result =
        usb_host_lib_set_root_port_power(false);
    if (result == ESP_OK) {
        result = root_result;
    }
    const platform_usb_status_t completion_status =
        complete_quiesce(result == ESP_OK);
    if (completion_status != PLATFORM_USB_STATUS_OK) {
        mark_fault();
        ESP_LOGE(TAG,
                 "USB_HOST_FAULT stage=quiesce-model resources=retained");
        return status_to_esp(completion_status);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "USB_HOST_FAULT stage=quiesce-root-power code=%s resources=retained",
                 esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "USB_HOST_QUIESCING root_port_enabled=0");
    return ESP_OK;
}

esp_err_t platform_usb_host_stop(TickType_t timeout_ticks)
{
    if (timeout_ticks == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    bool quiesce_required = false;
    platform_usb_status_t model_status = PLATFORM_USB_STATUS_OK;
    taskENTER_CRITICAL(&s_model_lock);
    ensure_model_initialized_locked();
    if (s_model.state == PLATFORM_USB_HOST_READY ||
        s_model.state == PLATFORM_USB_HOST_RUNNING) {
        if (s_model.lease_mask != 0U) {
            model_status = PLATFORM_USB_STATUS_CLASS_BUSY;
        } else {
            model_status = platform_usb_host_model_begin_quiesce(&s_model);
            quiesce_required = model_status == PLATFORM_USB_STATUS_OK;
        }
    } else if (s_model.state != PLATFORM_USB_HOST_QUIESCING) {
        model_status = PLATFORM_USB_STATUS_INVALID_STATE;
    }
    taskEXIT_CRITICAL(&s_model_lock);
    if (model_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(model_status);
    }

    if (quiesce_required) {
        esp_err_t power_result = ESP_OK;
#if CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B
        power_result = integrated_hub_hold_reset();
#endif
        const esp_err_t root_result =
            usb_host_lib_set_root_port_power(false);
        if (power_result == ESP_OK) {
            power_result = root_result;
        }
        const platform_usb_status_t completion_status =
            complete_quiesce(power_result == ESP_OK);
        if (completion_status != PLATFORM_USB_STATUS_OK) {
            mark_fault();
            ESP_LOGE(TAG,
                     "USB_HOST_FAULT stage=stop-quiesce-model "
                     "resources=retained");
            return status_to_esp(completion_status);
        }
        if (power_result != ESP_OK) {
            ESP_LOGE(TAG,
                     "USB_HOST_FAULT stage=stop-root-power code=%s resources=retained",
                     esp_err_to_name(power_result));
            return power_result;
        }
    }

    taskENTER_CRITICAL(&s_model_lock);
    model_status = platform_usb_host_model_begin_stop(&s_model);
    taskEXIT_CRITICAL(&s_model_lock);
    if (model_status != PLATFORM_USB_STATUS_OK) {
        return status_to_esp(model_status);
    }

    esp_err_t first_error = ESP_OK;
    const esp_err_t free_result = usb_host_device_free_all();
    if (free_result == ESP_ERR_NOT_FINISHED) {
        if (!wait_for_bits(PLATFORM_USB_HOST_EVENT_ALL_FREE, timeout_ticks) &&
            first_error == ESP_OK) {
            first_error = ESP_ERR_TIMEOUT;
        }
    } else if (free_result != ESP_OK && first_error == ESP_OK) {
        first_error = free_result;
    }

    request_daemon_stop();
    const bool daemon_stopped = wait_for_bits(
        PLATFORM_USB_HOST_EVENT_DAEMON_STOPPED, timeout_ticks);
    if (!daemon_stopped && first_error == ESP_OK) {
        first_error = ESP_ERR_TIMEOUT;
    }
    if (daemon_stopped) {
        s_daemon_task = NULL;
    }

    if (first_error == ESP_OK && daemon_stopped) {
        first_error = usb_host_uninstall();
    }

    if (first_error == ESP_OK) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }

    taskENTER_CRITICAL(&s_model_lock);
    (void)platform_usb_host_model_complete_stop(&s_model,
                                                first_error == ESP_OK);
    taskEXIT_CRITICAL(&s_model_lock);

    if (first_error == ESP_OK) {
        ESP_LOGI(TAG, "USB_HOST_STOPPED");
    } else {
        ESP_LOGE(TAG, "USB_HOST_STOP_FAILED code=%s",
                 esp_err_to_name(first_error));
    }
    return first_error;
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
