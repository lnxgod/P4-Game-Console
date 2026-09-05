#include "platform/display.h"
#include "platform_display_layout.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#pragma GCC diagnostic push
/* ESP-IDF 5.5.3 has two sign-conversion warnings in inline RISC-V headers. */
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "driver/gpio.h"
#include "driver/ledc.h"
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#include "driver/ppa.h"
#include "esp_lcd_st7701.h"
#include "waveshare_st7701_init.h"
#else
#include "esp_lcd_ek79007.h"
#endif
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#pragma GCC diagnostic pop

static const char *TAG = "platform_display";

/*
 * Display-only constants authorized by hardware/evidence/
 * elecrow-10.1-display-path.json. GPIO29 and GPIO41 remain untouched.
 */
#define DISPLAY_DSI_BUS_ID 0
#define DISPLAY_DSI_DATA_LANES 2
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#define DISPLAY_DSI_LANE_RATE_MBPS 500
#define DISPLAY_DPI_CLOCK_MHZ 30
#define DISPLAY_DPI_HBP 42
#define DISPLAY_DPI_HSYNC 12
#define DISPLAY_DPI_HFP 42
#define DISPLAY_DPI_VBP 2
#define DISPLAY_DPI_VSYNC 8
#define DISPLAY_DPI_VFP 60
#else
#define DISPLAY_DSI_LANE_RATE_MBPS 900
#define DISPLAY_DPI_CLOCK_MHZ 51
#define DISPLAY_DPI_HBP 160
#define DISPLAY_DPI_HSYNC 70
#define DISPLAY_DPI_HFP 160
#define DISPLAY_DPI_VBP 23
#define DISPLAY_DPI_VSYNC 10
#define DISPLAY_DPI_VFP 12
#endif
#define DISPLAY_DPHY_LDO_CHANNEL 3
#define DISPLAY_DPHY_LDO_MV 2500
#define DISPLAY_PANEL_LDO_CHANNEL 4
#define DISPLAY_PATTERN_HANDOFF_TIMEOUT_MS 250U
#define DISPLAY_PANEL_LDO_MV 3300
#define DISPLAY_BACKLIGHT_GPIO \
    ((gpio_num_t)PLATFORM_BOARD_LCD_BACKLIGHT_GPIO)
#define DISPLAY_BACKLIGHT_PWM_HZ 30000
#define DISPLAY_BACKLIGHT_DUTY_BITS LEDC_TIMER_11_BIT
#define DISPLAY_BACKLIGHT_MAX_DUTY 2047U
#define DISPLAY_FRAME_PIXELS \
    ((size_t)PLATFORM_DISPLAY_NATIVE_WIDTH * \
     (size_t)PLATFORM_DISPLAY_NATIVE_HEIGHT)
#define DISPLAY_FRAME_BYTES (DISPLAY_FRAME_PIXELS * sizeof(uint16_t))
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#define DISPLAY_GAME_PPA_SCALE 2.375f
#define DISPLAY_GAME_PPA_OFFSET_X 2U
#define DISPLAY_GAME_PPA_OFFSET_Y 20U
#define DISPLAY_GAME_PPA_WIDTH 475U
#define DISPLAY_GAME_PPA_HEIGHT 760U
#define DISPLAY_SHELL_PPA_SCALE 2.0f
#define DISPLAY_SHELL_PPA_OFFSET_X 0U
#define DISPLAY_SHELL_PPA_OFFSET_Y 16U
#define DISPLAY_SHELL_PPA_WIDTH 480U
#define DISPLAY_SHELL_PPA_HEIGHT 768U
#define DISPLAY_CONTENT_PPA_SCALE 1.0f
#define DISPLAY_CONTENT_PPA_OFFSET_X 0U
#define DISPLAY_CONTENT_PPA_OFFSET_Y 16U
#define DISPLAY_CONTENT_PPA_WIDTH 480U
#define DISPLAY_CONTENT_PPA_HEIGHT 768U
#endif

static esp_ldo_channel_handle_t s_dphy_ldo;
static esp_ldo_channel_handle_t s_panel_ldo;
static esp_lcd_dsi_bus_handle_t s_dsi_bus;
static esp_lcd_panel_io_handle_t s_dbi_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_backlight_ready;
static bool s_initialized;
static bool s_pattern_active;
static uint16_t *s_submit_frame;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#define DISPLAY_PANEL_FRAME_NONE UINT8_MAX
#define DISPLAY_CONTENT_HISTORY_DEPTH 4U
#define DISPLAY_CONTENT_MAX_REGIONS 4U
#define DISPLAY_INTERACTIVE_INPUT_MAX_AGE_US INT64_C(250000)

_Static_assert(sizeof(uintptr_t) == sizeof(uint32_t),
               "Waveshare callback token requires the ESP32-P4 RV32 ABI");

typedef enum {
    DISPLAY_MARGIN_PROFILE_DIRTY = 0,
    DISPLAY_MARGIN_PROFILE_GAME,
    DISPLAY_MARGIN_PROFILE_SHELL,
    DISPLAY_MARGIN_PROFILE_CONTENT,
} display_margin_profile_t;

typedef enum {
    DISPLAY_ACCELERATED_LAYOUT_GAME = 0,
    DISPLAY_ACCELERATED_LAYOUT_SHELL,
    DISPLAY_ACCELERATED_LAYOUT_CONTENT,
} display_accelerated_layout_t;

typedef enum {
    DISPLAY_PANEL_HANDOFF_NONE = 0,
    DISPLAY_PANEL_HANDOFF_RESERVED,
    DISPLAY_PANEL_HANDOFF_ARMED,
} display_panel_handoff_state_t;

static ppa_client_handle_t s_scaler;
static uint16_t *s_panel_frames[2];
/*
 * The panel driver applies its selected frame buffer at a refresh boundary.
 * A buffer is writable only when it is not the confirmed scanout buffer and
 * no unconfirmed handoff could still select it.  These fields are shared with
 * the DPI refresh ISR, so always access them under s_panel_frame_lock.
 */
static uint8_t s_confirmed_active_panel_frame;
static uint8_t s_pending_panel_frame = DISPLAY_PANEL_FRAME_NONE;
static display_panel_handoff_state_t s_panel_handoff_state;
static portMUX_TYPE s_panel_frame_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_refresh_callback_generation;
static bool s_refresh_callback_enabled;
static int64_t s_last_refresh_event_us;
static display_margin_profile_t s_margin_profiles[2];
static bool s_accelerator_failure_logged;

/* The producer records an input time while holding s_api_lock. It is copied
 * into one pending handoff under the frame lock, then consumed exactly once
 * by the DPI refresh ISR. Games never arm this metadata. */
typedef struct {
    bool valid;
    int64_t input_timestamp_us;
    int64_t armed_timestamp_us;
    uint32_t reuse_wait_us;
    uint32_t transform_us;
    uint8_t replay_region_count;
    platform_display_interactive_present_kind_t kind;
} display_interactive_handoff_t;

static int64_t s_interactive_input_timestamp_us;
static display_interactive_handoff_t s_interactive_handoff;

typedef struct {
    uint32_t generation;
    uint8_t region_count;
    platform_display_rgb565_region_t regions[DISPLAY_CONTENT_MAX_REGIONS];
} display_content_generation_t;

/* A partial source update is valid only when the target framebuffer contains
 * the same authoritative source generation apart from the recorded changes.
 * Double buffering means the inactive buffer normally trails by two submits,
 * so retain a bounded history and replay it from the caller's current full
 * source before selecting that buffer. */
static uint32_t s_content_generation;
static uint32_t s_panel_content_generation[2];
static bool s_panel_content_valid[2];
static uintptr_t s_content_source_pixels;
static size_t s_content_source_stride;
static display_content_generation_t
    s_content_history[DISPLAY_CONTENT_HISTORY_DEPTH];
#endif
static StaticSemaphore_t s_api_lock_storage;
static StaticSemaphore_t s_refresh_signal_storage;
static SemaphoreHandle_t s_api_lock;
static SemaphoreHandle_t s_refresh_signal;
static portMUX_TYPE s_sync_init_lock = portMUX_INITIALIZER_UNLOCKED;
static platform_display_stats_t s_stats;

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
static uint32_t elapsed_microseconds(int64_t started_us)
{
    const int64_t now_us = esp_timer_get_time();
    if (now_us <= started_us) {
        return 0U;
    }
    const uint64_t elapsed_us = (uint64_t)(now_us - started_us);
    return elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
}

static void record_pipeline_phase(uint32_t *last,
                                  uint32_t *maximum,
                                  uint32_t elapsed_us)
{
    __atomic_store_n(last, elapsed_us, __ATOMIC_RELAXED);
    uint32_t observed = __atomic_load_n(maximum, __ATOMIC_RELAXED);
    while (elapsed_us > observed &&
           !__atomic_compare_exchange_n(maximum, &observed, elapsed_us,
                                        false, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
}

static void IRAM_ATTR record_pipeline_refresh_interval(uint32_t elapsed_us)
{
    __atomic_store_n(&s_stats.pipeline_refresh_interval_last_us, elapsed_us,
                     __ATOMIC_RELAXED);
    uint32_t observed = __atomic_load_n(
        &s_stats.pipeline_refresh_interval_max_us, __ATOMIC_RELAXED);
    while (elapsed_us > observed &&
           !__atomic_compare_exchange_n(
               &s_stats.pipeline_refresh_interval_max_us, &observed,
               elapsed_us, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
    observed = __atomic_load_n(&s_stats.pipeline_refresh_interval_min_us,
                               __ATOMIC_RELAXED);
    while ((observed == 0U || elapsed_us < observed) &&
           !__atomic_compare_exchange_n(
               &s_stats.pipeline_refresh_interval_min_us, &observed,
               elapsed_us, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}
#endif

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

#if !CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
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
#endif

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
static bool panel_handoff_pending(void)
{
    bool pending;
    taskENTER_CRITICAL(&s_panel_frame_lock);
    pending = s_panel_handoff_state != DISPLAY_PANEL_HANDOFF_NONE;
    taskEXIT_CRITICAL(&s_panel_frame_lock);
    return pending;
}

static uint8_t confirmed_active_panel_frame(void)
{
    uint8_t active;
    taskENTER_CRITICAL(&s_panel_frame_lock);
    active = s_confirmed_active_panel_frame;
    taskEXIT_CRITICAL(&s_panel_frame_lock);
    return active;
}

static bool reserve_panel_handoff(uint8_t frame_index)
{
    bool reserved = false;
    taskENTER_CRITICAL(&s_panel_frame_lock);
    if (s_panel_handoff_state == DISPLAY_PANEL_HANDOFF_NONE) {
        s_pending_panel_frame = frame_index;
        s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_RESERVED;
        reserved = true;
    }
    taskEXIT_CRITICAL(&s_panel_frame_lock);
    return reserved;
}

static bool arm_panel_handoff(
    uint8_t frame_index, const display_interactive_handoff_t *interactive)
{
    bool armed = false;
    taskENTER_CRITICAL(&s_panel_frame_lock);
    if (s_panel_handoff_state == DISPLAY_PANEL_HANDOFF_RESERVED &&
        s_pending_panel_frame == frame_index) {
        s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_ARMED;
        if (interactive != NULL && interactive->valid) {
            s_interactive_handoff = *interactive;
        } else {
            memset(&s_interactive_handoff, 0, sizeof(s_interactive_handoff));
        }
        armed = true;
    }
    taskEXIT_CRITICAL(&s_panel_frame_lock);
    return armed;
}

static void cancel_reserved_panel_handoff(uint8_t frame_index)
{
    taskENTER_CRITICAL(&s_panel_frame_lock);
    if (s_panel_handoff_state == DISPLAY_PANEL_HANDOFF_RESERVED &&
        s_pending_panel_frame == frame_index) {
        s_pending_panel_frame = DISPLAY_PANEL_FRAME_NONE;
        s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_NONE;
        memset(&s_interactive_handoff, 0, sizeof(s_interactive_handoff));
    }
    taskEXIT_CRITICAL(&s_panel_frame_lock);
}

/*
 * A stale binary-semaphore wakeup is harmless: the loop re-checks the
 * ownership state before consuming another wakeup.  This lets a completed
 * handoff be observed even if the ISR ran just before xSemaphoreTake().
 */
static esp_err_t wait_for_pending_panel_handoff(TickType_t started,
                                                TickType_t budget)
{
    while (panel_handoff_pending()) {
        const TickType_t remaining = remaining_ticks(started, budget);
        if (remaining == 0 ||
            xSemaphoreTake(s_refresh_signal, remaining) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static void reset_panel_frame_ownership(void)
{
    taskENTER_CRITICAL(&s_panel_frame_lock);
    s_confirmed_active_panel_frame = 0U;
    s_pending_panel_frame = DISPLAY_PANEL_FRAME_NONE;
    s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_NONE;
    s_last_refresh_event_us = 0;
    memset(&s_interactive_handoff, 0, sizeof(s_interactive_handoff));
    taskEXIT_CRITICAL(&s_panel_frame_lock);
}

static void invalidate_content_region_state(void)
{
    s_content_generation = 0U;
    memset(s_panel_content_generation, 0,
           sizeof(s_panel_content_generation));
    memset(s_panel_content_valid, 0, sizeof(s_panel_content_valid));
    s_content_source_pixels = 0U;
    s_content_source_stride = 0U;
    memset(s_content_history, 0, sizeof(s_content_history));
}

static void IRAM_ATTR saturating_atomic_add_u32(uint32_t *value,
                                                uint32_t increment)
{
    uint32_t observed = __atomic_load_n(value, __ATOMIC_RELAXED);
    for (;;) {
        const uint32_t desired = UINT32_MAX - observed < increment
            ? UINT32_MAX : observed + increment;
        if (__atomic_compare_exchange_n(
                value, &observed, desired, false,
                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return;
        }
    }
}

static uint32_t prepare_refresh_callback_generation(void)
{
    taskENTER_CRITICAL(&s_panel_frame_lock);
    ++s_refresh_callback_generation;
    if (s_refresh_callback_generation == 0U) {
        ++s_refresh_callback_generation;
    }
    s_refresh_callback_enabled = false;
    s_confirmed_active_panel_frame = 0U;
    s_pending_panel_frame = DISPLAY_PANEL_FRAME_NONE;
    s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_NONE;
    s_last_refresh_event_us = 0;
    const uint32_t generation = s_refresh_callback_generation;
    taskEXIT_CRITICAL(&s_panel_frame_lock);
    return generation;
}

static void enable_refresh_callback_generation(uint32_t generation)
{
    taskENTER_CRITICAL(&s_panel_frame_lock);
    s_refresh_callback_enabled =
        generation != 0U && generation == s_refresh_callback_generation;
    taskEXIT_CRITICAL(&s_panel_frame_lock);
}

static void invalidate_refresh_callback_generation(void)
{
    taskENTER_CRITICAL(&s_panel_frame_lock);
    s_refresh_callback_enabled = false;
    ++s_refresh_callback_generation;
    if (s_refresh_callback_generation == 0U) {
        ++s_refresh_callback_generation;
    }
    taskEXIT_CRITICAL(&s_panel_frame_lock);
}

static void IRAM_ATTR record_interactive_latency_max(uint32_t *maximum,
                                                      uint32_t elapsed_us)
{
    uint32_t observed = __atomic_load_n(maximum, __ATOMIC_RELAXED);
    while (elapsed_us > observed &&
           !__atomic_compare_exchange_n(
               maximum, &observed, elapsed_us, false,
               __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static uint32_t IRAM_ATTR bounded_elapsed_us(int64_t ended_us,
                                             int64_t started_us)
{
    if (ended_us <= started_us) {
        return 0U;
    }
    const uint64_t elapsed_us = (uint64_t)(ended_us - started_us);
    return elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
}

static void IRAM_ATTR record_interactive_handoff(
    const display_interactive_handoff_t *handoff, int64_t refresh_time_us)
{
    if (handoff == NULL || !handoff->valid ||
        handoff->kind == PLATFORM_DISPLAY_INTERACTIVE_PRESENT_NONE) {
        return;
    }
    const uint32_t handoff_to_refresh_us = bounded_elapsed_us(
        refresh_time_us, handoff->armed_timestamp_us);
    const uint32_t input_to_refresh_us = bounded_elapsed_us(
        refresh_time_us, handoff->input_timestamp_us);
    saturating_atomic_add_u32(&s_stats.interactive_latency_samples, 1U);
    if (handoff->kind == PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL) {
        saturating_atomic_add_u32(
            &s_stats.interactive_partial_presentations, 1U);
    } else {
        saturating_atomic_add_u32(&s_stats.interactive_full_presentations,
                                  1U);
    }
    saturating_atomic_add_u32(&s_stats.interactive_input_to_refresh_total_us,
                              input_to_refresh_us);
    __atomic_store_n(&s_stats.interactive_input_to_refresh_last_us,
                     input_to_refresh_us, __ATOMIC_RELAXED);
    record_interactive_latency_max(
        &s_stats.interactive_input_to_refresh_max_us, input_to_refresh_us);
    saturating_atomic_add_u32(
        &s_stats.interactive_handoff_to_refresh_total_us,
        handoff_to_refresh_us);
    __atomic_store_n(&s_stats.interactive_handoff_to_refresh_last_us,
                     handoff_to_refresh_us, __ATOMIC_RELAXED);
    record_interactive_latency_max(
        &s_stats.interactive_handoff_to_refresh_max_us,
        handoff_to_refresh_us);
    __atomic_store_n(&s_stats.interactive_reuse_wait_last_us,
                     handoff->reuse_wait_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_stats.interactive_transform_last_us,
                     handoff->transform_us, __ATOMIC_RELAXED);
    __atomic_store_n(&s_stats.interactive_replay_region_count,
                     handoff->replay_region_count, __ATOMIC_RELAXED);
    __atomic_store_n(&s_stats.interactive_present_kind, handoff->kind,
                     __ATOMIC_RELAXED);
}
#endif

static bool IRAM_ATTR on_refresh_done(esp_lcd_panel_handle_t panel,
                                      esp_lcd_dpi_panel_event_data_t *event,
                                      void *user_context)
{
    (void)panel;
    (void)event;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const uint32_t callback_generation = (uint32_t)(uintptr_t)user_context;
    bool handoff_completed = false;
    display_interactive_handoff_t completed_interactive = {0};
    bool callback_current = false;
    uint32_t refresh_interval_us = 0U;
    const int64_t callback_time_us = esp_timer_get_time();
    taskENTER_CRITICAL_ISR(&s_panel_frame_lock);
    callback_current = s_refresh_callback_enabled &&
        callback_generation == s_refresh_callback_generation;
    if (callback_current &&
        s_panel_handoff_state == DISPLAY_PANEL_HANDOFF_RESERVED) {
        __atomic_fetch_add(&s_stats.pipeline_reserved_refreshes, 1U,
                           __ATOMIC_RELAXED);
    }
    if (callback_current) {
        if (s_last_refresh_event_us != 0 &&
            callback_time_us > s_last_refresh_event_us) {
            const uint64_t interval_us = (uint64_t)(
                callback_time_us - s_last_refresh_event_us);
            refresh_interval_us = interval_us > UINT32_MAX
                ? UINT32_MAX : (uint32_t)interval_us;
        }
        s_last_refresh_event_us = callback_time_us;
    }
    if (callback_current &&
        s_panel_handoff_state == DISPLAY_PANEL_HANDOFF_ARMED &&
        s_pending_panel_frame != DISPLAY_PANEL_FRAME_NONE) {
        s_confirmed_active_panel_frame = s_pending_panel_frame;
        s_pending_panel_frame = DISPLAY_PANEL_FRAME_NONE;
        s_panel_handoff_state = DISPLAY_PANEL_HANDOFF_NONE;
        completed_interactive = s_interactive_handoff;
        s_interactive_handoff = (display_interactive_handoff_t){0};
        handoff_completed = true;
    }
    taskEXIT_CRITICAL_ISR(&s_panel_frame_lock);
    if (!callback_current) {
        return false;
    }
    __atomic_fetch_add(&s_stats.pipeline_refresh_events, 1U,
                       __ATOMIC_RELAXED);
    if (refresh_interval_us != 0U) {
        record_pipeline_refresh_interval(refresh_interval_us);
    }
    if (handoff_completed) {
        record_interactive_handoff(&completed_interactive, callback_time_us);
    }
#else
    (void)user_context;
#endif

    __atomic_fetch_add(&s_stats.refresh_completions, 1U, __ATOMIC_RELAXED);
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    if (handoff_completed) {
        __atomic_fetch_add(&s_stats.submits_completed, 1U,
                           __ATOMIC_RELAXED);
    }
#endif
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (s_refresh_signal != NULL) {
        (void)xSemaphoreGiveFromISR(s_refresh_signal,
                                    &higher_priority_task_woken);
    }
    return higher_priority_task_woken == pdTRUE;
}

static esp_err_t configure_backlight_dark(void)
{
    const gpio_config_t pin_config = {
        .pin_bit_mask = UINT64_C(1) << DISPLAY_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&pin_config);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_level(DISPLAY_BACKLIGHT_GPIO, 0);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_timer_config_t timer_config = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = DISPLAY_BACKLIGHT_DUTY_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = DISPLAY_BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_USE_PLL_DIV_CLK,
        .deconfigure = false,
    };
    err = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        return err;
    }

    const ledc_channel_config_t channel_config = {
        .gpio_num = DISPLAY_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    err = ledc_channel_config(&channel_config);
    if (err == ESP_OK) {
        s_backlight_ready = true;
    }
    return err;
}

static void release_owned_resources(void)
{
    s_initialized = false;
    s_pattern_active = false;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    /* Make an ISR that began before callback unregister harmless. */
    invalidate_refresh_callback_generation();
#endif
    if (s_panel != NULL) {
        const esp_lcd_dpi_panel_event_callbacks_t callbacks = {0};
        (void)esp_lcd_dpi_panel_register_event_callbacks(s_panel, &callbacks,
                                                         NULL);
    }
    if (s_backlight_ready) {
        (void)ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        (void)ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        (void)ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        s_backlight_ready = false;
    }
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    if (s_scaler != NULL) {
        (void)ppa_unregister_client(s_scaler);
        s_scaler = NULL;
    }
    memset(s_panel_frames, 0, sizeof(s_panel_frames));
    memset(s_margin_profiles, 0, sizeof(s_margin_profiles));
    reset_panel_frame_ownership();
    invalidate_content_region_state();
    s_interactive_input_timestamp_us = 0;
    s_accelerator_failure_logged = false;
#endif
    if (s_submit_frame != NULL) {
        heap_caps_free(s_submit_frame);
        s_submit_frame = NULL;
    }
    if (s_panel != NULL) {
        (void)esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_dbi_io != NULL) {
        (void)esp_lcd_panel_io_del(s_dbi_io);
        s_dbi_io = NULL;
    }
    if (s_dsi_bus != NULL) {
        (void)esp_lcd_del_dsi_bus(s_dsi_bus);
        s_dsi_bus = NULL;
    }
    if (s_panel_ldo != NULL) {
        (void)esp_ldo_release_channel(s_panel_ldo);
        s_panel_ldo = NULL;
    }
    if (s_dphy_ldo != NULL) {
        (void)esp_ldo_release_channel(s_dphy_ldo);
        s_dphy_ldo = NULL;
    }
}

static esp_err_t set_brightness_locked(uint8_t percent)
{
    if (percent > 100U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_backlight_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Zero is always allowed for fail-dark cleanup. Light is allowed only
     * after panel reset and initialization completed successfully. */
    if (percent != 0U && !s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t duty = (DISPLAY_BACKLIGHT_MAX_DUTY * (uint32_t)percent) / 100U;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t platform_display_set_brightness(uint8_t percent)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t err = set_brightness_locked(percent);
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

esp_err_t platform_display_init(void)
{
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
#if !CONFIG_PLATFORM_DISPLAY_WAVESHARE_4_3_BUILD_ONLY
    ESP_LOGE(TAG, "P4_DISPLAY AUTHORIZATION_DENIED board=waveshare-4.3");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
#if !CONFIG_PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED
    ESP_LOGE(TAG, "P4_DISPLAY M1 AUTHORIZATION_DENIED scope=display-only");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#endif

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

    ESP_LOGI(TAG, "P4_DISPLAY M1 START profile=%s", platform_board_name());
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 SCOPE waveshare-st7701 backlight_gpio=%d "
             "reset_gpio=%d",
             PLATFORM_BOARD_LCD_BACKLIGHT_GPIO,
             PLATFORM_BOARD_LCD_RESET_GPIO);
#else
    ESP_LOGI(TAG, "P4_DISPLAY M1 SCOPE display-only gpio29=untouched gpio41=untouched");
#endif

    esp_err_t err = configure_backlight_dark();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=backlight-dark error=%s", esp_err_to_name(err));
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    ESP_LOGI(TAG, "P4_DISPLAY M1 BACKLIGHT_DARK gpio=%d pwm_hz=%u",
             PLATFORM_BOARD_LCD_BACKLIGHT_GPIO,
             (unsigned)DISPLAY_BACKLIGHT_PWM_HZ);

    const esp_ldo_channel_config_t dphy_ldo_config = {
        .chan_id = DISPLAY_DPHY_LDO_CHANNEL,
        .voltage_mv = DISPLAY_DPHY_LDO_MV,
    };
    err = esp_ldo_acquire_channel(&dphy_ldo_config, &s_dphy_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=ldo3 error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

#if !CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const esp_ldo_channel_config_t panel_ldo_config = {
        .chan_id = DISPLAY_PANEL_LDO_CHANNEL,
        .voltage_mv = DISPLAY_PANEL_LDO_MV,
    };
    err = esp_ldo_acquire_channel(&panel_ldo_config, &s_panel_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=ldo4 error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
#endif
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 POWER_READY ldo3_mv=%u panel_rail=board-owned",
             (unsigned)DISPLAY_DPHY_LDO_MV);
#else
    ESP_LOGI(TAG, "P4_DISPLAY M1 POWER_READY ldo3_mv=%u ldo4_mv=%u",
             (unsigned)DISPLAY_DPHY_LDO_MV,
             (unsigned)DISPLAY_PANEL_LDO_MV);
#endif

    const esp_lcd_dsi_bus_config_t dsi_bus_config = {
        .bus_id = DISPLAY_DSI_BUS_ID,
        .num_data_lanes = DISPLAY_DSI_DATA_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DISPLAY_DSI_LANE_RATE_MBPS,
    };
    err = esp_lcd_new_dsi_bus(&dsi_bus_config, &s_dsi_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=dsi-bus error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_config, &s_dbi_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=dbi-io error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DISPLAY_DPI_CLOCK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        .num_fbs = 2,
#else
        .num_fbs = 1,
#endif
        .video_timing = {
            .h_size = PLATFORM_DISPLAY_NATIVE_WIDTH,
            .v_size = PLATFORM_DISPLAY_NATIVE_HEIGHT,
            .hsync_back_porch = DISPLAY_DPI_HBP,
            .hsync_pulse_width = DISPLAY_DPI_HSYNC,
            .hsync_front_porch = DISPLAY_DPI_HFP,
            .vsync_back_porch = DISPLAY_DPI_VBP,
            .vsync_pulse_width = DISPLAY_DPI_VSYNC,
            .vsync_front_porch = DISPLAY_DPI_VFP,
        },
        .flags.use_dma2d = false,
    };
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const st7701_vendor_config_t vendor_config = {
        .init_cmds = waveshare_st7701_init_cmds,
        .init_cmds_size = sizeof(waveshare_st7701_init_cmds) /
            sizeof(waveshare_st7701_init_cmds[0]),
        .flags = {
            .use_mipi_interface = 1,
        },
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
#else
    const ek79007_vendor_config_t vendor_config = {
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_config,
            .lane_num = DISPLAY_DSI_DATA_LANES,
        },
    };
#endif
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = (gpio_num_t)PLATFORM_BOARD_LCD_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    err = esp_lcd_new_panel_st7701(s_dbi_io, &panel_config, &s_panel);
#else
    err = esp_lcd_new_panel_ek79007(s_dbi_io, &panel_config, &s_panel);
#endif
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-new error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-reset error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M1 FAIL stage=panel-init error=%s", esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    err = esp_lcd_dpi_panel_get_frame_buffer(
        s_panel, 2U, (void **)&s_panel_frames[0],
        (void **)&s_panel_frames[1]);
    if (err != ESP_OK || s_panel_frames[0] == NULL ||
        s_panel_frames[1] == NULL) {
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 FAIL stage=panel-framebuffers error=%s",
                 esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }
    const uint32_t refresh_callback_generation =
        prepare_refresh_callback_generation();
    const ppa_client_config_t scaler_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = 1U,
    };
    err = ppa_register_client(&scaler_config, &s_scaler);
    if (err != ESP_OK) {
        s_scaler = NULL;
        ESP_LOGW(TAG,
                 "P4_DISPLAY GAME_ACCELERATOR ready=0 fallback=cpu error=%s",
                 esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG,
                 "P4_DISPLAY GAME_ACCELERATOR ready=1 engine=ppa-srm "
                 "source=320x200 target=475x760 rotation_ccw=90 "
                 "double_buffer=1 refresh_synchronous=1");
        ESP_LOGI(TAG,
                 "P4_DISPLAY SHELL_ACCELERATOR ready=1 engine=ppa-srm "
                 "source=384x240 target=480x768 scale=2:1 "
                 "rotation_ccw=90 pipelined_double_buffer=1");
        ESP_LOGI(TAG,
                 "P4_DISPLAY CONTENT_ACCELERATOR ready=1 engine=ppa-srm "
                 "source=768x480 target=480x768 scale=1:1 "
                 "rotation_ccw=90 shell_handoff=pipelined "
                 "game_handoff=refresh-synchronous");
    }
#endif

    const esp_lcd_dpi_panel_event_callbacks_t callbacks = {
        .on_refresh_done = on_refresh_done,
    };
    /* ESP-IDF stores and returns user_ctx unchanged.  This opaque, nonzero
     * generation token is never dereferenced; the P4 build uses RV32/ILP32,
     * asserted above, so its bits round-trip through uintptr_t exactly. */
    err = esp_lcd_dpi_panel_register_event_callbacks(
        s_panel, &callbacks,
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        (void *)(uintptr_t)refresh_callback_generation
#else
        NULL
#endif
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "P4_DISPLAY M2 FAIL stage=refresh-callback error=%s",
                 esp_err_to_name(err));
        release_owned_resources();
        (void)xSemaphoreGive(s_api_lock);
        return err;
    }
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    enable_refresh_callback_generation(refresh_callback_generation);
#endif

    s_initialized = true;
    ESP_LOGI(TAG,
             "P4_DISPLAY M1 PANEL_READY native=%ux%u logical=%ux%u "
             "rotation_cw=%u format=rgb565 lane_mbps=%u dpi_mhz=%u board=%s",
             (unsigned)PLATFORM_DISPLAY_NATIVE_WIDTH,
             (unsigned)PLATFORM_DISPLAY_NATIVE_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_WIDTH,
             (unsigned)PLATFORM_DISPLAY_HEIGHT,
             (unsigned)PLATFORM_DISPLAY_ROTATION_CW_DEGREES,
             DISPLAY_DSI_LANE_RATE_MBPS, DISPLAY_DPI_CLOCK_MHZ,
             platform_board_name());
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
}

esp_err_t platform_display_deinit(void)
{
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_initialized && !s_backlight_ready && s_panel == NULL) {
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
    const char *name;
    switch (pattern) {
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_VERTICAL;
        name = "bars-vertical";
        break;
    case PLATFORM_DISPLAY_PATTERN_COLOR_BARS_HORIZONTAL:
        dsi_pattern = MIPI_DSI_PATTERN_BAR_HORIZONTAL;
        name = "bars-horizontal";
        break;
    case PLATFORM_DISPLAY_PATTERN_BER_VERTICAL:
        dsi_pattern = MIPI_DSI_PATTERN_BER_VERTICAL;
        name = "ber-vertical";
        break;
    case PLATFORM_DISPLAY_PATTERN_BLACK:
        dsi_pattern = MIPI_DSI_PATTERN_NONE;
        name = "black";
        break;
    default:
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const TickType_t started = xTaskGetTickCount();
    const TickType_t budget =
        milliseconds_to_ticks(DISPLAY_PATTERN_HANDOFF_TIMEOUT_MS);
    if (wait_for_pending_panel_handoff(started, budget) != ESP_OK) {
        ESP_LOGE(TAG,
                 "P4_DISPLAY M1 PATTERN_TIMEOUT timeout_ms=%u "
                 "stage=buffer-reuse handoff_pending=1",
                 (unsigned)DISPLAY_PATTERN_HANDOFF_TIMEOUT_MS);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }
#endif
    esp_err_t err = esp_lcd_dpi_panel_set_pattern(s_panel, dsi_pattern);
    if (err == ESP_OK) {
        s_pattern_active = dsi_pattern != MIPI_DSI_PATTERN_NONE;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        /* Pattern generation bypasses the normal source-to-framebuffer
         * contract, so a later shell dirty update must establish a full base. */
        invalidate_content_region_state();
#endif
        ESP_LOGI(TAG, "P4_DISPLAY M1 PATTERN name=%s", name);
    }
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

typedef bool (*display_layout_fn_t)(
    const uint16_t *, size_t, uint16_t *, size_t, size_t);

typedef struct {
    const platform_display_rgb565_region_t *regions;
    size_t region_count;
} display_content_region_request_t;

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
static void clear_accelerated_margins(uint16_t *destination,
                                      size_t offset_x,
                                      size_t offset_y,
                                      size_t width,
                                      size_t height)
{
    const size_t row_pixels = PLATFORM_DISPLAY_NATIVE_WIDTH;
    memset(destination, 0,
           offset_y * row_pixels * sizeof(*destination));
    memset(destination +
               (offset_y + height) * row_pixels,
           0,
           (PLATFORM_DISPLAY_NATIVE_HEIGHT - offset_y - height) *
               row_pixels * sizeof(*destination));
    for (size_t y = offset_y; y < offset_y + height; ++y) {
        uint16_t *const row = destination + y * row_pixels;
        memset(row, 0, offset_x * sizeof(*row));
        memset(row + offset_x + width, 0,
               (PLATFORM_DISPLAY_NATIVE_WIDTH - offset_x - width) *
                   sizeof(*row));
    }
}

static bool accelerate_frame(const uint16_t *source,
                             size_t source_stride_pixels,
                             uint16_t *destination,
                             uint8_t destination_index,
                             display_accelerated_layout_t accelerated_layout)
{
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_offset_x;
    uint32_t output_offset_y;
    uint32_t output_width;
    uint32_t output_height;
    float scale;
    display_margin_profile_t margin_profile;
    switch (accelerated_layout) {
    case DISPLAY_ACCELERATED_LAYOUT_GAME:
        source_width = PLATFORM_DISPLAY_GAME_WIDTH;
        source_height = PLATFORM_DISPLAY_GAME_HEIGHT;
        output_offset_x = DISPLAY_GAME_PPA_OFFSET_X;
        output_offset_y = DISPLAY_GAME_PPA_OFFSET_Y;
        output_width = DISPLAY_GAME_PPA_WIDTH;
        output_height = DISPLAY_GAME_PPA_HEIGHT;
        scale = DISPLAY_GAME_PPA_SCALE;
        margin_profile = DISPLAY_MARGIN_PROFILE_GAME;
        break;
    case DISPLAY_ACCELERATED_LAYOUT_SHELL:
        source_width = PLATFORM_DISPLAY_SHELL_WIDTH;
        source_height = PLATFORM_DISPLAY_SHELL_HEIGHT;
        output_offset_x = DISPLAY_SHELL_PPA_OFFSET_X;
        output_offset_y = DISPLAY_SHELL_PPA_OFFSET_Y;
        output_width = DISPLAY_SHELL_PPA_WIDTH;
        output_height = DISPLAY_SHELL_PPA_HEIGHT;
        scale = DISPLAY_SHELL_PPA_SCALE;
        margin_profile = DISPLAY_MARGIN_PROFILE_SHELL;
        break;
    case DISPLAY_ACCELERATED_LAYOUT_CONTENT:
        source_width = PLATFORM_DISPLAY_CONTENT_WIDTH;
        source_height = PLATFORM_DISPLAY_CONTENT_HEIGHT;
        output_offset_x = DISPLAY_CONTENT_PPA_OFFSET_X;
        output_offset_y = DISPLAY_CONTENT_PPA_OFFSET_Y;
        output_width = DISPLAY_CONTENT_PPA_WIDTH;
        output_height = DISPLAY_CONTENT_PPA_HEIGHT;
        scale = DISPLAY_CONTENT_PPA_SCALE;
        margin_profile = DISPLAY_MARGIN_PROFILE_CONTENT;
        break;
    default:
        return false;
    }
    if (s_scaler == NULL || source_stride_pixels != source_width ||
        destination == NULL || destination_index >= 2U) {
        return false;
    }
    const ppa_srm_oper_config_t operation = {
        .in = {
            .buffer = source,
            .pic_w = source_width,
            .pic_h = source_height,
            .block_w = source_width,
            .block_h = source_height,
            .block_offset_x = 0U,
            .block_offset_y = 0U,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = destination,
            .buffer_size = (uint32_t)DISPLAY_FRAME_BYTES,
            .pic_w = PLATFORM_DISPLAY_NATIVE_WIDTH,
            .pic_h = PLATFORM_DISPLAY_NATIVE_HEIGHT,
            .block_offset_x = output_offset_x,
            .block_offset_y = output_offset_y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = scale,
        .scale_y = scale,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    const esp_err_t result =
        ppa_do_scale_rotate_mirror(s_scaler, &operation);
    if (result == ESP_OK) {
        /* PPA writes only its rotated viewport. Clear the surrounding pixels
         * once whenever a DSI-owned buffer changes between the game and
         * native 768x480 content geometries. */
        if (s_margin_profiles[destination_index] != margin_profile) {
            clear_accelerated_margins(
                destination, output_offset_x, output_offset_y,
                output_width, output_height);
            s_margin_profiles[destination_index] = margin_profile;
        }
        __atomic_fetch_add(
            &s_stats.accelerated_submits, 1U, __ATOMIC_RELAXED);
        return true;
    }
    __atomic_fetch_add(
        &s_stats.accelerator_failures, 1U, __ATOMIC_RELAXED);
    if (!s_accelerator_failure_logged) {
        s_accelerator_failure_logged = true;
        ESP_LOGW(TAG,
                 "P4_DISPLAY ACCELERATOR_FAIL error=%s fallback=cpu",
                 esp_err_to_name(result));
    }
    return false;
}

static bool content_regions_valid(
    const platform_display_rgb565_region_t *regions, size_t region_count)
{
    if (regions == NULL || region_count == 0U ||
        region_count > DISPLAY_CONTENT_MAX_REGIONS) {
        return false;
    }
    for (size_t index = 0U; index < region_count; ++index) {
        platform_display_rgb565_region_t native_region;
        if (!platform_display_layout_map_content_region_ccw(
                &regions[index], &native_region)) {
            return false;
        }
    }
    return true;
}

static bool accelerate_content_region(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint16_t *destination,
    const platform_display_rgb565_region_t *source_region,
    platform_display_rgb565_region_t *native_region)
{
    if (s_scaler == NULL || source == NULL || destination == NULL ||
        source_stride_pixels != PLATFORM_DISPLAY_CONTENT_WIDTH ||
        source_region == NULL || native_region == NULL ||
        !platform_display_layout_map_content_region_ccw(
            source_region, native_region)) {
        return false;
    }
    const ppa_srm_oper_config_t operation = {
        .in = {
            .buffer = source,
            .pic_w = PLATFORM_DISPLAY_CONTENT_WIDTH,
            .pic_h = PLATFORM_DISPLAY_CONTENT_HEIGHT,
            .block_w = source_region->width,
            .block_h = source_region->height,
            .block_offset_x = source_region->x,
            .block_offset_y = source_region->y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = destination,
            .buffer_size = (uint32_t)DISPLAY_FRAME_BYTES,
            .pic_w = PLATFORM_DISPLAY_NATIVE_WIDTH,
            .pic_h = PLATFORM_DISPLAY_NATIVE_HEIGHT,
            .block_offset_x = native_region->x,
            .block_offset_y = native_region->y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    const esp_err_t result = ppa_do_scale_rotate_mirror(s_scaler, &operation);
    if (result == ESP_OK) {
        return true;
    }
    __atomic_fetch_add(&s_stats.accelerator_failures, 1U,
                       __ATOMIC_RELAXED);
    if (!s_accelerator_failure_logged) {
        s_accelerator_failure_logged = true;
        ESP_LOGW(TAG,
                 "P4_DISPLAY ACCELERATOR_FAIL error=%s fallback=cpu",
                 esp_err_to_name(result));
    }
    return false;
}

static bool content_history_append(
    uint32_t generation,
    const platform_display_rgb565_region_t *regions,
    size_t region_count)
{
    if (!content_regions_valid(regions, region_count) || generation == 0U) {
        return false;
    }
    display_content_generation_t *const entry =
        &s_content_history[generation % DISPLAY_CONTENT_HISTORY_DEPTH];
    entry->generation = generation;
    entry->region_count = (uint8_t)region_count;
    memcpy(entry->regions, regions, region_count * sizeof(*regions));
    return true;
}

static const display_content_generation_t *content_history_get(
    uint32_t generation)
{
    const display_content_generation_t *const entry =
        &s_content_history[generation % DISPLAY_CONTENT_HISTORY_DEPTH];
    return entry->generation == generation ? entry : NULL;
}
#endif

static esp_err_t submit_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    size_t minimum_stride,
    uint32_t timeout_ms,
    display_layout_fn_t layout,
    bool wait_for_presented_refresh,
    const display_content_region_request_t *content_regions)
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

    esp_err_t err = ESP_OK;
    if (!s_initialized || s_panel == NULL) {
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }

#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const bool game_layout =
        layout == platform_display_layout_rgb565_320x200;
    const bool shell_layout =
        layout == platform_display_layout_rgb565_384x240;
    const bool content_layout =
        layout == platform_display_layout_rgb565_768x480;
    /*
     * The preceding submit may have selected the other DSI frame buffer, but
     * only on_refresh_done() can prove that the hardware consumed that
     * selection.  Fence before reusing its former scanout buffer, not after
     * this handoff, so callers can render their next source frame while this
     * one is pending.
     */
    const int64_t reuse_wait_started_us = esp_timer_get_time();
    if (wait_for_pending_panel_handoff(started, budget) != ESP_OK) {
        record_pipeline_phase(&s_stats.pipeline_reuse_wait_last_us,
                              &s_stats.pipeline_reuse_wait_max_us,
                              elapsed_microseconds(reuse_wait_started_us));
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 SUBMIT_TIMEOUT timeout_ms=%" PRIu32
                 " stage=buffer-reuse handoff_pending=1 "
                 "backlight_preserved=1",
                 timeout_ms);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }
    const uint32_t reuse_wait_us = elapsed_microseconds(reuse_wait_started_us);
    record_pipeline_phase(&s_stats.pipeline_reuse_wait_last_us,
                          &s_stats.pipeline_reuse_wait_max_us,
                          reuse_wait_us);
    const uint8_t target_index =
        (uint8_t)(confirmed_active_panel_frame() == 0U ? 1U : 0U);
    uint16_t *const output_frame = s_panel_frames[target_index];
    if (output_frame == NULL) {
        err = ESP_ERR_INVALID_STATE;
        goto fail_dark;
    }
    const bool partial_requested = content_regions != NULL;
    bool partial_applied = false;
    uint8_t partial_replay_region_count = 0U;
    bool content_full_base_to_commit = false;
    bool content_full_base_is_delta = false;
    uint32_t content_generation_to_commit = 0U;
    uint32_t partial_source_pixels = 0U;
    int draw_y_start = 0;
    int draw_y_end = (int)PLATFORM_DISPLAY_NATIVE_HEIGHT;
    const int64_t transform_started_us = esp_timer_get_time();
    /* A wrap would make generation ordering ambiguous. Re-prime both panel
     * buffers with a conservative full frame instead; at 60 Hz this guard is
     * only reachable after more than two years of uninterrupted updates. */
    if (content_layout && s_content_generation == UINT32_MAX) {
        invalidate_content_region_state();
    }
    if (partial_requested && content_layout &&
        content_regions_valid(content_regions->regions,
                              content_regions->region_count) &&
        s_content_source_pixels == (uintptr_t)source &&
        s_content_source_stride == source_stride_pixels &&
        s_panel_content_valid[target_index] &&
        s_margin_profiles[target_index] == DISPLAY_MARGIN_PROFILE_CONTENT &&
        s_panel_content_generation[target_index] <= s_content_generation &&
        s_content_generation - s_panel_content_generation[target_index] <
            DISPLAY_CONTENT_HISTORY_DEPTH) {
        platform_display_rgb565_region_t replay_regions[
            DISPLAY_CONTENT_HISTORY_DEPTH * DISPLAY_CONTENT_MAX_REGIONS +
            DISPLAY_CONTENT_MAX_REGIONS];
        size_t replay_count = 0U;
        bool history_complete = true;
        for (uint32_t generation =
                 s_panel_content_generation[target_index] + 1U;
             generation <= s_content_generation; ++generation) {
            const display_content_generation_t *const entry =
                content_history_get(generation);
            if (entry == NULL) {
                history_complete = false;
                break;
            }
            for (size_t index = 0U; index < entry->region_count; ++index) {
                if (!platform_display_layout_compact_content_region(
                        replay_regions, &replay_count,
                        sizeof(replay_regions) / sizeof(replay_regions[0]),
                        &entry->regions[index])) {
                    history_complete = false;
                    break;
                }
            }
            if (!history_complete) {
                break;
            }
        }
        for (size_t index = 0U;
             history_complete && index < content_regions->region_count;
             ++index) {
            history_complete =
                platform_display_layout_compact_content_region(
                    replay_regions, &replay_count,
                    sizeof(replay_regions) / sizeof(replay_regions[0]),
                    &content_regions->regions[index]);
        }
        uint64_t estimated_partial_cost = 0U;
        uint32_t minimum_source_x = PLATFORM_DISPLAY_CONTENT_WIDTH;
        uint32_t maximum_source_x = 0U;
        for (size_t index = 0U;
             history_complete && index < replay_count; ++index) {
            const uint32_t pixels =
                (uint32_t)replay_regions[index].width *
                (uint32_t)replay_regions[index].height;
            /* ESP-IDF's PPA cache maintenance covers complete source rows and
             * complete native rows around each block. Include those extents,
             * the transform area, and a small per-job setup allowance rather
             * than treating a skinny rectangle as nearly free. */
            estimated_partial_cost += pixels +
                (uint64_t)PLATFORM_DISPLAY_CONTENT_WIDTH *
                    replay_regions[index].height +
                (uint64_t)PLATFORM_DISPLAY_NATIVE_WIDTH *
                    replay_regions[index].width + UINT64_C(16384);
            if (replay_regions[index].x < minimum_source_x) {
                minimum_source_x = replay_regions[index].x;
            }
            const uint32_t right = (uint32_t)replay_regions[index].x +
                replay_regions[index].width;
            if (right > maximum_source_x) {
                maximum_source_x = right;
            }
        }
        /* draw_bitmap() performs one final cache writeback over the union of
         * native scanlines, whose vertical extent is the source X span after
         * the 90-degree rotation. */
        if (replay_count > 0U) {
            estimated_partial_cost +=
                (uint64_t)PLATFORM_DISPLAY_NATIVE_WIDTH *
                (maximum_source_x - minimum_source_x);
        }
        const uint64_t full_source_pixels =
            (uint64_t)PLATFORM_DISPLAY_CONTENT_WIDTH *
            PLATFORM_DISPLAY_CONTENT_HEIGHT;
        const uint64_t estimated_full_cost = full_source_pixels * 3U +
            (uint64_t)PLATFORM_DISPLAY_NATIVE_WIDTH *
            PLATFORM_DISPLAY_NATIVE_HEIGHT;
        if (history_complete && replay_count > 0U &&
            estimated_partial_cost < estimated_full_cost) {
            bool transformed = replay_count > 0U;
            for (size_t index = 0U; index < replay_count; ++index) {
                platform_display_rgb565_region_t native_region;
                if (!accelerate_content_region(
                        source, source_stride_pixels, output_frame,
                        &replay_regions[index], &native_region)) {
                    transformed = false;
                    break;
                }
                const uint32_t pixels = (uint32_t)replay_regions[index].width *
                    (uint32_t)replay_regions[index].height;
                partial_source_pixels +=
                    UINT32_MAX - partial_source_pixels < pixels
                    ? UINT32_MAX - partial_source_pixels : pixels;
                if ((int)native_region.y < draw_y_start || index == 0U) {
                    draw_y_start = (int)native_region.y;
                }
                const int native_end = (int)native_region.y +
                    (int)native_region.height;
                if (native_end > draw_y_end || index == 0U) {
                    draw_y_end = native_end;
                }
            }
            if (transformed) {
                content_generation_to_commit = s_content_generation + 1U;
                if (content_generation_to_commit == 0U) {
                    content_generation_to_commit = 1U;
                }
                partial_applied = true;
                partial_replay_region_count = (uint8_t)replay_count;
            }
        }
    }
    if (!partial_applied) {
        if (partial_requested) {
            __atomic_fetch_add(&s_stats.partial_content_full_fallbacks, 1U,
                               __ATOMIC_RELAXED);
        }
        /* A complete transform is the conservative base whenever a region
         * history cannot prove that the inactive framebuffer is current. A
         * same-source partial fallback retains the other buffer's recorded
         * generation and records only the caller's actual delta so that
         * buffer can catch up without repeating this full transform. */
        const bool preserve_region_history = partial_requested &&
            content_layout &&
            s_content_source_pixels == (uintptr_t)source &&
            s_content_source_stride == source_stride_pixels;
        if (!preserve_region_history) {
            invalidate_content_region_state();
        }
        const display_accelerated_layout_t accelerated_layout = shell_layout
            ? DISPLAY_ACCELERATED_LAYOUT_SHELL
            : (content_layout ? DISPLAY_ACCELERATED_LAYOUT_CONTENT
                              : DISPLAY_ACCELERATED_LAYOUT_GAME);
        const bool accelerated =
            (game_layout || shell_layout || content_layout) &&
            accelerate_frame(source, source_stride_pixels, output_frame,
                             target_index, accelerated_layout);
        if (!accelerated && !layout(
                source, source_stride_pixels, output_frame,
                PLATFORM_DISPLAY_NATIVE_WIDTH,
                PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
            err = ESP_ERR_INVALID_ARG;
            goto fail;
        }
        if (!accelerated) {
            /* Both CPU layouts write the complete frame. Their exact output
             * uses the centered 768x480 logical viewport. */
            s_margin_profiles[target_index] =
                DISPLAY_MARGIN_PROFILE_CONTENT;
        }
        if (content_layout) {
            content_generation_to_commit = s_content_generation + 1U;
            if (content_generation_to_commit == 0U) {
                content_generation_to_commit = 1U;
            }
            content_full_base_to_commit = true;
            content_full_base_is_delta = preserve_region_history;
        }
    }
    const uint32_t transform_us = elapsed_microseconds(transform_started_us);
    record_pipeline_phase(&s_stats.pipeline_transform_last_us,
                          &s_stats.pipeline_transform_max_us, transform_us);
#else
    (void)content_regions;
    const int draw_y_start = 0;
    const int draw_y_end = (int)PLATFORM_DISPLAY_NATIVE_HEIGHT;
    if (s_submit_frame == NULL) {
        s_submit_frame = heap_caps_malloc(
            DISPLAY_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_submit_frame == NULL) {
            err = ESP_ERR_NO_MEM;
            goto fail_dark;
        }
    }
    if (!layout(
            source, source_stride_pixels, s_submit_frame,
            PLATFORM_DISPLAY_NATIVE_WIDTH,
            PLATFORM_DISPLAY_NATIVE_HEIGHT)) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    uint16_t *const output_frame = s_submit_frame;
#endif

    if (s_pattern_active) {
        err = esp_lcd_dpi_panel_set_pattern(
            s_panel, MIPI_DSI_PATTERN_NONE);
        if (err != ESP_OK) {
            goto fail_dark;
        }
        s_pattern_active = false;
    }
#if !CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    while (xSemaphoreTake(s_refresh_signal, 0) == pdTRUE) {
    }
#endif
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    /*
     * Reserve before draw_bitmap() selects the target in the DPI driver.  A
     * refresh arriving while that call is in progress sees RESERVED and cannot
     * promote the target prematurely.  Arming after a successful return may
     * intentionally defer to the following refresh, but never loses buffer
     * ownership or permits reuse before a confirmed handoff.
     */
    if (!reserve_panel_handoff(target_index)) {
        err = ESP_ERR_INVALID_STATE;
        goto fail;
    }
#endif
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    const int64_t handoff_started_us = esp_timer_get_time();
#endif
    err = esp_lcd_panel_draw_bitmap(s_panel, 0, draw_y_start,
                                    PLATFORM_DISPLAY_NATIVE_WIDTH,
                                    draw_y_end, output_frame);
    if (err != ESP_OK) {
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        record_pipeline_phase(&s_stats.pipeline_handoff_last_us,
                              &s_stats.pipeline_handoff_max_us,
                              elapsed_microseconds(handoff_started_us));
#endif
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
        cancel_reserved_panel_handoff(target_index);
#endif
        goto fail_dark;
    }
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    record_pipeline_phase(&s_stats.pipeline_handoff_last_us,
                          &s_stats.pipeline_handoff_max_us,
                          elapsed_microseconds(handoff_started_us));
    /* Completion is counted by on_refresh_done(), exactly once, when the
     * handoff becomes the confirmed scanout buffer. */
    display_interactive_handoff_t interactive = {0};
    const display_interactive_handoff_t *interactive_handoff = NULL;
    if (content_layout && !wait_for_presented_refresh) {
        const int64_t interactive_now_us = esp_timer_get_time();
        if (s_interactive_input_timestamp_us > 0 &&
            interactive_now_us >= s_interactive_input_timestamp_us &&
            interactive_now_us - s_interactive_input_timestamp_us <=
                DISPLAY_INTERACTIVE_INPUT_MAX_AGE_US) {
            interactive.valid = true;
            interactive.input_timestamp_us = s_interactive_input_timestamp_us;
            interactive.armed_timestamp_us = interactive_now_us;
            interactive.reuse_wait_us = reuse_wait_us;
            interactive.transform_us = transform_us;
            interactive.replay_region_count = partial_applied
                ? partial_replay_region_count : 0U;
            interactive.kind = partial_applied
                ? PLATFORM_DISPLAY_INTERACTIVE_PRESENT_PARTIAL
                : PLATFORM_DISPLAY_INTERACTIVE_PRESENT_FULL;
            interactive_handoff = &interactive;
        }
    }
    if (!arm_panel_handoff(target_index, interactive_handoff)) {
        cancel_reserved_panel_handoff(target_index);
        err = ESP_ERR_INVALID_STATE;
        goto fail_dark;
    }
    if (content_generation_to_commit != 0U) {
        s_content_generation = content_generation_to_commit;
        s_panel_content_generation[target_index] = content_generation_to_commit;
        s_panel_content_valid[target_index] = true;
        s_content_source_pixels = (uintptr_t)source;
        s_content_source_stride = source_stride_pixels;
        const platform_display_rgb565_region_t full_content_region = {
            .x = 0U,
            .y = 0U,
            .width = PLATFORM_DISPLAY_CONTENT_WIDTH,
            .height = PLATFORM_DISPLAY_CONTENT_HEIGHT,
        };
        const bool history_is_delta = partial_applied ||
            content_full_base_is_delta;
        const platform_display_rgb565_region_t *const history_regions =
            history_is_delta ? content_regions->regions :
            content_full_base_to_commit ? &full_content_region : NULL;
        const size_t history_region_count = history_is_delta
            ? content_regions->region_count
            : content_full_base_to_commit ? 1U : 0U;
        if (history_region_count != 0U && !content_history_append(
                content_generation_to_commit, history_regions,
                history_region_count)) {
            /* The selected frame remains safe, but without a replay record
             * the other buffer must receive a full base next time. */
            s_panel_content_valid[target_index] = false;
        }
        if (partial_applied) {
            /* Preserve accelerated_submits as a per-frame counter even when
             * one partial submit needed several PPA region operations. */
            __atomic_fetch_add(&s_stats.accelerated_submits, 1U,
                               __ATOMIC_RELAXED);
            __atomic_fetch_add(&s_stats.partial_content_submits, 1U,
                               __ATOMIC_RELAXED);
            saturating_atomic_add_u32(
                &s_stats.partial_content_source_pixels,
                partial_source_pixels);
        }
    }
    if (!wait_for_presented_refresh) {
        (void)xSemaphoreGive(s_api_lock);
        return ESP_OK;
    }
    /* Games retain the predecessor's refresh-synchronous cadence, while the
     * ownership state remains authoritative: RESERVED ignores any refresh
     * during draw_bitmap(), ARMED is promoted only by a later ISR. If the
     * wait times out, leave the handoff pending so no caller can reuse either
     * buffer until a refresh eventually confirms the selected target. */
    if (wait_for_pending_panel_handoff(started, budget) != ESP_OK) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 SUBMIT_TIMEOUT timeout_ms=%" PRIu32
                 " stage=presented-refresh handoff_pending=1 "
                 "backlight_preserved=1",
                 timeout_ms);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
#else
    const uint32_t refresh_baseline =
        __atomic_load_n(&s_stats.refresh_completions, __ATOMIC_ACQUIRE);
    /* CPU draw is a synchronous copy/cache-writeback. Capture the counter
     * after that copy returns, then qualify this submit on a later refresh. */
    if (wait_for_refresh_after(refresh_baseline, started, budget) != ESP_OK) {
        __atomic_fetch_add(&s_stats.submit_timeouts, 1U, __ATOMIC_RELAXED);
        const uint32_t refresh_current = __atomic_load_n(
            &s_stats.refresh_completions, __ATOMIC_ACQUIRE);
        ESP_LOGE(TAG,
                 "P4_DISPLAY M2 SUBMIT_TIMEOUT timeout_ms=%" PRIu32
                 " refresh_baseline=%" PRIu32 " refresh_current=%" PRIu32
                 " backlight_preserved=1",
                 timeout_ms, refresh_baseline, refresh_current);
        (void)xSemaphoreGive(s_api_lock);
        return ESP_ERR_TIMEOUT;
    }

    __atomic_fetch_add(&s_stats.submits_completed, 1U, __ATOMIC_RELAXED);
    (void)xSemaphoreGive(s_api_lock);
    return ESP_OK;
#endif

fail_dark:
    (void)set_brightness_locked(0);
fail:
    __atomic_fetch_add(&s_stats.submit_failures, 1U, __ATOMIC_RELAXED);
    ESP_LOGE(TAG, "P4_DISPLAY M2 SUBMIT_FAIL error=%s",
             esp_err_to_name(err));
    (void)xSemaphoreGive(s_api_lock);
    return err;
}

esp_err_t platform_display_submit_rgb565(const uint16_t *source,
                                         size_t source_stride_pixels,
                                         uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_GAME_WIDTH,
        timeout_ms, platform_display_layout_rgb565_320x200, true, NULL);
}

esp_err_t platform_display_submit_shell_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_SHELL_WIDTH,
        timeout_ms, platform_display_layout_rgb565_384x240, false, NULL);
}

esp_err_t platform_display_submit_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_CONTENT_WIDTH,
        timeout_ms, platform_display_layout_rgb565_768x480, false, NULL);
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
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    if (!content_regions_valid(regions, region_count)) {
        return ESP_ERR_INVALID_ARG;
    }
    const display_content_region_request_t request = {
        .regions = regions,
        .region_count = region_count,
    };
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_CONTENT_WIDTH,
        timeout_ms, platform_display_layout_rgb565_768x480, false, &request);
#else
    /* The other display adapters retain their established full-frame paths. */
    return platform_display_submit_content_rgb565(
        source, source_stride_pixels, timeout_ms);
#endif
}

esp_err_t platform_display_submit_game_content_rgb565(
    const uint16_t *source,
    size_t source_stride_pixels,
    uint32_t timeout_ms)
{
    return submit_rgb565(
        source, source_stride_pixels, PLATFORM_DISPLAY_CONTENT_WIDTH,
        timeout_ms, platform_display_layout_rgb565_768x480, true, NULL);
}

esp_err_t platform_display_record_interactive_input_timestamp(
    int64_t timestamp_us)
{
    if (timestamp_us < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ensure_sync_objects()) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_api_lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = ESP_OK;
#if CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3
    if (!s_initialized) {
        result = ESP_ERR_INVALID_STATE;
    } else if (timestamp_us != 0 && timestamp_us > esp_timer_get_time()) {
        result = ESP_ERR_INVALID_ARG;
    } else {
        s_interactive_input_timestamp_us = timestamp_us;
    }
#else
    (void)timestamp_us;
#endif
    (void)xSemaphoreGive(s_api_lock);
    return result;
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
    out_stats->accelerated_submits =
        __atomic_load_n(&s_stats.accelerated_submits, __ATOMIC_RELAXED);
    out_stats->accelerator_failures =
        __atomic_load_n(&s_stats.accelerator_failures, __ATOMIC_RELAXED);
    out_stats->pipeline_reuse_wait_last_us = __atomic_load_n(
        &s_stats.pipeline_reuse_wait_last_us, __ATOMIC_RELAXED);
    out_stats->pipeline_reuse_wait_max_us = __atomic_load_n(
        &s_stats.pipeline_reuse_wait_max_us, __ATOMIC_RELAXED);
    out_stats->pipeline_transform_last_us = __atomic_load_n(
        &s_stats.pipeline_transform_last_us, __ATOMIC_RELAXED);
    out_stats->pipeline_transform_max_us = __atomic_load_n(
        &s_stats.pipeline_transform_max_us, __ATOMIC_RELAXED);
    out_stats->pipeline_handoff_last_us = __atomic_load_n(
        &s_stats.pipeline_handoff_last_us, __ATOMIC_RELAXED);
    out_stats->pipeline_handoff_max_us = __atomic_load_n(
        &s_stats.pipeline_handoff_max_us, __ATOMIC_RELAXED);
    out_stats->pipeline_reserved_refreshes = __atomic_load_n(
        &s_stats.pipeline_reserved_refreshes, __ATOMIC_RELAXED);
    out_stats->pipeline_refresh_interval_last_us = __atomic_load_n(
        &s_stats.pipeline_refresh_interval_last_us, __ATOMIC_RELAXED);
    out_stats->pipeline_refresh_interval_min_us = __atomic_load_n(
        &s_stats.pipeline_refresh_interval_min_us, __ATOMIC_RELAXED);
    out_stats->pipeline_refresh_interval_max_us = __atomic_load_n(
        &s_stats.pipeline_refresh_interval_max_us, __ATOMIC_RELAXED);
    out_stats->pipeline_refresh_events = __atomic_load_n(
        &s_stats.pipeline_refresh_events, __ATOMIC_RELAXED);
    out_stats->partial_content_submits = __atomic_load_n(
        &s_stats.partial_content_submits, __ATOMIC_RELAXED);
    out_stats->partial_content_source_pixels = __atomic_load_n(
        &s_stats.partial_content_source_pixels, __ATOMIC_RELAXED);
    out_stats->partial_content_full_fallbacks = __atomic_load_n(
        &s_stats.partial_content_full_fallbacks, __ATOMIC_RELAXED);
    out_stats->interactive_latency_samples = __atomic_load_n(
        &s_stats.interactive_latency_samples, __ATOMIC_RELAXED);
    out_stats->interactive_partial_presentations = __atomic_load_n(
        &s_stats.interactive_partial_presentations, __ATOMIC_RELAXED);
    out_stats->interactive_full_presentations = __atomic_load_n(
        &s_stats.interactive_full_presentations, __ATOMIC_RELAXED);
    out_stats->interactive_input_to_refresh_total_us = __atomic_load_n(
        &s_stats.interactive_input_to_refresh_total_us, __ATOMIC_RELAXED);
    out_stats->interactive_input_to_refresh_max_us = __atomic_load_n(
        &s_stats.interactive_input_to_refresh_max_us, __ATOMIC_RELAXED);
    out_stats->interactive_input_to_refresh_last_us = __atomic_load_n(
        &s_stats.interactive_input_to_refresh_last_us, __ATOMIC_RELAXED);
    out_stats->interactive_handoff_to_refresh_total_us = __atomic_load_n(
        &s_stats.interactive_handoff_to_refresh_total_us, __ATOMIC_RELAXED);
    out_stats->interactive_handoff_to_refresh_max_us = __atomic_load_n(
        &s_stats.interactive_handoff_to_refresh_max_us, __ATOMIC_RELAXED);
    out_stats->interactive_handoff_to_refresh_last_us = __atomic_load_n(
        &s_stats.interactive_handoff_to_refresh_last_us, __ATOMIC_RELAXED);
    out_stats->interactive_reuse_wait_last_us = __atomic_load_n(
        &s_stats.interactive_reuse_wait_last_us, __ATOMIC_RELAXED);
    out_stats->interactive_transform_last_us = __atomic_load_n(
        &s_stats.interactive_transform_last_us, __ATOMIC_RELAXED);
    out_stats->interactive_replay_region_count = __atomic_load_n(
        &s_stats.interactive_replay_region_count, __ATOMIC_RELAXED);
    out_stats->interactive_present_kind = __atomic_load_n(
        &s_stats.interactive_present_kind, __ATOMIC_RELAXED);
    out_stats->underrun_count_available = false;
    return ESP_OK;
}
