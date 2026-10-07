/* Tab5 leased XRGB frames: conversion and blocking display run on a shared
 * worker. Legacy boards retain the synchronous adapter. */
#include "doom/video.h"
#include "doom/video_metrics.h"
#include "doom/video_convert.h"
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
#include "doom/video_indexed.h"
#include "doom_touch/input.h"
#endif
#include "platform/display.h"
#include "platform/display_worker_esp.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
#include <inttypes.h>
#include <string.h>

#define FRAME_PIXELS ((size_t)DOOM_VIDEO_WIDTH * DOOM_VIDEO_HEIGHT)
#ifndef P4_DOOM_TAB5_FUSED_PRESCALE
#define P4_DOOM_TAB5_FUSED_PRESCALE 0
#endif
#if P4_DOOM_TAB5_FUSED_PRESCALE
enum { RGB565_WIDTH = 384, RGB565_HEIGHT = 240 };
#else
enum { RGB565_WIDTH = DOOM_VIDEO_WIDTH, RGB565_HEIGHT = DOOM_VIDEO_HEIGHT };
#endif
#define RGB565_PIXELS ((size_t)RGB565_WIDTH * RGB565_HEIGHT)
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
/* One immutable packet per existing worker slot. Keeping an XRGB union arm
 * preserves true-color startup without changing the shared worker contract. */
enum { FRAME_XRGB8888 = 1, FRAME_INDEXED = 2 };
typedef struct {
    uint32_t format;
    uint32_t active_actions;
    union {
        uint32_t xrgb[FRAME_PIXELS];
        struct { uint32_t palette[256]; uint8_t pixels[FRAME_PIXELS]; } indexed;
    } payload;
} doom_frame_packet_t;
_Static_assert(sizeof(doom_frame_packet_t) % sizeof(uint32_t) == 0U,
               "packet must be whole worker words");
_Static_assert(offsetof(doom_frame_packet_t, payload) % _Alignof(uint32_t) == 0U,
               "packet payload must be word aligned");
#define PACKET_WORDS (sizeof(doom_frame_packet_t) / sizeof(uint32_t))
static uint32_t *s_row; /* Worker-only internal RAM; never placed on task stack. */
#endif
static display_worker_t *s_worker;
static display_worker_lease_t s_lease;
static uint16_t *s_rgb565; /* Worker-only while running; freed only after join. */
/* Worker owns counters until join. Diagnostics receives only copied values;
 * no formatting, serial output, locks or waits occur in the callback. */
static doom_video_metrics_t s_metrics;
static uint32_t s_report_frames;
static uint64_t s_window_convert_us, s_window_display_us, s_window_max_us;
static uint32_t bounded_us(uint64_t value)
{ return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value; }
static void copy_window(void)
{
    s_metrics.window_frames = s_report_frames;
    s_metrics.window_convert_avg_us = s_report_frames == 0U ? 0U
        : bounded_us(s_window_convert_us / s_report_frames);
    s_metrics.window_display_avg_us = s_report_frames == 0U ? 0U
        : bounded_us(s_window_display_us / s_report_frames);
    s_metrics.window_service_max_us = bounded_us(s_window_max_us);
}
static void publish_metrics(bool closed)
{
    copy_window();
    s_metrics.closed = closed;
    s_metrics.stack_free_bytes = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    doom_video_metrics_publish(&s_metrics);
}

/* A rejected packet/conversion is a terminal shared-worker failure even
 * when no display attempt occurred. Preserve display-attempt timing/counters;
 * publish only its error identity, capture time and lifetime fault summary. */
static int reject_frame(void)
{
    if (s_metrics.failures < UINT32_MAX) ++s_metrics.failures;
    s_metrics.captured_us = (uint64_t)esp_timer_get_time();
    s_metrics.error = ESP_ERR_INVALID_ARG;
    publish_metrics(false);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t as_esp(int result)
{
    if (result == DW_INVALID) return ESP_ERR_INVALID_STATE;
    if (result == DW_NO_MEMORY) return ESP_ERR_NO_MEM;
    if (result == DW_TIMEOUT) return ESP_ERR_TIMEOUT;
    return result;
}
static int submit_frame(void *context, const void *source, size_t stride,
                         uint32_t timeout_ms)
{
    (void)context;
    const uint64_t began = (uint64_t)esp_timer_get_time();
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    if (!source || stride != PACKET_WORDS) return reject_frame();
    const doom_frame_packet_t *const packet = source;
    if (packet->format == FRAME_INDEXED) {
        if (!doom_video_convert_indexed_touch_to_rgb565(
                packet->payload.indexed.pixels, DOOM_VIDEO_WIDTH,
                packet->payload.indexed.palette, packet->active_actions,
                s_rgb565, RGB565_WIDTH, s_row,
                P4_DOOM_TAB5_FUSED_PRESCALE != 0)) return reject_frame();
    } else if (packet->format == FRAME_XRGB8888 && packet->active_actions == 0U) {
        source = packet->payload.xrgb;
        stride = DOOM_VIDEO_WIDTH;
#else
    {
#endif
#if P4_DOOM_TAB5_FUSED_PRESCALE
        if (!doom_video_convert_xrgb8888_to_rgb565_384x240(
                source, stride, s_rgb565, RGB565_WIDTH))
#else
        if (!doom_video_convert_xrgb8888_to_rgb565(
                source, stride, s_rgb565, RGB565_WIDTH))
#endif
            return reject_frame();
    }
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    else return reject_frame();
#endif
    /* The existing platform owns all PPA and scanout-buffer retirement. */
    const uint64_t converted = (uint64_t)esp_timer_get_time();
#if P4_DOOM_TAB5_FUSED_PRESCALE
    /* Dedicated game entry preserves the baseline PPA and CPU fallback
     * images, scanout ownership and retirement fence, without CPU prescale. */
    const esp_err_t result = platform_display_submit_prescaled_game_rgb565(
        s_rgb565, RGB565_WIDTH, timeout_ms);
#else
    const esp_err_t result = platform_display_submit_rgb565(
        s_rgb565, RGB565_WIDTH, timeout_ms);
#endif
    const uint64_t ended = (uint64_t)esp_timer_get_time();
    if (result == ESP_OK) {
        if (s_metrics.completed < UINT32_MAX) ++s_metrics.completed;
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
        uint32_t *const count = packet->format == FRAME_INDEXED
            ? &s_metrics.indexed_completed : &s_metrics.xrgb_completed;
        if (*count < UINT32_MAX) ++*count;
#endif
    } else if (s_metrics.failures < UINT32_MAX) ++s_metrics.failures;
    ++s_metrics.sample_frames;
    ++s_report_frames;
    s_metrics.convert_total_us += converted - began;
    s_metrics.display_total_us += ended - converted;
    s_window_convert_us += converted - began;
    s_window_display_us += ended - converted;
    if (ended - began > s_window_max_us) s_window_max_us = ended - began;
    if (ended - began > s_metrics.service_max_us)
        s_metrics.service_max_us = ended - began;
    s_metrics.captured_us = ended;
    s_metrics.error = result;
    if (s_report_frames >= 150U || result != ESP_OK) {
        publish_metrics(false);
        s_report_frames = 0U;
        s_window_convert_us = s_window_display_us = s_window_max_us = 0U;
    }
    return result;
}
esp_err_t doom_video_init(void)
{
    if (s_worker || s_rgb565
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
        || s_row
#endif
    ) return ESP_ERR_INVALID_STATE;
    s_rgb565 = heap_caps_aligned_alloc(64U, RGB565_PIXELS * sizeof(*s_rgb565),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rgb565) return ESP_ERR_NO_MEM;
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    s_row = heap_caps_aligned_alloc(64U, DOOM_VIDEO_WIDTH * sizeof(*s_row),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_row) { heap_caps_free(s_rgb565); s_rgb565 = NULL; return ESP_ERR_NO_MEM; }
#endif
    memset(&s_metrics, 0, sizeof(s_metrics));
    s_report_frames = 0U;
    s_window_convert_us = s_window_display_us = s_window_max_us = 0U;
    s_metrics.indexed_enabled = P4_DOOM_INDEXED_PACKET_EXPERIMENT != 0;
    const uint32_t video_generation = doom_video_metrics_begin();
    const int result = display_worker_esp_create(submit_frame, NULL,
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
        PACKET_WORDS, 1U,
#else
        DOOM_VIDEO_WIDTH, DOOM_VIDEO_HEIGHT,
#endif
        sizeof(uint32_t), &s_worker);
    if (result != DW_OK) {
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
        heap_caps_free(s_row); s_row = NULL;
#endif
        heap_caps_free(s_rgb565); s_rgb565 = NULL;
    }
    else ESP_LOGI("doom_video", "P4_DOOM_VIDEO WORKER_INIT slots=2 frame_bytes=%u core=1 priority=2 fused_prescale=%d rgb565_width=%d rgb565_height=%d video_generation=%" PRIu32,
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
                  (unsigned)sizeof(doom_frame_packet_t),
#else
                  (unsigned)(FRAME_PIXELS * sizeof(uint32_t)),
#endif
                  P4_DOOM_TAB5_FUSED_PRESCALE, RGB565_WIDTH, RGB565_HEIGHT, video_generation);
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    if (result == DW_OK) ESP_LOGI("doom_video",
        "P4_DOOM_VIDEO INDEXED_PACKET_INIT packet_bytes=%u row_bytes=%u enabled=1",
        (unsigned)sizeof(doom_frame_packet_t), (unsigned)(DOOM_VIDEO_WIDTH * sizeof(*s_row)));
#endif
    return as_esp(result);
}
/* Add these two declarations to video.h only for Tab5. Caller writes the
 * complete frame through this lease, never a shared mutable overlay alias. */
esp_err_t doom_video_acquire_xrgb8888(uint32_t **pixels, uint32_t timeout_ms)
{
    if (pixels) *pixels = NULL;
    if (!pixels || !s_worker || s_lease.pixels) return ESP_ERR_INVALID_STATE;
    const int result = display_worker_acquire(s_worker, &s_lease, timeout_ms);
    if (result == DW_OK) {
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
        doom_frame_packet_t *const packet = s_lease.pixels;
        packet->format = FRAME_XRGB8888;
        packet->active_actions = 0U;
        *pixels = packet->payload.xrgb;
#else
        *pixels = s_lease.pixels;
#endif
    }
    return as_esp(result);
}
static esp_err_t publish_lease(bool wait, uint32_t timeout_ms)
{
    const int result = display_worker_commit(s_worker, &s_lease, timeout_ms, timeout_ms);
    if (result != DW_OK) {
        (void)display_worker_cancel(s_worker, &s_lease);
        return as_esp(result);
    }
    return wait ? as_esp(display_worker_flush(s_worker, timeout_ms)) : ESP_OK;
}
esp_err_t doom_video_publish_xrgb8888(bool wait, uint32_t timeout_ms)
{
    if (!s_worker || !s_lease.pixels) return ESP_ERR_INVALID_STATE;
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    if (((const doom_frame_packet_t *)s_lease.pixels)->format != FRAME_XRGB8888) {
        (void)display_worker_cancel(s_worker, &s_lease);
        return ESP_ERR_INVALID_STATE;
    }
#endif
    return publish_lease(wait, timeout_ms);
}
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
esp_err_t doom_video_acquire_indexed(uint8_t **pixels, uint32_t **palette,
                                    uint32_t timeout_ms)
{
    if (pixels) *pixels = NULL;
    if (palette) *palette = NULL;
    if (!pixels || !palette || !s_worker || s_lease.pixels)
        return ESP_ERR_INVALID_STATE;
    const int result = display_worker_acquire(s_worker, &s_lease, timeout_ms);
    if (result == DW_OK) {
        doom_frame_packet_t *const packet = s_lease.pixels;
        packet->format = FRAME_INDEXED;
        packet->active_actions = 0U;
        *pixels = packet->payload.indexed.pixels;
        *palette = packet->payload.indexed.palette;
    }
    return as_esp(result);
}
esp_err_t doom_video_publish_indexed(uint32_t active_actions, bool wait,
                                    uint32_t timeout_ms)
{
    if (!s_worker || !s_lease.pixels) return ESP_ERR_INVALID_STATE;
    doom_frame_packet_t *const packet = s_lease.pixels;
    const uint32_t mask = (UINT32_C(1) << DOOM_TOUCH_ACTION_COUNT) - UINT32_C(1);
    if (packet->format != FRAME_INDEXED || (active_actions & ~mask) != 0U) {
        (void)display_worker_cancel(s_worker, &s_lease);
        return ESP_ERR_INVALID_ARG;
    }
    packet->active_actions = active_actions;
    return publish_lease(wait, timeout_ms);
}
#endif
/* Compatibility API remains synchronous and consumes arbitrary caller-owned
 * source before return. Hot path uses acquire/publish to avoid this copy. */
esp_err_t doom_video_submit_xrgb8888(const uint32_t *source, size_t stride,
                                    uint32_t timeout_ms)
{
    if (!source || stride < DOOM_VIDEO_WIDTH) return ESP_ERR_INVALID_ARG;
    uint32_t *pixels = NULL;
    esp_err_t result = doom_video_acquire_xrgb8888(&pixels, timeout_ms);
    if (result != ESP_OK) return result;
    for (size_t y = 0U; y < DOOM_VIDEO_HEIGHT; ++y)
        memcpy(pixels + y * DOOM_VIDEO_WIDTH, source + y * stride,
               DOOM_VIDEO_WIDTH * sizeof(*pixels));
    return doom_video_publish_xrgb8888(true, timeout_ms);
}
esp_err_t doom_video_submit_black(uint32_t timeout_ms)
{
    uint32_t *pixels = NULL;
    const esp_err_t result = doom_video_acquire_xrgb8888(&pixels, timeout_ms);
    if (result != ESP_OK) return result;
    memset(pixels, 0, FRAME_PIXELS * sizeof(*pixels));
    return doom_video_publish_xrgb8888(true, timeout_ms);
}
esp_err_t doom_video_deinit(void)
{
    if (!s_worker) return ESP_ERR_INVALID_STATE;
    if (s_lease.pixels) (void)display_worker_cancel(s_worker, &s_lease);
    const int result = display_worker_stop(&s_worker, 1000U);
    if (result != DW_OK) return as_esp(result); /* Preserve ALL resources. */
    /* Join has transferred sole producer ownership back to the foreground.
     * Keep the worker's captured stack watermark, not this task's watermark. */
    copy_window();
    s_metrics.closed = true;
    doom_video_metrics_publish(&s_metrics);
#if P4_DOOM_INDEXED_PACKET_EXPERIMENT
    heap_caps_free(s_row); s_row = NULL;
#endif
    heap_caps_free(s_rgb565); s_rgb565 = NULL;
    return ESP_OK;
}
