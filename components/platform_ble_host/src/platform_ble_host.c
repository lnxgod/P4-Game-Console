// SPDX-License-Identifier: MIT

#include "platform/ble_host.h"

#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#pragma GCC diagnostic pop

#include "platform/radio_hosted.h"

/* NimBLE's store/config implementation exports this without a public header
 * declaration in the ESP-IDF 5.5.3 lock used by this repository. */
void ble_store_config_init(void);

enum {
    PLATFORM_BLE_HOST_MAX_CLIENTS = 4,
    PLATFORM_BLE_START_STACK_BYTES = 8192,
    PLATFORM_BLE_START_PRIORITY = 5,
    PLATFORM_BLE_START_CORE = 1,
};

typedef struct {
    platform_ble_host_status_t status;
    platform_ble_host_client_t clients[PLATFORM_BLE_HOST_MAX_CLIENTS];
    bool start_in_progress;
} platform_ble_host_runtime_t;

static const char *const TAG = "p4_ble_host";
static const char *const DEVICE_NAME = "P4 CONSOLE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_ble_host_runtime_t s_host;

static void set_state(platform_ble_host_state_t state, int error)
{
    portENTER_CRITICAL(&s_lock);
    s_host.status.state = state;
    s_host.status.last_error = error;
    portEXIT_CRITICAL(&s_lock);
}

static uint8_t copy_clients(
    platform_ble_host_client_t clients[PLATFORM_BLE_HOST_MAX_CLIENTS])
{
    portENTER_CRITICAL(&s_lock);
    const uint8_t count = s_host.status.client_count;
    memcpy(clients, s_host.clients,
           (size_t)count * sizeof(s_host.clients[0]));
    portEXIT_CRITICAL(&s_lock);
    return count;
}

static void host_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset reason=%d", reason);
    portENTER_CRITICAL(&s_lock);
    s_host.status.synced = false;
    s_host.status.state = PLATFORM_BLE_HOST_ERROR;
    s_host.status.last_error = reason;
    portEXIT_CRITICAL(&s_lock);

    platform_ble_host_client_t clients[PLATFORM_BLE_HOST_MAX_CLIENTS];
    const uint8_t count = copy_clients(clients);
    for (uint8_t index = 0U; index < count; ++index) {
        if (clients[index].on_reset != NULL) {
            clients[index].on_reset(reason, clients[index].context);
        }
    }
}

static void host_sync(void)
{
    uint8_t own_addr_type = 0U;
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) {
        result = ble_hs_id_infer_auto(0, &own_addr_type);
    }
    if (result != 0) {
        host_reset(result);
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_host.status.own_addr_type = own_addr_type;
    s_host.status.synced = true;
    s_host.status.state = PLATFORM_BLE_HOST_READY;
    s_host.status.last_error = 0;
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGI(TAG, "BLE_HOST_READY clients=%u bonding=persistent",
             (unsigned)s_host.status.client_count);

    platform_ble_host_client_t clients[PLATFORM_BLE_HOST_MAX_CLIENTS];
    const uint8_t count = copy_clients(clients);
    for (uint8_t index = 0U; index < count; ++index) {
        if (clients[index].on_sync != NULL) {
            clients[index].on_sync(clients[index].context);
        }
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void start_task(void *argument)
{
    (void)argument;
    ESP_LOGI(TAG,
             "BLE_HOST_START dma_free=%u dma_largest=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    set_state(PLATFORM_BLE_HOST_STARTING_RADIO, 0);
    const esp_err_t radio_result = platform_radio_hosted_start();
    if (radio_result != ESP_OK) {
        set_state(PLATFORM_BLE_HOST_ERROR, (int)radio_result);
        portENTER_CRITICAL(&s_lock);
        s_host.start_in_progress = false;
        portEXIT_CRITICAL(&s_lock);
        vTaskDelete(NULL);
        return;
    }

    set_state(PLATFORM_BLE_HOST_STARTING_NIMBLE, 0);
    esp_err_t result = nimble_port_init();
    if (result == ESP_OK) {
        ble_hs_cfg.reset_cb = host_reset;
        ble_hs_cfg.sync_cb = host_sync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 1U;
        ble_hs_cfg.sm_mitm = 0U;
        ble_hs_cfg.sm_sc = 1U;
        ble_hs_cfg.sm_our_key_dist =
            BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist =
            BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

        ble_svc_gap_init();
        ble_svc_gatt_init();
        platform_ble_host_client_t clients[PLATFORM_BLE_HOST_MAX_CLIENTS];
        const uint8_t count = copy_clients(clients);
        int ble_result = 0;
        for (uint8_t index = 0U;
             index < count && ble_result == 0; ++index) {
            if (clients[index].services != NULL) {
                ble_result = ble_gatts_count_cfg(clients[index].services);
                if (ble_result == 0) {
                    ble_result = ble_gatts_add_svcs(clients[index].services);
                }
            }
        }
        if (ble_result == 0) {
            ble_result = ble_svc_gap_device_name_set(DEVICE_NAME);
        }
        if (ble_result == 0) {
            ble_store_config_init();
        } else {
            result = ESP_FAIL;
            set_state(PLATFORM_BLE_HOST_ERROR, ble_result);
        }
    }
    if (result == ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_host.status.initialized = true;
        portEXIT_CRITICAL(&s_lock);
        nimble_port_freertos_init(host_task);
    } else {
        set_state(PLATFORM_BLE_HOST_ERROR, (int)result);
    }
    portENTER_CRITICAL(&s_lock);
    s_host.start_in_progress = false;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

esp_err_t platform_ble_host_register_client(
    const platform_ble_host_client_t *client)
{
    if (client == NULL ||
        (client->services == NULL && client->on_sync == NULL &&
         client->on_reset == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    for (uint8_t index = 0U;
         index < s_host.status.client_count; ++index) {
        const platform_ble_host_client_t *const registered =
            &s_host.clients[index];
        if (registered->services == client->services &&
            registered->on_sync == client->on_sync &&
            registered->on_reset == client->on_reset &&
            registered->context == client->context) {
            portEXIT_CRITICAL(&s_lock);
            return ESP_OK;
        }
    }
    if (s_host.status.initialized || s_host.start_in_progress) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_host.status.client_count >= PLATFORM_BLE_HOST_MAX_CLIENTS) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_NO_MEM;
    }
    s_host.clients[s_host.status.client_count++] = *client;
    portEXIT_CRITICAL(&s_lock);
    return ESP_OK;
}

esp_err_t platform_ble_host_start(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool initialized = s_host.status.initialized;
    const bool starting = s_host.start_in_progress;
    if (!initialized && !starting) {
        s_host.start_in_progress = true;
        /* Publish retry progress before the task can be scheduled. Wi-Fi's
         * worker must not mistake the previous error for this new attempt. */
        s_host.status.state = PLATFORM_BLE_HOST_STARTING_RADIO;
        s_host.status.last_error = 0;
    }
    portEXIT_CRITICAL(&s_lock);
    if (initialized || starting) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCore(
            start_task, "p4_ble_start", PLATFORM_BLE_START_STACK_BYTES,
            NULL, PLATFORM_BLE_START_PRIORITY, NULL,
            PLATFORM_BLE_START_CORE) != pdPASS) {
        portENTER_CRITICAL(&s_lock);
        s_host.start_in_progress = false;
        portEXIT_CRITICAL(&s_lock);
        set_state(PLATFORM_BLE_HOST_ERROR, ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

platform_ble_host_status_t platform_ble_host_status(void)
{
    portENTER_CRITICAL(&s_lock);
    const platform_ble_host_status_t status = s_host.status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}

bool platform_ble_host_ready(void)
{
    const platform_ble_host_status_t status = platform_ble_host_status();
    return status.state == PLATFORM_BLE_HOST_READY && status.synced;
}

esp_err_t platform_ble_host_own_addr_type(uint8_t *own_addr_type)
{
    if (own_addr_type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const platform_ble_host_status_t status = platform_ble_host_status();
    if (status.state != PLATFORM_BLE_HOST_READY || !status.synced) {
        return ESP_ERR_INVALID_STATE;
    }
    *own_addr_type = status.own_addr_type;
    return ESP_OK;
}
