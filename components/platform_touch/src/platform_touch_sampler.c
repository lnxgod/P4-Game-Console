#include "platform/touch_sampler.h"

#include <limits.h>
#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "p4/frame_scheduler.h"
#include "platform_touch_sample_queue.h"
#include "platform_touch_sampler_private.h"

#define TOUCH_SAMPLER_STACK_BYTES 4096U
#define TOUCH_SAMPLER_PRIORITY 2U

struct platform_touch_sampler {
    platform_touch_t *touch;
    platform_touch_sample_queue_t queue;
    p4_tick_scheduler_t scheduler;
    portMUX_TYPE lock;
    atomic_bool stop_requested;
    atomic_bool quiescent;
    uint64_t poll_calls;
    uint64_t poll_total_us;
    uint32_t poll_max_us;
};

static void add_poll_duration(platform_touch_sampler_t *sampler,
                              uint64_t duration_us)
{
    if (sampler->poll_calls != UINT64_MAX) ++sampler->poll_calls;
    sampler->poll_total_us = UINT64_MAX - sampler->poll_total_us < duration_us
        ? UINT64_MAX : sampler->poll_total_us + duration_us;
    const uint32_t bounded_us = duration_us > UINT32_MAX
        ? UINT32_MAX : (uint32_t)duration_us;
    if (bounded_us > sampler->poll_max_us) {
        sampler->poll_max_us = bounded_us;
    }
}

static void touch_sampler_worker(void *context)
{
    platform_touch_sampler_t *sampler = context;
    p4_tick_scheduler_t scheduler = sampler->scheduler;
    TickType_t last_wake = xTaskGetTickCount();
    while (!atomic_load_explicit(&sampler->stop_requested,
                                  memory_order_acquire)) {
        platform_touch_frame_t frame;
        platform_touch_frame_neutral(&frame);
        const int64_t began_us = esp_timer_get_time();
        const esp_err_t polled = platform_touch_poll_sampled(sampler->touch,
                                                            &frame);
        const int64_t completed_us = esp_timer_get_time();
        const uint64_t duration_us = completed_us >= began_us
            ? (uint64_t)(completed_us - began_us) : 0U;
        taskENTER_CRITICAL(&sampler->lock);
        add_poll_duration(sampler, duration_us);
        const esp_err_t published = platform_touch_sample_queue_publish(
            &sampler->queue, &frame, polled, completed_us);
        taskEXIT_CRITICAL(&sampler->lock);
        if (published != ESP_OK || atomic_load_explicit(
                &sampler->stop_requested, memory_order_acquire)) {
            break;
        }

        uint32_t interval_ticks = 0U;
        /* Start validates the scheduler. An unexpected scheduling failure
         * must fail closed rather than increase the hardware polling rate. */
        if (p4_tick_scheduler_next(&scheduler, &interval_ticks) !=
                P4_SCHEDULER_OK || interval_ticks == 0U) {
            taskENTER_CRITICAL(&sampler->lock);
            (void)platform_touch_sample_queue_publish(
                &sampler->queue, &frame, ESP_ERR_INVALID_STATE,
                esp_timer_get_time());
            taskEXIT_CRITICAL(&sampler->lock);
            break;
        } else {
            const TickType_t now = xTaskGetTickCount();
            if ((TickType_t)(now - last_wake) >= (TickType_t)interval_ticks) {
                last_wake = now;
            }
            vTaskDelayUntil(&last_wake, (TickType_t)interval_ticks);
        }
    }
    /* FINAL access to sampler/touch/queue. A successful stop can free the
     * context after acquiring this store. Only the independent task stack/TCB
     * remain; they are reaped through self-deletion, never cross-core delete. */
    atomic_store_explicit(&sampler->quiescent, true, memory_order_release);
    vTaskDelete(NULL);
}

esp_err_t platform_touch_sampler_start(
    platform_touch_t *touch, platform_touch_sampler_t **out_sampler)
{
    if (out_sampler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*out_sampler != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_touch_sampler_t *sampler = heap_caps_calloc(
        1U, sizeof(*sampler), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (sampler == NULL) {
        return ESP_ERR_NO_MEM;
    }
    sampler->touch = touch;
    sampler->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&sampler->stop_requested, false);
    atomic_init(&sampler->quiescent, true);
    platform_touch_sample_queue_init(&sampler->queue, esp_timer_get_time());
    if (p4_tick_scheduler_init(&sampler->scheduler, configTICK_RATE_HZ,
            PLATFORM_TOUCH_SAMPLER_SAMPLE_HZ) != P4_SCHEDULER_OK) {
        heap_caps_free(sampler);
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t claimed = platform_touch_sampler_claim(touch);
    if (claimed != ESP_OK) {
        heap_caps_free(sampler);
        return claimed;
    }
    atomic_store_explicit(&sampler->quiescent, false, memory_order_release);
    /* Unaffined priority 2 remains below Doom audio (3) and Wi-Fi lifecycle
     * (4); existing renderer/display/audio ownership and affinity are intact. */
    if (xTaskCreate(touch_sampler_worker, "p4-touch", TOUCH_SAMPLER_STACK_BYTES,
                    sampler, TOUCH_SAMPLER_PRIORITY, NULL) != pdPASS) {
        atomic_store_explicit(&sampler->quiescent, true, memory_order_release);
        atomic_store_explicit(&sampler->stop_requested, true,
                              memory_order_release);
        const esp_err_t released = platform_touch_sampler_release(touch);
        if (released != ESP_OK) {
            *out_sampler = sampler;
            return released;
        }
        heap_caps_free(sampler);
        return ESP_ERR_NO_MEM;
    }
    *out_sampler = sampler;
    return ESP_OK;
}

esp_err_t platform_touch_sampler_read(
    platform_touch_sampler_t *sampler, platform_touch_frame_t *out_frame,
    bool *available)
{
    if (out_frame != NULL) {
        platform_touch_frame_neutral(out_frame);
    }
    if (available != NULL) {
        *available = false;
    }
    if (sampler == NULL || out_frame == NULL || available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&sampler->lock);
    /* Capture after locking: another core may have published a newer
     * completion between read entry and this lock acquisition. */
    const int64_t now_us = esp_timer_get_time();
    const esp_err_t result = platform_touch_sample_queue_take(
        &sampler->queue, now_us, out_frame, available);
    taskEXIT_CRITICAL(&sampler->lock);
    return result;
}

void platform_touch_sampler_discard(platform_touch_sampler_t *sampler)
{
    if (sampler == NULL) {
        return;
    }
    taskENTER_CRITICAL(&sampler->lock);
    platform_touch_sample_queue_discard(&sampler->queue);
    taskEXIT_CRITICAL(&sampler->lock);
}

esp_err_t platform_touch_sampler_snapshot(
    platform_touch_sampler_t *sampler, platform_touch_sampler_stats_t *out_stats)
{
    if (out_stats != NULL) {
        memset(out_stats, 0, sizeof(*out_stats));
    }
    if (sampler == NULL || out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    taskENTER_CRITICAL(&sampler->lock);
    platform_touch_sample_queue_stats(&sampler->queue, out_stats);
    out_stats->poll_calls = sampler->poll_calls;
    out_stats->poll_total_us = sampler->poll_total_us;
    out_stats->poll_max_us = sampler->poll_max_us;
    taskEXIT_CRITICAL(&sampler->lock);
    return ESP_OK;
}

esp_err_t platform_touch_sampler_stop(
    platform_touch_sampler_t **sampler_handle, uint32_t timeout_ms)
{
    if (sampler_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    platform_touch_sampler_t *sampler = *sampler_handle;
    if (sampler == NULL) {
        return ESP_OK;
    }
    atomic_store_explicit(&sampler->stop_requested, true, memory_order_release);
    const int64_t began_us = esp_timer_get_time();
    const uint64_t timeout_us = (uint64_t)timeout_ms * 1000U;
    while (!atomic_load_explicit(&sampler->quiescent, memory_order_acquire)) {
        const int64_t now_us = esp_timer_get_time();
        if (now_us < began_us || (uint64_t)(now_us - began_us) >= timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1U);
    }
    const esp_err_t released = platform_touch_sampler_release(sampler->touch);
    if (released != ESP_OK) {
        return released;
    }
    heap_caps_free(sampler);
    *sampler_handle = NULL;
    return ESP_OK;
}
