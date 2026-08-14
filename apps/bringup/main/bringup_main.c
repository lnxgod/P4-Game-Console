#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "p4_bringup";

#define PSRAM_TEST_BYTES (1024U * 1024U)
#define HEARTBEAT_PERIOD_MS 5000U

static bool test_psram(void)
{
    uint32_t *const allocation = heap_caps_malloc(
        PSRAM_TEST_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (allocation == NULL) {
        return false;
    }
    volatile uint32_t *const words = allocation;

    const size_t word_count = PSRAM_TEST_BYTES / sizeof(*words);
    for (size_t index = 0; index < word_count; ++index) {
        words[index] = UINT32_C(0xa5c30000) ^ (uint32_t)index;
    }

    bool passed = true;
    for (size_t index = 0; index < word_count; ++index) {
        const uint32_t expected = UINT32_C(0xa5c30000) ^ (uint32_t)index;
        if (words[index] != expected) {
            passed = false;
            break;
        }
    }

    heap_caps_free(allocation);
    return passed;
}

void app_main(void)
{
    esp_chip_info_t chip = {0};
    uint32_t flash_bytes = 0;

    esp_chip_info(&chip);
    const esp_err_t flash_result = esp_flash_get_size(NULL, &flash_bytes);
    const bool psram_initialized = esp_psram_is_initialized();
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const bool psram_test_passed = psram_initialized && test_psram();

    ESP_LOGI(TAG, "DEFCON P4 platform bring-up");
    ESP_LOGI(TAG, "ESP-IDF=%s target=%s model=%d cores=%u revision=%u.%u",
             esp_get_idf_version(), CONFIG_IDF_TARGET, (int)chip.model, chip.cores,
             chip.revision / 100, chip.revision % 100);
    ESP_LOGI(TAG, "flash_result=%s flash_bytes=%" PRIu32,
             esp_err_to_name(flash_result), flash_bytes);
    ESP_LOGI(TAG, "psram_initialized=%s psram_total=%zu psram_free=%zu",
             psram_initialized ? "true" : "false", psram_total, psram_free);
    ESP_LOGI(TAG, "psram_test_bytes=%u psram_test=%s", PSRAM_TEST_BYTES,
             psram_test_passed ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "reset_reason=%d internal_free=%zu internal_largest=%zu",
             (int)esp_reset_reason(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    const bool pass = chip.model == CHIP_ESP32P4 &&
                      flash_result == ESP_OK &&
                      flash_bytes == 16U * 1024U * 1024U &&
                      psram_initialized &&
                      psram_total >= 31U * 1024U * 1024U &&
                      psram_test_passed;

    if (pass) {
        ESP_LOGI(TAG,
                 "P4_ACCEPTANCE:PASS chip=ESP32-P4 flash=16MiB psram>=31MiB psram_test=1MiB");
    } else {
        ESP_LOGE(TAG,
                 "P4_ACCEPTANCE:FAIL expected ESP32-P4, 16MiB flash, initialized 32MiB-class PSRAM, and passing memory test");
    }

    uint32_t uptime_seconds = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
        uptime_seconds += HEARTBEAT_PERIOD_MS / 1000U;
        ESP_LOGI(TAG, "P4_HEARTBEAT status=%s uptime_s=%" PRIu32,
                 pass ? "PASS" : "FAIL", uptime_seconds);
    }
}
