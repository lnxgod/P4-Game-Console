// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Physical USB administration only; framing/CRC are not authentication. */
enum { P4_CLOCK_REQUEST_BYTES=20, P4_CLOCK_RESPONSE_BYTES=28 };
typedef struct { bool present, valid, pending; uint32_t unix_seconds; int error; } p4_clock_status_t;
typedef struct {
    bool (*send)(void *, const uint8_t *, size_t);
    p4_clock_status_t (*status)(void *);
    int (*set)(void *, uint32_t);
    void *context;
    uint8_t request[P4_CLOCK_REQUEST_BYTES];
    size_t used;
    uint64_t last_byte_ms;
    uint32_t last_nonce, last_seconds;
    int last_result;
} p4_clock_control_t;
/* STATUS=1, SYNC=2. Result: 0 accepted, 1 malformed, 2 busy, 3 unavailable,
 * 4 setting failed. Accepted SYNC is asynchronous: poll pending/error/readback. */
bool p4_clock_control_consume(p4_clock_control_t *, const uint8_t *, size_t, uint64_t);
