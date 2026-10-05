// SPDX-License-Identifier: Apache-2.0
// Panel timings and initialization derived from pinned Espressif Tab5 BSP.
#include "platform/display.h"
#include "platform/tab5.h"
#include "platform_display_layout.h"
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/ledc.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include "disp_init_data.h"
#include "disp_init_data_1.h"
#include "disp_init_data_st7121.h"

static esp_lcd_dsi_bus_handle_t s_dsi;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static esp_ldo_channel_handle_t s_ldo;
static uint16_t *s_frames[2];
static unsigned s_active;
static bool s_ready, s_pending, s_backlight;
static uint32_t s_pending_refresh;
static StaticSemaphore_t s_lock_storage, s_refresh_storage;
static SemaphoreHandle_t s_lock, s_refresh;
static platform_display_stats_t s_stats;
static uint32_t s_refresh_count;
#define FRAME_BYTES ((size_t)PLATFORM_DISPLAY_NATIVE_WIDTH * PLATFORM_DISPLAY_NATIVE_HEIGHT * sizeof(uint16_t))

static bool IRAM_ATTR refreshed(esp_lcd_panel_handle_t panel,
    esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    (void)panel; (void)event; (void)context;
    __atomic_fetch_add(&s_refresh_count, 1U, __ATOMIC_RELEASE);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_refresh, &woken);
    return woken == pdTRUE;
}
static esp_err_t brightness(uint8_t percent)
{
    if (percent > 100) return ESP_ERR_INVALID_ARG;
    if (!s_backlight) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)percent * 1023U / 100U);
    return ret == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) : ret;
}
static esp_err_t release(void)
{
    s_ready=false;
    esp_err_t ret=s_backlight?brightness(0):ESP_OK;
    if (ret!=ESP_OK) return ret;
    if (s_panel) {
        const esp_lcd_dpi_panel_event_callbacks_t callbacks={0};
        ret=esp_lcd_dpi_panel_register_event_callbacks(s_panel,&callbacks,NULL);
        if (ret!=ESP_OK) return ret;
        ret=esp_lcd_panel_del(s_panel);
        if (ret!=ESP_OK) return ret;
        s_panel=NULL; s_frames[0]=s_frames[1]=NULL; s_pending=false;
    }
    if (s_io) { ret=esp_lcd_panel_io_del(s_io); if (ret!=ESP_OK) return ret; s_io=NULL; }
    if (s_dsi) { ret=esp_lcd_del_dsi_bus(s_dsi); if (ret!=ESP_OK) return ret; s_dsi=NULL; }
    if (s_ldo) { ret=esp_ldo_release_channel(s_ldo); if (ret!=ESP_OK) return ret; s_ldo=NULL; }
    if (s_backlight) {
        ret=ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,0);
        if (ret!=ESP_OK) return ret;
        s_backlight=false;
    }
    return ESP_OK;
}
esp_err_t platform_display_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
        s_refresh = xSemaphoreCreateBinaryStatic(&s_refresh_storage);
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    if (s_ready) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    /* A failed teardown retains ownership; finish it before allocating again. */
    esp_err_t ret=release();
    if (ret!=ESP_OK) { xSemaphoreGive(s_lock); return ret; }
    platform_tab5_panel_t kind;
#define TRY(call) do { ret = (call); if (ret != ESP_OK) goto fail; } while (0)
    const ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE,
        .duty_resolution=LEDC_TIMER_10_BIT, .timer_num=LEDC_TIMER_0,
        .freq_hz=5000, .clk_cfg=LEDC_AUTO_CLK};
    const ledc_channel_config_t light = {.gpio_num=PLATFORM_BOARD_LCD_BACKLIGHT_GPIO,
        .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0,
        .timer_sel=LEDC_TIMER_0, .duty=0, .hpoint=0};
    TRY(ledc_timer_config(&timer)); TRY(ledc_channel_config(&light)); s_backlight=true;
    TRY(platform_tab5_init());
    TRY(platform_tab5_speaker_enable(false));
    TRY(platform_tab5_display_reset());
    TRY(platform_tab5_panel_detect(&kind));
    const esp_ldo_channel_config_t power = {.chan_id=3, .voltage_mv=2500};
    TRY(esp_ldo_acquire_channel(&power, &s_ldo));
    const esp_lcd_dsi_bus_config_t bus = {.bus_id=0, .num_data_lanes=2,
        .phy_clk_src=MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps=kind == TAB5_PANEL_ST7121 ? 965 : 1000};
    TRY(esp_lcd_new_dsi_bus(&bus, &s_dsi));
    const esp_lcd_dbi_io_config_t dbi = {.virtual_channel=0, .lcd_cmd_bits=8, .lcd_param_bits=8};
    TRY(esp_lcd_new_panel_io_dbi(s_dsi, &dbi, &s_io));
    const bool ili = kind == TAB5_PANEL_ILI9881C;
    const bool st1 = kind == TAB5_PANEL_ST7121;
    const esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel=0, .dpi_clk_src=MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz=ili ? 60 : 70, .in_color_format=LCD_COLOR_FMT_RGB565,
        .num_fbs=2,
        .video_timing={.h_size=720,.v_size=1280,
            .hsync_back_porch=ili ? 140 : 40,.hsync_pulse_width=ili ? 40 : 2,
            .hsync_front_porch=40,.vsync_back_porch=ili ? 20 : st1 ? 24 : 8,
            .vsync_pulse_width=ili ? 4 : st1 ? 20 : 2,
            .vsync_front_porch=ili ? 20 : st1 ? 200 : 220},
    };
    const ili9881c_vendor_config_t ili_vendor = {
        .init_cmds=disp_init_data_ili9881c,
        .init_cmds_size=sizeof(disp_init_data_ili9881c)/sizeof(disp_init_data_ili9881c[0]),
        .mipi_config={.dsi_bus=s_dsi,.dpi_config=&dpi,.lane_num=2},
    };
    const st7123_vendor_config_t st_vendor = {
        .init_cmds=st1 ? disp_init_data_st7121 : disp_init_data_st7123,
        .init_cmds_size=st1 ? sizeof(disp_init_data_st7121)/sizeof(disp_init_data_st7121[0])
            : sizeof(disp_init_data_st7123)/sizeof(disp_init_data_st7123[0]),
        .mipi_config={.dsi_bus=s_dsi,.dpi_config=&dpi},
    };
    const esp_lcd_panel_dev_config_t cfg = {.reset_gpio_num=-1,
        .rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB,.bits_per_pixel=16,
        .vendor_config=ili ? (void *)&ili_vendor : (void *)&st_vendor};
    TRY(ili ? esp_lcd_new_panel_ili9881c(s_io,&cfg,&s_panel) : esp_lcd_new_panel_st7123(s_io,&cfg,&s_panel));
    TRY(esp_lcd_panel_reset(s_panel));
    TRY(esp_lcd_panel_init(s_panel));
    TRY(esp_lcd_dpi_panel_get_frame_buffer(s_panel,2,(void **)&s_frames[0],(void **)&s_frames[1]));
    for (unsigned i=0;i<2;++i) {
        memset(s_frames[i],0,FRAME_BYTES);
        TRY(esp_cache_msync(s_frames[i],FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_C2M));
    }
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_refresh_done=refreshed};
    TRY(esp_lcd_dpi_panel_register_event_callbacks(s_panel,&callbacks,NULL));
    TRY(esp_lcd_panel_disp_on_off(s_panel,true));
    s_active=0; s_pending=false; s_ready=true;
    ESP_LOGI("tab5_display","P4_DISPLAY READY board=m5stack-tab5 panel=%s native=720x1280 logical=1280x720 viewport=1152x720+64+0 format=rgb565",platform_tab5_panel_name(kind));
    xSemaphoreGive(s_lock); return ESP_OK;
fail:
    { const esp_err_t cleanup=release(); if (cleanup!=ESP_OK) ret=cleanup; }
    xSemaphoreGive(s_lock); return ret;
#undef TRY
}
esp_err_t platform_display_deinit(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t ret=release(); xSemaphoreGive(s_lock); return ret;
}
esp_err_t platform_display_set_brightness(uint8_t percent)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(1000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret = s_ready ? brightness(percent) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(s_lock); return ret;
}
static esp_err_t finish_pending(TickType_t start, TickType_t budget)
{
    /* Two refresh boundaries conservatively cover a handoff racing the ISR.
     * On timeout retain the pending buffer; never overwrite a possibly active FB. */
    while (s_pending && (uint32_t)(__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE)-s_pending_refresh)<2U) {
        TickType_t elapsed=xTaskGetTickCount()-start;
        if (elapsed>=budget || xSemaphoreTake(s_refresh,budget-elapsed)!=pdTRUE) return ESP_ERR_TIMEOUT;
    }
    if (s_pending) { s_active^=1U; s_pending=false; }
    return ESP_OK;
}
typedef bool (*layout_fn)(const uint16_t *,size_t,uint16_t *,size_t,size_t);
static esp_err_t submit(const uint16_t *source,size_t stride,size_t width,uint32_t timeout,layout_fn layout)
{
    if (!source || stride<width || stride>SIZE_MAX/480U/sizeof(uint16_t)) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    TickType_t budget=pdMS_TO_TICKS(timeout),start=xTaskGetTickCount();
    if (xSemaphoreTake(s_lock,budget)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    if (!s_ready) goto done;
    ++s_stats.submits_started;
    ret=finish_pending(start,budget);
    if (ret!=ESP_OK) goto done;
    uint16_t *dest=s_frames[s_active^1U];
    if (!layout(source,stride,dest,PLATFORM_DISPLAY_NATIVE_WIDTH,PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
        ret=ESP_ERR_INVALID_ARG; goto done;
    }
    ret=esp_cache_msync(dest,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret!=ESP_OK) goto done;
    ret=esp_lcd_dpi_panel_set_pattern(s_panel,MIPI_DSI_PATTERN_NONE);
    if (ret!=ESP_OK) goto done;
    ret=esp_lcd_panel_draw_bitmap(s_panel,0,0,720,1280,dest);
    if (ret!=ESP_OK) goto done;
    s_pending_refresh=__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE);
    s_pending=true;
    ret=finish_pending(start,budget);
    if (ret==ESP_OK) ++s_stats.submits_completed;
done:
    if (ret==ESP_ERR_TIMEOUT) ++s_stats.submit_timeouts;
    else if (ret!=ESP_OK) ++s_stats.submit_failures;
    xSemaphoreGive(s_lock); return ret;
}
esp_err_t platform_display_submit_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,320,timeout,platform_display_layout_rgb565_320x200); }
esp_err_t platform_display_submit_shell_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,384,timeout,platform_display_layout_rgb565_384x240); }
esp_err_t platform_display_submit_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,768,timeout,platform_display_layout_rgb565_768x480); }
esp_err_t platform_display_submit_game_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return platform_display_submit_content_rgb565(s,stride,timeout); }
esp_err_t platform_display_submit_content_regions_rgb565(const uint16_t *s,size_t stride,
    const platform_display_rgb565_region_t *regions,size_t count,uint32_t timeout)
{
    if (count && !regions) return ESP_ERR_INVALID_ARG;
    for (size_t i=0;i<count;++i) if (!regions[i].width || !regions[i].height ||
        regions[i].x>=768 || regions[i].y>=480 || regions[i].width>768-regions[i].x ||
        regions[i].height>480-regions[i].y) return ESP_ERR_INVALID_ARG;
    return platform_display_submit_content_rgb565(s,stride,timeout);
}
esp_err_t platform_display_record_interactive_input_timestamp(int64_t timestamp)
{ (void)timestamp; return ESP_OK; }
esp_err_t platform_display_get_stats(platform_display_stats_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_lock) { *out=(platform_display_stats_t){0};return ESP_OK; }
    if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    *out=s_stats;
    out->refresh_completions=__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE);
    xSemaphoreGive(s_lock);return ESP_OK;
}
esp_err_t platform_display_show_pattern(platform_display_pattern_t pattern)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    mipi_dsi_pattern_type_t value;
    switch(pattern) {
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL:value=MIPI_DSI_PATTERN_BAR_VERTICAL;break;
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL:value=MIPI_DSI_PATTERN_BAR_HORIZONTAL;break;
    case PLATFORM_DISPLAY_PATTERN_BER_VERTICAL:value=MIPI_DSI_PATTERN_BER_VERTICAL;break;
    case PLATFORM_DISPLAY_PATTERN_BLACK:value=MIPI_DSI_PATTERN_NONE;break;
    default:xSemaphoreGive(s_lock);return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret=s_ready ? esp_lcd_dpi_panel_set_pattern(s_panel,value):ESP_ERR_INVALID_STATE;
    if (ret==ESP_OK && pattern==PLATFORM_DISPLAY_PATTERN_BLACK) {
        const TickType_t start=xTaskGetTickCount(),budget=pdMS_TO_TICKS(250);
        ret=finish_pending(start,budget);
        if (ret==ESP_OK) {
            uint16_t *dest=s_frames[s_active^1U];
            memset(dest,0,FRAME_BYTES);
            ret=esp_cache_msync(dest,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (ret==ESP_OK) ret=esp_lcd_panel_draw_bitmap(s_panel,0,0,720,1280,dest);
            if (ret==ESP_OK) {
                s_pending_refresh=__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE);
                s_pending=true;
                ret=finish_pending(start,budget);
            }
        }
    }
    xSemaphoreGive(s_lock);return ret;
}
