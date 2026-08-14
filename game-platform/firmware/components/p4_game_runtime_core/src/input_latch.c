#include "p4/runtime_core.h"

#include <stddef.h>
#include <string.h>

static void advance_epoch(p4_input_latch_t *latch)
{
    latch->input_epoch++;
    if (latch->input_epoch == 0U) {
        latch->input_epoch = 1U;
    }
}

void p4_input_latch_init(p4_input_latch_t *latch)
{
    if (latch == NULL) {
        return;
    }

    memset(latch, 0, sizeof(*latch));
    latch->input_epoch = 1U;
    latch->neutral_barrier = true;
}

void p4_input_latch_reset(p4_input_latch_t *latch)
{
    if (latch == NULL) {
        return;
    }

    advance_epoch(latch);
    latch->previous_down = 0U;
    latch->source_epoch = 0U;
    latch->source_seen = false;
    latch->previous_connected = false;
    latch->neutral_barrier = true;
}

void p4_input_latch_sample(
    p4_input_latch_t *latch,
    const p4_raw_input_t *raw,
    uint64_t tick,
    p4_input_frame_t *frame_out)
{
    bool connected;
    bool source_changed = false;
    uint64_t down = 0U;

    if (latch == NULL || frame_out == NULL) {
        return;
    }

    memset(frame_out, 0, sizeof(*frame_out));
    frame_out->tick = tick;
    frame_out->version = P4_GAME_API_VERSION;
    frame_out->size = (uint16_t)sizeof(*frame_out);

    connected = raw != NULL && raw->connected != 0U;
    if (raw != NULL) {
        frame_out->sampled_at_us = raw->sampled_at_us;
        if (!latch->source_seen) {
            latch->source_epoch = raw->source_epoch;
            latch->source_seen = true;
        } else if (raw->source_epoch != latch->source_epoch) {
            latch->source_epoch = raw->source_epoch;
            source_changed = true;
        }
    }

    if (source_changed) {
        advance_epoch(latch);
        latch->previous_down = 0U;
        latch->previous_connected = false;
        latch->neutral_barrier = true;
    }

    if (!connected) {
        if (latch->previous_connected) {
            advance_epoch(latch);
            latch->neutral_barrier = true;
        }
        frame_out->released = latch->previous_down;
        latch->previous_down = 0U;
        latch->previous_connected = false;
        frame_out->input_epoch = latch->input_epoch;
        return;
    }

    if (latch->neutral_barrier) {
        latch->neutral_barrier = false;
        latch->previous_down = 0U;
        latch->previous_connected = true;
        frame_out->input_epoch = latch->input_epoch;
        return;
    }

    down = raw->down & P4_BUTTON_MASK;
    frame_out->down = down;
    frame_out->pressed = down & ~latch->previous_down;
    frame_out->released = latch->previous_down & ~down;
    frame_out->left_x = raw->left_x;
    frame_out->left_y = raw->left_y;
    frame_out->right_x = raw->right_x;
    frame_out->right_y = raw->right_y;
    frame_out->left_trigger = raw->left_trigger;
    frame_out->right_trigger = raw->right_trigger;
    frame_out->dpad = raw->dpad & P4_DPAD_MASK;
    frame_out->connected = 1U;
    frame_out->input_epoch = latch->input_epoch;

    latch->previous_down = down;
    latch->previous_connected = true;
}

