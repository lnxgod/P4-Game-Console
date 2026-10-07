#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/touch_sampler.h"
#include "platform_touch_sampler_private.h"

struct platform_touch { bool claimed; };
static struct platform_touch s_touch;
static void (*s_entry)(void *);
static void *s_context;
static int64_t s_now_us;
static int64_t s_poll_starts[16];
static uint32_t s_poll_duration_us;
static uint32_t s_first_duration_us;
static unsigned s_polls, s_claims, s_releases, s_deletes, s_allocations;
static unsigned s_delay_calls, s_waits, s_critical;
static unsigned s_fail_poll_at;
static bool s_alloc_fails, s_task_fails, s_run_in_create, s_invalid_frame;
static bool s_run_worker_before_lock;
static esp_err_t s_claim_result, s_release_result;
static platform_touch_sampler_t **s_stop_in_poll;
static platform_touch_sampler_t **s_stop_in_delete;
static void (*s_during_delay)(void);
static platform_touch_sampler_t **s_recovery_sampler;

static void reset(void)
{
    assert(s_allocations == 0U);
    memset(&s_touch, 0, sizeof(s_touch));
    s_entry = NULL; s_context = NULL; s_now_us = 1000000;
    memset(s_poll_starts, 0, sizeof(s_poll_starts));
    s_poll_duration_us = 3000U; s_first_duration_us = 0U;
    s_polls = 0U; s_claims = 0U; s_releases = 0U; s_deletes = 0U;
    s_delay_calls = 0U; s_waits = 0U; s_critical = 0U;
    s_fail_poll_at = 1U;
    s_alloc_fails = false; s_task_fails = false; s_run_in_create = false;
    s_invalid_frame = false; s_claim_result = ESP_OK; s_release_result = ESP_OK;
    s_stop_in_poll = NULL; s_stop_in_delete = NULL;
    s_run_worker_before_lock = false;
    s_during_delay = NULL; s_recovery_sampler = NULL;
}

void sampler_test_enter(portMUX_TYPE *lock)
{
    assert(lock != NULL && lock->held == 0U && s_critical == 0U);
    if (s_run_worker_before_lock) {
        s_run_worker_before_lock = false;
        s_entry(s_context);
    }
    lock->held = 1U; ++s_critical;
}
void sampler_test_exit(portMUX_TYPE *lock)
{
    assert(lock != NULL && lock->held == 1U && s_critical == 1U);
    lock->held = 0U; --s_critical;
}
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (s_alloc_fails) return NULL;
    void *memory = calloc(count, size);
    assert(memory != NULL); ++s_allocations;
    return memory;
}
void heap_caps_free(void *memory)
{
    assert(memory != NULL && s_allocations > 0U && s_critical == 0U);
    --s_allocations; free(memory);
}
int64_t esp_timer_get_time(void) { return s_now_us; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(s_now_us / 1000); }
BaseType_t xTaskCreate(void (*entry)(void *), const char *name, uint32_t stack,
                      void *context, UBaseType_t priority, TaskHandle_t *handle)
{
    assert(strcmp(name, "p4-touch") == 0 && stack == 4096U && priority == 2U);
    assert(handle == NULL && s_critical == 0U);
    if (s_task_fails) return 0;
    s_entry = entry; s_context = context;
    if (s_run_in_create) entry(context);
    return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    assert(ticks == 1U && s_critical == 0U);
    s_now_us += (int64_t)ticks * 1000; ++s_delay_calls;
}
void vTaskDelayUntil(TickType_t *last, TickType_t interval)
{
    assert(s_critical == 0U && (interval == 16U || interval == 17U));
    const TickType_t target = *last + interval;
    assert(target > xTaskGetTickCount()); /* No missed-period catch-up burst. */
    s_now_us = (int64_t)target * 1000; *last = target; ++s_waits;
    if (s_during_delay != NULL) s_during_delay();
}
void vTaskDelete(TaskHandle_t handle)
{
    assert(handle == NULL && s_critical == 0U); ++s_deletes;
    if (s_stop_in_delete != NULL) {
        assert(platform_touch_sampler_stop(s_stop_in_delete, 0U) == ESP_OK);
        assert(*s_stop_in_delete == NULL && s_allocations == 0U);
        s_stop_in_delete = NULL;
    }
}
esp_err_t platform_touch_sampler_claim(platform_touch_t *touch)
{
    assert(touch == &s_touch && !touch->claimed); ++s_claims;
    if (s_claim_result != ESP_OK) return s_claim_result;
    touch->claimed = true; return ESP_OK;
}
esp_err_t platform_touch_sampler_release(platform_touch_t *touch)
{
    assert(touch == &s_touch && touch->claimed && s_critical == 0U); ++s_releases;
    if (s_release_result != ESP_OK) return s_release_result;
    touch->claimed = false; return ESP_OK;
}
esp_err_t platform_touch_poll_sampled(platform_touch_t *touch,
                                      platform_touch_frame_t *frame)
{
    assert(touch == &s_touch && touch->claimed && s_critical == 0U);
    assert(s_polls < 16U); s_poll_starts[s_polls] = s_now_us; ++s_polls;
    if (s_stop_in_poll != NULL) {
        platform_touch_sampler_t *owned = *s_stop_in_poll;
        assert(platform_touch_sampler_stop(s_stop_in_poll, 0U) == ESP_ERR_TIMEOUT);
        assert(*s_stop_in_poll == owned && s_releases == 0U && touch->claimed);
        s_stop_in_poll = NULL;
    }
    s_now_us += (int64_t)((s_polls == 1U && s_first_duration_us > 0U)
        ? s_first_duration_us : s_poll_duration_us);
    platform_touch_frame_neutral(frame);
    frame->sequence = (uint32_t)s_polls;
    if (s_polls == s_fail_poll_at) {
        if (s_invalid_frame) {
            frame->valid = 0U;
            return ESP_OK;
        }
        return ESP_ERR_TIMEOUT;
    }
    frame->timestamp_us = s_now_us;
    frame->contact_count = 1U; frame->contacts[0].x = 100U;
    frame->contacts[0].y = 100U;
    return ESP_OK;
}

static void test_start_failure(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    assert(platform_touch_sampler_start(NULL, &sampler) == ESP_ERR_INVALID_ARG);
    assert(platform_touch_sampler_start(&s_touch, NULL) == ESP_ERR_INVALID_ARG);
    s_alloc_fails = true;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_ERR_NO_MEM);
    assert(sampler == NULL && s_claims == 0U);
    s_alloc_fails = false; s_claim_result = ESP_ERR_INVALID_STATE;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_ERR_INVALID_STATE);
    assert(sampler == NULL && s_releases == 0U && s_allocations == 0U);
    s_claim_result = ESP_OK; s_task_fails = true;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_ERR_NO_MEM);
    assert(sampler == NULL && s_releases == 1U && !s_touch.claimed);
    assert(s_allocations == 0U && s_deletes == 0U);
}

static void test_failed_start_release_is_retryable(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_task_fails = true; s_release_result = ESP_FAIL;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_FAIL);
    assert(sampler != NULL && s_touch.claimed && s_allocations == 1U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_FAIL);
    assert(sampler != NULL && s_touch.claimed && s_releases == 2U);
    s_release_result = ESP_OK;
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(sampler == NULL && !s_touch.claimed && s_releases == 3U);
    assert(s_allocations == 0U && s_deletes == 0U);
}

static void test_stop_timeout_then_retry(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    platform_touch_sampler_t *owned = sampler;
    assert(platform_touch_sampler_stop(&sampler, 10U) == ESP_ERR_TIMEOUT);
    assert(sampler == owned && s_touch.claimed && s_releases == 0U);
    assert(s_now_us == 1010000 && s_delay_calls == 10U && s_deletes == 0U);
    platform_touch_frame_t frame; bool available = true;
    assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
    assert(s_polls == 0U);
    platform_touch_sampler_discard(sampler);
    s_entry(s_context); /* Resume after stop request: worker makes no I2C call. */
    assert(s_polls == 0U && s_deletes == 1U);
    s_release_result = ESP_FAIL;
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_FAIL);
    assert(sampler == owned && s_touch.claimed && s_allocations == 1U);
    s_release_result = ESP_OK;
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(sampler == NULL && !s_touch.claimed && s_releases == 2U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(s_releases == 2U);
}

static void test_fault_exits_and_preserves_diagnostics(bool invalid)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_invalid_frame = invalid; s_run_in_create = true;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    assert(sampler != NULL && s_deletes == 1U && s_polls == 1U);
    platform_touch_frame_t frame; bool available = false;
    const esp_err_t expected = invalid ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
    assert(platform_touch_sampler_read(sampler, &frame, &available) == expected);
    assert(frame.contact_count == 0U);
    platform_touch_sampler_stats_t stats;
    assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
    assert(stats.samples == 1U && stats.poll_calls == 1U);
    assert(stats.poll_total_us == 3000U);
    assert(stats.poll_max_us == 3000U && stats.last_completed_us == 0);
    assert(stats.fault == expected && s_polls == 1U && s_releases == 0U);
    s_now_us += 100000;
    assert(platform_touch_sampler_read(sampler, &frame, &available) == expected);
    assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
    assert(stats.poll_calls == 1U && stats.producer_stalls == 0U &&
           stats.recoveries == 0U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(s_releases == 1U && s_allocations == 0U);
}

static void test_cadence(bool overrun)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_fail_poll_at = 4U;
    if (overrun) s_first_duration_us = 40000U;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    s_entry(s_context);
    assert(s_polls == 4U && s_waits == 3U && s_deletes == 1U);
    assert(s_poll_starts[0] == 1000000);
    assert(s_poll_starts[1] == (overrun ? 1056000 : 1016000));
    assert(s_poll_starts[2] == (overrun ? 1073000 : 1033000));
    assert(s_poll_starts[3] == (overrun ? 1090000 : 1050000));
    platform_touch_sampler_stats_t stats;
    assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
    assert(stats.poll_total_us == (overrun ? 49000U : 12000U));
    assert(stats.poll_max_us == (overrun ? 40000U : 3000U));
    assert(stats.poll_calls == 4U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
}

static void test_stop_during_poll_retains_until_final_access(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_fail_poll_at = 16U;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    s_stop_in_poll = &sampler;
    s_entry(s_context);
    assert(s_polls == 1U && s_waits == 0U && s_deletes == 1U);
    assert(sampler != NULL && s_touch.claimed && s_releases == 0U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(sampler == NULL && !s_touch.claimed && s_releases == 1U);
}

static void test_context_can_be_reclaimed_before_task_stack_returns(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    s_stop_in_delete = &sampler;
    /* Simulate the joining core reclaiming context immediately after the
     * final release-store, before this worker's independent stack is reaped.
     * ASan catches any context access after that ownership transfer. */
    s_entry(s_context);
    assert(sampler == NULL && !s_touch.claimed && s_allocations == 0U);
    assert(s_deletes == 1U && s_releases == 1U);
}

static void test_publisher_between_read_entry_and_queue_lock(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_fail_poll_at = 16U;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    s_stop_in_poll = &sampler;
    s_run_worker_before_lock = true;
    platform_touch_frame_t frame; bool available = false;
    /* Publish after read starts but before it acquires the queue lock. The
     * consumer clock must be at least this sample's completion timestamp. */
    assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
    assert(available && frame.valid == 1U && frame.contact_count == 1U);
    assert(frame.timestamp_us == 1003000 && s_polls == 1U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
}

static void test_double_start_preserves_owned_handle(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    platform_touch_sampler_t *owned = sampler;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_ERR_INVALID_STATE);
    assert(sampler == owned && s_claims == 1U && s_allocations == 1U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_ERR_TIMEOUT);
    s_entry(s_context);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(sampler == NULL && s_releases == 1U && s_allocations == 0U);
}

static void observe_stall_and_recovery(void)
{
    assert(s_recovery_sampler != NULL && *s_recovery_sampler != NULL);
    platform_touch_sampler_t *const sampler = *s_recovery_sampler;
    platform_touch_frame_t frame;
    platform_touch_sampler_stats_t stats;
    bool available = false;
    if (s_waits == 1U) {
        assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
        assert(available && frame.contact_count == 1U);
        s_now_us += 60000; /* The worker's dispatch is delayed, no I2C error. */
        assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
        assert(available && frame.valid && frame.contact_count == 0U);
        assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
        assert(!available);
        assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
        assert(stats.poll_calls == 1U && stats.producer_stalls == 1U &&
               stats.recoveries == 0U && stats.fault == ESP_OK);
        assert(s_polls == 1U && s_claims == 1U && s_releases == 0U);
    } else {
        assert(s_waits == 2U);
        assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
        assert(available && frame.valid && frame.contact_count == 1U);
        assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
        assert(stats.poll_calls == 2U && stats.producer_stalls == 1U &&
               stats.recoveries == 1U && stats.fault == ESP_OK);
        assert(s_claims == 1U && s_releases == 0U && s_touch.claimed);
        assert(platform_touch_sampler_stop(s_recovery_sampler, 0U) == ESP_ERR_TIMEOUT);
        assert(*s_recovery_sampler == sampler); /* No owner replacement. */
    }
}

static void test_worker_survives_stall_and_recovers(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_fail_poll_at = 16U;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    platform_touch_sampler_t *const owned = sampler;
    s_recovery_sampler = &sampler;
    s_during_delay = observe_stall_and_recovery;
    s_entry(s_context);
    assert(sampler == owned && s_polls == 2U && s_waits == 2U);
    assert(s_deletes == 1U && s_releases == 0U);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
    assert(sampler == NULL && s_claims == 1U && s_releases == 1U);
}

static void test_delayed_worker_start_recovers(void)
{
    reset(); platform_touch_sampler_t *sampler = NULL;
    s_fail_poll_at = 16U;
    assert(platform_touch_sampler_start(&s_touch, &sampler) == ESP_OK);
    platform_touch_sampler_t *const owned = sampler;
    s_now_us += 60000;
    platform_touch_frame_t frame;
    bool available = false;
    assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
    assert(available && frame.valid && frame.contact_count == 0U && s_polls == 0U);
    assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
    assert(!available && s_polls == 0U);
    s_stop_in_poll = &sampler;
    s_entry(s_context);
    assert(sampler == owned && s_claims == 1U && s_releases == 0U);
    assert(platform_touch_sampler_read(sampler, &frame, &available) == ESP_OK);
    assert(available && frame.valid && frame.contact_count == 1U);
    platform_touch_sampler_stats_t stats;
    assert(platform_touch_sampler_snapshot(sampler, &stats) == ESP_OK);
    assert(stats.poll_calls == 1U && stats.producer_stalls == 1U &&
           stats.recoveries == 1U && stats.fault == ESP_OK);
    assert(platform_touch_sampler_stop(&sampler, 0U) == ESP_OK);
}

int main(void)
{
    test_start_failure();
    test_failed_start_release_is_retryable();
    test_stop_timeout_then_retry();
    test_fault_exits_and_preserves_diagnostics(false);
    test_fault_exits_and_preserves_diagnostics(true);
    test_cadence(false);
    test_cadence(true);
    test_stop_during_poll_retains_until_final_access();
    test_context_can_be_reclaimed_before_task_stack_returns();
    test_publisher_between_read_entry_and_queue_lock();
    test_double_start_preserves_owned_handle();
    test_worker_survives_stall_and_recovers();
    test_delayed_worker_start_recovers();
    assert(s_allocations == 0U && s_critical == 0U);
    puts("touch sampler runtime: 13 lifecycle/cadence/recovery cases passed");
    return 0;
}
