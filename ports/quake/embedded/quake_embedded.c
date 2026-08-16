// SPDX-License-Identifier: GPL-2.0-or-later

#include "p4/quake.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p4/platform.h"
#include "platform/board.h"
#include "platform/display.h"
#include "quake_embedded_internal.h"
#include "quakegeneric.h"
#include "quakedef.h"

enum {
    CONTENT_WIDTH = 768,
    CONTENT_HEIGHT = 480,
    ENGINE_WIDTH = QUAKEGENERIC_RES_X,
    ENGINE_HEIGHT = QUAKEGENERIC_RES_Y,
    ENGINE_VIEW_HEIGHT = 450,
    ENGINE_VIEW_TOP = (CONTENT_HEIGHT - ENGINE_VIEW_HEIGHT) / 2,
    KEY_QUEUE_CAPACITY = 32,
    TOUCH_CONTROL_COUNT = 8,
    AUDIO_CHANNELS = 2,
    AUDIO_SAMPLE_RATE = P4_GAME_PLATFORM_AUDIO_SAMPLE_RATE_HZ,
    AUDIO_RING_SAMPLES = 32768,
    AUDIO_BLOCK_FRAMES = P4_GAME_PLATFORM_AUDIO_MAX_WRITE_FRAMES,
    AUDIO_TASK_STACK_BYTES = 4096,
    /*
     * The pinned WinQuake code has legitimate legacy frames as large as
     * 32 KiB (save-game parsing) and Con_CheckResize alone uses 16 KiB.
     * Keep those frames off Console OS's deliberately small main-task stack.
     */
    ENGINE_TASK_STACK_BYTES = 96 * 1024,
    ENGINE_TASK_PRIORITY = tskIDLE_PRIORITY + 2,
    DISPLAY_TIMEOUT_MS = 250,
    DISPLAY_RECOVERY_BRIGHTNESS_PERCENT = 25,
    MAX_CONSECUTIVE_DISPLAY_TIMEOUTS = 3,
    MINIMUM_PSRAM_BEFORE_START = 24 * 1024 * 1024,
};

_Static_assert((int)CONTENT_WIDTH == (int)PLATFORM_DISPLAY_CONTENT_WIDTH &&
                   (int)CONTENT_HEIGHT == (int)PLATFORM_DISPLAY_CONTENT_HEIGHT,
               "Quake adapter must target the Console OS 768x480 canvas");
_Static_assert(AUDIO_RING_SAMPLES % (AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS) == 0,
               "audio ring must contain whole backend blocks");

typedef struct {
    int key;
    bool down;
} queued_key_t;

typedef struct {
    uint16_t left;
    uint16_t top;
    uint16_t width;
    uint16_t height;
    int key;
} touch_control_t;

typedef struct {
    p4_quake_config_t config;
    esp_err_t result;
    bool complete;
} quake_task_context_t;

static const char *const TAG = "p4_quake";
static const touch_control_t TOUCH_CONTROLS[TOUCH_CONTROL_COUNT] = {
    {70U, 306U, 62U, 54U, K_UPARROW},
    {70U, 416U, 62U, 56U, K_DOWNARROW},
    {8U, 360U, 62U, 58U, K_LEFTARROW},
    {132U, 360U, 62U, 58U, K_RIGHTARROW},
    {548U, 374U, 90U, 90U, K_SPACE},
    {656U, 350U, 104U, 116U, K_CTRL},
    {8U, 8U, 78U, 38U, K_ESCAPE},
    {682U, 8U, 78U, 38U, K_ENTER},
};

static p4_quake_config_t s_config;
static bool s_running;
static bool s_fatal;
static bool s_engine_quit;
static uint16_t *s_content_pixels;
static uint16_t s_palette[256];
static queued_key_t s_keys[KEY_QUEUE_CAPACITY];
static size_t s_key_head;
static size_t s_key_count;
static bool s_touch_held[TOUCH_CONTROL_COUNT];
static uint32_t s_frames;
static uint32_t s_touch_failures;
static uint32_t s_display_timeouts;
static uint32_t s_consecutive_display_timeouts;
static bool s_display_recovery_pending;

static dma_t s_dma;
static int16_t *s_audio_ring;
static p4_game_platform_audio_t s_audio;
static TaskHandle_t s_audio_task;
static TaskHandle_t s_audio_stop_waiter;
static bool s_audio_worker_run;
static bool s_audio_backend_ready;
static int64_t s_audio_started_us;

static bool push_key(bool down, int key)
{
    if (key <= 0 || s_key_count >= KEY_QUEUE_CAPACITY) {
        return false;
    }
    const size_t tail = (s_key_head + s_key_count) % KEY_QUEUE_CAPACITY;
    s_keys[tail] = (queued_key_t){.key = key, .down = down};
    ++s_key_count;
    return true;
}

static bool point_in_control(
    uint16_t x,
    uint16_t y,
    const touch_control_t *control)
{
    return x >= control->left && x < control->left + control->width &&
           y >= control->top && y < control->top + control->height;
}

static bool touch_to_content(
    uint16_t physical_x,
    uint16_t physical_y,
    uint16_t *content_x,
    uint16_t *content_y)
{
    const uint16_t relative_x =
        (uint16_t)(physical_x - PLATFORM_BOARD_GAME_MARGIN_LEFT);
    const uint16_t relative_y =
        (uint16_t)(physical_y - PLATFORM_BOARD_GAME_MARGIN_TOP);
    if (relative_x >= PLATFORM_BOARD_GAME_VIEWPORT_WIDTH ||
        relative_y >= PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT) {
        return false;
    }
    *content_x = (uint16_t)(
        (uint32_t)relative_x * CONTENT_WIDTH /
        PLATFORM_BOARD_GAME_VIEWPORT_WIDTH);
    *content_y = (uint16_t)(
        (uint32_t)relative_y * CONTENT_HEIGHT /
        PLATFORM_BOARD_GAME_VIEWPORT_HEIGHT);
    return true;
}

static void apply_touch_state(const bool held[TOUCH_CONTROL_COUNT])
{
    for (size_t index = 0U; index < TOUCH_CONTROL_COUNT; ++index) {
        if (held[index] != s_touch_held[index]) {
            if (!push_key(held[index], TOUCH_CONTROLS[index].key)) {
                p4_quake_report_fatal();
            }
            s_touch_held[index] = held[index];
        }
    }
}

static void poll_touch(void)
{
    bool held[TOUCH_CONTROL_COUNT] = {false};
    platform_touch_frame_t frame;
    platform_touch_frame_neutral(&frame);
    const esp_err_t result = s_config.touch == NULL
        ? ESP_ERR_INVALID_STATE
        : platform_touch_poll(s_config.touch, &frame);
    if (result != ESP_OK || frame.valid == 0U ||
        frame.contact_count > PLATFORM_TOUCH_MAX_CONTACTS) {
        if (s_touch_failures != UINT32_MAX) {
            ++s_touch_failures;
        }
        apply_touch_state(held);
        return;
    }
    for (size_t contact = 0U; contact < frame.contact_count; ++contact) {
        uint16_t x = 0U;
        uint16_t y = 0U;
        if (!touch_to_content(
                frame.contacts[contact].x, frame.contacts[contact].y, &x, &y)) {
            continue;
        }
        for (size_t control = 0U; control < TOUCH_CONTROL_COUNT; ++control) {
            held[control] = held[control] ||
                point_in_control(x, y, &TOUCH_CONTROLS[control]);
        }
    }
    apply_touch_state(held);
}

static uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue)
{
    return (uint16_t)(((uint16_t)(red & UINT8_C(0xf8)) << 8U) |
                      ((uint16_t)(green & UINT8_C(0xfc)) << 3U) |
                      ((uint16_t)blue >> 3U));
}

static uint16_t darken(uint16_t pixel)
{
    return (uint16_t)((pixel >> 1U) & UINT16_C(0x7bef));
}

static void overlay_control(
    uint16_t *pixels,
    const touch_control_t *control,
    bool pressed)
{
    const uint16_t border = pressed ? UINT16_C(0xffe0) : UINT16_C(0xffff);
    for (uint16_t y = control->top;
         y < control->top + control->height; ++y) {
        for (uint16_t x = control->left;
             x < control->left + control->width; ++x) {
            uint16_t *const pixel = &pixels[(size_t)y * CONTENT_WIDTH + x];
            const bool edge = x == control->left ||
                x + 1U == control->left + control->width ||
                y == control->top ||
                y + 1U == control->top + control->height;
            *pixel = edge ? border : darken(*pixel);
        }
    }
    const uint16_t center_x = (uint16_t)(control->left + control->width / 2U);
    const uint16_t center_y = (uint16_t)(control->top + control->height / 2U);
    for (int delta = -7; delta <= 7; ++delta) {
        const size_t horizontal =
            (size_t)center_y * CONTENT_WIDTH + (size_t)((int)center_x + delta);
        const size_t vertical =
            (size_t)((int)center_y + delta) * CONTENT_WIDTH + center_x;
        pixels[horizontal] = border;
        pixels[vertical] = border;
    }
}

void QG_Init(void)
{
    s_content_pixels = heap_caps_calloc(
        (size_t)CONTENT_WIDTH * CONTENT_HEIGHT,
        sizeof(*s_content_pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_content_pixels == NULL) {
        ESP_LOGE(TAG, "P4_QUAKE INIT_FAIL stage=content-frame");
        p4_quake_report_fatal();
    }
}

void QG_Quit(void)
{
    s_engine_quit = true;
    s_running = false;
}

void QG_DrawFrame(void *pixels)
{
    if (pixels == NULL || s_content_pixels == NULL) {
        p4_quake_report_fatal();
        return;
    }
    const uint8_t *const indexed = pixels;
    memset(s_content_pixels, 0,
           (size_t)CONTENT_WIDTH * CONTENT_HEIGHT * sizeof(*s_content_pixels));
    for (size_t y = 0U; y < ENGINE_VIEW_HEIGHT; ++y) {
        const size_t source_y = y * ENGINE_HEIGHT / ENGINE_VIEW_HEIGHT;
        uint16_t *const row =
            &s_content_pixels[(y + ENGINE_VIEW_TOP) * CONTENT_WIDTH];
        for (size_t x = 0U; x < CONTENT_WIDTH; ++x) {
            const size_t source_x = x * ENGINE_WIDTH / CONTENT_WIDTH;
            row[x] = s_palette[indexed[source_y * ENGINE_WIDTH + source_x]];
        }
    }
    for (size_t index = 0U; index < TOUCH_CONTROL_COUNT; ++index) {
        overlay_control(s_content_pixels, &TOUCH_CONTROLS[index],
                        s_touch_held[index]);
    }
    const esp_err_t result = platform_display_submit_content_rgb565(
        s_content_pixels, CONTENT_WIDTH, DISPLAY_TIMEOUT_MS);
    if (result == ESP_ERR_TIMEOUT) {
        if (s_display_timeouts != UINT32_MAX) {
            ++s_display_timeouts;
        }
        if (s_consecutive_display_timeouts != UINT32_MAX) {
            ++s_consecutive_display_timeouts;
        }
        s_display_recovery_pending = true;
        ESP_LOGW(TAG,
                 "P4_QUAKE FRAME_DROPPED reason=display-timeout "
                 "consecutive=%lu limit=%u",
                 (unsigned long)s_consecutive_display_timeouts,
                 (unsigned)MAX_CONSECUTIVE_DISPLAY_TIMEOUTS);
        if (s_consecutive_display_timeouts >=
            MAX_CONSECUTIVE_DISPLAY_TIMEOUTS) {
            p4_quake_report_fatal();
        }
        return;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "P4_QUAKE FRAME_FAIL error=%s", esp_err_to_name(result));
        p4_quake_report_fatal();
        return;
    }
    s_consecutive_display_timeouts = 0U;
    if (s_display_recovery_pending) {
        const esp_err_t recovery_result = platform_display_set_brightness(
            DISPLAY_RECOVERY_BRIGHTNESS_PERCENT);
        if (recovery_result != ESP_OK) {
            ESP_LOGE(TAG, "P4_QUAKE DISPLAY_RECOVERY_FAIL error=%s",
                     esp_err_to_name(recovery_result));
            p4_quake_report_fatal();
            return;
        }
        s_display_recovery_pending = false;
        ESP_LOGI(TAG, "P4_QUAKE DISPLAY_RECOVERED timeouts=%lu",
                 (unsigned long)s_display_timeouts);
    }
    if (s_frames != UINT32_MAX) {
        ++s_frames;
    }
    if (s_frames == 1U) {
        ESP_LOGI(TAG,
                 "P4_QUAKE FIRST_FRAME stack_low_water_bytes=%u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
}

void QG_SetPalette(unsigned char palette[768])
{
    if (palette == NULL) {
        return;
    }
    for (size_t index = 0U; index < 256U; ++index) {
        s_palette[index] = rgb565(
            palette[index * 3U],
            palette[index * 3U + 1U],
            palette[index * 3U + 2U]);
    }
}

int QG_GetKey(int *down, int *key)
{
    if (down == NULL || key == NULL || s_key_count == 0U) {
        return 0;
    }
    const queued_key_t event = s_keys[s_key_head];
    s_key_head = (s_key_head + 1U) % KEY_QUEUE_CAPACITY;
    --s_key_count;
    *down = event.down ? 1 : 0;
    *key = event.key;
    return 1;
}

void QG_GetMouseMove(int *x, int *y)
{
    if (x != NULL) {
        *x = 0;
    }
    if (y != NULL) {
        *y = 0;
    }
}

void QG_GetJoyAxes(float *axes)
{
    if (axes != NULL) {
        memset(axes, 0, QUAKEGENERIC_JOY_MAX_AXES * sizeof(*axes));
    }
}

void p4_quake_report_fatal(void)
{
    s_fatal = true;
    s_running = false;
}

static void audio_worker(void *argument)
{
    (void)argument;
    int16_t block[AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS];
    size_t sample_position = 0U;
    while (__atomic_load_n(&s_audio_worker_run, __ATOMIC_ACQUIRE)) {
        memcpy(block, &s_audio_ring[sample_position], sizeof(block));
        const esp_err_t result = p4_game_platform_audio_write(
            &s_audio, block, AUDIO_BLOCK_FRAMES);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "P4_QUAKE AUDIO_FAIL error=%s",
                     esp_err_to_name(result));
            s_audio_backend_ready = false;
            p4_quake_report_fatal();
            break;
        }
        sample_position =
            (sample_position + AUDIO_BLOCK_FRAMES * AUDIO_CHANNELS) %
            AUDIO_RING_SAMPLES;
        __atomic_store_n(&s_dma.samplepos, (int)sample_position, __ATOMIC_RELEASE);
    }
    s_audio_task = NULL;
    TaskHandle_t const waiter = s_audio_stop_waiter;
    if (waiter != NULL) {
        xTaskNotifyGive(waiter);
    }
    vTaskDelete(NULL);
}

qboolean SNDDMA_Init(void)
{
    s_audio_ring = heap_caps_calloc(
        AUDIO_RING_SAMPLES, sizeof(*s_audio_ring),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_audio_ring == NULL) {
        return false;
    }
    s_dma = (dma_t){
        .gamealive = true,
        .soundalive = true,
        .splitbuffer = false,
        .channels = AUDIO_CHANNELS,
        .samples = AUDIO_RING_SAMPLES,
        .submission_chunk = 1,
        .samplepos = 0,
        .samplebits = 16,
        .speed = AUDIO_SAMPLE_RATE,
        .buffer = (unsigned char *)s_audio_ring,
    };
    shm = &s_dma;
    s_audio_started_us = esp_timer_get_time();
    p4_game_platform_audio_init(&s_audio);
    const esp_err_t open_result = p4_game_platform_audio_open(
        &s_audio, s_config.audio_runtime_authorized,
        s_config.audio_control_bus, s_config.master_volume_step);
    if (open_result != ESP_OK) {
        ESP_LOGW(TAG,
                 "P4_QUAKE AUDIO_DEGRADED stage=open error=%s fallback=silent",
                 esp_err_to_name(open_result));
        return true;
    }
    s_audio_backend_ready = true;
    s_audio_worker_run = true;
    const BaseType_t task_result = xTaskCreate(
        audio_worker, "p4_quake_audio", AUDIO_TASK_STACK_BYTES,
        NULL, 5U, &s_audio_task);
    if (task_result != pdPASS) {
        s_audio_worker_run = false;
        s_audio_backend_ready = false;
        (void)p4_game_platform_audio_close(&s_audio);
        ESP_LOGW(TAG, "P4_QUAKE AUDIO_DEGRADED stage=worker fallback=silent");
    }
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    if (shm == NULL) {
        return 0;
    }
    if (s_audio_backend_ready) {
        return __atomic_load_n(&s_dma.samplepos, __ATOMIC_ACQUIRE);
    }
    const int64_t elapsed_us = esp_timer_get_time() - s_audio_started_us;
    const uint64_t frames = elapsed_us > 0
        ? (uint64_t)elapsed_us * AUDIO_SAMPLE_RATE / UINT64_C(1000000)
        : 0U;
    const int position = (int)(
        (frames * AUDIO_CHANNELS) % AUDIO_RING_SAMPLES);
    s_dma.samplepos = position;
    return position;
}

void SNDDMA_Submit(void)
{
    /* The worker continuously drains Quake's classic lock-free DMA ring. */
}

void SNDDMA_Shutdown(void)
{
    if (s_audio_task != NULL) {
        s_audio_stop_waiter = xTaskGetCurrentTaskHandle();
        __atomic_store_n(&s_audio_worker_run, false, __ATOMIC_RELEASE);
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000U)) == 0U) {
            ESP_LOGE(TAG, "P4_QUAKE AUDIO_FAIL stage=worker-stop-timeout");
            p4_quake_report_fatal();
        }
        s_audio_stop_waiter = NULL;
    }
    if (s_audio.hardware_touched) {
        const esp_err_t close_result = p4_game_platform_audio_close(&s_audio);
        if (close_result != ESP_OK || !s_audio.safe_high_proven ||
            s_audio.backend != NULL) {
            ESP_LOGE(TAG, "P4_QUAKE AUDIO_FAIL stage=close error=%s",
                     esp_err_to_name(close_result));
            p4_quake_report_fatal();
        }
    }
    heap_caps_free(s_audio_ring);
    s_audio_ring = NULL;
    memset(&s_dma, 0, sizeof(s_dma));
    shm = NULL;
    s_audio_backend_ready = false;
    s_audio_worker_run = false;
}

static void reset_state(const p4_quake_config_t *config)
{
    s_config = *config;
    s_running = true;
    s_fatal = false;
    s_engine_quit = false;
    s_content_pixels = NULL;
    memset(s_palette, 0, sizeof(s_palette));
    memset(s_keys, 0, sizeof(s_keys));
    s_key_head = 0U;
    s_key_count = 0U;
    memset(s_touch_held, 0, sizeof(s_touch_held));
    s_frames = 0U;
    s_touch_failures = 0U;
    s_display_timeouts = 0U;
    s_consecutive_display_timeouts = 0U;
    s_display_recovery_pending = false;
    s_audio_ring = NULL;
    s_audio_task = NULL;
    s_audio_stop_waiter = NULL;
    s_audio_worker_run = false;
    s_audio_backend_ready = false;
    p4_game_platform_audio_init(&s_audio);
}

static esp_err_t run_engine(const p4_quake_config_t *config)
{
    const size_t largest_psram = heap_caps_get_largest_free_block(
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (largest_psram < MINIMUM_PSRAM_BEFORE_START) {
        ESP_LOGE(TAG,
                 "P4_QUAKE START_REJECTED reason=psram largest=%u required=%u",
                 (unsigned)largest_psram, (unsigned)MINIMUM_PSRAM_BEFORE_START);
        return ESP_ERR_NO_MEM;
    }
    reset_state(config);
    char *arguments[] = {
        "p4-quake",
        "-basedir",
        (char *)config->basedir,
        "-noudp",
    };
    ESP_LOGI(TAG,
             "P4_QUAKE START canvas=768x480 engine=512x300 target_fps=30 "
             "storage=read-only udp=deferred volume_step=%u/10",
             (unsigned)config->master_volume_step);
    QG_Create((int)(sizeof(arguments) / sizeof(arguments[0])), arguments);

    int64_t previous_us = esp_timer_get_time();
    int64_t next_frame_us = previous_us;
    while (s_running) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us < next_frame_us) {
            vTaskDelay(1U);
            continue;
        }
        double delta = (double)(now_us - previous_us) / 1000000.0;
        if (delta < 1.0 / 240.0) {
            delta = 1.0 / 240.0;
        } else if (delta > 0.1) {
            delta = 0.1;
        }
        previous_us = now_us;
        next_frame_us += 33333;
        if (next_frame_us < now_us - 100000) {
            next_frame_us = now_us + 33333;
        }
        poll_touch();
        QG_Tick(delta);
    }
    bool neutral[TOUCH_CONTROL_COUNT] = {false};
    apply_touch_state(neutral);
    if (!s_engine_quit) {
        Host_Shutdown();
    }
    heap_caps_free(s_content_pixels);
    s_content_pixels = NULL;
    ESP_LOGI(TAG,
             "P4_QUAKE STOP frames=%lu touch_failures=%lu "
             "display_timeouts=%lu fatal=%u "
             "restart_required=1",
             (unsigned long)s_frames,
             (unsigned long)s_touch_failures,
             (unsigned long)s_display_timeouts,
             s_fatal ? 1U : 0U);
    return s_fatal ? ESP_FAIL : ESP_OK;
}

static void engine_task(void *argument)
{
    quake_task_context_t *const context = argument;
    context->result = run_engine(&context->config);
    __atomic_store_n(&context->complete, true, __ATOMIC_RELEASE);
    /* The waiting owner deletes this WithCaps task and releases its stack. */
    vTaskSuspend(NULL);
}

esp_err_t p4_quake_run(const p4_quake_config_t *config)
{
    if (config == NULL || config->basedir == NULL || config->touch == NULL ||
        config->master_volume_step == 0U ||
        config->master_volume_step > 10U ||
        !p4_quake_sys_set_root(config->basedir)) {
        return ESP_ERR_INVALID_ARG;
    }

    quake_task_context_t context = {
        .config = *config,
        .result = ESP_FAIL,
        .complete = false,
    };
    TaskHandle_t task = NULL;
    const BaseType_t task_result = xTaskCreateWithCaps(
        engine_task, "p4_quake_engine", ENGINE_TASK_STACK_BYTES, &context,
        ENGINE_TASK_PRIORITY, &task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (task_result != pdPASS || task == NULL) {
        ESP_LOGE(TAG,
                 "P4_QUAKE START_REJECTED reason=engine-stack bytes=%u",
                 (unsigned)ENGINE_TASK_STACK_BYTES);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "P4_QUAKE TASK_READY stack_bytes=%u memory=psram priority=%u",
             (unsigned)ENGINE_TASK_STACK_BYTES,
             (unsigned)ENGINE_TASK_PRIORITY);
    while (!__atomic_load_n(&context.complete, __ATOMIC_ACQUIRE)) {
        vTaskDelay(1U);
    }
    vTaskDeleteWithCaps(task);
    return context.result;
}
