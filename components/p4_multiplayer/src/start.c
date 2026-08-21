// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

#include <limits.h>

enum {
    P4_MP_START_MIN_HOLD_MS = 250,
    P4_MP_START_MAX_HOLD_MS = 10000,
    P4_MP_START_MAX_TIMEOUT_MS = 60000,
    P4_MP_START_READY = 1,
};

static const uint8_t s_start_magic[4] = {'P', '4', 'S', 'T'};

static bool timing_valid(uint32_t hold_ms, uint32_t timeout_ms)
{
    return hold_ms >= P4_MP_START_MIN_HOLD_MS &&
        hold_ms <= P4_MP_START_MAX_HOLD_MS &&
        timeout_ms >= hold_ms &&
        timeout_ms <= P4_MP_START_MAX_TIMEOUT_MS;
}

void p4_mp_start_barrier_init(p4_mp_start_barrier_t *barrier)
{
    if (barrier != NULL) {
        *barrier = (p4_mp_start_barrier_t){0};
    }
}

p4_mp_status_t p4_mp_start_barrier_begin(
    p4_mp_start_barrier_t *barrier,
    uint16_t token,
    uint64_t now_ms,
    uint32_t hold_ms,
    uint32_t timeout_ms)
{
    if (barrier == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (token == 0U || !timing_valid(hold_ms, timeout_ms)) {
        return P4_MP_BAD_IDENTITY;
    }
    if ((barrier->state == P4_MP_START_WAITING ||
         barrier->state == P4_MP_START_ARMED) &&
        barrier->token != token) {
        return P4_MP_WRONG_SESSION;
    }
    if (barrier->state == P4_MP_START_WAITING ||
        barrier->state == P4_MP_START_ARMED) {
        return P4_MP_OK;
    }
    *barrier = (p4_mp_start_barrier_t){
        .state = P4_MP_START_WAITING,
        .token = token,
        .hold_ms = hold_ms,
        .timeout_ms = timeout_ms,
        .started_ms = now_ms,
    };
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_start_barrier_observe_ready(
    p4_mp_start_barrier_t *barrier,
    uint16_t token,
    uint64_t now_ms,
    uint32_t hold_ms,
    uint32_t timeout_ms)
{
    if (barrier == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (barrier->state == P4_MP_START_IDLE) {
        const p4_mp_status_t status = p4_mp_start_barrier_begin(
            barrier, token, now_ms, hold_ms, timeout_ms);
        if (status != P4_MP_OK) {
            return status;
        }
    }
    if (barrier->token != token) {
        return P4_MP_WRONG_SESSION;
    }
    if (barrier->state == P4_MP_START_DUE) {
        return P4_MP_OK;
    }
    if (barrier->state != P4_MP_START_WAITING &&
        barrier->state != P4_MP_START_ARMED) {
        return P4_MP_INVALID_STATE;
    }
    if (barrier->state == P4_MP_START_WAITING) {
        barrier->state = P4_MP_START_ARMED;
        barrier->launch_at_ms = now_ms + barrier->hold_ms;
    }
    return P4_MP_OK;
}

p4_mp_start_state_t p4_mp_start_barrier_poll(
    p4_mp_start_barrier_t *barrier,
    uint64_t now_ms)
{
    if (barrier == NULL) {
        return P4_MP_START_IDLE;
    }
    if (barrier->state == P4_MP_START_WAITING &&
        now_ms >= barrier->started_ms &&
        now_ms - barrier->started_ms >= barrier->timeout_ms) {
        barrier->state = P4_MP_START_TIMED_OUT;
    } else if (barrier->state == P4_MP_START_ARMED &&
               now_ms >= barrier->launch_at_ms) {
        barrier->state = P4_MP_START_DUE;
    }
    return barrier->state;
}

uint32_t p4_mp_start_barrier_remaining_ms(
    const p4_mp_start_barrier_t *barrier,
    uint64_t now_ms)
{
    if (barrier == NULL || barrier->state != P4_MP_START_ARMED ||
        now_ms >= barrier->launch_at_ms) {
        return 0U;
    }
    const uint64_t remaining = barrier->launch_at_ms - now_ms;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

void p4_mp_start_barrier_cancel(p4_mp_start_barrier_t *barrier)
{
    p4_mp_start_barrier_init(barrier);
}

p4_mp_status_t p4_mp_start_ready_encode(
    uint16_t token,
    uint8_t payload[P4_MP_START_PAYLOAD_BYTES])
{
    if (payload == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (token == 0U) {
        return P4_MP_BAD_IDENTITY;
    }
    payload[0] = s_start_magic[0];
    payload[1] = s_start_magic[1];
    payload[2] = s_start_magic[2];
    payload[3] = s_start_magic[3];
    payload[4] = P4_MP_START_SCHEMA;
    payload[5] = P4_MP_START_READY;
    payload[6] = (uint8_t)token;
    payload[7] = (uint8_t)(token >> 8U);
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_start_ready_decode(
    const uint8_t *payload,
    size_t payload_length,
    uint16_t *token_out)
{
    if (payload == NULL || token_out == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (payload_length != P4_MP_START_PAYLOAD_BYTES) {
        return P4_MP_BAD_LENGTH;
    }
    for (size_t index = 0U; index < sizeof(s_start_magic); ++index) {
        if (payload[index] != s_start_magic[index]) {
            return P4_MP_BAD_MAGIC;
        }
    }
    if (payload[4] != P4_MP_START_SCHEMA) {
        return P4_MP_BAD_VERSION;
    }
    if (payload[5] != P4_MP_START_READY) {
        return P4_MP_BAD_FLAGS;
    }
    const uint16_t token = (uint16_t)payload[6] |
        (uint16_t)((uint16_t)payload[7] << 8U);
    if (token == 0U) {
        return P4_MP_BAD_IDENTITY;
    }
    *token_out = token;
    return P4_MP_OK;
}
