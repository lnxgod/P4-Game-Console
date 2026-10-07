/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef P4_DOOM_CHECKPOINT_SERVICE_H
#define P4_DOOM_CHECKPOINT_SERVICE_H

#include "p4/doom_checkpoint_transfer.h"

enum {
    P4_CS_SNAPSHOTS = 2,
    P4_CS_CAPTURE_WAIT_MS = 1000,
    P4_CS_LEASE_MS = 110000,
    P4_CS_NO_SNAPSHOT = 0xff,
};

/* Calls, capture, and queued sends must be serialized by the owning adapter.
 * The capture callback executes only at a safe engine boundary. It writes at
 * most capacity bytes, returns false for an unsupported/transient boundary,
 * and reports the NEXT tic, current map and active membership (host bit set).
 * Callbacks cannot reenter the coordinator or modify any pinned snapshot.
 * Send queues/copies borrowed bytes before returning and never delivers ACKs
 * synchronously. The service owns no journal, engine, allocation or transport.
 */
typedef bool (*p4_cs_capture_fn)(void *context, uint8_t *storage, size_t capacity,
    size_t *length, uint32_t *next_tic, uint32_t *map, uint8_t *members);
typedef bool (*p4_cs_hash_fn)(void *context, const uint8_t *bytes, size_t length,
    uint8_t sha256[32]);
typedef bool (*p4_cs_send_fn)(void *context, uint8_t slot,
    const uint8_t *bytes, size_t length);

typedef struct {
    uint8_t *storage;
    size_t capacity;
    p4_ct_meta meta;
    uint64_t captured_ms;
    uint8_t refs; /* Player-slot bitmask, bits 1..3 only. */
    bool valid;
} p4_cs_snapshot;

typedef struct {
    p4_ct_tx tx;
    uint64_t nonce, requested_ms, last_metadata_ms;
    uint8_t snapshot;
    bool pending, metadata_sent, metadata_acked;
} p4_cs_guest;

/* Public layout permits caller-owned/static storage; fields are service-owned. */
typedef struct {
    p4_cs_snapshot snapshots[P4_CS_SNAPSHOTS];
    p4_cs_guest guests[P4_CT_MAX_GUESTS]; /* index = player slot - 1 */
    p4_cs_hash_fn hash;
    void *hash_context;
    uint64_t next_checkpoint, last_now_ms, session;
    uint32_t schema;
    uint8_t content_id[32], next_guest, failed_slots;
    bool initialized, identity_set;
} p4_cs_host;

/* Two disjoint caller-owned buffers, each capacity bytes, 1..512 KiB.
 * These buffers remain exclusive to the service until it is discarded. */
bool p4_cs_init(p4_cs_host *, uint8_t *buffer0, uint8_t *buffer1, size_t capacity,
    p4_cs_hash_fn, void *hash_context);
/* Same slot+nonce is idempotent and never extends deadlines. A new nonce
 * explicitly replaces that slot's prior request. Duplicate active nonces on
 * other slots are rejected. A failed slot cannot be requested again until
 * expire reports its failure or explicit retire acknowledges it. Admission
 * nonce history after retirement belongs to the adapter. */
bool p4_cs_request(p4_cs_host *, uint8_t slot, uint64_t nonce, uint64_t now_ms);
/* Capture once for all pending slots into one free buffer and hash once.
 * Return the started player-slot mask; no pending slots means no callback.
 * The first successful capture pins schema/session/content until re-init.
 * Failed capture/hash publishes nothing and can retry for at most one second
 * from each request; expire reports the eventual failed slot mask. */
uint8_t p4_cs_boundary(p4_cs_host *, uint32_t schema, uint64_t session,
    const uint8_t content_id[32], uint64_t now_ms, p4_cs_capture_fn, void *context);
/* Explicit owner retirement also acknowledges any latched failure. */
void p4_cs_retire(p4_cs_host *, uint8_t slot);
/* At most two total attempted sends including metadata, round-robin over
 * three guests. Live control suppresses every checkpoint send. */
unsigned p4_cs_poll(p4_cs_host *, bool live_control_pending, uint64_t now_ms,
    p4_cs_send_fn, void *context);
/* ACKs must already pass the adapter's protocol/route/peer/attempt gates.
 * Exact transfer identity and ACK semantics are checked again here. */
p4_ct_result p4_cs_ack(p4_cs_host *, uint8_t slot,
    const uint8_t *bytes, size_t length, uint64_t now_ms);
/* Retires timed-out/failed requests and snapshots whose next tic is older
 * than the rolling journal's first retained tic. Completed transfers remain
 * pinned until activation calls retire, but still expire at the 110s lease.
 * Failures detected by other service calls are latched until this read. */
uint8_t p4_cs_expire(p4_cs_host *, uint32_t first_retained_tic, uint64_t now_ms);
const p4_ct_meta *p4_cs_meta(const p4_cs_host *, uint8_t slot);
bool p4_cs_complete(const p4_cs_host *, uint8_t slot);

#endif
