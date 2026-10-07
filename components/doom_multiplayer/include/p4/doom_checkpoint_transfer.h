/* SPDX-License-Identifier: GPL-2.0-or-later
 * Staged transfer codec; the owning adapter registers its dedicated wire path.
 * Packets exceed existing GCE1's 48-byte inner payload cap: integration needs
 * a dedicated, negotiated Arena checkpoint path (maximum 1024 total bytes).
 * Preserve existing Game API/GCE1 limits and check route/session before parsing.
 * The outer owner authenticates peers and pins identity/content for an attempt.
 * Calls are serialized by that owner; callbacks must not reenter this module.
 * Verified completion ends transfer deadlines: receiver data/final ACKs remain
 * cached until the owner retires/resets the attempt. The owner still enforces
 * outer session expiry and releases snapshot/history leases separately.
 */
#ifndef P4_CHECKPOINT_TRANSFER_H
#define P4_CHECKPOINT_TRANSFER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    P4_CT_MAX_BYTES = 512 * 1024, P4_CT_CHUNK_BYTES = 896,
    P4_CT_WINDOW = 4, P4_CT_MAX_GUESTS = 3, P4_CT_SEND_BUDGET = 2,
    P4_CT_ID_WIRE = 36, P4_CT_META_WIRE = 116, P4_CT_CHUNK_HEADER = 44,
    P4_CT_ACK_WIRE = 44, P4_CT_PACKET_MAX = P4_CT_CHUNK_HEADER + P4_CT_CHUNK_BYTES,
    P4_CT_RETRY_MS = 250, P4_CT_SILENCE_MS = 30000, P4_CT_TOTAL_MS = 90000
};
typedef enum {
    P4_CT_REJECT = -1, P4_CT_IDLE = 0, P4_CT_PROGRESS = 1,
    P4_CT_READY = 2, P4_CT_DUPLICATE = 3
} p4_ct_result;
typedef struct {
    uint32_t schema;
    uint64_t session, attempt, checkpoint;
    uint8_t content_id[32];
} p4_ct_identity;
typedef struct {
    p4_ct_identity identity;
    uint32_t length, next_tic, map;
    uint8_t members;
    uint8_t sha256[32];
} p4_ct_meta;
/* Must perform full SHA-256 and compare all 32 expected bytes. A NULL verifier
 * is rejected. Production callers must supply their pinned SHA implementation. */
typedef bool (*p4_ct_verify_fn)(void *, const uint8_t *, size_t, const uint8_t[32]);
typedef struct {
    p4_ct_meta meta;
    uint8_t *storage;
    size_t capacity;
    uint32_t base, count;
    uint8_t seen;
    bool active, verified, failed;
    uint64_t started_ms, last_peer_ms, last_now_ms;
    p4_ct_verify_fn verify;
    void *verify_context;
} p4_ct_rx;
typedef struct {
    p4_ct_meta meta;
    const uint8_t *storage;
    uint32_t base, count;
    uint8_t acknowledged, sent;
    uint64_t sent_ms[P4_CT_WINDOW];
    bool active, verified, failed;
    uint64_t started_ms, last_peer_ms, last_now_ms;
} p4_ct_tx;
typedef struct { p4_ct_tx *guests[P4_CT_MAX_GUESTS]; uint8_t next_guest; } p4_ct_host;
typedef bool (*p4_ct_send_fn)(void *, unsigned guest, const uint8_t *, size_t);

bool p4_ct_meta_encode(const p4_ct_meta *, uint8_t *, size_t, size_t *);
bool p4_ct_meta_decode(p4_ct_meta *, const uint8_t *, size_t);
uint32_t p4_ct_chunk_count(uint32_t length);
/* Start copies metadata and resets the state. The receiver's buffer is private
 * scratch: its owner must not decode it except through p4_ct_rx_data(). */
bool p4_ct_rx_start(p4_ct_rx *, const p4_ct_identity *expected, const p4_ct_meta *,
                    uint8_t *, size_t, p4_ct_verify_fn, void *, uint64_t now_ms);
p4_ct_result p4_ct_rx_chunk(p4_ct_rx *, const uint8_t *, size_t, uint64_t now_ms);
bool p4_ct_rx_ack(const p4_ct_rx *, uint8_t *, size_t, size_t *);
const uint8_t *p4_ct_rx_data(const p4_ct_rx *, size_t *);
bool p4_ct_rx_tick(p4_ct_rx *, uint64_t now_ms);
/* Source bytes and metadata must stay immutable/pinned until sender retirement.
 * Start verifies the source digest before any packet can be prepared. */
bool p4_ct_tx_start(p4_ct_tx *, const p4_ct_meta *, const uint8_t *, size_t,
                    p4_ct_verify_fn, void *, uint64_t now_ms);
p4_ct_result p4_ct_tx_ack(p4_ct_tx *, const uint8_t *, size_t, uint64_t now_ms);
bool p4_ct_tx_prepare(p4_ct_tx *, uint64_t now_ms, uint8_t *, size_t, size_t *, uint32_t *index);
bool p4_ct_tx_sent(p4_ct_tx *, uint32_t index, uint64_t now_ms);
bool p4_ct_tx_tick(p4_ct_tx *, uint64_t now_ms);
/* Initialize with {0}, then assign at most the three fixed guest pointers.
 * Returns attempted chunk callbacks (including queue rejection), at most two.
 * Pending live control/recovery/activation consumes priority: zero chunk sends.
 * Callback enqueues only and owns a synchronous borrowed packet; copy before
 * returning. Process resulting ACKs after poll returns, never inside callback. */
unsigned p4_ct_host_poll(p4_ct_host *, bool live_control_pending, uint64_t now_ms,
                         p4_ct_send_fn, void *);
#endif
