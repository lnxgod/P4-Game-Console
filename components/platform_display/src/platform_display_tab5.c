// SPDX-License-Identifier: Apache-2.0
// Panel timings and initialization derived from pinned Espressif Tab5 BSP.
#include "platform/display.h"
#include "platform/tab5.h"
#include "platform_display_layout.h"
#include "tab5_frame_queue.h"
#include "tab5_game_prescale.h"
#include "tab5_prescaled_game_transform.h"
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
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
static uint16_t *s_frames[TAB5_FRAME_COUNT];
static uint16_t *s_game_scale;
static ppa_client_handle_t s_scaler;
static tab5_frame_queue_t s_queue;
static tab5_frame_history_t s_ui_history[TAB5_FRAME_COUNT];
/* Protected by s_lock; valid only after a successful native game submit. */
static bool s_native_margins_black[TAB5_FRAME_COUNT];
static bool s_ready, s_backlight, s_pattern;
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
    s_ready=false;memset(s_ui_history,0,sizeof(s_ui_history));
    memset(s_native_margins_black,0,sizeof(s_native_margins_black));
    esp_err_t ret=s_backlight?brightness(0):ESP_OK;
    if (ret!=ESP_OK) return ret;
    if (s_scaler) {
        ret=ppa_unregister_client(s_scaler);
        if (ret!=ESP_OK) return ret;
        s_scaler=NULL;
    }
    heap_caps_free(s_game_scale); s_game_scale=NULL;
    if (s_panel) {
        const esp_lcd_dpi_panel_event_callbacks_t callbacks={0};
        ret=esp_lcd_dpi_panel_register_event_callbacks(s_panel,&callbacks,NULL);
        if (ret!=ESP_OK) return ret;
        ret=esp_lcd_panel_del(s_panel);
        if (ret!=ESP_OK) return ret;
        s_panel=NULL; memset(s_frames,0,sizeof(s_frames)); tab5_frame_queue_init(&s_queue);
    }
    if (s_io) { ret=esp_lcd_panel_io_del(s_io); if (ret!=ESP_OK) return ret; s_io=NULL; }
    if (s_dsi) { ret=esp_lcd_del_dsi_bus(s_dsi); if (ret!=ESP_OK) return ret; s_dsi=NULL; }
    if (s_ldo) { ret=esp_ldo_release_channel(s_ldo); if (ret!=ESP_OK) return ret; s_ldo=NULL; }
    if (s_backlight) {
        ret=ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,0);
        if (ret!=ESP_OK) return ret;
        ret=gpio_reset_pin(PLATFORM_BOARD_LCD_BACKLIGHT_GPIO);
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
        .num_fbs=TAB5_FRAME_COUNT,
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
    /* The ST7123 table sets both MADCTL mirror bits. Match the pinned BSP's
     * post-init normalization so display pixels and native touch share axes. */
    TRY(esp_lcd_panel_invert_color(s_panel,false));
    TRY(esp_lcd_panel_mirror(s_panel,false,false));
    TRY(esp_lcd_dpi_panel_get_frame_buffer(s_panel,TAB5_FRAME_COUNT,(void **)&s_frames[0],(void **)&s_frames[1],(void **)&s_frames[2]));
    /* The DPI driver allocates and clears all buffers before enabling scanout.
     * Do not rewrite its active buffer after panel_init has started DMA. */
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_refresh_done=refreshed};
    TRY(esp_lcd_dpi_panel_register_event_callbacks(s_panel,&callbacks,NULL));
    TRY(esp_lcd_panel_disp_on_off(s_panel,true));
    const ppa_client_config_t scaler_config={.oper_type=PPA_OPERATION_SRM,.max_pending_trans_num=1U};
    TRY(ppa_register_client(&scaler_config,&s_scaler));
    s_game_scale=heap_caps_aligned_alloc(64,384U*240U*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!s_game_scale) { ret=ESP_ERR_NO_MEM; goto fail; }
    tab5_frame_queue_init(&s_queue); s_pattern=false; s_ready=true;
    ESP_LOGI("tab5_display","FRAME_QUEUE buffers=%u framebuffer_bytes=%u extra_psram_bytes=%u free_psram_bytes=%u retirement_refreshes=%u",
        (unsigned)TAB5_FRAME_COUNT,(unsigned)FRAME_BYTES,(unsigned)FRAME_BYTES,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)TAB5_RETIRE_REFRESHES);
    ESP_LOGI("tab5_display","PPA_READY rotation_ccw=90 os=1280x720 os_scale=1 game_viewport=1152x720 content_scale=1.5 game_prescale=384x240");
    ESP_LOGI("tab5_display","P4_DISPLAY READY board=m5stack-tab5 panel=%s native=720x1280 logical=1280x720 os=1280x720 game_viewport=1152x720+64+0 format=rgb565",platform_tab5_panel_name(kind));
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
static esp_err_t wait_frame_slot(TickType_t start,TickType_t budget,int *slot)
{
    for(;;){
        *slot=tab5_frame_queue_begin(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
        s_stats.submits_completed=s_queue.completed;
        if(*slot>=0)return ESP_OK;
        if(s_queue.failed)return ESP_ERR_INVALID_STATE;
        const TickType_t elapsed=xTaskGetTickCount()-start;
        if(elapsed>=budget||xSemaphoreTake(s_refresh,budget-elapsed)!=pdTRUE)return ESP_ERR_TIMEOUT;
    }
}
static esp_err_t finish_pending(TickType_t start,TickType_t budget)
{
    for(;;){
        tab5_frame_queue_observe(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
        s_stats.submits_completed=s_queue.completed;
        if(!s_queue.pending[s_queue.selected])return ESP_OK;
        const TickType_t elapsed=xTaskGetTickCount()-start;
        if(elapsed>=budget||xSemaphoreTake(s_refresh,budget-elapsed)!=pdTRUE)return ESP_ERR_TIMEOUT;
    }
}
typedef bool (*layout_fn)(const uint16_t *,size_t,uint16_t *,size_t,size_t);
static esp_err_t accelerate(const uint16_t *source,size_t stride,size_t width,uint16_t *dest)
{
    size_t height=(width==1280U||width==1152U)?720U:(width==768U?480U:240U);
    const float scale=(width==1280U||width==1152U)?1.0f:(width==768U?1.5f:3.0f);
    if (width==320U) {
        /* 3.6 is not representable by PPA's four fractional bits. A small
         * CPU prescale followed by exact 3x PPA keeps the full touch viewport. */
        const int64_t prescale_start=esp_timer_get_time();
        if (!tab5_game_prescale_rgb565(source,stride,s_game_scale,384U))
            return ESP_ERR_INVALID_ARG;
        s_stats.pipeline_prescale_last_us=(uint32_t)(esp_timer_get_time()-prescale_start);
        if (s_stats.pipeline_prescale_last_us>s_stats.pipeline_prescale_max_us)
            s_stats.pipeline_prescale_max_us=s_stats.pipeline_prescale_last_us;
        source=s_game_scale; stride=384U; width=384U;
    }
    const ppa_srm_oper_config_t op={
        .in={.buffer=source,.pic_w=(uint32_t)stride,.pic_h=(uint32_t)height,
            .block_w=(uint32_t)width,.block_h=(uint32_t)height,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .out={.buffer=dest,.buffer_size=(uint32_t)FRAME_BYTES,
            .pic_w=720U,.pic_h=1280U,.block_offset_y=width==1280U?0U:64U,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,
        .scale_x=scale,.scale_y=scale,
        .mode=PPA_TRANS_MODE_BLOCKING,
    };
    const int64_t ppa_start=esp_timer_get_time();
    const esp_err_t ret=ppa_do_scale_rotate_mirror(s_scaler,&op);
    s_stats.pipeline_ppa_last_us=(uint32_t)(esp_timer_get_time()-ppa_start);
    if (s_stats.pipeline_ppa_last_us>s_stats.pipeline_ppa_max_us)
        s_stats.pipeline_ppa_max_us=s_stats.pipeline_ppa_last_us;
    return ret;
}
static esp_err_t accelerate_ui_region(const uint16_t *source,size_t stride,
    uint16_t *dest,const platform_display_rgb565_region_t *r,const platform_display_rgb565_region_t *out)
{
    const ppa_srm_oper_config_t op={
        .in={.buffer=source,.pic_w=(uint32_t)stride,.pic_h=720,
            .block_offset_x=r->x,.block_offset_y=r->y,.block_w=r->width,.block_h=r->height,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .out={.buffer=dest,.buffer_size=(uint32_t)FRAME_BYTES,.pic_w=720,.pic_h=1280,
            .block_offset_x=out->x,.block_offset_y=out->y,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle=PPA_SRM_ROTATION_ANGLE_90,.scale_x=1,.scale_y=1,.mode=PPA_TRANS_MODE_BLOCKING,
    };
    const esp_err_t ret=ppa_do_scale_rotate_mirror(s_scaler,&op);
    /* Retain the conservative full-destination invalidation after DMA. The
     * pinned IDF also invalidates the rotated output window before DMA; this
     * fence keeps subsequent CPU access/cache writeback coherent. */
    return ret==ESP_OK?esp_cache_msync(dest,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_M2C):ret;
}
static esp_err_t submit(const uint16_t *source,size_t stride,size_t width,uint32_t timeout,layout_fn layout,bool wait_presented,const platform_display_rgb565_region_t *damage)
{
    if (!source || stride<width || stride>SIZE_MAX/720U/sizeof(uint16_t)) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    TickType_t budget=pdMS_TO_TICKS(timeout),start=xTaskGetTickCount();
    if (xSemaphoreTake(s_lock,budget)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    int slot=-1;
    if (!s_ready) goto done;
    ++s_stats.submits_started;
    const int64_t wait_start=esp_timer_get_time();
    ret=wait_frame_slot(start,budget,&slot);
    s_stats.pipeline_reuse_wait_last_us=(uint32_t)(esp_timer_get_time()-wait_start);
    if(s_stats.pipeline_reuse_wait_last_us>s_stats.pipeline_reuse_wait_max_us)
        s_stats.pipeline_reuse_wait_max_us=s_stats.pipeline_reuse_wait_last_us;
    if (ret!=ESP_OK) goto done;
    uint16_t *dest=s_frames[(unsigned)slot];
    /* Every UI/other transform or failed submit invalidates this slot before
     * mutation. Native transforms only write the centered 1152-row viewport. */
    const bool native = width == 320U || width == 768U;
    const bool reuse_margins = native && s_native_margins_black[(unsigned)slot];
    s_native_margins_black[(unsigned)slot] = false;
    if (width != 1280U && !reuse_margins) {
        memset(dest, 0, 64U * 720U * sizeof(*dest));
        memset(dest + 1216U * 720U, 0, 64U * 720U * sizeof(*dest));
    }
    platform_display_rgb565_region_t region={0,0,1280,720},mapped;
    const platform_display_rgb565_region_t current=damage?*damage:region;
    const bool replay=width==1280U&&damage&&s_ui_history[slot].source==source&&s_ui_history[slot].stride==stride;
    const platform_display_rgb565_region_t previous=replay&&s_ui_history[slot].damage.width?s_ui_history[slot].damage:current;
    if(width==1280U&&!platform_display_layout_tab5_damage(&current,replay?&previous:NULL,&region,&mapped)){
        ret=ESP_ERR_INVALID_ARG;goto done;
    }
    /* Invalidate history before any buffer mutation; failed submissions can
     * only be followed by a complete reconstruction. */
    s_ui_history[slot].source=NULL;
    /* These subphases describe this game submission only. UI submissions
     * use their existing transform timing and must not retain stale values. */
    s_stats.pipeline_prescale_last_us=0;
    s_stats.pipeline_ppa_last_us=0;
    const int64_t transform_start=esp_timer_get_time();
    const bool prescaled_game =
        layout == platform_display_layout_rgb565_prescaled_game_384x240;
    tab5_prescaled_game_transform_result_t game_transform = {ESP_OK, false};
    if (prescaled_game) {
        game_transform = tab5_prescaled_game_transform(source, stride, dest,
            PLATFORM_DISPLAY_NATIVE_WIDTH, PLATFORM_DISPLAY_NATIVE_HEIGHT,
            accelerate);
        ret = game_transform.accelerator_result;
    } else {
        ret=width==1280U?accelerate_ui_region(source,stride,dest,&region,&mapped):accelerate(source,stride,width,dest);
    }
    if (ret==ESP_OK) {
        ++s_stats.accelerated_submits;
        if(replay){++s_stats.partial_content_submits;s_stats.partial_content_source_pixels+=(uint32_t)region.width*region.height;}
    }
    else {
        ++s_stats.accelerator_failures;
        if (s_stats.accelerator_failures==1U)
            ESP_LOGW("tab5_display","PPA_FAILED error=%s fallback=cpu",esp_err_to_name(ret));
        if (!(prescaled_game ? game_transform.complete :
              layout(source,stride,dest,PLATFORM_DISPLAY_NATIVE_WIDTH,PLATFORM_DISPLAY_NATIVE_HEIGHT))) {
            ret=ESP_ERR_INVALID_ARG; goto done;
        }
    }
    tab5_frame_queue_source_complete(&s_queue); /* PPA is BLOCKING; source is consumed before return. */
    s_stats.pipeline_transform_last_us=(uint32_t)(esp_timer_get_time()-transform_start);
    if (s_stats.pipeline_transform_last_us>s_stats.pipeline_transform_max_us)
        s_stats.pipeline_transform_max_us=s_stats.pipeline_transform_last_us;
    const int64_t handoff_start=esp_timer_get_time();
    ret=esp_cache_msync(dest,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret!=ESP_OK) goto done;
    if (s_pattern) {
        ret=esp_lcd_dpi_panel_set_pattern(s_panel,MIPI_DSI_PATTERN_NONE);
        if (ret!=ESP_OK) goto done;
        s_pattern=false;
    }
    ret=esp_lcd_panel_draw_bitmap(s_panel,0,0,720,1280,dest);
    if(ret!=ESP_OK){tab5_frame_queue_cancel(&s_queue,true);goto done;}
    if(!tab5_frame_queue_publish(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE))){ret=ESP_ERR_INVALID_STATE;goto done;}
    s_stats.pipeline_handoff_last_us=(uint32_t)(esp_timer_get_time()-handoff_start);
    if(s_stats.pipeline_handoff_last_us>s_stats.pipeline_handoff_max_us)
        s_stats.pipeline_handoff_max_us=s_stats.pipeline_handoff_last_us;
    s_stats.pipeline_reserved_refreshes=TAB5_RETIRE_REFRESHES;
    if(width==1280U)tab5_frame_history_commit(s_ui_history,(unsigned)slot,source,stride,current);
    else memset(s_ui_history,0,sizeof(s_ui_history));
    const bool first_frame=s_stats.submits_completed==0U;
    ret=wait_presented || first_frame ? finish_pending(start,budget) : ESP_OK;
    if (ret==ESP_OK && first_frame)
        ESP_LOGI("tab5_display", "FIRST_FRAME elapsed_ticks=%lu transform_us=%lu refresh=%lu",
            (unsigned long)(xTaskGetTickCount()-start),
            (unsigned long)s_stats.pipeline_transform_last_us,
            (unsigned long)__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
done:
    if (slot >= 0 && ret == ESP_OK && (width == 320U || width == 768U))
        s_native_margins_black[(unsigned)slot] = true;
    if(ret!=ESP_OK){memset(s_ui_history,0,sizeof(s_ui_history));tab5_frame_queue_cancel(&s_queue,false);} /* The caller may advance its source after a dropped frame. */
    if (ret==ESP_ERR_TIMEOUT) {
        ++s_stats.submit_timeouts;
        ESP_LOGW("tab5_display", "SUBMIT_TIMEOUT pending=%u refresh=%lu armed=%lu elapsed_ticks=%lu budget_ticks=%lu",
            s_queue.pending[s_queue.selected], (unsigned long)__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE),
            (unsigned long)s_queue.published_at[s_queue.selected], (unsigned long)(xTaskGetTickCount()-start), (unsigned long)budget);
    }
    else if (ret!=ESP_OK) ++s_stats.submit_failures;
    xSemaphoreGive(s_lock); return ret;
}
esp_err_t platform_display_submit_ui_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,1280,timeout,platform_display_layout_rgb565_1280x720,false,NULL); }
esp_err_t platform_display_copy_scanout_rgb565(uint16_t *destination,
    size_t destination_bytes,uint32_t timeout_ms)
{
    if (!destination || destination_bytes<FRAME_BYTES) return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    const TickType_t start=xTaskGetTickCount(),budget=pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_lock,budget)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    if (s_ready && !s_pattern && !s_queue.failed) {
        ret=finish_pending(start,budget);
        if (ret==ESP_OK && s_queue.completed==0U) ret=ESP_ERR_INVALID_STATE;
        if (ret==ESP_OK) {
            const uint16_t *source=s_frames[s_queue.selected];
            /* PPA writes memory directly; invalidate before the CPU copy. */
            ret=esp_cache_msync((void *)source,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (ret==ESP_OK) memcpy(destination,source,FRAME_BYTES);
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}
esp_err_t platform_display_submit_ui_region_rgb565(const uint16_t *s,size_t stride,
    const platform_display_rgb565_region_t *r,uint32_t timeout)
{
    platform_display_rgb565_region_t source,destination;
    if(!platform_display_layout_tab5_damage(r,NULL,&source,&destination))return ESP_ERR_INVALID_ARG;
    return submit(s,stride,1280,timeout,platform_display_layout_rgb565_1280x720,false,r);
}
esp_err_t platform_display_submit_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,320,timeout,platform_display_layout_rgb565_320x200,false,NULL); }
esp_err_t platform_display_submit_prescaled_game_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,384,timeout,platform_display_layout_rgb565_prescaled_game_384x240,false,NULL); }
esp_err_t platform_display_submit_shell_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,384,timeout,platform_display_layout_rgb565_384x240,false,NULL); }
esp_err_t platform_display_submit_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,768,timeout,platform_display_layout_rgb565_768x480,false,NULL); }
esp_err_t platform_display_submit_game_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,768,timeout,platform_display_layout_rgb565_768x480,false,NULL); }
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
    tab5_frame_queue_observe(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
    s_stats.submits_completed=s_queue.completed;
    *out=s_stats;
    out->refresh_completions=__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE);
    xSemaphoreGive(s_lock);return ESP_OK;
}
/* Offline Tab5 candidate extracted from exact 0.71 getter. */
esp_err_t platform_display_try_get_stats(platform_display_stats_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_lock) { *out=(platform_display_stats_t){0};return ESP_OK; }
    if (xSemaphoreTake(s_lock,0)!=pdTRUE) return ESP_ERR_TIMEOUT;
    tab5_frame_queue_observe(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
    s_stats.submits_completed=s_queue.completed;
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
    memset(s_ui_history,0,sizeof(s_ui_history));
    memset(s_native_margins_black,0,sizeof(s_native_margins_black));
    if (ret==ESP_OK) s_pattern=value!=MIPI_DSI_PATTERN_NONE;
    if (ret==ESP_OK && pattern==PLATFORM_DISPLAY_PATTERN_BLACK) {
        const TickType_t start=xTaskGetTickCount(),budget=pdMS_TO_TICKS(250);
        int slot=-1;
        ret=wait_frame_slot(start,budget,&slot);
        if (ret==ESP_OK) {
            uint16_t *dest=s_frames[(unsigned)slot];
            memset(dest,0,FRAME_BYTES);
            ret=esp_cache_msync(dest,FRAME_BYTES,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            tab5_frame_queue_source_complete(&s_queue);
            if(ret==ESP_OK){
                ret=esp_lcd_panel_draw_bitmap(s_panel,0,0,720,1280,dest);
                if(ret!=ESP_OK)tab5_frame_queue_cancel(&s_queue,true);
            }
            if(ret==ESP_OK){
                if(!tab5_frame_queue_publish(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE)))ret=ESP_ERR_INVALID_STATE;
                else ret=finish_pending(start,budget);
            }
            if(ret!=ESP_OK)tab5_frame_queue_cancel(&s_queue,false);
        }
    }
    xSemaphoreGive(s_lock);return ret;
}
