/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic host test of the actual coordinator + transfer sources.
 */
#include "p4/doom_checkpoint_service.h"
#include "checkpoint_host_sha.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions, groups;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #condition); exit(1); \
} } while (0)
#define RUN(test) do { ++groups; test(); printf("PASS %s\n", #test); } while (0)

static void sha256(const uint8_t *data, size_t length, uint8_t out[32]) {
    CHECK(p4_doom_checkpoint_sha256(data, length, out));
}
static bool verify_sha(void *context, const uint8_t *data, size_t length, const uint8_t expected[32]) {
    unsigned *calls = context;
    uint8_t actual[32];
    if (calls) ++*calls;
    sha256(data, length, actual);
    return memcmp(actual, expected, sizeof actual) == 0;
}

enum { MAX_EVENTS = 12000 };
typedef struct {
    p4_ct_rx rx;
    uint8_t *storage;
    p4_ct_identity expected;
    unsigned verify_calls, metadata, chunks;
    bool dropped_metadata, dropped_chunk[600], dropped_final;
} peer;
typedef struct { uint8_t slot, bytes[P4_CT_PACKET_MAX]; size_t length; } queued_packet;
typedef struct {
    p4_cs_host host;
    uint8_t *buffers[2], content[32];
    peer peers[3];
    size_t capacity, capture_length;
    uint32_t capture_tic, capture_map;
    uint8_t capture_members;
    unsigned capture_calls, hash_calls, total_sends, queued, order_count;
    uint8_t order[MAX_EVENTS], kinds[MAX_EVENTS];
    queued_packet queue[P4_CT_SEND_BUDGET];
    bool capture_fail, hash_fail, reject_send, loss, drop_ack;
    uint64_t now;
} fixture;

static bool capture(void *context, uint8_t *storage, size_t capacity, size_t *length,
                    uint32_t *next_tic, uint32_t *map, uint8_t *members) {
    fixture *f = context;
    ++f->capture_calls;
    CHECK(capacity == f->capacity);
    if (f->capture_fail) return false;
    for (size_t i = 0; i < f->capture_length && i < capacity; ++i)
        storage[i] = (uint8_t)((i * 31u + f->capture_tic) & 255u);
    *length = f->capture_length;
    *next_tic = f->capture_tic;
    *map = f->capture_map;
    *members = f->capture_members;
    return true;
}
static bool hash_capture(void *context, const uint8_t *bytes, size_t length, uint8_t digest[32]) {
    fixture *f = context;
    ++f->hash_calls;
    if (f->hash_fail) return false;
    sha256(bytes, length, digest);
    return true;
}
static void fixture_init(fixture *f, size_t length) {
    memset(f, 0, sizeof(*f));
    f->capacity = P4_CT_MAX_BYTES;
    f->capture_length = length;
    f->capture_tic = 1000;
    f->capture_map = 3;
    f->capture_members = 1;
    f->now = 100;
    for (unsigned i = 0; i < 32; ++i) f->content[i] = (uint8_t)(i * 7u + 1u);
    for (unsigned i = 0; i < 2; ++i) {
        f->buffers[i] = malloc(f->capacity);
        CHECK(f->buffers[i] != NULL);
        memset(f->buffers[i], 0x5a, f->capacity);
    }
    for (unsigned i = 0; i < 3; ++i) {
        f->peers[i].storage = malloc(f->capacity);
        CHECK(f->peers[i].storage != NULL);
    }
    CHECK(p4_cs_init(&f->host, f->buffers[0], f->buffers[1], f->capacity, hash_capture, f));
}
static void fixture_free(fixture *f) {
    for (unsigned i = 0; i < 2; ++i) free(f->buffers[i]);
    for (unsigned i = 0; i < 3; ++i) free(f->peers[i].storage);
}
static uint8_t boundary(fixture *f) {
    uint8_t result = p4_cs_boundary(&f->host, 2, UINT64_C(0x123456789abcdef), f->content,
                                  f->now, capture, f);
    for (uint8_t slot = 1; slot <= 3; ++slot) {
        if (!(result & (uint8_t)(1u << slot))) continue;
        const p4_ct_meta *meta = p4_cs_meta(&f->host, slot);
        CHECK(meta != NULL);
        peer *p = &f->peers[slot - 1u];
        memset(&p->rx, 0, sizeof(p->rx));
        p->expected = meta->identity;
    }
    return result;
}
static void request(fixture *f, uint8_t slot, uint64_t nonce) {
    CHECK(p4_cs_request(&f->host, slot, nonce, f->now));
}
static bool send_packet(void *context, uint8_t slot, const uint8_t *bytes, size_t length) {
    fixture *f = context;
    CHECK(slot >= 1 && slot <= 3);
    CHECK(length >= P4_CT_ID_WIRE && length <= P4_CT_PACKET_MAX);
    CHECK(f->order_count < MAX_EVENTS);
    f->order[f->order_count] = slot;
    f->kinds[f->order_count++] = bytes[5];
    ++f->total_sends;
    if (f->reject_send) return false;
    CHECK(f->queued < P4_CT_SEND_BUDGET);
    queued_packet *q = &f->queue[f->queued++];
    q->slot = slot;
    q->length = length;
    memcpy(q->bytes, bytes, length);
    return true;
}
static uint32_t get32(const uint8_t *p) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)p[i] << (i * 8u);
    return value;
}
static void deliver(fixture *f) {
    for (unsigned i = 0; i < f->queued; ++i) {
        queued_packet *q = &f->queue[i];
        peer *p = &f->peers[q->slot - 1u];
        if (q->bytes[5] == 1) {
            ++p->metadata;
            if (f->loss && !p->dropped_metadata) { p->dropped_metadata = true; continue; }
            p4_ct_meta meta;
            CHECK(p4_ct_meta_decode(&meta, q->bytes, q->length));
            CHECK(meta.identity.attempt == p->expected.attempt);
            if (!p->rx.active) {
                CHECK(p4_ct_rx_start(&p->rx, &p->expected, &meta, p->storage, f->capacity,
                                     verify_sha, &p->verify_calls, f->now));
            } else {
                CHECK(p->rx.meta.identity.checkpoint == meta.identity.checkpoint);
                CHECK(p->rx.meta.identity.attempt == meta.identity.attempt);
            }
        } else {
            CHECK(q->bytes[5] == 2);
            CHECK(p->rx.active); /* Metadata and its ACK must precede chunks. */
            ++p->chunks;
            uint32_t index = get32(q->bytes + 36);
            CHECK(index < 600);
            if (f->loss && index % 7u == 0 && !p->dropped_chunk[index]) {
                p->dropped_chunk[index] = true;
                continue;
            }
            p4_ct_result result = p4_ct_rx_chunk(&p->rx, q->bytes, q->length, f->now);
            CHECK(result == P4_CT_PROGRESS || result == P4_CT_READY || result == P4_CT_DUPLICATE);
        }
        if (f->drop_ack) continue;
        if (f->loss && p->rx.verified && !p->dropped_final) { p->dropped_final = true; continue; }
        if (f->loss && (f->total_sends + q->slot) % 11u == 0) continue;
        uint8_t ack[P4_CT_ACK_WIRE]; size_t length = 0;
        CHECK(p4_ct_rx_ack(&p->rx, ack, sizeof ack, &length));
        p4_ct_result result = p4_cs_ack(&f->host, q->slot, ack, length, f->now);
        CHECK(result == P4_CT_PROGRESS || result == P4_CT_READY || result == P4_CT_DUPLICATE);
    }
    f->queued = 0;
}
static unsigned poll(fixture *f, bool priority, bool drain) {
    CHECK(f->queued == 0);
    unsigned before = f->total_sends;
    unsigned calls = p4_cs_poll(&f->host, priority, f->now, send_packet, f);
    CHECK(calls <= P4_CT_SEND_BUDGET);
    CHECK(f->total_sends - before == calls);
    if (priority) CHECK(calls == 0);
    if (drain) deliver(f);
    return calls;
}
static bool all_complete(const fixture *f, unsigned guests) {
    for (uint8_t slot = 1; slot <= guests; ++slot)
        if (!p4_cs_complete(&f->host, slot)) return false;
    return true;
}
static uint64_t finish(fixture *f, unsigned guests) {
    uint64_t start = f->now;
    while (f->now - start < P4_CT_TOTAL_MS) {
        (void)poll(f, false, true);
        CHECK(p4_cs_expire(&f->host, 0, f->now) == 0);
        if (all_complete(f, guests)) break;
        f->now += 50; /* 20 Hz owner polling, total two callbacks per poll. */
    }
    CHECK(all_complete(f, guests));
    for (uint8_t slot = 1; slot <= guests; ++slot) {
        size_t length = 0;
        const uint8_t *data = p4_ct_rx_data(&f->peers[slot - 1u].rx, &length);
        const p4_ct_meta *meta = p4_cs_meta(&f->host, slot);
        CHECK(data != NULL && meta != NULL && length == meta->length);
        CHECK(memcmp(data, f->host.guests[slot - 1u].tx.storage, length) == 0);
        CHECK(f->peers[slot - 1u].verify_calls == 1);
    }
    return f->now - start;
}

static void test_initialization_and_requests(void) {
    fixture f; fixture_init(&f, 897);
    p4_cs_host rejected;
    CHECK(!p4_cs_init(NULL, f.buffers[0], f.buffers[1], f.capacity, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, NULL, f.buffers[1], f.capacity, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, f.buffers[0], f.buffers[0], f.capacity, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, f.buffers[0], f.buffers[0] + 1, f.capacity, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, f.buffers[0], f.buffers[1], 0, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, f.buffers[0], f.buffers[1], P4_CT_MAX_BYTES + 1u, hash_capture, &f));
    CHECK(!p4_cs_init(&rejected, f.buffers[0], f.buffers[1], f.capacity, NULL, &f));
    CHECK(!p4_cs_request(&f.host, 0, 11, f.now));
    CHECK(!p4_cs_request(&f.host, 4, 11, f.now));
    CHECK(!p4_cs_request(&f.host, 1, 0, f.now));
    CHECK(boundary(&f) == 0 && f.capture_calls == 0 && f.hash_calls == 0);
    request(&f, 1, 11);
    CHECK(!p4_cs_request(&f.host, 2, 11, f.now));
    f.now += 900;
    request(&f, 1, 11);
    CHECK(f.host.guests[0].requested_ms == 100);
    CHECK(p4_cs_expire(&f.host, 0, 1099) == 0);
    CHECK(p4_cs_expire(&f.host, 0, 1100) == 2);
    CHECK(p4_cs_expire(&f.host, 0, 1100) == 0);
    CHECK(p4_cs_meta(&f.host, 1) == NULL);
    fixture_free(&f);
}
static void test_shared_snapshot_lifecycle(void) {
    for (unsigned guests = 1; guests <= 3; guests += 2) {
        fixture f; fixture_init(&f, 896 * 9u + 13u);
        for (uint8_t slot = 1; slot <= guests; ++slot) request(&f, slot, 100u + slot);
        CHECK(boundary(&f) == (guests == 1 ? 2 : 14));
        CHECK(f.capture_calls == 1 && f.hash_calls == 1);
        const p4_ct_meta *first = p4_cs_meta(&f.host, 1);
        CHECK(first != NULL);
        uint64_t checkpoint = first->identity.checkpoint;
        CHECK(checkpoint != 0);
        for (uint8_t slot = 1; slot <= guests; ++slot) {
            const p4_ct_meta *meta = p4_cs_meta(&f.host, slot);
            CHECK(meta != NULL && meta->identity.checkpoint == checkpoint);
            CHECK(meta->identity.attempt == 100u + slot);
            CHECK(meta->next_tic == 1000 && meta->map == 3 && meta->members == 1);
        }
        CHECK(boundary(&f) == 0 && f.capture_calls == 1);
        (void)finish(&f, guests);
        CHECK(f.hash_calls == 1);
        uint8_t index = f.host.guests[0].snapshot;
        CHECK(index < P4_CS_SNAPSHOTS);
        for (uint8_t slot = 1; slot <= guests; ++slot) {
            CHECK(f.host.snapshots[index].refs & (uint8_t)(1u << slot));
            p4_cs_retire(&f.host, slot);
            CHECK(p4_cs_meta(&f.host, slot) == NULL && !p4_cs_complete(&f.host, slot));
            CHECK(!(f.host.snapshots[index].refs & (uint8_t)(1u << slot)));
        }
        CHECK(f.host.snapshots[index].refs == 0);
        fixture_free(&f);
    }
}
static void test_metadata_retry_priority_and_budget(void) {
    fixture f; fixture_init(&f, 896 * 8u);
    for (uint8_t slot = 1; slot <= 3; ++slot) request(&f, slot, 200u + slot);
    CHECK(boundary(&f) == 14);
    CHECK(poll(&f, true, true) == 0);
    f.reject_send = true;
    for (unsigned i = 0; i < 6; ++i) CHECK(poll(&f, false, true) == 2);
    CHECK(f.total_sends == 12);
    for (unsigned i = 0; i < 12; ++i) CHECK(f.order[i] == i % 3u + 1u && f.kinds[i] == 1);
    for (unsigned i = 0; i < 3; ++i) CHECK(!f.host.guests[i].metadata_sent && f.host.guests[i].tx.sent == 0);
    f.reject_send = false;
    f.loss = true;
    (void)finish(&f, 3);
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(f.peers[i].metadata >= 2 && f.peers[i].dropped_metadata);
        CHECK(f.peers[i].dropped_final);
    }
    fixture_free(&f);
}
static void test_maximum_snapshot_loss_at_20hz(void) {
    for (unsigned guests = 1; guests <= 3; guests += 2) {
        fixture f; fixture_init(&f, P4_CT_MAX_BYTES);
        for (uint8_t slot = 1; slot <= guests; ++slot) request(&f, slot, 300u + slot);
        CHECK(boundary(&f) == (guests == 1 ? 2 : 14));
        f.loss = true;
        uint64_t elapsed = finish(&f, guests);
        CHECK(elapsed < 90000 && f.hash_calls == 1 && f.capture_calls == 1);
        printf("MAX_SNAPSHOT guests=%u bytes=%u elapsed_ms=%llu sends=%u\n", guests,
               P4_CT_MAX_BYTES, (unsigned long long)elapsed, f.total_sends);
        fixture_free(&f);
    }
}
static void test_replacement_leave_stale_ack(void) {
    fixture f; fixture_init(&f, 1793);
    request(&f, 1, 401);
    CHECK(boundary(&f) == 2);
    (void)poll(&f, false, true);
    uint8_t old_ack[P4_CT_ACK_WIRE]; size_t length = 0;
    CHECK(p4_ct_rx_ack(&f.peers[0].rx, old_ack, sizeof old_ack, &length));
    uint64_t old_checkpoint = p4_cs_meta(&f.host, 1)->identity.checkpoint;
    uint8_t old_index = f.host.guests[0].snapshot;
    f.now += 1;
    request(&f, 1, 402);
    CHECK(p4_cs_meta(&f.host, 1) == NULL);
    CHECK(f.host.snapshots[old_index].refs == 0);
    CHECK(p4_cs_ack(&f.host, 1, old_ack, length, f.now) == P4_CT_REJECT);
    CHECK(boundary(&f) == 2);
    CHECK(p4_cs_meta(&f.host, 1)->identity.checkpoint != old_checkpoint);
    CHECK(p4_cs_meta(&f.host, 1)->identity.attempt == 402);
    CHECK(p4_cs_ack(&f.host, 1, old_ack, length, f.now) == P4_CT_REJECT);
    CHECK(!f.host.guests[0].metadata_acked && f.host.guests[0].tx.base == 0);
    (void)finish(&f, 1);
    p4_cs_retire(&f.host, 1);
    CHECK(p4_cs_meta(&f.host, 1) == NULL);
    CHECK(p4_cs_ack(&f.host, 1, old_ack, length, f.now) == P4_CT_REJECT);
    request(&f, 1, 403);
    CHECK(boundary(&f) == 2 && p4_cs_meta(&f.host, 1)->identity.attempt == 403);
    CHECK(p4_cs_ack(&f.host, 1, old_ack, length, f.now) == P4_CT_REJECT);
    fixture_free(&f);
}
static void test_busy_buffers_pending_deadline_and_release(void) {
    fixture f; fixture_init(&f, 1793);
    request(&f, 1, 501); CHECK(boundary(&f) == 2);
    f.now += 1; request(&f, 2, 502); CHECK(boundary(&f) == 4);
    CHECK(f.capture_calls == 2 && f.hash_calls == 2);
    CHECK(f.host.guests[0].snapshot != f.host.guests[1].snapshot);
    f.now += 1; request(&f, 3, 503);
    CHECK(boundary(&f) == 0 && f.capture_calls == 2);
    CHECK(p4_cs_expire(&f.host, 0, f.now + 999) == 0);
    f.now += 1000;
    CHECK(p4_cs_expire(&f.host, 0, f.now) == 8);
    CHECK(p4_cs_meta(&f.host, 1) != NULL && p4_cs_meta(&f.host, 2) != NULL);
    CHECK(!f.host.guests[2].pending && p4_cs_meta(&f.host, 3) == NULL);
    p4_cs_retire(&f.host, 1);
    request(&f, 3, 504);
    CHECK(boundary(&f) == 8 && f.capture_calls == 3 && f.hash_calls == 3);
    CHECK(p4_cs_meta(&f.host, 2) != NULL && p4_cs_meta(&f.host, 3) != NULL);
    fixture_free(&f);
}
static void test_completed_lease_and_journal_eviction(void) {
    fixture f; fixture_init(&f, 1);
    request(&f, 1, 601); CHECK(boundary(&f) == 2);
    (void)finish(&f, 1);
    CHECK(p4_cs_expire(&f.host, 1000, 110099) == 0);
    CHECK(p4_cs_complete(&f.host, 1));
    CHECK(p4_cs_expire(&f.host, 1000, 110100) == 2);
    CHECK(!p4_cs_complete(&f.host, 1) && p4_cs_meta(&f.host, 1) == NULL);
    fixture_free(&f);
    fixture_init(&f, 1793);
    request(&f, 1, 602); request(&f, 2, 603); CHECK(boundary(&f) == 6);
    CHECK(p4_cs_expire(&f.host, 1000, f.now) == 0);
    CHECK(p4_cs_expire(&f.host, 1001, f.now) == 6);
    CHECK(p4_cs_meta(&f.host, 1) == NULL && p4_cs_meta(&f.host, 2) == NULL);
    CHECK(p4_cs_expire(&f.host, 1001, f.now) == 0);
    fixture_free(&f);
}
static void test_transfer_and_silence_deadlines(void) {
    fixture f; fixture_init(&f, 1793);
    request(&f, 1, 701); CHECK(boundary(&f) == 2);
    CHECK(p4_cs_expire(&f.host, 0, 30099) == 0);
    CHECK(p4_cs_expire(&f.host, 0, 30100) == 2);
    fixture_free(&f);
    fixture_init(&f, 1793);
    request(&f, 1, 702); CHECK(boundary(&f) == 2);
    (void)poll(&f, false, true); /* Metadata ACK keeps zero progress legitimate. */
    uint8_t ack[P4_CT_ACK_WIRE]; size_t length = 0;
    CHECK(p4_ct_rx_ack(&f.peers[0].rx, ack, sizeof ack, &length));
    for (uint64_t now = 20100; now <= 80100; now += 20000) {
        CHECK(p4_cs_ack(&f.host, 1, ack, length, now) == P4_CT_DUPLICATE);
        CHECK(p4_cs_expire(&f.host, 0, now) == 0);
    }
    CHECK(p4_cs_expire(&f.host, 0, 90099) == 0);
    CHECK(p4_cs_expire(&f.host, 0, 90100) == 2);
    CHECK(p4_cs_meta(&f.host, 1) == NULL);
    fixture_free(&f);
}
static void test_invalid_capture_and_identity(void) {
    for (unsigned invalid = 0; invalid < 7; ++invalid) {
        fixture f; fixture_init(&f, 1793);
        request(&f, 1, 800u + invalid);
        if (invalid == 0) f.capture_fail = true;
        if (invalid == 1) f.hash_fail = true;
        if (invalid == 2) f.capture_length = 0;
        if (invalid == 3) f.capture_length = P4_CT_MAX_BYTES + 1u;
        if (invalid == 4) f.capture_map = 0;
        if (invalid == 5) f.capture_members = 2;
        if (invalid == 6) f.capture_members = 0x11;
        CHECK(boundary(&f) == 0);
        CHECK(p4_cs_meta(&f.host, 1) == NULL && f.host.guests[0].pending);
        CHECK(!f.host.snapshots[0].valid && !f.host.snapshots[1].valid);
        CHECK(poll(&f, false, true) == 0);
        CHECK(p4_cs_expire(&f.host, 0, 1099) == 0);
        CHECK(p4_cs_expire(&f.host, 0, 1100) == 2);
        fixture_free(&f);
    }
    fixture f; fixture_init(&f, 1793);
    request(&f, 1, 900);
    CHECK(p4_cs_boundary(&f.host, 0, 7, f.content, f.now, capture, &f) == 0);
    CHECK(p4_cs_boundary(&f.host, 2, 0, f.content, f.now, capture, &f) == 0);
    CHECK(p4_cs_boundary(&f.host, 2, 7, NULL, f.now, capture, &f) == 0);
    CHECK(f.capture_calls == 0 && f.hash_calls == 0);
    CHECK(boundary(&f) == 2);
    request(&f, 2, 901);
    CHECK(p4_cs_boundary(&f.host, 3, UINT64_C(0x123456789abcdef), f.content, f.now, capture, &f) == 0);
    CHECK(p4_cs_boundary(&f.host, 2, 7, f.content, f.now, capture, &f) == 0);
    f.content[0] ^= 1;
    CHECK(boundary(&f) == 0);
    CHECK(f.capture_calls == 1 && f.hash_calls == 1 && p4_cs_meta(&f.host, 1) != NULL);
    fixture_free(&f);
}
static void test_transient_capture_retry_and_active_idempotency(void) {
    fixture f; fixture_init(&f, 897);
    request(&f, 1, 1001);
    f.capture_fail = true;
    CHECK(boundary(&f) == 0 && f.capture_calls == 1 && f.hash_calls == 0);
    f.now = 600;
    CHECK(boundary(&f) == 0 && f.capture_calls == 2 && f.hash_calls == 0);
    f.capture_fail = false;
    f.now = 1099;
    CHECK(boundary(&f) == 2 && f.capture_calls == 3 && f.hash_calls == 1);
    const p4_ct_meta *meta = p4_cs_meta(&f.host, 1);
    CHECK(meta != NULL);
    uint64_t checkpoint = meta->identity.checkpoint;
    uint64_t started = f.host.guests[0].tx.started_ms;
    uint64_t requested = f.host.guests[0].requested_ms;
    f.now = 2000;
    request(&f, 1, 1001);
    CHECK(p4_cs_meta(&f.host, 1)->identity.checkpoint == checkpoint);
    CHECK(f.host.guests[0].requested_ms == requested && f.host.guests[0].tx.started_ms == started);
    CHECK(boundary(&f) == 0 && f.capture_calls == 3 && f.hash_calls == 1);
    (void)finish(&f, 1);
    CHECK(p4_cs_expire(&f.host, 1001, f.now) == 2); /* Completed snapshots retain journal leases. */
    CHECK(p4_cs_meta(&f.host, 1) == NULL);
    fixture_free(&f);
}
static void test_clock_regression_and_latched_failures(void) {
    fixture f; fixture_init(&f, 897);
    request(&f, 1, 1101); request(&f, 2, 1102);
    CHECK(boundary(&f) == 6);
    f.now = 99;
    CHECK(poll(&f, false, true) == 0);
    CHECK(p4_cs_meta(&f.host, 1) == NULL && p4_cs_meta(&f.host, 2) == NULL);
    CHECK(p4_cs_expire(&f.host, 0, 100) == 6);
    CHECK(p4_cs_expire(&f.host, 0, 100) == 0);
    fixture_free(&f);
    fixture_init(&f, 897);
    request(&f, 1, 1103); CHECK(boundary(&f) == 2);
    f.now = 30100;
    CHECK(poll(&f, true, true) == 0); /* Priority suppresses sends but must not hide expiry. */
    CHECK(p4_cs_expire(&f.host, 0, f.now) == 2);
    CHECK(p4_cs_expire(&f.host, 0, f.now) == 0);
    fixture_free(&f);
}
static void test_expired_duplicate_cannot_restart_deadline(void) {
    fixture f; fixture_init(&f, 897);
    request(&f, 1, 1201);
    CHECK(!p4_cs_request(&f.host, 1, 1201, 1100));
    CHECK(!f.host.guests[0].pending && p4_cs_meta(&f.host, 1) == NULL);
    CHECK(!p4_cs_request(&f.host, 1, 1299, 1100)); /* New nonce cannot consume failure. */
    CHECK(p4_cs_expire(&f.host, 0, 1100) == 2);
    CHECK(p4_cs_expire(&f.host, 0, 1100) == 0);
    CHECK(p4_cs_request(&f.host, 1, 1299, 1100));
    CHECK(f.host.guests[0].pending && f.host.guests[0].requested_ms == 1100);
    fixture_free(&f);
    fixture_init(&f, 897);
    request(&f, 1, 1202); CHECK(boundary(&f) == 2);
    CHECK(!p4_cs_request(&f.host, 1, 1202, 30100));
    CHECK(!f.host.guests[0].pending && p4_cs_meta(&f.host, 1) == NULL);
    CHECK(p4_cs_expire(&f.host, 0, 30100) == 2);
    fixture_free(&f);
    fixture_init(&f, 897);
    request(&f, 1, 1203);
    CHECK(!p4_cs_request(&f.host, 1, 1204, 1100)); /* Expiry occurs in this replacement call. */
    CHECK(!f.host.guests[0].pending);
    CHECK(p4_cs_expire(&f.host, 0, 1100) == 2);
    CHECK(p4_cs_request(&f.host, 1, 1204, 1100));
    fixture_free(&f);
}
int main(void) {
    RUN(test_initialization_and_requests);
    RUN(test_shared_snapshot_lifecycle);
    RUN(test_metadata_retry_priority_and_budget);
    RUN(test_maximum_snapshot_loss_at_20hz);
    RUN(test_replacement_leave_stale_ack);
    RUN(test_busy_buffers_pending_deadline_and_release);
    RUN(test_completed_lease_and_journal_eviction);
    RUN(test_transfer_and_silence_deadlines);
    RUN(test_invalid_capture_and_identity);
    RUN(test_transient_capture_retry_and_active_idempotency);
    RUN(test_clock_regression_and_latched_failures);
    RUN(test_expired_duplicate_cannot_restart_deadline);
    printf("PASS all %u groups, %u assertions (actual coordinator/transfer, real SHA-256)\n", groups, assertions);
    return 0;
}
