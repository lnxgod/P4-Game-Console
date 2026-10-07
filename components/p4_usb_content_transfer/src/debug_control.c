// SPDX-License-Identifier: MIT
#include "p4/debug_control.h"

#include <string.h>

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t value = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        for (unsigned j = 0; j < 8U; ++j) {
            value = (value >> 1U) ^
                    ((value & 1U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return ~value;
}

static uint64_t deadline(uint64_t now, uint32_t duration)
{
    return UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
}

void p4_debug_control_init(p4_debug_control_t *control,
                           p4_debug_send_fn send,
                           p4_debug_status_fn status,
                           void *context)
{
    if (!control) return;
    memset(control, 0, sizeof(*control));
    control->send = send;
    control->status = status;
    control->context = context;
}

void p4_debug_control_release_inputs(p4_debug_control_t *control)
{
    if (!control) return;
    control->input.touch_down = false;
    control->input.buttons = 0;
    control->input.x = 0;
    control->input.y = 0;
    control->input_until_ms = 0;
}

p4_debug_input_t p4_debug_control_sample(p4_debug_control_t *control,
                                         uint64_t now_ms)
{
    if (!control) return (p4_debug_input_t){0};
    if (control->input.enabled && now_ms >= control->session_until_ms) {
        control->input.enabled = false;
        control->session_until_ms = 0;
        p4_debug_control_release_inputs(control);
    } else if (control->input_until_ms && now_ms >= control->input_until_ms) {
        p4_debug_control_release_inputs(control);
    }
    return control->input;
}

static bool valid_request(const uint8_t *request)
{
    const uint8_t command = request[5];
    if (request[4] != 1U || request[7] || request[26] || request[27] ||
        get32(request + 28) != crc32(request, 28) ||
        command < P4_DEBUG_STATUS || command > P4_DEBUG_CLOSE) return false;
    if (command == P4_DEBUG_INPUT) {
        return !(request[6] & ~1U) &&
               !(get32(request + 16) & ~(uint32_t)P4_DEBUG_BUTTON_MASK) &&
               get16(request + 20) < P4_DEBUG_WIDTH &&
               get16(request + 22) < P4_DEBUG_HEIGHT &&
               get16(request + 24) >= 1U &&
               get16(request + 24) <= P4_DEBUG_INPUT_MAX_MS;
    }
    return request[6] == 0U && get32(request + 16) == 0U &&
           get16(request + 20) == 0U && get16(request + 22) == 0U &&
           get16(request + 24) == 0U;
}

static p4_debug_result_t apply_request(p4_debug_control_t *control, uint64_t now)
{
    const uint8_t *request = control->request;
    const uint8_t command = request[5];
    const uint32_t session = get32(request + 8);
    const uint32_t sequence = get32(request + 12);

    if (!valid_request(request)) return P4_DEBUG_INVALID;
    if (command == P4_DEBUG_STATUS) return P4_DEBUG_OK;
    if (!session || !sequence) return P4_DEBUG_INVALID;

    const bool same_session = session == control->session;
    if (same_session && control->have_last_request && sequence == control->sequence) {
        if (memcmp(request, control->last_request, P4_DEBUG_REQUEST_BYTES) != 0) {
            return P4_DEBUG_SEQUENCE;
        }
        /* A reply may be lost after CLOSE. A retry must never reopen a session
         * or reapply input that expired or was cleared during a handoff. */
        return control->input.enabled || command == P4_DEBUG_CLOSE ?
               P4_DEBUG_OK : P4_DEBUG_SESSION;
    }

    if (command == P4_DEBUG_OPEN) {
        if (control->input.enabled && !same_session) return P4_DEBUG_BUSY;
        if (same_session && control->have_last_request && sequence < control->sequence) {
            return P4_DEBUG_SEQUENCE;
        }
        p4_debug_control_release_inputs(control);
        control->input.enabled = true;
        control->session = session;
    } else {
        if (!control->input.enabled || !same_session) return P4_DEBUG_SESSION;
        if (sequence < control->sequence) return P4_DEBUG_SEQUENCE;
        if (command == P4_DEBUG_INPUT) {
            control->input.touch_down = (request[6] & 1U) != 0U;
            control->input.buttons = get32(request + 16);
            control->input.x = control->input.touch_down ? get16(request + 20) : 0U;
            control->input.y = control->input.touch_down ? get16(request + 22) : 0U;
            control->input_until_ms = deadline(now, get16(request + 24));
        } else {
            p4_debug_control_release_inputs(control);
            if (command == P4_DEBUG_CLOSE) control->input.enabled = false;
        }
    }
    control->session_until_ms = control->input.enabled ?
                                deadline(now, P4_DEBUG_SESSION_MS) : 0U;
    control->sequence = sequence;
    memcpy(control->last_request, request, P4_DEBUG_REQUEST_BYTES);
    control->have_last_request = true;
    return P4_DEBUG_OK;
}

static void handle(p4_debug_control_t *control, uint64_t now)
{
    const p4_debug_result_t result = apply_request(control, now);
    const p4_debug_input_t input = p4_debug_control_sample(control, now);
    uint8_t reply[P4_DEBUG_RESPONSE_BYTES] = {0};
    memcpy(reply, "P4E1", 4);
    reply[4] = 1;
    reply[5] = control->request[5];
    reply[6] = (uint8_t)result;
    reply[7] = (uint8_t)((input.enabled ? 1U : 0U) | (input.touch_down ? 2U : 0U));
    put32(reply + 8, get32(control->request + 8));
    put32(reply + 12, get32(control->request + 12));
    put32(reply + 16, input.buttons);
    put16(reply + 20, input.x);
    put16(reply + 22, input.y);
    put32(reply + 24, input.enabled ? (uint32_t)(control->session_until_ms - now) : 0U);
    put32(reply + 28, control->input_until_ms ? (uint32_t)(control->input_until_ms - now) : 0U);
    if (control->status) {
        control->status(control->context, (char *)reply + 32, P4_DEBUG_STATUS_BYTES);
    }
    reply[123] = 0;
    put32(reply + 124, crc32(reply, 124));
    (void)control->send(control->context, reply, sizeof(reply));
}

bool p4_debug_control_consume(p4_debug_control_t *control,
                              const uint8_t *bytes, size_t size,
                              uint64_t now_ms)
{
    if (!control || !control->send || (!bytes && size)) return false;
    (void)p4_debug_control_sample(control, now_ms);
    if (control->used && (now_ms < control->last_byte_ms ||
        now_ms - control->last_byte_ms > 1000U)) control->used = 0;
    bool claimed = control->used >= 4U;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = bytes[i];
        if (control->used < 4U && byte != (uint8_t)"P4D1"[control->used]) {
            control->used = byte == 'P' ? 1U : 0U;
            if (control->used) control->request[0] = byte;
        } else {
            control->request[control->used++] = byte;
            if (control->used >= 4U) claimed = true;
            if (control->used == P4_DEBUG_REQUEST_BYTES) {
                handle(control, now_ms);
                control->used = 0;
            }
        }
        control->last_byte_ms = now_ms;
    }
    return claimed;
}
