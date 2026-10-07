// SPDX-License-Identifier: Apache-2.0
// Panel timings and initialization derived from pinned Espressif Tab5 BSP.
#include "platform/display.h"
#include "platform/tab5.h"
#include "platform_display_layout.h"
#include "tab5_frame_queue.h"
#include "tab5_raster_copy.h"
#include "tab5_ui_scroll.h"
#include "tab5_ui_region.h"
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_ili9881c.h"
#include "esp_lcd_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_cache.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
/* Pinned flash-encryption header pulls an efuse HAL bitfield assignment. */
#include "esp_flash_encrypt.h"
#pragma GCC diagnostic pop
#include "esp_async_fbcpy.h"
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
static esp_async_fbcpy_handle_t s_copy;
static tab5_frame_queue_t s_queue;
static tab5_frame_history_t s_ui_history[TAB5_FRAME_COUNT];
static tab5_ui_scroll_tag_t s_scroll_tags[TAB5_FRAME_COUNT];
/* True only after aligned startup or full PPA invalidation and exclusively DMA-written physical
 * pixels thereafter. Neither the application nor this adapter reads these
 * buffers through the CPU; partial PPA and physical DMA preserve this proof. */
static bool s_frame_dma_clean[TAB5_FRAME_COUNT];
static bool s_physical_scroll_supported;
static bool s_ready, s_backlight, s_pattern;
static StaticSemaphore_t s_lock_storage, s_refresh_storage;
static SemaphoreHandle_t s_lock, s_refresh;
/* Persistent internal callback state; s_lock serializes the complete private
 * fbcpy transaction (its pinned implementation has one static configuration).
 * DPI use_dma2d stays false, so there is no second private copier caller. */
static DRAM_ATTR StaticSemaphore_t s_copy_done_storage;
static DRAM_ATTR SemaphoreHandle_t s_copy_done;
static platform_display_stats_t s_stats;
static uint32_t s_refresh_count;
static portMUX_TYPE s_telemetry_lock=portMUX_INITIALIZER_UNLOCKED;
static int64_t s_last_refresh_us,s_input_us,s_last_attributed_input_us;
static tab5_interactive_handoff_t s_interactive[TAB5_FRAME_COUNT];
#define FRAME_BYTES ((size_t)PLATFORM_DISPLAY_NATIVE_WIDTH * PLATFORM_DISPLAY_NATIVE_HEIGHT * sizeof(uint16_t))
#define COPY_CACHE_CHUNK_BYTES ((size_t)32768U)
static esp_err_t cache_chunks(void *buffer,size_t bytes,int flags);
static void copy_phase_timing(int64_t start,uint32_t *last,uint32_t *peak);
static esp_err_t copy_physical_rectangle_joined(const uint16_t *source,uint16_t *destination,
    platform_display_rgb565_region_t src,platform_display_rgb565_region_t dst,
    uint32_t timeout,bool *accepted);

static bool IRAM_ATTR copy_completed(esp_async_fbcpy_handle_t handle,
    esp_async_fbcpy_event_data_t *event,void *context)
{
    (void)handle;(void)event;(void)context;
    BaseType_t woken=pdFALSE;
    xSemaphoreGiveFromISR(s_copy_done,&woken);
    return woken==pdTRUE;
}

static void IRAM_ATTR record_interactive_refresh(
    const tab5_interactive_handoff_t *sample,int64_t now)
{
    const uint32_t input_elapsed=tab5_elapsed_us(now,sample->input_us);
    const uint32_t handoff_elapsed=tab5_elapsed_us(now,sample->handoff_us);
    s_stats.interactive_latency_samples=tab5_saturating_add(s_stats.interactive_latency_samples,1U);
    s_stats.interactive_input_to_refresh_total_us=tab5_saturating_add(s_stats.interactive_input_to_refresh_total_us,input_elapsed);
    s_stats.interactive_handoff_to_refresh_total_us=tab5_saturating_add(s_stats.interactive_handoff_to_refresh_total_us,handoff_elapsed);
    if(input_elapsed>s_stats.interactive_input_to_refresh_max_us)s_stats.interactive_input_to_refresh_max_us=input_elapsed;
    if(handoff_elapsed>s_stats.interactive_handoff_to_refresh_max_us)s_stats.interactive_handoff_to_refresh_max_us=handoff_elapsed;
    s_stats.interactive_input_to_refresh_last_us=input_elapsed;
    s_stats.interactive_handoff_to_refresh_last_us=handoff_elapsed;
    s_stats.interactive_reuse_wait_last_us=sample->reuse_wait_us;
    s_stats.interactive_transform_last_us=sample->transform_us;
    s_stats.interactive_replay_region_count=sample->replay_regions;
    s_stats.interactive_present_kind=sample->kind;
    if(sample->kind==PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL)
        s_stats.interactive_partial_presentations=tab5_saturating_add(s_stats.interactive_partial_presentations,1U);
    else s_stats.interactive_full_presentations=tab5_saturating_add(s_stats.interactive_full_presentations,1U);
}
static bool IRAM_ATTR refreshed(esp_lcd_panel_handle_t panel,
    esp_lcd_dpi_panel_event_data_t *event, void *context)
{
    (void)panel; (void)event; (void)context;
    const int64_t now=esp_timer_get_time();
    const uint32_t refresh=__atomic_add_fetch(&s_refresh_count,1U,__ATOMIC_RELEASE);
    taskENTER_CRITICAL_ISR(&s_telemetry_lock);
    const uint32_t interval=tab5_elapsed_us(now,s_last_refresh_us);
    if(interval){
        s_stats.pipeline_refresh_interval_last_us=interval;
        if(!s_stats.pipeline_refresh_interval_min_us||interval<s_stats.pipeline_refresh_interval_min_us)
            s_stats.pipeline_refresh_interval_min_us=interval;
        if(interval>s_stats.pipeline_refresh_interval_max_us)s_stats.pipeline_refresh_interval_max_us=interval;
    }
    s_last_refresh_us=now;
    s_stats.pipeline_refresh_events=tab5_saturating_add(s_stats.pipeline_refresh_events,1U);
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i)
        if(tab5_interactive_refresh_ready(&s_interactive[i],refresh)){
            record_interactive_refresh(&s_interactive[i],now);
            s_interactive[i].pending=false;
        }
    taskEXIT_CRITICAL_ISR(&s_telemetry_lock);
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
    memset(s_scroll_tags,0,sizeof(s_scroll_tags));
    memset(s_frame_dma_clean,0,sizeof(s_frame_dma_clean));
    s_physical_scroll_supported=false;
    taskENTER_CRITICAL(&s_telemetry_lock);
    s_last_refresh_us=0;s_input_us=0;s_last_attributed_input_us=0;
    memset(s_interactive,0,sizeof(s_interactive));
    taskEXIT_CRITICAL(&s_telemetry_lock);
    esp_err_t ret=s_backlight?brightness(0):ESP_OK;
    if (ret!=ESP_OK) return ret;
    if (s_scaler) {
        ret=ppa_unregister_client(s_scaler);
        if (ret!=ESP_OK) return ret;
        s_scaler=NULL;
    }
    /* release() is called under s_lock only after any accepted copy joined.
     * The private API cannot cancel safely or uninstall an in-flight copy. */
    if(s_copy){
        ret=esp_async_fbcpy_uninstall(s_copy);
        if(ret!=ESP_OK)return ret;
        s_copy=NULL;
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
        s_copy_done = xSemaphoreCreateBinaryStatic(&s_copy_done_storage);
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
        .flags.use_dma2d=false, /* Only our joined logical-cache copier uses private fbcpy. */
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
    /* Pinned DPI calloc+C2M has already published black memory. Invalidate
     * those CPU cache lines in bounded chunks, without changing framebuffer
     * memory or reading any pixels. Active scanout is read-only, so this also
     * safely establishes the initial DMA-only proof for the selected frame.
     * Untagged retired slots can then repair from the first authoritative UI
     * frame without ever requiring a full PPA warm-up for each initial slot. */
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){
        /* M2C explicitly forbids UNALIGNED in pinned IDF. The DPI allocator
         * aligns its bases and this fixed full-screen size/chunk are cache
         * line multiples. An unavailable proof only costs a warm-up frame;
         * it must not prevent the normal display path from starting. */
        if(((uintptr_t)s_frames[i]%TAB5_RASTER_CACHE_LINE)==0U &&
           (FRAME_BYTES%TAB5_RASTER_CACHE_LINE)==0U){
            const esp_err_t clean=cache_chunks(s_frames[i],FRAME_BYTES,
                ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            s_frame_dma_clean[i]=clean==ESP_OK;
            if(clean!=ESP_OK)ESP_LOGW("tab5_display",
                "SCROLL_STARTUP_FALLBACK slot=%u cache_error=%s",i,esp_err_to_name(clean));
        }else ESP_LOGW("tab5_display",
            "SCROLL_STARTUP_FALLBACK slot=%u reason=cache_alignment",i);
    }
    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {.on_refresh_done=refreshed};
    TRY(esp_lcd_dpi_panel_register_event_callbacks(s_panel,&callbacks,NULL));
    TRY(esp_lcd_panel_disp_on_off(s_panel,true));
    const ppa_client_config_t scaler_config={.oper_type=PPA_OPERATION_SRM,.max_pending_trans_num=1U};
    TRY(ppa_register_client(&scaler_config,&s_scaler));
    const esp_async_fbcpy_config_t copy_config={};
    ret=esp_async_fbcpy_install(&copy_config,&s_copy);
    if(ret!=ESP_OK){
        s_copy=NULL;
        ESP_LOGW("tab5_display","PREPARED_COPY_FALLBACK backend=ppa install_error=%s",esp_err_to_name(ret));
    }else ESP_LOGI("tab5_display","PREPARED_COPY_READY backend=dma2d source_flush=idle destination_preserve=boundary_lines completion=joined");
    /* Odd RGB565 ROI fields are supported by the owner's v1.3 TRM §6.4.11
     * for nonencrypted memory. The whole DPI allocations remain aligned.
     * Do not apply that contract to an encrypted external-memory unit. */
    s_physical_scroll_supported=s_copy&&!esp_flash_encryption_enabled();
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
        for (size_t y=0;y<240U;++y) {
            const uint16_t *row=source+(y*200U/240U)*stride;
            for (size_t x=0;x<384U;++x) s_game_scale[y*384U+x]=row[x*320U/384U];
        }
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
    return ppa_do_scale_rotate_mirror(s_scaler,&op);
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
    /* Pinned PPA pre-invalidates the aligned rotated output rows before DMA.
     * This adapter does not access dest from the CPU afterwards, so another
     * full-frame invalidate/writeback is unnecessary. BLOCKING consumes the
     * source before return; DPI publication later flushes only changed rows. */
    return ppa_do_scale_rotate_mirror(s_scaler,&op);
}
static esp_err_t submit(const uint16_t *source,size_t stride,size_t width,uint32_t timeout,layout_fn layout,bool wait_presented,const platform_display_rgb565_region_t *damage,const platform_display_ui_scroll_t *state)
{
    if (!source || stride<width || stride>SIZE_MAX/720U/sizeof(uint16_t)) return ESP_ERR_INVALID_ARG;
    if(state&&(width!=1280U||!tab5_ui_scroll_state_valid(state)))return ESP_ERR_INVALID_ARG;
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    TickType_t budget=pdMS_TO_TICKS(timeout),start=xTaskGetTickCount();
    if (xSemaphoreTake(s_lock,budget)!=pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    int slot=-1;
    s_stats.ui_region_clone_last_us=0U;
    if (!s_ready) goto done;
    ++s_stats.submits_started;
    const int64_t wait_start=esp_timer_get_time();
    ret=wait_frame_slot(start,budget,&slot);
    s_stats.pipeline_reuse_wait_last_us=(uint32_t)(esp_timer_get_time()-wait_start);
    if(s_stats.pipeline_reuse_wait_last_us>s_stats.pipeline_reuse_wait_max_us)
        s_stats.pipeline_reuse_wait_max_us=s_stats.pipeline_reuse_wait_last_us;
    if (ret!=ESP_OK) goto done;
    uint16_t *dest=s_frames[(unsigned)slot];
    const bool cpu_margins=width!=1280U;
    bool cpu_full_write=false;
    if (cpu_margins) {
        s_frame_dma_clean[slot]=false;
        memset(dest, 0, 64U * 720U * sizeof(*dest));
        memset(dest + 1216U * 720U, 0, 64U * 720U * sizeof(*dest));
    }
    platform_display_rgb565_region_t region={0,0,1280,720},mapped={0,0,720,1280};
    const platform_display_rgb565_region_t current=damage?*damage:region;
    const bool replay=width==1280U&&damage&&s_ui_history[slot].source==source&&s_ui_history[slot].stride==stride;
    const platform_display_rgb565_region_t previous=replay&&s_ui_history[slot].damage.width?s_ui_history[slot].damage:current;
    if(width==1280U&&!platform_display_layout_tab5_damage(&current,replay?&previous:NULL,&region,&mapped)){
        ret=ESP_ERR_INVALID_ARG;goto done;
    }
    const unsigned selected=s_queue.selected;
    const bool clone=width==1280U&&damage&&
        tab5_ui_region_clone_eligible(source,stride,damage,&region,replay,
            &s_ui_history[selected],&s_scroll_tags[selected],
            (uintptr_t)s_frames[selected],(uintptr_t)dest,
            s_physical_scroll_supported&&!s_pattern,
            s_frame_dma_clean[selected],s_frame_dma_clean[slot]);
    bool cloned=false;
    /* Invalidate history before any buffer mutation; failed submissions can
     * only be followed by a complete reconstruction. */
    s_ui_history[slot].source=NULL;
    s_scroll_tags[slot].valid=false;
    const int64_t transform_start=esp_timer_get_time();
    if(clone){
        const int64_t clone_start=esp_timer_get_time();
        bool accepted=false;
        const platform_display_rgb565_region_t whole={0,0,720,1280};
        ret=copy_physical_rectangle_joined(s_frames[selected],dest,whole,whole,timeout,&accepted);
        copy_phase_timing(clone_start,&s_stats.ui_region_clone_last_us,
            &s_stats.ui_region_clone_max_us);
        /* Rejection writes no pixels, so the already-computed replay/full
         * reconstruction remains valid. Accepted DMA retains ownership until
         * successful completion; no unfenced timeout reaches this fallback. */
        if(accepted&&ret==ESP_OK){
            cloned=true;region=current;mapped=tab5_ui_scroll_rotate(current);
        }else if(accepted){
            region=(platform_display_rgb565_region_t){0,0,1280,720};
            mapped=(platform_display_rgb565_region_t){0,0,720,1280};
        }
    }
    ret=width==1280U?accelerate_ui_region(source,stride,dest,&region,&mapped):accelerate(source,stride,width,dest);
    if(ret!=ESP_OK&&cloned){
        /* The ordinary API supplies a complete authoritative raster. After
         * joined clone/region PPA, reconstruct it fully before CPU fallback.
         * Never replay historical damage over a partially replaced target. */
        ++s_stats.accelerator_failures;
        cloned=false;region=(platform_display_rgb565_region_t){0,0,1280,720};
        mapped=(platform_display_rgb565_region_t){0,0,720,1280};
        ret=accelerate_ui_region(source,stride,dest,&region,&mapped);
    }
    if (ret==ESP_OK) {
        if(width==1280U&&region.x==0U&&region.y==0U&&region.width==1280U&&region.height==720U)
            s_frame_dma_clean[slot]=true;
        ++s_stats.accelerated_submits;
        if((replay||cloned)&&((uint32_t)region.width*region.height<1280U*720U)){
            ++s_stats.partial_content_submits;
            s_stats.partial_content_source_pixels=tab5_saturating_add(s_stats.partial_content_source_pixels,(uint32_t)region.width*region.height);
        }
    }
    else {
        ++s_stats.accelerator_failures;
        if (s_stats.accelerator_failures==1U)
            ESP_LOGW("tab5_display","PPA_FAILED error=%s fallback=cpu",esp_err_to_name(ret));
        s_frame_dma_clean[slot]=false;
        if (!layout(source,stride,dest,PLATFORM_DISPLAY_NATIVE_WIDTH,PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
            ret=ESP_ERR_INVALID_ARG; goto done;
        }
        cpu_full_write=true;
        s_frame_dma_clean[slot]=false;
    }
    tab5_frame_queue_source_complete(&s_queue); /* PPA is BLOCKING; source is consumed before return. */
    s_stats.pipeline_transform_last_us=(uint32_t)(esp_timer_get_time()-transform_start);
    if (s_stats.pipeline_transform_last_us>s_stats.pipeline_transform_max_us)
        s_stats.pipeline_transform_max_us=s_stats.pipeline_transform_last_us;
    const int64_t handoff_start=esp_timer_get_time();
    tab5_frame_publication_t publication;
    const platform_display_rgb565_region_t published=cloned?
        (platform_display_rgb565_region_t){0,0,720,1280}:mapped;
    if(!tab5_frame_publication_rows(&published,cpu_full_write,cpu_margins,&publication)){
        ret=ESP_ERR_INVALID_ARG;goto done;
    }
    const bool partial=width==1280U&&!publication.cpu_dirty&&
        (uint32_t)region.width*region.height<1280U*720U;
    if(width==1280U&&damage&&!partial)++s_stats.partial_content_full_fallbacks;
    if (s_pattern) {
        ret=esp_lcd_dpi_panel_set_pattern(s_panel,MIPI_DSI_PATTERN_NONE);
        if (ret!=ESP_OK) goto done;
        s_pattern=false;
    }
    /* dest is exactly a DPI-owned framebuffer. In the pinned driver's
     * no-copy branch these coordinates bound its C2M row flush and still
     * select the complete buffer. CPU fallback and game margins need all rows. */
    ret=esp_lcd_panel_draw_bitmap(s_panel,0,publication.y,720,
        (int)publication.y+publication.height,dest);
    if(ret!=ESP_OK){tab5_frame_queue_cancel(&s_queue,true);goto done;}
    if(!tab5_frame_queue_publish(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE))){ret=ESP_ERR_INVALID_STATE;goto done;}
    if(cloned)s_stats.ui_region_cloned_frames=tab5_saturating_add(s_stats.ui_region_cloned_frames,1U);
    taskENTER_CRITICAL(&s_telemetry_lock);
    if(width==1280U&&s_input_us>0&&s_input_us>s_last_attributed_input_us){
        s_interactive[slot]=(tab5_interactive_handoff_t){
            .input_us=s_input_us,.handoff_us=esp_timer_get_time(),
            .published_refresh=s_queue.published_at[slot],
            .reuse_wait_us=s_stats.pipeline_reuse_wait_last_us,
            .transform_us=s_stats.pipeline_transform_last_us,
            .kind=partial?PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL:PLATFORM_DISPLAY_INTERACTIVE_PRESENT_FULL,
            .replay_regions=partial?1U:0U,.pending=true,
        };
        s_last_attributed_input_us=s_input_us;
    }
    s_input_us=0;
    taskEXIT_CRITICAL(&s_telemetry_lock);
    s_stats.pipeline_handoff_last_us=(uint32_t)(esp_timer_get_time()-handoff_start);
    if(s_stats.pipeline_handoff_last_us>s_stats.pipeline_handoff_max_us)
        s_stats.pipeline_handoff_max_us=s_stats.pipeline_handoff_last_us;
    s_stats.pipeline_reserved_refreshes=TAB5_RETIRE_REFRESHES;
    if(width==1280U)tab5_frame_history_commit(s_ui_history,(unsigned)slot,source,stride,current);
    else memset(s_ui_history,0,sizeof(s_ui_history));
    if(state)tab5_ui_scroll_tag_commit(&s_scroll_tags[slot],source,stride,state);
    else memset(s_scroll_tags,0,sizeof(s_scroll_tags));
    const bool first_frame=s_stats.submits_completed==0U;
    ret=wait_presented || first_frame ? finish_pending(start,budget) : ESP_OK;
    if (ret==ESP_OK && first_frame)
        ESP_LOGI("tab5_display", "FIRST_FRAME elapsed_ticks=%lu transform_us=%lu refresh=%lu",
            (unsigned long)(xTaskGetTickCount()-start),
            (unsigned long)s_stats.pipeline_transform_last_us,
            (unsigned long)__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
done:
    if(ret!=ESP_OK){memset(s_ui_history,0,sizeof(s_ui_history));memset(s_scroll_tags,0,sizeof(s_scroll_tags));tab5_frame_queue_cancel(&s_queue,false);} /* The caller may advance its source after a dropped frame. */
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
{ return submit(s,stride,1280,timeout,platform_display_layout_rgb565_1280x720,false,NULL,NULL); }
esp_err_t platform_display_submit_ui_region_rgb565(const uint16_t *s,size_t stride,
    const platform_display_rgb565_region_t *r,uint32_t timeout)
{
    platform_display_rgb565_region_t source,destination;
    if(!platform_display_layout_tab5_damage(r,NULL,&source,&destination))return ESP_ERR_INVALID_ARG;
    return submit(s,stride,1280,timeout,platform_display_layout_rgb565_1280x720,false,r,NULL);
}
esp_err_t platform_display_submit_ui_tagged_rgb565(const uint16_t *source,size_t stride,
    const platform_display_rgb565_region_t *damage,const platform_display_ui_scroll_t *state,uint32_t timeout)
{
    return submit(source,stride,1280,timeout,platform_display_layout_rgb565_1280x720,false,damage,state);
}
static esp_err_t cache_chunks(void *buffer,size_t bytes,int flags)
{
    for(size_t offset=0;offset<bytes;){
        const size_t remaining=bytes-offset;
        const size_t chunk=remaining>COPY_CACHE_CHUNK_BYTES?COPY_CACHE_CHUNK_BYTES:remaining;
        const esp_err_t ret=esp_cache_msync((uint8_t *)buffer+offset,chunk,flags);
        if(ret!=ESP_OK)return ret;
        offset+=chunk;
    }
    return ESP_OK;
}
static bool copy_destination_owned(const uint16_t *destination,size_t bytes)
{
    const uintptr_t dest=(uintptr_t)destination;
    /* This helper owns logical rasters only. It cannot bypass scanout's
     * retirement fence by accepting any portion of a driver framebuffer. */
    for(unsigned i=0;i<TAB5_FRAME_COUNT;++i){
        const uintptr_t frame=(uintptr_t)s_frames[i];
        if(dest<frame+FRAME_BYTES&&frame<dest+bytes)return false;
    }
    return true;
}
static esp_err_t copy_public_ppa(const uint16_t *source,
    size_t source_stride,size_t source_height,
    const platform_display_rgb565_region_t *source_region,
    uint16_t *destination,size_t destination_stride,size_t destination_height,
    uint16_t destination_x,uint16_t destination_y,const tab5_raster_copy_plan_t *plan)
{
    /* PPA pre-invalidates whole output rows, not only the copied columns.
     * Preserve neighbouring CPU-dirty chrome before that invalidation. The
     * source and destination are exclusively owned through BLOCKING DMA, so
     * no CPU reload can make the pre-invalidated destination stale again. */
    const esp_err_t ret=cache_chunks((uint8_t *)destination+plan->destination_cache_offset,
        plan->destination_cache_bytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if(ret!=ESP_OK)return ret;
    const ppa_srm_oper_config_t op={
        .in={.buffer=source,.pic_w=(uint32_t)source_stride,.pic_h=(uint32_t)source_height,
            .block_offset_x=source_region->x,.block_offset_y=source_region->y,
            .block_w=source_region->width,.block_h=source_region->height,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .out={.buffer=destination,.buffer_size=(uint32_t)plan->destination_bytes,
            .pic_w=(uint32_t)destination_stride,.pic_h=(uint32_t)destination_height,
            .block_offset_x=destination_x,.block_offset_y=destination_y,.srm_cm=PPA_SRM_COLOR_MODE_RGB565},
        .rotation_angle=PPA_SRM_ROTATION_ANGLE_0,.scale_x=1,.scale_y=1,.mode=PPA_TRANS_MODE_BLOCKING,
    };
    return ppa_do_scale_rotate_mirror(s_scaler,&op);
}
static esp_err_t prepare_dma_destination(uint16_t *destination,const tab5_raster_dma_plan_t *plan)
{
    const bool contiguous=plan->cache_bytes==plan->row_stride_bytes;
    for(unsigned row=0;row<plan->row_count;++row){
        const size_t row_offset=(size_t)row*plan->row_stride_bytes;
        for(unsigned boundary=0;boundary<plan->boundary_count;++boundary){
            const esp_err_t ret=esp_cache_msync((uint8_t *)destination+row_offset+
                plan->boundary_offset[boundary],TAB5_RASTER_CACHE_LINE,ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if(ret!=ESP_OK)return ret;
        }
        if(!contiguous){
            const esp_err_t ret=cache_chunks((uint8_t *)destination+row_offset+plan->cache_offset,
                plan->cache_bytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if(ret!=ESP_OK)return ret;
        }
    }
    if(contiguous)return cache_chunks((uint8_t *)destination+plan->cache_offset,
        (size_t)plan->row_count*plan->row_stride_bytes,ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    return ESP_OK;
}
static void copy_phase_timing(int64_t start,uint32_t *last,uint32_t *peak)
{
    *last=tab5_elapsed_us(esp_timer_get_time(),start);
    if(*last>*peak)*peak=*last;
}
static esp_err_t copy_dma_joined(esp_async_fbcpy_trans_desc_t *transaction,
    uint32_t timeout,bool *accepted)
{
    *accepted=false;
    while(xSemaphoreTake(s_copy_done,0U)==pdTRUE){}
    /* The pinned implementation consumes stack transaction fields before
     * return, retaining its own descriptors. The persistent callback state and
     * its one static configuration remain exclusively owned through success. */
    const int64_t enqueue_start=esp_timer_get_time();
    const esp_err_t ret=esp_async_fbcpy(s_copy,transaction,copy_completed,NULL);
    copy_phase_timing(enqueue_start,&s_stats.prepared_copy_enqueue_last_us,
        &s_stats.prepared_copy_enqueue_max_us);
    if(ret!=ESP_OK)return ret;
    *accepted=true;
    const int64_t wait_start=esp_timer_get_time();
    if(xSemaphoreTake(s_copy_done,pdMS_TO_TICKS(timeout))!=pdTRUE){
        ESP_LOGW("tab5_display","PREPARED_COPY_WAIT timeout_ms=%lu source_retained=1 destination_retained=1",
            (unsigned long)timeout);
        /* There is no failure/cancellation callback. A missing success cannot
         * release either surface, uninstall the handle or authorize fallback.
         * The joined UI worker independently holds/poisons on its deadline. */
        while(xSemaphoreTake(s_copy_done,portMAX_DELAY)!=pdTRUE){}
    }
    copy_phase_timing(wait_start,&s_stats.prepared_copy_wait_last_us,
        &s_stats.prepared_copy_wait_max_us);
    return ESP_OK;
}
esp_err_t platform_display_prepare_rgb565_rows(const uint16_t *source,
    size_t source_stride,size_t source_height,size_t first_row,size_t row_count,uint32_t timeout)
{
    tab5_raster_publication_plan_t plan;
    if(!tab5_raster_publication_plan((uintptr_t)source,source_stride,source_height,
        first_row,row_count,&plan))return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(s_lock,pdMS_TO_TICKS(timeout))!=pdTRUE)return ESP_ERR_TIMEOUT;
    const esp_err_t ret=s_ready?cache_chunks((uint8_t *)source+plan.cache_offset,
        plan.cache_bytes,ESP_CACHE_MSYNC_FLAG_DIR_C2M):ESP_ERR_INVALID_STATE;
    xSemaphoreGive(s_lock);return ret;
}
static esp_err_t copy_rectangle(const uint16_t *source,size_t source_stride,size_t source_height,
    const platform_display_rgb565_region_t *source_region,uint16_t *destination,
    size_t destination_stride,size_t destination_height,uint16_t destination_x,
    uint16_t destination_y,uint32_t timeout,bool prepared)
{
    tab5_raster_copy_plan_t plan;
    if(!tab5_raster_copy_plan((uintptr_t)source,source_stride,source_height,source_region,
        (uintptr_t)destination,destination_stride,destination_height,destination_x,destination_y,&plan))
        return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(s_lock,pdMS_TO_TICKS(timeout))!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    if(prepared){
        s_stats.prepared_copy_prepare_last_us=0U;
        s_stats.prepared_copy_enqueue_last_us=0U;
        s_stats.prepared_copy_wait_last_us=0U;
    }
    if(!s_ready)goto done;
    if(!copy_destination_owned(destination,plan.destination_bytes)){ret=ESP_ERR_INVALID_ARG;goto done;}
    tab5_raster_dma_plan_t dma;
    if(!prepared||!s_copy||!tab5_raster_dma_plan((uintptr_t)source,source_stride,source_height,
        source_region,(uintptr_t)destination,destination_stride,destination_height,
        destination_x,destination_y,&dma))goto public_copy;
    /* Pixel addresses are not validated by this private driver. Only RAM
     * backing ranges can be prepared destinations or immutable UI caches. */
    if(!(esp_ptr_internal(source)||esp_ptr_external_ram(source))||
       !(esp_ptr_internal((const uint8_t *)source+plan.source_bytes-1U)||
         esp_ptr_external_ram((const uint8_t *)source+plan.source_bytes-1U))||
       !(esp_ptr_internal(destination)||esp_ptr_external_ram(destination))||
       !(esp_ptr_internal((uint8_t *)destination+plan.destination_bytes-1U)||
         esp_ptr_external_ram((uint8_t *)destination+plan.destination_bytes-1U))){ret=ESP_ERR_INVALID_ARG;goto done;}
    const int64_t prepare_start=esp_timer_get_time();
    ret=prepare_dma_destination(destination,&dma);
    copy_phase_timing(prepare_start,&s_stats.prepared_copy_prepare_last_us,
        &s_stats.prepared_copy_prepare_max_us);
    if(ret!=ESP_OK)goto done;
    esp_async_fbcpy_trans_desc_t transaction={
        .src_buffer=source,.dst_buffer=destination,
        .src_buffer_size_x=source_stride,.src_buffer_size_y=source_height,
        .dst_buffer_size_x=destination_stride,.dst_buffer_size_y=destination_height,
        .src_offset_x=source_region->x,.src_offset_y=source_region->y,
        .dst_offset_x=destination_x,.dst_offset_y=destination_y,
        .copy_size_x=source_region->width,.copy_size_y=source_region->height,
        .pixel_format_unique_id={.color_type_id=LCD_COLOR_FMT_RGB565},
    };
    bool accepted=false;
    ret=copy_dma_joined(&transaction,timeout,&accepted);
    if(ret!=ESP_OK)goto public_copy; /* Rejection occurs before DMA enqueue. */
    /* Every copied cache line was invalidated before DMA and no CPU accesses
     * were allowed while it ran. Joined return makes CPU overlays read fresh
     * memory without a redundant post-invalidation. */
    ret=ESP_OK;
    goto done;
public_copy:
    ret=copy_public_ppa(source,source_stride,source_height,source_region,destination,
        destination_stride,destination_height,destination_x,destination_y,&plan);
done:
    xSemaphoreGive(s_lock);return ret;
}
esp_err_t platform_display_copy_rgb565_rectangle(const uint16_t *source,
    size_t source_stride,size_t source_height,const platform_display_rgb565_region_t *source_region,
    uint16_t *destination,size_t destination_stride,size_t destination_height,
    uint16_t destination_x,uint16_t destination_y,uint32_t timeout)
{
    return copy_rectangle(source,source_stride,source_height,source_region,destination,
        destination_stride,destination_height,destination_x,destination_y,timeout,false);
}
esp_err_t platform_display_copy_prepared_rgb565_rectangle(const uint16_t *source,
    size_t source_stride,size_t source_height,const platform_display_rgb565_region_t *source_region,
    uint16_t *destination,size_t destination_stride,size_t destination_height,
    uint16_t destination_x,uint16_t destination_y,uint32_t timeout)
{
    return copy_rectangle(source,source_stride,source_height,source_region,destination,
        destination_stride,destination_height,destination_x,destination_y,timeout,true);
}
static esp_err_t copy_physical_rectangle_joined(const uint16_t *source,uint16_t *destination,
    platform_display_rgb565_region_t src,platform_display_rgb565_region_t dst,
    uint32_t timeout,bool *accepted)
{
    esp_async_fbcpy_trans_desc_t transaction={
        .src_buffer=source,.dst_buffer=destination,
        .src_buffer_size_x=720U,.src_buffer_size_y=1280U,
        .dst_buffer_size_x=720U,.dst_buffer_size_y=1280U,
        .src_offset_x=src.x,.src_offset_y=src.y,
        .dst_offset_x=dst.x,.dst_offset_y=dst.y,
        .copy_size_x=src.width,.copy_size_y=src.height,
        .pixel_format_unique_id={.color_type_id=LCD_COLOR_FMT_RGB565},
    };
    return copy_dma_joined(&transaction,timeout,accepted);
}
esp_err_t platform_display_submit_ui_scroll_rgb565(const uint16_t *source,size_t stride,
    const platform_display_ui_scroll_t *state,uint32_t timeout)
{
    if(!source||stride<1280U||stride>SIZE_MAX/720U/sizeof(uint16_t)||
        !tab5_ui_scroll_state_valid(state))return ESP_ERR_INVALID_ARG;
    tab5_ui_scroll_plan_t plan;
    if(!tab5_ui_scroll_plan(state,&plan))return ESP_ERR_NOT_SUPPORTED;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    const TickType_t budget=pdMS_TO_TICKS(timeout),start=xTaskGetTickCount();
    if(xSemaphoreTake(s_lock,budget)!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t ret=ESP_ERR_INVALID_STATE;
    int slot=-1;
    bool mutated=false;
    s_stats.prepared_copy_prepare_last_us=0U;
    s_stats.prepared_copy_enqueue_last_us=0U;
    s_stats.prepared_copy_wait_last_us=0U;
    s_stats.ui_scroll_repair_last_us=0U;
    if(!s_ready)goto done;
    ++s_stats.submits_started;
    if(!s_physical_scroll_supported||s_pattern||
       !tab5_ui_scroll_tag_matches_context(&s_scroll_tags[s_queue.selected],source,stride,state,state->previous_context)||
       s_scroll_tags[s_queue.selected].state.current_offset!=state->previous_offset){
        ret=ESP_ERR_NOT_SUPPORTED;goto done;
    }
    const int64_t reuse_start=esp_timer_get_time();
    ret=wait_frame_slot(start,budget,&slot);
    copy_phase_timing(reuse_start,&s_stats.pipeline_reuse_wait_last_us,
        &s_stats.pipeline_reuse_wait_max_us);
    if(ret!=ESP_OK)goto done;
    const unsigned selected=s_queue.selected;
    uint16_t *destination=s_frames[(unsigned)slot];
    const uint16_t *physical_source=s_frames[selected];
    tab5_ui_scroll_repair_t repair;
    bool needs_repair=false;
    if((unsigned)slot==selected||physical_source==destination||
       ((uintptr_t)physical_source&3U)||((uintptr_t)destination&3U)||FRAME_BYTES%4U||
       !tab5_ui_scroll_composition_plan(&s_scroll_tags[selected],&s_scroll_tags[slot],source,
           stride,state,s_frame_dma_clean[slot],&repair,&needs_repair)){
        ret=ESP_ERR_NOT_SUPPORTED;goto done;
    }
    const bool reused_previous_context=tab5_ui_scroll_previous_context_reusable(
        &s_scroll_tags[slot],source,stride,state,s_frame_dma_clean[slot]);
    /* Initialization or full PPA invalidated the complete destination, and
     * subsequent writes were DMA-only. No CPU reads repopulate its cache, so neither C2M nor M2C
     * is needed here. The selected framebuffer's published memory is immutable
     * throughout DMA; concurrent scanout is read-only. Descriptor bases refer
     * to whole 720x1280 allocations, with X/Y/width in pixels. Its 1440-byte row
     * stride and odd RGB565 offsets/widths need no ROI alignment under the
     * nonencrypted-memory v1.3 TRM contract; this is not the logical cache API. */
    const int64_t transform_start=esp_timer_get_time();
    bool accepted=false;
    if(needs_repair){
        const int64_t repair_start=esp_timer_get_time();
        /* Copy only the stationary complement, in up to four disjoint pieces.
         * Its selected source carries the previous context; every difference
         * to the new context is covered by the explicit stationary patch.
         * An older target
         * need not trigger a logical redraw or whole-frame PPA warm-up. */
        for(unsigned i=0;i<repair.count;++i){
            ret=copy_physical_rectangle_joined(physical_source,destination,
                repair.rectangles[i],repair.rectangles[i],timeout,&accepted);
            if(accepted){
                mutated=true;s_scroll_tags[slot].valid=false;
                memset(s_ui_history,0,sizeof(s_ui_history));
            }
            if(ret!=ESP_OK){
                if(!mutated)ret=ESP_ERR_NOT_SUPPORTED;
                goto done;
            }
        }
        copy_phase_timing(repair_start,&s_stats.ui_scroll_repair_last_us,
            &s_stats.ui_scroll_repair_max_us);
    }
    ret=copy_physical_rectangle_joined(physical_source,destination,
        plan.interior_source,plan.interior_destination,timeout,&accepted);
    if(!accepted){if(!mutated)ret=ESP_ERR_NOT_SUPPORTED;goto done;}
    mutated=true;
    s_scroll_tags[slot].valid=false;
    memset(s_ui_history,0,sizeof(s_ui_history));
    if(ret!=ESP_OK)goto done;
    /* Each transaction joins before another uses the pinned private copier's
     * static configuration. PPA is also blocking. Rotate only exposed logical
     * rows and the complete scrollbar, never the stale viewport interior. */
    ret=accelerate_ui_region(source,stride,destination,&plan.exposed,&plan.exposed_destination);
    if(ret==ESP_OK)ret=accelerate_ui_region(source,stride,destination,
        &state->scrollbar,&plan.scrollbar_destination);
    if(ret==ESP_OK&&tab5_ui_scroll_patch_present(state))
        ret=accelerate_ui_region(source,stride,destination,
            &state->stationary_damage,&plan.stationary_destination);
    copy_phase_timing(transform_start,&s_stats.pipeline_transform_last_us,
        &s_stats.pipeline_transform_max_us);
    if(ret!=ESP_OK){++s_stats.accelerator_failures;goto done;}
    ++s_stats.accelerated_submits;
    ++s_stats.partial_content_submits;
    const uint32_t source_pixels=(uint32_t)plan.exposed.width*plan.exposed.height+
        (uint32_t)state->scrollbar.width*state->scrollbar.height+
        (uint32_t)state->stationary_damage.width*state->stationary_damage.height;
    s_stats.partial_content_source_pixels=tab5_saturating_add(s_stats.partial_content_source_pixels,source_pixels);
    tab5_frame_queue_source_complete(&s_queue);
    tab5_frame_publication_t publication;
    const platform_display_rgb565_region_t published=needs_repair?
        (platform_display_rgb565_region_t){0,0,720,1280}:plan.physical_damage;
    if(!tab5_frame_publication_rows(&published,false,false,&publication)){
        ret=ESP_ERR_INVALID_STATE;goto done;
    }
    const int64_t handoff_start=esp_timer_get_time();
    /* Exact driver-owned pointer selects the composed buffer. Publish changed
     * rows, including the whole frame when stationary context was repaired. */
    ret=esp_lcd_panel_draw_bitmap(s_panel,0,publication.y,720,
        (int)publication.y+publication.height,destination);
    if(ret!=ESP_OK){tab5_frame_queue_cancel(&s_queue,true);goto done;}
    if(!tab5_frame_queue_publish(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE))){
        ret=ESP_ERR_INVALID_STATE;goto done;
    }
    tab5_ui_scroll_tag_commit(&s_scroll_tags[slot],source,stride,state);
    if(needs_repair)s_stats.ui_scroll_repaired_frames=
        tab5_saturating_add(s_stats.ui_scroll_repaired_frames,1U);
    if(reused_previous_context)s_stats.ui_scroll_previous_context_reused_frames=
        tab5_saturating_add(s_stats.ui_scroll_previous_context_reused_frames,1U);
    taskENTER_CRITICAL(&s_telemetry_lock);
    if(s_input_us>0&&s_input_us>s_last_attributed_input_us){
        s_interactive[slot]=(tab5_interactive_handoff_t){
            .input_us=s_input_us,.handoff_us=esp_timer_get_time(),
            .published_refresh=s_queue.published_at[slot],
            .reuse_wait_us=s_stats.pipeline_reuse_wait_last_us,
            .transform_us=s_stats.pipeline_transform_last_us,
            .kind=PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL,
            .replay_regions=tab5_ui_scroll_patch_present(state)?3U:2U,.pending=true,
        };
        s_last_attributed_input_us=s_input_us;
    }
    s_input_us=0;
    taskEXIT_CRITICAL(&s_telemetry_lock);
    copy_phase_timing(handoff_start,&s_stats.pipeline_handoff_last_us,
        &s_stats.pipeline_handoff_max_us);
    s_stats.pipeline_reserved_refreshes=TAB5_RETIRE_REFRESHES;
    ret=ESP_OK;
done:
    if(ret!=ESP_OK){
        /* Eligibility/enqueue rejection touched no pixels: keep every existing
         * tag/history so ordinary fallback can warm another slot. After any
         * accepted DMA, no incomplete-source fallback is authorized. */
        if(mutated){
            memset(s_ui_history,0,sizeof(s_ui_history));
            memset(s_scroll_tags,0,sizeof(s_scroll_tags));
            if(ret==ESP_ERR_NOT_SUPPORTED)ret=ESP_FAIL;
        }
        tab5_frame_queue_cancel(&s_queue,false);
        if(ret==ESP_ERR_TIMEOUT)++s_stats.submit_timeouts;
        else if(ret!=ESP_ERR_NOT_SUPPORTED)++s_stats.submit_failures;
    }
    xSemaphoreGive(s_lock);return ret;
}
esp_err_t platform_display_submit_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,320,timeout,platform_display_layout_rgb565_320x200,false,NULL,NULL); }
esp_err_t platform_display_submit_shell_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,384,timeout,platform_display_layout_rgb565_384x240,false,NULL,NULL); }
esp_err_t platform_display_submit_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,768,timeout,platform_display_layout_rgb565_768x480,false,NULL,NULL); }
esp_err_t platform_display_submit_game_content_rgb565(const uint16_t *s,size_t stride,uint32_t timeout)
{ return submit(s,stride,768,timeout,platform_display_layout_rgb565_768x480,false,NULL,NULL); }
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
{
    if(timestamp<0)return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(&s_telemetry_lock);s_input_us=timestamp;taskEXIT_CRITICAL(&s_telemetry_lock);
    return ESP_OK;
}
esp_err_t platform_display_get_stats(platform_display_stats_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_lock) { *out=(platform_display_stats_t){0};return ESP_OK; }
    if (xSemaphoreTake(s_lock,pdMS_TO_TICKS(1000))!=pdTRUE) return ESP_ERR_TIMEOUT;
    tab5_frame_queue_observe(&s_queue,__atomic_load_n(&s_refresh_count,__ATOMIC_ACQUIRE));
    s_stats.submits_completed=s_queue.completed;
    taskENTER_CRITICAL(&s_telemetry_lock);*out=s_stats;taskEXIT_CRITICAL(&s_telemetry_lock);
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
    memset(s_scroll_tags,0,sizeof(s_scroll_tags));
    if (ret==ESP_OK) s_pattern=value!=MIPI_DSI_PATTERN_NONE;
    if (ret==ESP_OK && pattern==PLATFORM_DISPLAY_PATTERN_BLACK) {
        const TickType_t start=xTaskGetTickCount(),budget=pdMS_TO_TICKS(250);
        int slot=-1;
        ret=wait_frame_slot(start,budget,&slot);
        if (ret==ESP_OK) {
            uint16_t *dest=s_frames[(unsigned)slot];
            s_frame_dma_clean[slot]=false;
            memset(dest,0,FRAME_BYTES);
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
