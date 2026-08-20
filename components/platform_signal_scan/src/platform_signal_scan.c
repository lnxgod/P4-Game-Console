// SPDX-License-Identifier: MIT

#include "platform/signal_scan.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#pragma GCC diagnostic pop
#include "p4/signal_scan.h"

enum {
    PLATFORM_SIGNAL_SCAN_RAW_RESULTS = 32,
    PLATFORM_SIGNAL_SCAN_TASK_STACK_BYTES = 12 * 1024,
    PLATFORM_SIGNAL_SCAN_TASK_PRIORITY = 4,
    PLATFORM_SIGNAL_SCAN_PASSIVE_CHANNEL_MS = 120,
};

static const char *const TAG = "p4_signal_scan";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_ready;
static bool s_scan_pending;
static bool s_identity_persistent;
static uint64_t s_focus_token;
static uint8_t s_session_key[P4_SIGNAL_SCAN_KEY_BYTES];
static p4_game_signal_snapshot_t s_snapshot = {
    .status = P4_GAME_SIGNAL_UNAVAILABLE,
};

static size_t bounded_ssid_length(const uint8_t ssid[33])
{
    size_t length = 0U;
    while (length < P4_SIGNAL_SCAN_SSID_MAX_BYTES && ssid[length] != 0U) {
        ++length;
    }
    return length;
}

static bool ssid_has_visible_name(const uint8_t *ssid, size_t ssid_bytes)
{
    if (ssid == NULL || ssid_bytes == 0U) {
        return false;
    }
    for (size_t index = 0U; index < ssid_bytes; ++index) {
        if (ssid[index] != (uint8_t)' ') {
            return true;
        }
    }
    return false;
}

static esp_err_t load_or_create_identity_key(void)
{
    static const char *const identity_namespace = "p4_signal";
    static const char *const identity_key = "token_key";
    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        identity_namespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    size_t key_bytes = sizeof(s_session_key);
    result = nvs_get_blob(
        handle, identity_key, s_session_key, &key_bytes);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        esp_fill_random(s_session_key, sizeof(s_session_key));
        result = nvs_set_blob(
            handle, identity_key, s_session_key, sizeof(s_session_key));
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
    } else if (result == ESP_OK && key_bytes != sizeof(s_session_key)) {
        result = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        s_identity_persistent = true;
    }
    return result;
}

static void publish_status(p4_game_signal_status_t status)
{
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_snapshot.status = status;
        if (s_snapshot.generation != UINT32_MAX) {
            ++s_snapshot.generation;
        }
        (void)xSemaphoreGive(s_lock);
    }
}

static void publish_results(const wifi_ap_record_t *records, uint16_t count,
                            uint64_t focus_token)
{
    p4_game_signal_snapshot_t next = {
        .status = P4_GAME_SIGNAL_READY,
    };
    p4_game_signal_t sanitized[PLATFORM_SIGNAL_SCAN_RAW_RESULTS];
    size_t sanitized_count = 0U;
    size_t hidden_filtered = 0U;
    const uint16_t raw_limit = (uint16_t)PLATFORM_SIGNAL_SCAN_RAW_RESULTS;
    const uint16_t bounded_count = count < raw_limit ? count : raw_limit;
    for (uint16_t index = 0U; index < bounded_count; ++index) {
        const wifi_ap_record_t *const record = &records[index];
        p4_game_signal_t signal;
        const size_t ssid_bytes = bounded_ssid_length(record->ssid);
        if (!ssid_has_visible_name(record->ssid, ssid_bytes)) {
            ++hidden_filtered;
            continue;
        }
        if (!p4_signal_scan_make_game_signal(
                s_session_key, record->bssid, record->ssid, ssid_bytes,
                record->rssi, record->primary,
                record->authmode != WIFI_AUTH_OPEN, &signal)) {
            continue;
        }
        size_t insert_at = sanitized_count;
        while (insert_at > 0U &&
               signal.rssi_dbm > sanitized[insert_at - 1U].rssi_dbm) {
            sanitized[insert_at] = sanitized[insert_at - 1U];
            --insert_at;
        }
        sanitized[insert_at] = signal;
        ++sanitized_count;
    }
    size_t focus_index = sanitized_count;
    for (size_t index = 0U; index < sanitized_count; ++index) {
        if (focus_token != 0U && sanitized[index].token == focus_token) {
            focus_index = index;
            next.results[next.count++] = sanitized[index];
            break;
        }
    }
    for (size_t index = 0U;
         index < sanitized_count && next.count < P4_GAME_SIGNAL_MAX_RESULTS;
         ++index) {
        if (index != focus_index) {
            next.results[next.count++] = sanitized[index];
        }
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        next.generation = s_snapshot.generation == UINT32_MAX
            ? UINT32_MAX : s_snapshot.generation + 1U;
        s_snapshot = next;
        s_scan_pending = false;
        (void)xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG,
             "passive scan complete raw=%u named=%u published=%u "
             "hidden_filtered=%u",
             (unsigned)bounded_count, (unsigned)sanitized_count,
             (unsigned)next.count, (unsigned)hidden_filtered);
}

static esp_err_t initialize_radio(void)
{
    esp_err_t result = esp_hosted_init();
    if (result != ESP_OK) {
        return result;
    }
    result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&wifi_config);
    if (result != ESP_OK) {
        return result;
    }
    esp_hosted_coprocessor_fwver_t coprocessor = {0};
    result = esp_hosted_get_coprocessor_fwversion(&coprocessor);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "C6 hosted firmware=%lu.%lu.%lu",
                 (unsigned long)coprocessor.major1,
                 (unsigned long)coprocessor.minor1,
                 (unsigned long)coprocessor.patch1);
    } else {
        ESP_LOGW(TAG, "C6 firmware identity unavailable: %s",
                 esp_err_to_name(result));
    }
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        return result;
    }
    return esp_wifi_start();
}

static void signal_scan_task(void *argument)
{
    (void)argument;
    const esp_err_t initialized = initialize_radio();
    if (initialized != ESP_OK) {
        ESP_LOGE(TAG, "C6 passive scanner unavailable: %s",
                 esp_err_to_name(initialized));
        publish_status(P4_GAME_SIGNAL_UNAVAILABLE);
        vTaskDelete(NULL);
        return;
    }
    if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
        s_ready = true;
        s_snapshot.status = P4_GAME_SIGNAL_IDLE;
        if (s_snapshot.generation != UINT32_MAX) {
            ++s_snapshot.generation;
        }
        (void)xSemaphoreGive(s_lock);
    }
    ESP_LOGI(TAG, "C6 passive scanner ready");
    ESP_LOGI(TAG, "opaque identity scope=%s raw_bssid_exposed=0",
             s_identity_persistent ? "device" : "boot-session");

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint64_t focus_token = 0U;
        if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            focus_token = s_focus_token;
            (void)xSemaphoreGive(s_lock);
        }
        wifi_scan_config_t scan_config = {
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_PASSIVE,
            .scan_time.passive = PLATFORM_SIGNAL_SCAN_PASSIVE_CHANNEL_MS,
        };
        wifi_ap_record_t records[PLATFORM_SIGNAL_SCAN_RAW_RESULTS];
        (void)memset(records, 0, sizeof(records));
        esp_err_t result = esp_wifi_scan_start(&scan_config, true);
        uint16_t count = PLATFORM_SIGNAL_SCAN_RAW_RESULTS;
        if (result == ESP_OK) {
            result = esp_wifi_scan_get_ap_records(&count, records);
        }
        if (result == ESP_OK) {
            publish_results(records, count, focus_token);
        } else {
            ESP_LOGW(TAG, "passive scan failed: %s", esp_err_to_name(result));
            if (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
                s_snapshot.status = P4_GAME_SIGNAL_ERROR;
                s_scan_pending = false;
                if (s_snapshot.generation != UINT32_MAX) {
                    ++s_snapshot.generation;
                }
                (void)xSemaphoreGive(s_lock);
            }
        }
        (void)memset(records, 0, sizeof(records));
    }
}

esp_err_t platform_signal_scan_start(void)
{
    if (s_task != NULL || s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t identity_result = load_or_create_identity_key();
    if (identity_result != ESP_OK) {
        esp_fill_random(s_session_key, sizeof(s_session_key));
        ESP_LOGW(TAG,
                 "persistent opaque identity unavailable: %s; "
                 "falling back to boot-session identity",
                 esp_err_to_name(identity_result));
    }
    const BaseType_t created = xTaskCreate(
        signal_scan_task, "p4_signal_scan",
        PLATFORM_SIGNAL_SCAN_TASK_STACK_BYTES, NULL,
        PLATFORM_SIGNAL_SCAN_TASK_PRIORITY, &s_task);
    if (created != pdPASS) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        (void)memset(s_session_key, 0, sizeof(s_session_key));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool platform_signal_scan_ready(void)
{
    bool ready = false;
    if (s_lock != NULL && xSemaphoreTake(s_lock, 0U) == pdTRUE) {
        ready = s_ready;
        (void)xSemaphoreGive(s_lock);
    }
    return ready;
}

bool platform_signal_scan_request(void *context, uint64_t focus_token)
{
    (void)context;
    if (s_lock == NULL || s_task == NULL ||
        xSemaphoreTake(s_lock, 0U) != pdTRUE) {
        return false;
    }
    const bool accepted = s_ready && !s_scan_pending;
    if (accepted) {
        s_scan_pending = true;
        s_focus_token = focus_token;
        s_snapshot.status = P4_GAME_SIGNAL_SCANNING;
        if (s_snapshot.generation != UINT32_MAX) {
            ++s_snapshot.generation;
        }
    }
    (void)xSemaphoreGive(s_lock);
    if (accepted) {
        xTaskNotifyGive(s_task);
    }
    return accepted;
}

bool platform_signal_scan_read(
    void *context, p4_game_signal_snapshot_t *snapshot)
{
    (void)context;
    if (snapshot == NULL || s_lock == NULL ||
        xSemaphoreTake(s_lock, 0U) != pdTRUE) {
        return false;
    }
    *snapshot = s_snapshot;
    (void)xSemaphoreGive(s_lock);
    return true;
}
