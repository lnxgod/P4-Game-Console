// SPDX-License-Identifier: MIT
#ifndef TOUCH_SCROLL_VELOCITY_H
#define TOUCH_SCROLL_VELOCITY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    TOUCH_SCROLL_SAMPLE_MAX_GAP_US = 80000,
    TOUCH_SCROLL_RELEASE_MAX_AGE_US = 150000,
    TOUCH_SCROLL_VELOCITY_LIMIT_Q16 = 196608,
    TOUCH_SCROLL_DOWN_HIGHLIGHT_DELAY_US = 24000,
    TOUCH_SCROLL_VELOCITY_FILTER_US = 16000,
};

typedef struct {
    uint32_t gesture_sequence;
    int64_t started_us, deadline_us;
} touch_scroll_highlight_t;

static inline bool touch_scroll_highlight_begin(touch_scroll_highlight_t *state,
    uint32_t gesture_sequence, int64_t report_us, int64_t now_us)
{
    *state = (touch_scroll_highlight_t){0};
    if (!gesture_sequence || report_us <= 0 ||
        report_us > INT64_MAX - TOUCH_SCROLL_DOWN_HIGHLIGHT_DELAY_US || now_us < report_us ||
        now_us - report_us >= TOUCH_SCROLL_DOWN_HIGHLIGHT_DELAY_US)
        return false;
    *state = (touch_scroll_highlight_t){
        .gesture_sequence = gesture_sequence,
        .started_us = now_us,
        .deadline_us = report_us + TOUCH_SCROLL_DOWN_HIGHLIGHT_DELAY_US,
    };
    return true;
}

static inline bool touch_scroll_highlight_deferred(touch_scroll_highlight_t *state,
    uint32_t gesture_sequence, bool candidate, int64_t now_us)
{
    if (!candidate || !gesture_sequence || state->gesture_sequence != gesture_sequence ||
        now_us < state->started_us || now_us >= state->deadline_us) {
        *state = (touch_scroll_highlight_t){0};
        return false;
    }
    return true;
}

/* The sampler owns this state. A mailbox copies it together with its frame;
 * the joined UI task authorizes any replay against its consumed gesture. */
typedef struct {
    uint32_t gesture_sequence;
    uint16_t last_x, last_y;
    int64_t last_down_us, last_motion_us, release_us;
    int32_t velocity_q16_per_ms;
    int32_t moving_velocity_q16_per_ms;
    bool down, blocked, release_valid;
} touch_scroll_velocity_t;

static inline int32_t touch_scroll_stillness_velocity(const touch_scroll_velocity_t *state,
    int64_t now_us)
{
    if (state->last_motion_us <= 0 || now_us < state->last_motion_us ||
        now_us - state->last_motion_us >= TOUCH_SCROLL_SAMPLE_MAX_GAP_US)
        return 0;
    /* Same-coordinate ST712x polls have fresh software timestamps. A bounded
     * elapsed-time envelope avoids treating each 120 Hz acquisition as a new
     * multiplicative deceleration while a finger is briefly still at lift. */
    const int64_t remaining = TOUCH_SCROLL_SAMPLE_MAX_GAP_US -
        (now_us - state->last_motion_us);
    return (int32_t)((int64_t)state->moving_velocity_q16_per_ms * remaining /
        TOUCH_SCROLL_SAMPLE_MAX_GAP_US);
}

static inline void touch_scroll_velocity_cancel(touch_scroll_velocity_t *state)
{
    const uint32_t sequence = state->gesture_sequence;
    *state = (touch_scroll_velocity_t){
        .gesture_sequence = sequence, .blocked = true,
    };
}

static inline void touch_scroll_velocity_sample(touch_scroll_velocity_t *state,
    bool successful, bool valid, unsigned contacts, uint16_t x, uint16_t y,
    int64_t report_us, int64_t observed_us)
{
    if (!successful || !valid || contacts > 1U ||
        (contacts && (x >= 1280U || y >= 720U || report_us <= 0 ||
                     report_us > observed_us))) {
        touch_scroll_velocity_cancel(state);
        return;
    }
    if (!contacts) {
        if (state->blocked) {
            state->blocked = false;
            return;
        }
        if (state->down) {
            state->down = false;
            state->release_us = observed_us;
            state->velocity_q16_per_ms =
                touch_scroll_stillness_velocity(state, observed_us);
            state->release_valid = state->last_down_us > 0 &&
                observed_us >= state->last_down_us;
        }
        return; /* Repeated neutral polls retain the first release time. */
    }
    if (state->blocked) {
        return; /* A failed or multi-contact run must lift before restarting. */
    }
    if (!state->down) {
        uint32_t sequence = state->gesture_sequence + 1U;
        if (!sequence) sequence = 1U;
        *state = (touch_scroll_velocity_t){
            .gesture_sequence = sequence, .last_x = x, .last_y = y,
            .last_down_us = report_us, .down = true,
        };
        return;
    }
    if (report_us < state->last_down_us) {
        touch_scroll_velocity_cancel(state);
        return;
    }
    if (report_us == state->last_down_us) {
        /* Repeated hardware reports have no new elapsed-time information. */
        if (x != state->last_x || y != state->last_y)
            touch_scroll_velocity_cancel(state);
        return;
    }
    const int64_t elapsed_us = report_us - state->last_down_us;
    const int delta = (int)state->last_y - (int)y; /* content follows -finger Y */
    if (elapsed_us > TOUCH_SCROLL_SAMPLE_MAX_GAP_US) {
        state->velocity_q16_per_ms = 0;
        state->moving_velocity_q16_per_ms = 0;
        state->last_motion_us = 0;
    } else if (delta) {
        int64_t velocity = (int64_t)delta * INT64_C(65536000) / elapsed_us;
        if (velocity > TOUCH_SCROLL_VELOCITY_LIMIT_Q16)
            velocity = TOUCH_SCROLL_VELOCITY_LIMIT_Q16;
        if (velocity < -TOUCH_SCROLL_VELOCITY_LIMIT_Q16)
            velocity = -TOUCH_SCROLL_VELOCITY_LIMIT_Q16;
        const int32_t previous = state->last_down_us > state->last_motion_us
            ? touch_scroll_stillness_velocity(state, report_us)
            : state->moving_velocity_q16_per_ms;
        if (!state->last_motion_us || !previous ||
            (previous < 0) != (velocity < 0)) {
            state->velocity_q16_per_ms = (int32_t)velocity;
        } else {
            state->velocity_q16_per_ms = (int32_t)(
                ((int64_t)previous * TOUCH_SCROLL_VELOCITY_FILTER_US +
                 velocity * elapsed_us) /
                (TOUCH_SCROLL_VELOCITY_FILTER_US + elapsed_us));
        }
        state->moving_velocity_q16_per_ms = state->velocity_q16_per_ms;
        state->last_motion_us = report_us;
    } else {
        state->velocity_q16_per_ms =
            touch_scroll_stillness_velocity(state, report_us);
    }
    state->last_x = x;
    state->last_y = y;
    state->last_down_us = report_us;
}

static inline bool touch_scroll_release_matches(const touch_scroll_velocity_t *state,
    uint32_t consumed_sequence, int64_t now_us)
{
    return state->release_valid && !state->down && !state->blocked &&
        consumed_sequence != 0U && state->gesture_sequence == consumed_sequence &&
        state->release_us > 0 && now_us >= state->release_us &&
        now_us - state->release_us <= TOUCH_SCROLL_RELEASE_MAX_AGE_US &&
        state->last_down_us > 0 && state->release_us >= state->last_down_us &&
        state->release_us - state->last_down_us <= TOUCH_SCROLL_SAMPLE_MAX_GAP_US;
}

static inline bool touch_scroll_release_replay(const touch_scroll_velocity_t *state,
    uint32_t consumed_sequence, int64_t consumed_down_us,
    uint16_t consumed_x, uint16_t consumed_y, int64_t now_us)
{
    return touch_scroll_release_matches(state, consumed_sequence, now_us) &&
        state->last_down_us > consumed_down_us &&
        (state->last_x != consumed_x || state->last_y != consumed_y);
}

static inline bool touch_scroll_release_velocity(const touch_scroll_velocity_t *state,
    uint32_t consumed_sequence, int64_t now_us, int32_t *velocity)
{
    if (!velocity || !touch_scroll_release_matches(state, consumed_sequence, now_us) ||
        state->last_motion_us <= 0 || state->release_us < state->last_motion_us ||
        state->release_us - state->last_motion_us > TOUCH_SCROLL_SAMPLE_MAX_GAP_US)
        return false;
    *velocity = state->velocity_q16_per_ms;
    return true;
}

#endif
