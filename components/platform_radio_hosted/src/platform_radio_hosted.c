// SPDX-License-Identifier: MIT

#include "platform/radio_hosted.h"
#include "sdkconfig.h"
#if CONFIG_P4_BOARD_M5STACK_TAB5
#include "platform/tab5.h"
#endif

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_hosted.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

static const char *const TAG = "p4_hosted_radio";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_radio_hosted_status_t s_status = {
    .state = PLATFORM_RADIO_HOSTED_OFF,
    .last_error = ESP_OK,
};

esp_err_t platform_radio_hosted_start(void)
{
    portENTER_CRITICAL(&s_lock);
    if (s_status.state == PLATFORM_RADIO_HOSTED_READY) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_OK;
    }
    if (s_status.state == PLATFORM_RADIO_HOSTED_STARTING) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = PLATFORM_RADIO_HOSTED_STARTING;
    s_status.last_error = ESP_OK;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "starting board-authorized C6 SDIO transport");
    esp_err_t result = ESP_OK;
#if CONFIG_P4_BOARD_M5STACK_TAB5
    result = platform_tab5_radio_power(true);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
#endif
    if (result == ESP_OK) result = esp_hosted_init();

    portENTER_CRITICAL(&s_lock);
    s_status.last_error = result;
    s_status.state = result == ESP_OK
        ? PLATFORM_RADIO_HOSTED_READY : PLATFORM_RADIO_HOSTED_FAILED;
    portEXIT_CRITICAL(&s_lock);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "C6 SDIO transport failed: %s",
                 esp_err_to_name(result));
        return result;
    }
    /* In BLE-only builds, NimBLE's pinned VHCI low-level initializer performs
     * the C6 reset and waits for the hosted transport.  Do not issue an RPC or
     * poll transport readiness here: both happen before that initializer and
     * ESP-Hosted 1.4.7 can double-free a failed early RPC. */
    ESP_LOGI(TAG,
             "C6 hosted initialized; link activation delegated to NimBLE VHCI");
    return ESP_OK;
}

platform_radio_hosted_status_t platform_radio_hosted_status(void)
{
    portENTER_CRITICAL(&s_lock);
    const platform_radio_hosted_status_t status = s_status;
    portEXIT_CRITICAL(&s_lock);
    return status;
}
