// SPDX-License-Identifier: MIT
#include "touch_scroll_velocity.h"
#include <assert.h>
#include <stdio.h>

static void down(touch_scroll_velocity_t *state, uint16_t y, int64_t time)
{
    touch_scroll_velocity_sample(state, true, true, 1U, 500U, y, time, time);
}
static void up(touch_scroll_velocity_t *state, int64_t time)
{
    touch_scroll_velocity_sample(state, true, true, 0U, 0U, 0U, 0, time);
}

static int32_t sampled_trajectory(unsigned hz, bool accelerating)
{
    touch_scroll_velocity_t state = {0};
    const int64_t start = 3000000;
    down(&state, 600U, start);
    const unsigned movement_samples = hz / 10U;
    uint16_t final_y = 600U;
    for (unsigned sample = 1U; sample <= movement_samples; ++sample) {
        const int64_t elapsed = (int64_t)sample * 1000000 / hz;
        const int64_t displacement = accelerating
            ? elapsed * elapsed / 100000000 : elapsed * 2 / 1000;
        final_y = (uint16_t)(600 - displacement);
        down(&state, final_y, start + elapsed);
    }
    for (unsigned sample = movement_samples + 1U; ; ++sample) {
        const int64_t elapsed = (int64_t)sample * 1000000 / hz;
        if (elapsed > 125000) break;
        down(&state, final_y, start + elapsed);
    }
    up(&state, start + 125000);
    int32_t velocity = 0;
    assert(touch_scroll_release_velocity(&state, state.gesture_sequence,
        start + 125000, &velocity));
    return velocity;
}

static void test_stillness_tail(int64_t tail, int64_t poll_period)
{
    touch_scroll_velocity_t state = {0};
    for (int64_t time = 0; time <= 40000; time += 10000)
        down(&state, (uint16_t)(500 - time * 2 / 1000), 4000000 + time);
    for (int64_t time = poll_period; time <= tail; time += poll_period)
        down(&state, 420U, 4040000 + time);
    up(&state, 4040000 + tail);
    int32_t velocity = 0;
    assert(touch_scroll_release_velocity(&state, state.gesture_sequence,
        4040000 + tail, &velocity));
    const int32_t expected = (int32_t)(INT64_C(131072) * (80000 - tail) / 80000);
    assert(velocity == expected); /* UP-time decay is independent of poll count. */
}

int main(void)
{
    touch_scroll_highlight_t highlight = {0};
    assert(touch_scroll_highlight_begin(&highlight, 1U, 1000000, 1001000));
    assert(touch_scroll_highlight_deferred(&highlight, 1U, true, 1023999));
    assert(!touch_scroll_highlight_deferred(&highlight, 1U, true, 1024000));
    assert(highlight.gesture_sequence == 0U); /* held tap paints by24ms */
    assert(touch_scroll_highlight_begin(&highlight, 2U, 1100000, 1100000));
    assert(!touch_scroll_highlight_deferred(&highlight, 2U, false, 1108000));
    assert(highlight.gesture_sequence == 0U); /* drag/action/page/owner cancels */
    assert(touch_scroll_highlight_begin(&highlight, 3U, 1200000, 1200000));
    assert(!touch_scroll_highlight_deferred(&highlight, 4U, true, 1208000));
    assert(!touch_scroll_highlight_begin(&highlight, 4U, 1300000, 1324000));
    assert(!touch_scroll_highlight_begin(&highlight, 0U, 1300000, 1300000));
    assert(!touch_scroll_highlight_begin(&highlight, 4U, 1300000, 1299999));
    assert(!touch_scroll_highlight_begin(&highlight, 4U, INT64_MAX, INT64_MAX));
    assert(touch_scroll_highlight_begin(&highlight, 5U, 1400000, 1401000));
    assert(!touch_scroll_highlight_deferred(&highlight, 5U, true, 1400500));
    /* Equivalent physical motion and25ms lift tail at both acquisition rates. */
    const int32_t speed60 = sampled_trajectory(60U, false);
    const int32_t speed120 = sampled_trajectory(120U, false);
    const int32_t constant_difference = speed60 > speed120
        ? speed60 - speed120 : speed120 - speed60;
    assert(constant_difference < 2704); /* <3% of1.375px/ms */
    const int32_t acceleration60 = sampled_trajectory(60U, true);
    const int32_t acceleration120 = sampled_trajectory(120U, true);
    const int32_t acceleration_difference = acceleration60 > acceleration120
        ? acceleration60 - acceleration120 : acceleration120 - acceleration60;
    assert(acceleration_difference < acceleration120 / 10); /* <10% */
    test_stillness_tail(16000, 8000);
    test_stillness_tail(16000, 4000);
    test_stillness_tail(25000, 8000);
    test_stillness_tail(25000, 5000);
    touch_scroll_velocity_t state = {0};
    down(&state, 500U, 1000000);
    const uint32_t first = state.gesture_sequence;
    down(&state, 480U, 1010000);
    assert(state.velocity_q16_per_ms == 131072);
    down(&state, 480U, 1010000); /* repeated status is not a velocity sample */
    assert(state.velocity_q16_per_ms == 131072);
    down(&state, 460U, 1020000);
    up(&state, 1025000);
    int32_t velocity = 0;
    assert(touch_scroll_release_velocity(&state, first, 1140000, &velocity));
    assert(velocity == 122880); /*5ms stillness, retained through a116ms UI stall */
    assert(touch_scroll_release_replay(&state, first, 1000000, 500U, 500U, 1140000));
    assert(!touch_scroll_release_replay(&state, first, 1020000, 500U, 460U, 1140000));
    assert(!touch_scroll_release_replay(&state, first, 1000000, 500U, 460U, 1140000));
    assert(!touch_scroll_release_velocity(&state, first, 1175001, &velocity));
    assert(!touch_scroll_release_velocity(&state, first + 1U, 1040000, &velocity));
    up(&state, 1100000);
    assert(state.release_us == 1025000); /* neutral repeats cannot extend freshness */

    down(&state, 400U, 1200000);
    assert(state.gesture_sequence != first && !state.release_valid);
    assert(!touch_scroll_release_velocity(&state, first, 1200000, &velocity));
    down(&state, 300U, 1290000); /* no speed estimate across a stale report gap */
    up(&state, 1295000);
    assert(!touch_scroll_release_velocity(&state, state.gesture_sequence, 1300000, &velocity));

    down(&state, 400U, 1400000);
    down(&state, 390U, 1410000);
    down(&state, 410U, 1420000); /* reversal uses current direction immediately */
    assert(state.velocity_q16_per_ms == -131072);
    for (int64_t time = 1430000; time <= 1510000; time += 10000)
        down(&state, 410U, time);
    up(&state, 1515000);
    assert(!touch_scroll_release_velocity(&state, state.gesture_sequence, 1520000, &velocity));

    touch_scroll_velocity_t hold = {0};
    down(&hold, 500U, 1550000);
    down(&hold, 480U, 1560000);
    down(&hold, 480U, 1640000);
    assert(hold.velocity_q16_per_ms == 0); /* exact80ms stopped hold */
    touch_scroll_velocity_t older_hold = hold;
    up(&hold, 1640000);
    assert(touch_scroll_release_velocity(&hold, hold.gesture_sequence, 1640000, &velocity));
    assert(velocity == 0);
    up(&older_hold, 1640001);
    assert(!touch_scroll_release_velocity(&older_hold, older_hold.gesture_sequence, 1640001, &velocity));

    down(&state, 400U, 1600000);
    down(&state, 300U, 1601000);
    assert(state.velocity_q16_per_ms == TOUCH_SCROLL_VELOCITY_LIMIT_Q16);
    touch_scroll_velocity_sample(&state, false, false, 0U, 0U, 0U, 0, 1602000);
    assert(state.blocked && !state.release_valid);
    down(&state, 200U, 1603000);
    assert(!state.down);
    up(&state, 1604000);
    assert(!state.blocked && !state.release_valid);
    down(&state, 400U, 1700000);
    touch_scroll_velocity_sample(&state, true, true, 2U, 500U, 300U, 1710000, 1710000);
    up(&state, 1720000);
    assert(!state.release_valid);

    down(&state, 400U, 1800000);
    up(&state, 1810000); /* tap/stop-fling tap has no movement to inject */
    assert(!touch_scroll_release_velocity(&state, state.gesture_sequence, 1820000, &velocity));
    assert(!touch_scroll_release_replay(&state, state.gesture_sequence, 1800000, 500U, 400U, 1820000));
    down(&state, 400U, 1900000);
    down(&state, 390U, 1890000); /* clock/provenance regression cancels */
    assert(state.blocked);
    up(&state, 1910000);
    state.gesture_sequence = UINT32_MAX;
    down(&state, 400U, 2000000);
    assert(state.gesture_sequence == 1U);
    puts("PASS touch release velocity: bounded highlight, real report timing,60/120Hz agreement, poll-independent lift-tail decay, gesture identity, replay, stalls, holds, reversals and cancellation");
    return 0;
}
