// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Local physical USB debugging only. CRC and session IDs are not authentication.
 * All calls, including sampling, must be made by the same application task.
 * The host owns the cable exclusively: do not interleave another protocol's
 * frames inside a debug request or coalesce them in the same dispatch buffer.
 *
 * Request (32 bytes, little endian): P4D1, version[4]=1, command[5],
 * flags[6] (bit 0 touch), zero[7], session32[8], sequence32[12], buttons32[16],
 * x16[20], y16[22], hold_ms16[24], zero16[26], CRC32[28] over bytes 0..27.
 * Only INPUT has flags/buttons/coordinates/hold data. STATUS is read-only and
 * may use session=sequence=0. All mutations need a nonzero session/sequence.
 *
 * Reply (128 bytes): P4E1, version[4]=1, command[5], result[6], flags[7]
 * (bit 0 session, bit 1 touch), echoed session32[8]/sequence32[12],
 * buttons32[16], x16[20], y16[22], remaining session_ms32[24]/input_ms32[28],
 * NUL-terminated status text[32..123], CRC32[124] over bytes 0..123.
 *
 * OPEN obtains a 30-second lease. Accepted new mutations renew it; retries
 * never renew or replay input. Input expires after at most one second.
 * Sequence values strictly increase within a session and never wrap. */
enum {
    P4_DEBUG_REQUEST_BYTES = 32,
    P4_DEBUG_RESPONSE_BYTES = 128,
    P4_DEBUG_STATUS_BYTES = 92,
    P4_DEBUG_WIDTH = 1280,
    P4_DEBUG_HEIGHT = 720,
    P4_DEBUG_SESSION_MS = 30000,
    P4_DEBUG_INPUT_MAX_MS = 1000,
    P4_DEBUG_BUTTON_MASK = 0xff,
};
typedef enum {
    P4_DEBUG_STATUS = 1,
    P4_DEBUG_OPEN = 2,
    P4_DEBUG_INPUT = 3,
    P4_DEBUG_RELEASE = 4,
    P4_DEBUG_CLOSE = 5,
} p4_debug_command_t;
typedef enum {
    P4_DEBUG_OK = 0,
    P4_DEBUG_INVALID = 1,
    P4_DEBUG_BUSY = 2,
    P4_DEBUG_SESSION = 3,
    P4_DEBUG_SEQUENCE = 4,
} p4_debug_result_t;
typedef struct {
    bool enabled;
    bool touch_down;
    uint16_t x, y;
    uint32_t buttons;
} p4_debug_input_t;
typedef bool (*p4_debug_send_fn)(void *context, const uint8_t *bytes, size_t size);
typedef void (*p4_debug_status_fn)(void *context, char *buffer, size_t size);
typedef struct {
    p4_debug_send_fn send;
    p4_debug_status_fn status;
    void *context;
    uint8_t request[P4_DEBUG_REQUEST_BYTES];
    uint8_t last_request[P4_DEBUG_REQUEST_BYTES];
    size_t used;
    uint64_t last_byte_ms, session_until_ms, input_until_ms;
    uint32_t session, sequence;
    bool have_last_request;
    p4_debug_input_t input;
} p4_debug_control_t;

void p4_debug_control_init(p4_debug_control_t *control,
                           p4_debug_send_fn send,
                           p4_debug_status_fn status,
                           void *context);
bool p4_debug_control_consume(p4_debug_control_t *control,
                              const uint8_t *bytes, size_t size,
                              uint64_t now_ms);
p4_debug_input_t p4_debug_control_sample(p4_debug_control_t *control,
                                         uint64_t now_ms);
/* Clear all injected inputs at an application transition, retaining the lease. */
void p4_debug_control_release_inputs(p4_debug_control_t *control);
