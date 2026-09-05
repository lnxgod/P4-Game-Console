// SPDX-License-Identifier: MIT

#include "platform/display.h"
#include "platform_display_layout.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_lt8912b.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

static const char *TAG = "platform_display";

enum {
    DISPLAY_DSI_BUS_ID = 0,
    DISPLAY_DSI_DATA_LANES = 2,
    DISPLAY_DSI_LANE_RATE_MBPS = 1000,
    DISPLAY_DPHY_LDO_CHANNEL = 3,
    DISPLAY_DPHY_LDO_MV = 2500,
    DISPLAY_I2C_PORT = 1,
    DISPLAY_I2C_SDA_GPIO = 7,
    DISPLAY_I2C_SCL_GPIO = 8,
    DISPLAY_I2C_HZ = 400000,
    DISPLAY_PIXEL_BYTES = 3,
};

#define DISPLAY_FRAME_BYTES \
    ((size_t)PLATFORM_DISPLAY_WIDTH * (size_t)PLATFORM_DISPLAY_HEIGHT * \
     (size_t)DISPLAY_PIXEL_BYTES)

static esp_ldo_channel_handle_t s_dphy_ldo;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_dsi_bus_handle_t s_dsi_bus;
static esp_lcd_panel_io_handle_t s_main_io;
static esp_lcd_panel_io_handle_t s_cec_io;
static esp_lcd_panel_io_handle_t s_avi_io;
static esp_lcd_panel_handle_t s_panel;
static uint8_t *s_submit_frame;
static bool s_initialized;
static bool s_sleeping = true;
static uint8_t s_brightness_percent;
static StaticSemaphore_t s_api_lock_storage;
static StaticSemaphore_t s_refresh_signal_storage;
static SemaphoreHandle_t s_api_lock;
static SemaphoreHandle_t s_refresh_signal;
static portMUX_TYPE s_sync_init_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_display_stats_t s_stats;

static bool ensure_sync_objects(void)
{
    taskENTER_CRITICAL(&s_sync_init_lock);
    if (s_api_lock == NULL) {
        s_api_lock = xSemaphoreCreateMutexStatic(&s_api_lock_storage);
    }
    if (s_refresh_signal == NULL) {
        s_refresh_signal =
            xSemaphoreCreateBinaryStatic(&s_refresh_signal_storage);
    }
    const bool ready = s_api_lock != NULL && s_refresh_signal != NULL;
    taskEXIT_CRITICAL(&s_sync_init_lock);
    return ready;
}

static TickType_t milliseconds_to_ticks(uint32_t milliseconds)
{
    if (milliseconds == 0U) {
        return 0;
    }
    uint64_t ticks =
        (((uint64_t)milliseconds * (uint64_t)configTICK_RATE_HZ) + 999U) /
        1000U;
    if (ticks > (uint64_t)portMAX_DELAY) {
        ticks = (uint64_t)portMAX_DELAY;
    }
    return (TickType_t)ticks;
}

static TickType_t remaining_ticks(TickType_t started, TickType_t budget)
{
    if (budget == portMAX_DELAY) {
        return portMAX_DELAY;
    }
    const TickType_t elapsed = xTaskGetTickCount() - started;
    return elapsed >= budget ? 0 : budget - elapsed;
}

static esp_err_t wait_for_refresh_after(uint32_t baseline,
                                        TickType_t started,
                                        TickType_t budget)
{
    while (__atomic_load_n(&s_stats.refresh_completions, __ATOMIC_ACQUIRE) ==
           baseline) {
        const TickType_t remaining = remaining_ticks(started, budget);
        if (remaining == 0 ||
            xSemaphoreTake(s_refresh_signal, remaining) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static bool IRAM_ATTR on_refresh_done(esp_lcd_panel_handle_t panel,
                                      esp_lcd_dpi_panel_event_data_t *event,
                                      void *user_context)
{
    (void)panel;
    (void)event;
    (void)user_context;
    __atomic_fetch_add(&s_stats.refresh_completions, 1U, __ATOMIC_RELAXED);
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_refresh_signal != NULL) {
        (void)xSemaphoreGiveFromISR(s_refresh_signal,
                                    &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

static void release_owned_resources(void)
{
    s_initialized = false;
    s_brightness_percent = 0U;
    if (s_panel != NULL) {
        (void)esp_lcd_panel_disp_sleep(s_panel, true);
        const esp_lcd_dpi_panel_event_callbacks_t callbacks = {0};
        (void)esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks,
                                                         NULL);
    }
    if (s_submit_frame != NULL) {
        heap_caps_free(s_submit_frame);
        s_submit_frame = NULL;
    }
    if (s_panel != NULL) {
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_avi_io != NULL) {
        (void)esp_lcd_panel_io_del(s_avi_io);
        s_avi_io = NULL;
    }
    if (s_cec_io != NULL) {
        (void)esp_lcd_panel_io_del(s_cec_io);
        s_cec_io = NULL;
    }
    if (s_main_io != NULL) {
        (void)esp_lcd_panel_io_del(s_main_io);
        s_main_io = NULL;
    }
    if (s_dsi_bus != NULL) {
        (void)esp_lcd_del_dsi_bus(s_dsi_bus);
        s_dsi_bus = NULL;
    }
    if (s_i2c_bus != NULL) {
        (void)i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
    if (s_dphy_ldo != NULL) {
        (void)esp_ldo_release_channel(s_dphy_ldo);
        s_dphy_ldo = NULL;
    }
    s_sleeping = true;
}

static esp_err_t set_brightness_locked(uint8_t percent)
{
    if (percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_panel == NULL || (percent != 0U && !s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool sleep = percent == 0U;
    if (sleep != s_sleeping) {
        const esp_err_t result = esp_lcd_panel_disp_sleep(s_panel, sleep);
        if (result != ESP_OK) {
            return result;
        }
        s_sleeping = sleep;
    }
    s_brightness_percent = percent;
    return ESP_OK;
}

esp_err_t platform_display_set_brightness(uint8_t percent)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = set_brightness_locked(percent);
    (void)xSemaphoreGive(s_api_lock);
    return result;
}

static esp_err_t new_i2c_io(uint8_t address,
                            esp_lcd_panel_io_handle_t *out_io)
{
    const esp_lcd_panel_io_i2c_config_t config =
        LT8912B_IO_CFG(DISPLAY_I2C_HZ, address);
    return esp_lcd_new_panel_io_i2c(s_i2c_bus, &config, out_io);
}

esp_err_t platform_display_init(void)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (s_initialized) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG,
             "P4_DISPLAY START profile=olimex-esp32-p4-pc-rev-b "
             "transport=hdmi bridge=lt8912b");
    esp_err_t result;
    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = DISPLAY_DPHY_LDO_CHANNEL,
        .voltage_mv = DISPLAY_DPHY_LDO_MV,
    };
    result = esp_ldo_acquire_channel(&ldo_config, &s_dphy_ldo);
    if (result != ESP_OK) {
        goto fail;
    }

    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = DISPLAY_I2C_PORT,
        .sda_io_num = DISPLAY_I2C_SDA_GPIO,
        .scl_io_num = DISPLAY_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    result = i2c_new_master_bus(&i2c_config, &s_i2c_bus);
    if (result != ESP_OK) {
        goto fail;
    }

    const esp_lcd_dsi_bus_config_t dsi_config = {
        .bus_id = DISPLAY_DSI_BUS_ID,
        .num_data_lanes = DISPLAY_DSI_DATA_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DISPLAY_DSI_LANE_RATE_MBPS,
    };
    result = esp_lcd_new_dsi_bus(&dsi_config, &s_dsi_bus);
    if (result != ESP_OK) {
        goto fail;
    }
    result = new_i2c_io(LT8912B_IO_I2C_MAIN_ADDRESS, &s_main_io);
    if (result == ESP_OK) {
        result = new_i2c_io(LT8912B_IO_I2C_CEC_ADDRESS, &s_cec_io);
    }
    if (result == ESP_OK) {
        result = new_i2c_io(LT8912B_IO_I2C_AVI_ADDRESS, &s_avi_io);
    }
    if (result != ESP_OK) {
        goto fail;
    }

    esp_lcd_dpi_panel_config_t dpi_config =
        LT8912B_1280x720_PANEL_60HZ_DPI_CONFIG_WITH_FBS(1);
    dpi_config.flags.use_dma2d = true;
    const lt8912b_vendor_config_t vendor_config = {
        .video_timing = ESP_LCD_LT8912B_VIDEO_TIMING_1280x720_60Hz(),
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = DISPLAY_DSI_DATA_LANES,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24,
        .vendor_config = (void *)&vendor_config,
    };
    const esp_lcd_panel_lt8912b_io_t panel_io = {
        .main = s_main_io,
        .cec_dsi = s_cec_io,
        .avi = s_avi_io,
    };
    result = esp_lcd_new_panel_lt8912b(&panel_io, &panel_config, &s_panel);
    if (result == ESP_OK) {
        result = esp_lcd_panel_reset(s_panel);
    }
    if (result == ESP_OK) {
        result = esp_lcd_panel_init(s_panel);
    }
    if (result != ESP_OK) {
        goto fail;
    }

    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_refresh_done = on_refresh_done,
    };
    result = esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks,
                                                        NULL);
    if (result == ESP_OK) {
        result = esp_lcd_panel_disp_sleep(s_panel, true);
    }
    if (result != ESP_OK) {
        goto fail;
    }
    s_sleeping = true;
    s_initialized = true;
    ESP_LOGI(TAG,
             "P4_DISPLAY READY resolution=1280x720 format=rgb888 "
             "viewport=960x600+160+60 lane_mbps=1000");
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "P4_DISPLAY FAIL error=%s", esp_err_to_name(result));
    release_owned_resources();
    (void)xSemaphoreGive(s_api_lock);
    return result;
}

esp_err_t platform_display_deinit(void)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_initialized && s_panel == NULL) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }
    release_owned_resources();
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
}

esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_initialized || s_panel == NULL) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_STATE;
    }
    mipi_dsi_pattern_type_t dsi_pattern;
    switch (pattern) {
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_VERTICAL;
        break;
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_HORIZONTAL;
        break;
    case PLATFORM_DISPLAY_PATTERN_BER_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BER_VERTICAL;
        break;
    case PLATFORM_DISPLAY_PATTERN_BLACK:
        dsi_pattern = MIPI_DSI_PATTERN_NONE;
        break;
    default:
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t result =
        esp_lcd_dpi_panel_set_pattern(s_panel, dsi_pattern);
    (void)xSemaphoreGive(s_api_lock);
    return result;
}

typedef bool (*display_hdmi_layout_fn_t)(
    const uint16_t *, size_t, uint8_t *, size_t, size_t);

static esp_err_t submit_rgb565(const uint16_t *source,
                               size_t source_stride_pixels,
                               size_t minimum_stride,
                               uint32_t timeout_ms,
                               display_hdmi_layout_fn_t layout)
{
    if (source == NULL || source_stride_pixels < minimum_stride ||
        layout == NULL) {
        __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_sync_objects()) {
        __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_NO_MEM;
    }
    const TickType_t budget = milliseconds_to_ticks(timeout_ms);
    const TickType_t started = xTaskGetTickCount();
    if (xSemaphoreTake(s_api_lock, budget) != pdTRUE) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        return ESP_ERR_TIMEOUT;
    }
    __atomic_fetch_add(&s_stats.submits_started, 1U, __ATOMIC_RELAXED);

    esp_err_t result = ESP_OK;
    if (!s_initialized || s_panel == NULL) {
        result = ESP_ERR_INVALID_STATE;
        goto fail;
    }
    if (s_submit_frame == NULL) {
        s_submit_frame = heap_caps_malloc(
            DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_submit_frame == NULL) {
            result = ESP_ERR_NO_MEM;
            goto fail_dark;
        }
    }
    if (!layout(
            source, source_stride_pixels, s_submit_frame,
            PLATFORM_DISPLAY_WIDTH * DISPLAY_PIXEL_BYTES,
            PLATFORM_DISPLAY_HEIGHT)) {
        result = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    result = esp_lcd_dpi_panel_set_pattern(s_panel, MIPI_DSI_PATTERN_NONE);
    if (result != ESP_OK) {
        goto fail_dark;
    }
    while (xSemaphoreTake(s_refresh_signal, 0) == pdTRUE) {
    }
    result = esp_lcd_panel_draw_bitmap(
        s_panel, 0, 0, PLATFORM_DISPLAY_WIDTH, PLATFORM_DISPLAY_HEIGHT,
        s_submit_frame);
    if (result != ESP_OK) {
        goto fail_dark;
    }
    const uint32_t baseline =
        __atomic_load_n(&s_stats.refresh_completions, __ATOMIC_ACQUIRE);
    if (wait_for_refresh_after(baseline, started, budget) != ESP_OK) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        (void)set_brightness_locked(0U);
        ESP_LOGE(TAG, "P4_DISPLAY SUBMIT_TIMEOUT timeout_ms=%" PRIu32,
                 timeout_ms);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }
    __atomic_fetch_add(&s_stats.submits_completed, 1U, __ATOMIC_RELAXED);
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;

fail_dark:
    (void)set_brightness_locked(0U);
fail:
    __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
    ESP_LOGE(TAG, "P4_DISPLAY SUBMIT_FAIL error=%s",
             esp_err_to_name(result));
    (void)xSemaphoreGive(s_api_lock);
    return result;
}

esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_GAME_WIDTH,
        timeout_ms, platform_display_layout_rgb565_to_rgb888_1280x720);
}

esp_err_t platform_display_submit_shell_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_SHELL_WIDTH,
        timeout_ms,
        platform_display_layout_rgb565_384x240_to_rgb888_1280x720);
}

esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    (void)source;
    (void)source_stride_pixels;
    (void)timeout_ms;
    /* The HDMI adapter has no reviewed 768x480-to-1280x720 content layout.
     * Fail explicitly instead of misinterpreting a native-content buffer as
     * the existing 320x200 game surface. */
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t platform_display_submit_content_regions_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    const platform_display_rgb565_region_t *regions,
    size_t region_count,
    uint32_t timeout_ms)
{
    if (regions == NULL || region_count == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    /* HDMI has no native-content path; preserve that explicit boundary. */
    return platform_display_submit_content_rgb565(
        source, source_stride_pixels, timeout_ms);
}

esp_err_t platform_display_submit_game_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return platform_display_submit_content_rgb565(
        source, source_stride_pixels, timeout_ms);
}

esp_err_t platform_display_record_interactive_input_timestamp(
    int64_t timestamp_us)
{
    if (timestamp_us < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    /* HDMI has no DSI framebuffer handoff to correlate. Keep this a portable
     * no-op so common shell input code need not special-case the adapter. */
    return ESP_OK;
}

esp_err_t platform_display_get_stats(platform_display_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out_stats->submits_started =
        __atomic_load_n(&s_stats.submits_started, __ATOMIC_RELAXED);
    out_stats->submits_completed =
        __atomic_load_n(&s_stats.submits_completed, __ATOMIC_RELAXED);
    out_stats->submit_timeouts =
        __atomic_load_n(&s_stats.submit_timeouts, __ATOMIC_RELAXED);
    out_stats->submit_failures =
        __atomic_load_n(&s_stats.submit_failures, __ATOMIC_RELAXED);
    out_stats->refresh_completions =
        __atomic_load_n(&s_stats.refresh_completions, __ATOMIC_RELAXED);
    out_stats->accelerated_submits = 0U;
    out_stats->accelerator_failures = 0U;
    out_stats->pipeline_reuse_wait_last_us = 0U;
    out_stats->pipeline_reuse_wait_max_us = 0U;
    out_stats->pipeline_transform_last_us = 0U;
    out_stats->pipeline_transform_max_us = 0U;
    out_stats->pipeline_handoff_last_us = 0U;
    out_stats->pipeline_handoff_max_us = 0U;
    out_stats->pipeline_reserved_refreshes = 0U;
    out_stats->pipeline_refresh_interval_last_us = 0U;
    out_stats->pipeline_refresh_interval_min_us = 0U;
    out_stats->pipeline_refresh_interval_max_us = 0U;
    out_stats->pipeline_refresh_events = 0U;
    out_stats->partial_content_submits = 0U;
    out_stats->partial_content_source_pixels = 0U;
    out_stats->partial_content_full_fallbacks = 0U;
    out_stats->interactive_latency_samples = 0U;
    out_stats->interactive_partial_presentations = 0U;
    out_stats->interactive_full_presentations = 0U;
    out_stats->interactive_input_to_refresh_total_us = 0U;
    out_stats->interactive_input_to_refresh_max_us = 0U;
    out_stats->interactive_input_to_refresh_last_us = 0U;
    out_stats->interactive_handoff_to_refresh_total_us = 0U;
    out_stats->interactive_handoff_to_refresh_max_us = 0U;
    out_stats->interactive_handoff_to_refresh_last_us = 0U;
    out_stats->interactive_reuse_wait_last_us = 0U;
    out_stats->interactive_transform_last_us = 0U;
    out_stats->interactive_replay_region_count = 0U;
    out_stats->interactive_present_kind =
        PLATFORM_DISPLAY_INTERACTIVE_PRESENT_NONE;
    out_stats->underrun_count_available = false;
    return ESP_OK;
}
