/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "p4/doom_checkpoint_service.h"
#include <string.h>

static bool slot_valid(uint8_t slot)
{ return slot >= 1U && slot <= P4_CT_MAX_GUESTS; }

static void release_guest(p4_cs_host *host, uint8_t slot)
{
    p4_cs_guest *guest = &host->guests[slot - 1U];
    if (guest->snapshot < P4_CS_SNAPSHOTS) {
        p4_cs_snapshot *snapshot = &host->snapshots[guest->snapshot];
        snapshot->refs &= (uint8_t)~(1U << slot);
        if (!snapshot->refs) snapshot->valid = false;
    }
    *guest = (p4_cs_guest){.snapshot = P4_CS_NO_SNAPSHOT};
}

/* All failures are latched until the owner asks for the failed-slot mask.
 * Poll/boundary cannot silently lose the notification while retiring storage. */
static void expire_at(p4_cs_host *host, uint32_t first_tic, uint64_t now)
{
    const bool backwards = now < host->last_now_ms;
    if (!backwards) host->last_now_ms = now;
    for (uint8_t slot = 1U; slot <= P4_CT_MAX_GUESTS; ++slot) {
        p4_cs_guest *guest = &host->guests[slot - 1U];
        if (!guest->nonce) continue;
        bool failed = backwards || now < guest->requested_ms;
        if (guest->pending) {
            failed = failed || now - guest->requested_ms >= P4_CS_CAPTURE_WAIT_MS;
        } else if (guest->snapshot >= P4_CS_SNAPSHOTS) {
            failed = true;
        } else {
            const p4_cs_snapshot *snapshot = &host->snapshots[guest->snapshot];
            failed = failed || !snapshot->valid || !(snapshot->refs & (1U << slot)) ||
                now < snapshot->captured_ms || now - snapshot->captured_ms >= P4_CS_LEASE_MS ||
                snapshot->meta.next_tic < first_tic;
            if (!failed && !p4_ct_tx_tick(&guest->tx, now)) failed = true;
        }
        if (failed) {
            release_guest(host, slot);
            host->failed_slots |= (uint8_t)(1U << slot);
        }
    }
}

bool p4_cs_init(p4_cs_host *host, uint8_t *buffer0, uint8_t *buffer1, size_t capacity,
    p4_cs_hash_fn hash, void *context)
{
    if (!host) return false;
    *host = (p4_cs_host){0};
    if (!buffer0 || !buffer1 || !capacity || capacity > P4_CT_MAX_BYTES || !hash) return false;
    const uintptr_t a = (uintptr_t)buffer0, b = (uintptr_t)buffer1;
    if ((a <= b ? b - a : a - b) < capacity) return false;
    host->snapshots[0].storage = buffer0;
    host->snapshots[1].storage = buffer1;
    host->snapshots[0].capacity = host->snapshots[1].capacity = capacity;
    for (unsigned i = 0; i < P4_CT_MAX_GUESTS; ++i)
        host->guests[i].snapshot = P4_CS_NO_SNAPSHOT;
    host->hash = hash;
    host->hash_context = context;
    host->next_checkpoint = 1;
    host->initialized = true;
    return true;
}

void p4_cs_retire(p4_cs_host *host, uint8_t slot)
{
    if (!host || !host->initialized || !slot_valid(slot)) return;
    release_guest(host, slot);
    host->failed_slots &= (uint8_t)~(1U << slot);
}

bool p4_cs_request(p4_cs_host *host, uint8_t slot, uint64_t nonce, uint64_t now)
{
    if (!host || !host->initialized || !slot_valid(slot) || !nonce) return false;
    p4_cs_guest *guest = &host->guests[slot - 1U];
    const bool same_request = guest->nonce == nonce;
    expire_at(host, 0, now);
    if (now < host->last_now_ms || (host->failed_slots & (1U << slot))) return false;
    if (same_request) return guest->nonce == nonce;
    for (uint8_t other = 1U; other <= P4_CT_MAX_GUESTS; ++other)
        if (other != slot && host->guests[other - 1U].nonce == nonce) return false;
    p4_cs_retire(host, slot);
    *guest = (p4_cs_guest){.nonce = nonce, .requested_ms = now,
        .snapshot = P4_CS_NO_SNAPSHOT, .pending = true};
    return true;
}

/* p4_ct_tx_start still validates metadata and initializes the transfer. Its
 * private service verifier recognizes only this immutable, already-hashed
 * snapshot; hashing it again for each guest would stall the safe boundary. */
static bool cached_snapshot_hash(void *context, const uint8_t *bytes, size_t length,
    const uint8_t expected[32])
{
    const p4_cs_snapshot *snapshot = context;
    return snapshot && snapshot->valid && bytes == snapshot->storage &&
        length == snapshot->meta.length && !memcmp(expected, snapshot->meta.sha256, 32);
}

uint8_t p4_cs_boundary(p4_cs_host *host, uint32_t schema, uint64_t session,
    const uint8_t content[32], uint64_t now, p4_cs_capture_fn capture, void *context)
{
    if (!host || !host->initialized) return 0;
    expire_at(host, 0, now);
    if (now < host->last_now_ms || !schema || !session || !content || !capture ||
        !host->next_checkpoint) return 0;
    if (host->identity_set && (host->schema != schema || host->session != session ||
        memcmp(host->content_id, content, 32))) return 0;
    uint8_t pending = 0;
    for (uint8_t slot = 1U; slot <= P4_CT_MAX_GUESTS; ++slot)
        if (host->guests[slot - 1U].pending) pending |= (uint8_t)(1U << slot);
    if (!pending) return 0;
    unsigned buffer = 0;
    while (buffer < P4_CS_SNAPSHOTS && host->snapshots[buffer].refs) ++buffer;
    if (buffer == P4_CS_SNAPSHOTS) return 0;
    p4_cs_snapshot *snapshot = &host->snapshots[buffer];
    snapshot->valid = false;
    snapshot->meta = (p4_ct_meta){0};
    size_t length = 0;
    uint32_t next_tic = 0, map = 0;
    uint8_t members = 0;
    if (!capture(context, snapshot->storage, snapshot->capacity,
                 &length, &next_tic, &map, &members) ||
        !length || length > snapshot->capacity || length > P4_CT_MAX_BYTES ||
        !map || !(members & 1U) || (members & 0xf0U)) return 0;
    p4_ct_meta meta = {.identity = {.schema = schema, .session = session,
            .attempt = 1, .checkpoint = host->next_checkpoint},
        .length = (uint32_t)length, .next_tic = next_tic, .map = map, .members = members};
    memcpy(meta.identity.content_id, content, 32);
    if (!host->hash(host->hash_context, snapshot->storage, length, meta.sha256)) return 0;
    snapshot->meta = meta;
    snapshot->captured_ms = now;
    snapshot->valid = true;
    uint8_t started = 0;
    for (uint8_t slot = 1U; slot <= P4_CT_MAX_GUESTS; ++slot) {
        if (!(pending & (1U << slot))) continue;
        p4_cs_guest *guest = &host->guests[slot - 1U];
        meta.identity.attempt = guest->nonce;
        if (!p4_ct_tx_start(&guest->tx, &meta, snapshot->storage, length,
                            cached_snapshot_hash, snapshot, now)) {
            release_guest(host, slot);
            host->failed_slots |= (uint8_t)(1U << slot);
            continue;
        }
        guest->snapshot = (uint8_t)buffer;
        guest->pending = false;
        guest->metadata_sent = guest->metadata_acked = false;
        guest->last_metadata_ms = 0;
        snapshot->refs |= (uint8_t)(1U << slot);
        started |= (uint8_t)(1U << slot);
    }
    if (!started) snapshot->valid = false;
    else {
        host->identity_set = true;
        host->schema = schema;
        host->session = session;
        memcpy(host->content_id, content, 32);
        ++host->next_checkpoint; /* Exhaustion at zero rejects further captures. */
    }
    return started;
}

unsigned p4_cs_poll(p4_cs_host *host, bool control, uint64_t now,
    p4_cs_send_fn send, void *context)
{
    if (!host || !host->initialized) return 0;
    expire_at(host, 0, now);
    if (control || !send || now < host->last_now_ms) return 0;
    unsigned attempts = 0;
    while (attempts < P4_CT_SEND_BUDGET) {
        bool prepared = false;
        for (unsigned scan = 0; scan < P4_CT_MAX_GUESTS; ++scan) {
            const unsigned index = (unsigned)host->next_guest % P4_CT_MAX_GUESTS;
            host->next_guest = (uint8_t)((index + 1U) % P4_CT_MAX_GUESTS);
            p4_cs_guest *guest = &host->guests[index];
            if (!guest->nonce || guest->pending || !guest->tx.active || guest->tx.verified) continue;
            uint8_t packet[P4_CT_PACKET_MAX];
            size_t length = 0;
            uint32_t chunk = 0;
            const bool metadata = !guest->metadata_sent || (!guest->metadata_acked &&
                now - guest->last_metadata_ms >= P4_CT_RETRY_MS);
            if (metadata) {
                if (!p4_ct_meta_encode(&guest->tx.meta, packet, sizeof packet, &length)) continue;
            } else {
                /* A queued metadata packet can still be lost. The receiver's
                 * exact-identity ACK establishes its storage/window before any
                 * chunk consumes the shared send budget. */
                if (!guest->metadata_acked ||
                    !p4_ct_tx_prepare(&guest->tx, now, packet, sizeof packet, &length, &chunk)) continue;
            }
            prepared = true;
            ++attempts;
            if (send(context, (uint8_t)(index + 1U), packet, length)) {
                if (metadata) {
                    guest->metadata_sent = true;
                    guest->last_metadata_ms = now;
                } else {
                    (void)p4_ct_tx_sent(&guest->tx, chunk, now);
                }
            }
            break;
        }
        if (!prepared) break;
    }
    return attempts;
}

p4_ct_result p4_cs_ack(p4_cs_host *host, uint8_t slot,
    const uint8_t *bytes, size_t length, uint64_t now)
{
    if (!host || !host->initialized || !slot_valid(slot)) return P4_CT_REJECT;
    expire_at(host, 0, now);
    p4_cs_guest *guest = &host->guests[slot - 1U];
    if (now < host->last_now_ms || !guest->nonce || guest->pending || !guest->metadata_sent)
        return P4_CT_REJECT;
    const p4_ct_result result = p4_ct_tx_ack(&guest->tx, bytes, length, now);
    if (result != P4_CT_REJECT) guest->metadata_acked = true;
    return result;
}

uint8_t p4_cs_expire(p4_cs_host *host, uint32_t first_tic, uint64_t now)
{
    if (!host || !host->initialized) return 0;
    expire_at(host, first_tic, now);
    const uint8_t failed = host->failed_slots;
    host->failed_slots = 0;
    return failed;
}

const p4_ct_meta *p4_cs_meta(const p4_cs_host *host, uint8_t slot)
{
    if (!host || !host->initialized || !slot_valid(slot)) return NULL;
    const p4_cs_guest *guest = &host->guests[slot - 1U];
    return guest->nonce && !guest->pending && guest->tx.active && !guest->tx.failed
        ? &guest->tx.meta : NULL;
}

bool p4_cs_complete(const p4_cs_host *host, uint8_t slot)
{
    return p4_cs_meta(host, slot) && host->guests[slot - 1U].tx.verified;
}
