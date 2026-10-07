#include "platform_touch_sample_queue.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static platform_touch_frame_t contact(uint16_t x, int64_t acquired)
{
    platform_touch_frame_t f;
    platform_touch_frame_neutral(&f);
    f.contact_count = 1U;
    f.timestamp_us = acquired;
    f.contacts[0].x = x;
    f.contacts[0].y = 200U;
    return f;
}

static void publish(platform_touch_sample_queue_t *q, uint16_t x,
                    int64_t acquired, int64_t completed)
{
    const platform_touch_frame_t f = contact(x, acquired);
    assert(platform_touch_sample_queue_publish(q, &f, ESP_OK, completed) == ESP_OK);
}

static platform_touch_frame_t take(platform_touch_sample_queue_t *q,
                                   int64_t now, bool expected_available)
{
    platform_touch_frame_t f;
    bool available = !expected_available;
    assert(platform_touch_sample_queue_take(q, now, &f, &available) == ESP_OK);
    assert(available == expected_available && f.valid == 1U);
    return f;
}

static void test_start_stall_then_recovery(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    (void)take(&q, 51000, false); /* Inclusive 50 ms freshness boundary. */
    assert(take(&q, 51001, true).contact_count == 0U);
    assert(q.stats.fault == ESP_OK && q.stats.producer_stalls == 1U);
    assert(q.stats.stale_neutralizations == 1U && q.stats.recoveries == 0U);
    (void)take(&q, 70000, false);
    (void)take(&q, 90000, false);
    assert(q.stats.producer_stalls == 1U && q.stats.stale_neutralizations == 1U);
    publish(&q, 123U, 91000, 92000);
    assert(take(&q, 92000, true).contacts[0].x == 123U);
    assert(q.stats.recoveries == 1U && q.stats.fault == ESP_OK);
}

static void test_held_key_stall_then_recovery(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 1000);
    assert(take(&q, 1000, true).contact_count == 1U);
    assert(take(&q, 51001, true).contact_count == 0U);
    (void)take(&q, 60000, false);
    assert(q.stats.last_completed_us == 1000 && q.stats.samples == 1U);
    publish(&q, 124U, 61000, 62000);
    assert(take(&q, 62000, true).contacts[0].x == 124U);
    assert(q.stats.producer_stalls == 1U && q.stats.recoveries == 1U);
}

static void test_unobserved_publish_gap(bool current_down)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 1000);
    assert(take(&q, 1000, true).contact_count == 1U);
    platform_touch_frame_t f = contact(124U, 62000);
    if (!current_down) platform_touch_frame_neutral(&f);
    assert(platform_touch_sample_queue_publish(&q, &f, ESP_OK, 63000) == ESP_OK);
    /* The fresh publication must not erase a missing release boundary. */
    assert(take(&q, 63000, true).contact_count == 0U);
    f = take(&q, 63000, true);
    assert(f.contact_count == (current_down ? 1U : 0U));
    if (current_down) assert(f.contacts[0].x == 124U);
    (void)take(&q, 63000, false);
    assert(q.stats.producer_stalls == 1U && q.stats.recoveries == 1U);
    assert(q.stats.stale_neutralizations == 1U && q.stats.fault == ESP_OK);
}

static void test_delayed_first_publication(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 62000, 63000);
    assert(take(&q, 63000, true).contact_count == 0U);
    assert(take(&q, 63000, true).contact_count == 1U);
    assert(q.stats.producer_stalls == 1U && q.stats.recoveries == 1U);
}

static void test_gap_drops_old_history(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 100U, 1000, 1000);
    publish(&q, 101U, 16000, 17000);
    publish(&q, 102U, 99000, 100000);
    assert(take(&q, 100000, true).contact_count == 0U);
    assert(take(&q, 100000, true).contacts[0].x == 102U);
    (void)take(&q, 100000, false);
}

static void test_stale_contact_does_not_resurrect(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 1000);
    assert(take(&q, 1000, true).contact_count == 1U);
    publish(&q, 123U, 1000, 18000);
    publish(&q, 123U, 1000, 35000);
    publish(&q, 123U, 1000, 52000);
    assert(take(&q, 53000, true).contact_count == 0U);
    publish(&q, 123U, 1000, 69000);
    assert(take(&q, 70000, true).contact_count == 0U);
    assert(q.stats.fault == ESP_OK && q.stats.producer_stalls == 0U);
    assert(q.stats.recoveries == 0U);
    publish(&q, 124U, 71000, 72000);
    assert(take(&q, 72000, true).contacts[0].x == 124U);
}

static void test_delivered_contact_expires_while_producer_healthy(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 40000);
    assert(take(&q, 40000, true).contact_count == 1U);
    assert(take(&q, 51001, true).contact_count == 0U);
    (void)take(&q, 52000, false);
    assert(q.stats.producer_stalls == 0U && q.stats.stale_neutralizations == 1U);
}

static void test_stale_history_is_not_a_producer_stall(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    for (unsigned i = 0U; i < 5U; ++i) {
        const int64_t time = 1000 + (int64_t)i * 16000;
        publish(&q, (uint16_t)(100U + i), time, time);
    }
    assert(take(&q, 70000, true).contact_count == 0U);
    assert(q.stats.producer_stalls == 0U && q.stats.recoveries == 0U);
    publish(&q, 200U, 80000, 81000);
    assert(take(&q, 81000, true).contacts[0].x == 200U);
}

static void test_real_fault_remains_terminal(bool malformed)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    assert(take(&q, 51001, true).contact_count == 0U);
    platform_touch_frame_t f = contact(123U, 60000);
    const esp_err_t fault = malformed ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
    if (malformed) f.valid = 0U;
    assert(platform_touch_sample_queue_publish(&q, &f,
        malformed ? ESP_OK : fault, 61000) == fault);
    f = contact(124U, 70000);
    assert(platform_touch_sample_queue_publish(&q, &f, ESP_OK, 71000) == fault);
    bool available = false;
    assert(platform_touch_sample_queue_take(&q, 72000, &f, &available) == fault);
    assert(available && !f.valid && !f.contact_count);
    assert(q.stats.fault == fault && q.stats.recoveries == 0U);
}

static void test_stall_counters_saturate(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    q.stats.producer_stalls = UINT32_MAX;
    q.stats.recoveries = UINT32_MAX;
    q.stats.stale_neutralizations = UINT32_MAX;
    assert(take(&q, 51001, true).contact_count == 0U);
    publish(&q, 123U, 60000, 61000);
    assert(take(&q, 61000, true).contact_count == 1U);
    assert(q.stats.producer_stalls == UINT32_MAX);
    assert(q.stats.recoveries == UINT32_MAX);
    assert(q.stats.stale_neutralizations == UINT32_MAX);
}

static void test_debug_discard_preserves_stall_episode(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    assert(take(&q, 51001, true).contact_count == 0U);
    platform_touch_sample_queue_discard(&q);
    (void)take(&q, 60000, false);
    publish(&q, 123U, 61000, 62000);
    assert(take(&q, 62000, true).contact_count == 1U);
    assert(q.stats.producer_stalls == 1U && q.stats.recoveries == 1U);
}

static void test_short_tap_keeps_both_transitions(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 1000);
    platform_touch_frame_t neutral;
    platform_touch_frame_neutral(&neutral);
    assert(platform_touch_sample_queue_publish(&q, &neutral, ESP_OK, 18000) == ESP_OK);
    assert(take(&q, 20000, true).contact_count == 1U);
    assert(take(&q, 20000, true).contact_count == 0U);
    (void)take(&q, 20000, false);
}

static void test_overflow_keeps_neutral_fence(void)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    for (unsigned i = 0U; i <= PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY; ++i) {
        const int64_t time = 1001 + (int64_t)i;
        publish(&q, (uint16_t)(100U + i), time, time);
    }
    assert(q.stats.overflows == 1U && q.stats.producer_stalls == 0U);
    assert(take(&q, 2000, true).contact_count == 0U);
    assert(take(&q, 2000, true).contacts[0].x ==
           100U + PLATFORM_TOUCH_SAMPLER_QUEUE_CAPACITY);
}

static void test_coalescing_preserves_recovery_fence(bool current_down)
{
    platform_touch_sample_queue_t q;
    platform_touch_sample_queue_init(&q, 1000);
    publish(&q, 123U, 1000, 1000);
    assert(take(&q, 1000, true).contact_count == 1U);
    platform_touch_frame_t f = contact(123U, 62000);
    if (!current_down) platform_touch_frame_neutral(&f);
    assert(platform_touch_sample_queue_publish(&q, &f, ESP_OK, 63000) == ESP_OK);
    if (current_down) f.timestamp_us = 70000;
    assert(platform_touch_sample_queue_publish(&q, &f, ESP_OK, 71000) == ESP_OK);
    assert(take(&q, 72000, true).contact_count == 0U);
    f = take(&q, 72000, true);
    assert(f.contact_count == (current_down ? 1U : 0U));
    if (current_down) assert(f.timestamp_us == 70000);
    (void)take(&q, 72000, false);
    assert(q.stats.producer_stalls == 1U && q.stats.recoveries == 1U);
}

int main(void)
{
    test_start_stall_then_recovery();
    test_held_key_stall_then_recovery();
    test_unobserved_publish_gap(true);
    test_unobserved_publish_gap(false);
    test_delayed_first_publication();
    test_gap_drops_old_history();
    test_stale_contact_does_not_resurrect();
    test_delivered_contact_expires_while_producer_healthy();
    test_stale_history_is_not_a_producer_stall();
    test_real_fault_remains_terminal(false);
    test_real_fault_remains_terminal(true);
    test_stall_counters_saturate();
    test_debug_discard_preserves_stall_episode();
    test_short_tap_keeps_both_transitions();
    test_overflow_keeps_neutral_fence();
    test_coalescing_preserves_recovery_fence(true);
    test_coalescing_preserves_recovery_fence(false);
    puts("touch sampler queue: 17 stall/recovery/fault cases passed");
    return 0;
}
