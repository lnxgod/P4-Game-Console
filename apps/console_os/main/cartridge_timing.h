// SPDX-License-Identifier: MIT
#ifndef CONSOLE_CARTRIDGE_TIMING_H
#define CONSOLE_CARTRIDGE_TIMING_H
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
typedef struct {
    uint32_t samples,maximum_us;
    uint64_t total_us;
} cartridge_phase_t;
typedef struct {
    bool started;
    int64_t previous_us;
    uint32_t fractional_us;
    cartridge_phase_t wall;
} cartridge_frame_clock_t;
static inline void cartridge_phase_record(cartridge_phase_t *phase,int64_t microseconds)
{
    if(microseconds<0||phase->samples==UINT32_MAX)return;
    const uint32_t duration=(uint64_t)microseconds>UINT32_MAX?UINT32_MAX:(uint32_t)microseconds;
    ++phase->samples;phase->total_us+=duration;
    if(duration>phase->maximum_us)phase->maximum_us=duration;
}
static inline uint32_t cartridge_phase_average(const cartridge_phase_t *phase)
{ return phase->samples?(uint32_t)(phase->total_us/phase->samples):0U; }
static inline void cartridge_frame_clock_start(cartridge_frame_clock_t *clock,int64_t now_us)
{ clock->started=true;clock->previous_us=now_us;clock->fractional_us=0U; }
static inline uint32_t cartridge_frame_clock_delta(cartridge_frame_clock_t *clock,int64_t now_us,uint32_t game_cap_ms)
{
    const int64_t elapsed=now_us>=clock->previous_us?now_us-clock->previous_us:0;
    clock->previous_us=now_us;cartridge_phase_record(&clock->wall,elapsed);
    const uint64_t cap_us = (uint64_t)game_cap_ms * 1000U;
    if ((uint64_t)elapsed >= cap_us) {
        clock->fractional_us = 0U; /* Discard long-stall debt. */
        return game_cap_ms;
    }
    const uint64_t accumulated = (uint64_t)elapsed + clock->fractional_us;
    clock->fractional_us = (uint32_t)(accumulated % 1000U);
    const uint32_t milliseconds = (uint32_t)(accumulated / 1000U);
    return milliseconds < 1U ? 1U : milliseconds;
}
/* FreeRTOS ticks wrap at 32 bits on the pinned ESP32-P4 target. Deadline deltas
 * are below half a tick epoch; future deadlines and one-tick jitter are on time. */
static inline uint32_t cartridge_deadline_late_ticks(uint32_t now,uint32_t deadline)
{
    const int32_t difference=(int32_t)(now-deadline);
    return difference>1?(uint32_t)difference:0U;
}
/* Budget native audio from wall time, independent of display/game cadence.
 * A stalled frame may recover at most 100 ms; older debt is discarded instead
 * of creating an unbounded catch-up loop. All writes still use the OS pump. */
enum { CARTRIDGE_AUDIO_CATCHUP_US = 100000 };
typedef struct {
    bool started;
    int64_t previous_us;
    uint32_t fractional;
    uint64_t discarded_us;
} cartridge_audio_clock_t;
static inline void cartridge_audio_clock_start(cartridge_audio_clock_t *clock,
                                               int64_t now_us)
{
    *clock = (cartridge_audio_clock_t){.started = true, .previous_us = now_us};
}
static inline uint32_t cartridge_audio_clock_budget(cartridge_audio_clock_t *clock,
                                                    int64_t now_us,
                                                    uint32_t sample_rate)
{
    if (!clock->started) {
        cartridge_audio_clock_start(clock, now_us);
        return 0U;
    }
    uint64_t elapsed = now_us >= clock->previous_us
        ? (uint64_t)(now_us - clock->previous_us) : 0U;
    clock->previous_us = now_us;
    if (elapsed > CARTRIDGE_AUDIO_CATCHUP_US) {
        const uint64_t discarded = elapsed - CARTRIDGE_AUDIO_CATCHUP_US;
        clock->discarded_us = UINT64_MAX - clock->discarded_us < discarded
            ? UINT64_MAX : clock->discarded_us + discarded;
        elapsed = CARTRIDGE_AUDIO_CATCHUP_US;
    }
    const uint64_t numerator = elapsed * sample_rate + clock->fractional;
    clock->fractional = (uint32_t)(numerator % UINT64_C(1000000));
    return (uint32_t)(numerator / UINT64_C(1000000));
}
static inline uint32_t cartridge_audio_take_chunk(uint32_t *remaining,
                                                uint32_t maximum)
{
    const uint32_t frames = *remaining > maximum ? maximum : *remaining;
    *remaining -= frames;
    return frames;
}
#endif
