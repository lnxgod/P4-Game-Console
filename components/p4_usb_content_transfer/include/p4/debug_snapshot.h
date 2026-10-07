// SPDX-License-Identifier: MIT
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Local physical USB diagnostics only; IDs and CRCs are not authentication.
 * The application calls every function from one task and owns the USB cable
 * exclusively while requesting a snapshot. Do not interleave protocols within
 * a request or coalesce different protocols in one dispatch buffer.
 *
 * Request (32 bytes, little endian): P4S1, version[4]=1, command[5],
 * BEGIN flags[6], zero[7], nonzero capture_id32[8], nonzero sequence32[12], offset32[16],
 * length16[20], zero[22..27], CRC32[28] over bytes 0..27.
 * BEGIN and END require offset=length=0. READ requires length in 1..2048.
 * BEGIN flag bit 0 selects quarter width and height; other bits must be zero.
 * READ and END require flags=0. Full resolution remains the default flags=0.
 * Sequence is echoed for correlation; reads may be retried or reordered.
 *
 * Response: 32-byte header, payload, then CRC32 over header and payload.
 * Header: P4T1, version[4]=1, command[5], result[6], zero[7], echoed ID32[8],
 * sequence32[12], offset32[16], total_bytes32[20], payload_length16[24],
 * width16[26], height16[28], format[30]=1 (little-endian RGB565),
 * rotation[31]=1 (host rotates 90 degrees clockwise for logical orientation).
 * Only successful READ has payload; lengths are clamped at the buffer end.
 * Metadata is zero when no matching active snapshot exists.
 *
 * Capture transfers ownership of a tightly packed width*height*2 byte buffer
 * to this service, capped at 1,843,200 bytes (the full Tab5 scanout).
 * Quarter mode requires dimensions divisible by four and compacts this owned
 * copy in place, selecting native pixel (4*x+2,4*y+2) for each output pixel.
 * The allocation and release pointer are unchanged; the response dimensions
 * describe the smaller image, capped at 115,200 bytes, with the same rotation.
 * The buffer must remain immutable until release; a failed capture
 * that returns a buffer is also released. Send consumes/copies a complete
 * response synchronously. All four callbacks are required. Availability is
 * checked before accepting operations and by poll; losing availability frees
 * any active snapshot. The caller must poll even without USB input and cancel
 * before mode/content handoffs.
 *
 * One snapshot may exist at a time. BEGIN with its ID and flags is idempotent, without
 * renewing either deadline. Reusing the active ID with different BEGIN flags
 * returns INVALID. Valid READ renews the 30-second idle deadline; the absolute
 * lifetime is 120 seconds. Use a new random nonzero ID for each
 * capture. Retrying the most recently ended/cancelled ID never recaptures.
 * END may be retried, but can never release a different active ID. */
enum {
    P4_DEBUG_SNAPSHOT_REQUEST_BYTES = 32,
    P4_DEBUG_SNAPSHOT_HEADER_BYTES = 32,
    P4_DEBUG_SNAPSHOT_MAX_PAYLOAD = 2048,
    P4_DEBUG_SNAPSHOT_MAX_RESPONSE = 2084,
    P4_DEBUG_SNAPSHOT_MAX_IMAGE_BYTES = 1843200,
    P4_DEBUG_SNAPSHOT_QUARTER = 1,
    P4_DEBUG_SNAPSHOT_MAX_QUARTER_BYTES = 115200,
    P4_DEBUG_SNAPSHOT_IDLE_MS = 30000,
    P4_DEBUG_SNAPSHOT_LIFETIME_MS = 120000,
};

typedef enum {
    P4_DEBUG_SNAPSHOT_BEGIN = 1,
    P4_DEBUG_SNAPSHOT_READ = 2,
    P4_DEBUG_SNAPSHOT_END = 3,
} p4_debug_snapshot_command_t;

typedef enum {
    P4_DEBUG_SNAPSHOT_OK = 0,
    P4_DEBUG_SNAPSHOT_INVALID = 1,
    P4_DEBUG_SNAPSHOT_BUSY = 2,
    P4_DEBUG_SNAPSHOT_EXPIRED = 3,
    P4_DEBUG_SNAPSHOT_UNAVAILABLE = 4,
    P4_DEBUG_SNAPSHOT_FAILED = 5,
} p4_debug_snapshot_result_t;

typedef bool (*p4_debug_snapshot_send_fn)(void *, const uint8_t *, size_t);
typedef bool (*p4_debug_snapshot_capture_fn)(void *, uint8_t **, size_t *,
                                           uint16_t *, uint16_t *);
typedef void (*p4_debug_snapshot_release_fn)(void *, uint8_t *);
typedef bool (*p4_debug_snapshot_available_fn)(void *);

typedef struct {
    p4_debug_snapshot_send_fn send;
    p4_debug_snapshot_capture_fn capture;
    p4_debug_snapshot_release_fn release;
    p4_debug_snapshot_available_fn available;
    void *context;
    uint8_t request[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    size_t used;
    uint64_t last_byte_ms, started_ms, idle_until_ms, absolute_until_ms;
    uint8_t *data;
    size_t size;
    uint16_t width, height;
    uint8_t flags;
    uint32_t capture_id, last_ended_id, last_cancelled_id;
} p4_debug_snapshot_t;

void p4_debug_snapshot_init(p4_debug_snapshot_t *snapshot,
                            p4_debug_snapshot_send_fn send,
                            p4_debug_snapshot_capture_fn capture,
                            p4_debug_snapshot_release_fn release,
                            p4_debug_snapshot_available_fn available,
                            void *context);
bool p4_debug_snapshot_consume(p4_debug_snapshot_t *snapshot,
                               const uint8_t *bytes, size_t size,
                               uint64_t now_ms);
void p4_debug_snapshot_poll(p4_debug_snapshot_t *snapshot, uint64_t now_ms);
void p4_debug_snapshot_cancel(p4_debug_snapshot_t *snapshot);
