#include <stdbool.h>
#include <stddef.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#pragma GCC diagnostic pop
#include "platform/display.h"

static const char *TAG = "display_diag";

static void fail_dark(const char *stage, esp_err_t err)
{
    esp_err_t dark_err = platform_display_set_brightness(0);
    if (dark_err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 BACKLIGHT_ZERO_FAIL stage=%s error=%s",
                 stage, esp_err_to_name(dark_err));
    }
    ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL_DARK stage=%s error=%s", stage, esp_err_to_name(err));
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t err = platform_display_init();
    if (err != ESP_OK) {
        fail_dark("init", err);
    }

    err = platform_display_show_pattern(PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL);
    if (err != ESP_OK) {
        fail_dark("first-pattern", err);
    }
    err = platform_display_set_brightness(25);
    if (err != ESP_OK) {
        fail_dark("backlight", err);
    }
    ESP_LOGI(TAG, "P4_DISPLAY M1 BACKLIGHT_SET brightness_percent=25 visibility=pending-observation");

    const platform_display_pattern_t patterns[] = {
        PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL,
        PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL,
        PLATFORM_DISPLAY_PATTERN_BER_VERTICAL,
    };
    size_t pattern_index = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        pattern_index = (pattern_index + 1U) % (sizeof(patterns) / sizeof(patterns[0]));
        err = platform_display_show_pattern(patterns[pattern_index]);
        if (err != ESP_OK) {
            fail_dark("pattern-cycle", err);
        }
        ESP_LOGI(TAG, "P4_DISPLAY M1 HEARTBEAT pattern_index=%u",
                 (unsigned int)pattern_index);
    }
}
