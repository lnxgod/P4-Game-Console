#ifndef PLATFORM_TOUCH_SAMPLE_QUEUE_H
#define PLATFORM_TOUCH_SAMPLE_QUEUE_H

#include "platform/touch_sampler.h"

typedef struct {
    platform_touch_frame_t frame;
    int64_t completed_us;
} platform_touch_sample_t;

typedef struct {
    platform_touch_sample_t pending[PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY];
    platform_touch_frame_t delivered;
    platform_touch_sampler_stats_t stats;
    int64_t started_us;
    uint8_t read_index;
    uint8_t count;
    bool have_sample;
    bool producer_stalled;
} platform_touch_sample_queue_t;

/* Pure state. The runtime serializes all calls; no hardware or OS access. */
void platform_touch_sample_queue_init(platform_touch_sample_queue_t *queue,
                                     int64_t now_us);
esp_err_t platform_touch_sample_queue_publish(
    platform_touch_sample_queue_t *queue, const platform_touch_frame_t *frame,
    esp_err_t result, int64_t completed_us);
esp_err_t platform_touch_sample_queue_take(
    platform_touch_sample_queue_t *queue, int64_t now_us,
    platform_touch_frame_t *frame, bool *available);
void platform_touch_sample_queue_discard(platform_touch_sample_queue_t *queue);
void platform_touch_sample_queue_stats(const platform_touch_sample_queue_t *queue,
                                      platform_touch_sampler_stats_t *out);

#endif
