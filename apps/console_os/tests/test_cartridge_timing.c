// SPDX-License-Identifier: MIT
#include "cartridge_timing.h"
#include <assert.h>
#include <stdio.h>
static void test_audio_cadence(uint32_t hz)
{
    cartridge_audio_clock_t clock = {0};
    const int64_t start = INT64_C(9000000); /* Loading is never audio debt. */
    cartridge_audio_clock_start(&clock, start);
    uint64_t samples = 0U, game_ms = 0U;
    cartridge_frame_clock_t frame_clock = {0};
    cartridge_frame_clock_start(&frame_clock, start);
    for (uint32_t frame = 1U; frame <= hz * 120U; ++frame) {
        const int64_t now = start + (int64_t)((uint64_t)frame * 1000000U / hz);
        game_ms += cartridge_frame_clock_delta(&frame_clock, now, 100U);
        assert(game_ms == (uint64_t)(now - start) / 1000U);
        uint32_t remaining = cartridge_audio_clock_budget(&clock, now, 16000U);
        while (remaining != 0U) {
            const uint32_t chunk = cartridge_audio_take_chunk(&remaining, 267U);
            assert(chunk > 0U && chunk <= 267U);
            samples += chunk;
        }
        assert(samples == (uint64_t)(now - start) * 16000U / 1000000U);
    }
    assert(samples == UINT64_C(1920000));
    assert(game_ms == UINT64_C(120000) && frame_clock.fractional_us == 0U);
    assert(clock.fractional == 0U && clock.discarded_us == 0U);
}
static void test_audio_stalls(void)
{
    cartridge_audio_clock_t clock = {0};
    assert(cartridge_audio_clock_budget(&clock, INT64_C(9000000), 16000U) == 0U);
    int64_t now = INT64_C(9000000);
    for (unsigned stall = 0U; stall < 1000U; ++stall) {
        now += INT64_C(2000000);
        uint32_t remaining = cartridge_audio_clock_budget(&clock, now, 16000U);
        assert(remaining == 1600U);
        unsigned chunks = 0U;
        while (remaining != 0U) {
            assert(cartridge_audio_take_chunk(&remaining, 267U) <= 267U);
            ++chunks;
        }
        assert(chunks == 6U);
        assert(cartridge_audio_clock_budget(&clock, now, 16000U) == 0U);
    }
    assert(clock.discarded_us == UINT64_C(1900000000));
    /* A new short frame resumes normally; no old stall debt remains. */
    assert(cartridge_audio_clock_budget(&clock, now + 16667, 16000U) == 266U);
    assert(clock.fractional == 672000U);
    assert(cartridge_audio_clock_budget(&clock, now + 33334, 16000U) == 267U);
    assert(clock.fractional == 344000U);
}
int main(void)
{
    test_audio_cadence(30U); test_audio_cadence(57U); test_audio_cadence(60U);
    test_audio_stalls();
    cartridge_frame_clock_t clock={0};
    /* Load/initial present took 3 seconds: first gameplay delta starts after it. */
    cartridge_frame_clock_start(&clock,3000000);
    assert(cartridge_frame_clock_delta(&clock,3016667,100U)==16U);
    assert(clock.wall.maximum_us==16667U);
    assert(cartridge_frame_clock_delta(&clock,3366667,100U)==100U);
    assert(clock.fractional_us == 0U);
    assert(clock.wall.maximum_us==350000U); /* raw stall survives game clamping */
    assert(clock.wall.total_us==366667U&&clock.wall.samples==2U);
    assert(cartridge_frame_clock_delta(&clock,3366667,100U)==1U);
    assert(cartridge_deadline_late_ticks(101U,100U)==0U);
    assert(cartridge_deadline_late_ticks(99U,100U)==0U);
    assert(cartridge_deadline_late_ticks(150U,100U)==50U);
    uint32_t deadline=100U,now=150U;
    if(cartridge_deadline_late_ticks(now,deadline))deadline=now;
    assert(cartridge_deadline_late_ticks(167U,deadline+17U)==0U); /* debt cleared */
    assert(cartridge_deadline_late_ticks(3U,UINT32_MAX-2U)==6U);
    assert(cartridge_deadline_late_ticks(UINT32_MAX-2U,3U)==0U);
    cartridge_phase_t phase={0};cartridge_phase_record(&phase,100);cartridge_phase_record(&phase,300);
    assert(cartridge_phase_average(&phase)==200U&&phase.maximum_us==300U);
    cartridge_phase_record(&phase,-1);assert(phase.samples==2U);
    phase.samples=UINT32_MAX;cartridge_phase_record(&phase,999);assert(phase.maximum_us==300U);
    puts("PASS cartridge timing: load excluded, raw stalls retained, game delta capped, phase averages bounded, late deadlines rebased, tick wrap safe; 30/57/60 Hz game and audio clocks have no drift, stall catch-up <=6 chunks, load excluded");
    return 0;
}
