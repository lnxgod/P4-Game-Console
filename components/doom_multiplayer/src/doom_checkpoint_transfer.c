/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "p4/doom_checkpoint_transfer.h"
#include <string.h>

enum { CT_META = 1, CT_CHUNK = 2, CT_ACK = 3 };
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p) {
    uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8U * i); return v;
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8U * i); return v;
}
static void put16(uint8_t *p, uint16_t v) { for (unsigned i = 0; i < 2; ++i) { p[i] = (uint8_t)v; v >>= 8; } }
static void put32(uint8_t *p, uint32_t v) { for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)v; v >>= 8; } }
static void put64(uint8_t *p, uint64_t v) { for (unsigned i = 0; i < 8; ++i) { p[i] = (uint8_t)v; v >>= 8; } }
static bool identity_valid(const p4_ct_identity *id) { return id && id->schema && id->session && id->attempt && id->checkpoint; }
static bool identity_equal(const p4_ct_identity *a, const p4_ct_identity *b) {
    return a->schema == b->schema && a->session == b->session && a->attempt == b->attempt &&
        a->checkpoint == b->checkpoint && !memcmp(a->content_id,b->content_id,32);
}
static bool meta_valid(const p4_ct_meta *m) {
    return m && identity_valid(&m->identity) && m->length && m->length <= P4_CT_MAX_BYTES &&
        m->map && m->members && !(m->members & 0xf0U);
}
uint32_t p4_ct_chunk_count(uint32_t length) {
    if (!length || length > P4_CT_MAX_BYTES) return 0;
    return 1U + (length - 1U) / P4_CT_CHUNK_BYTES;
}
static uint32_t chunk_length(uint32_t length, uint32_t index) {
    const uint32_t count = p4_ct_chunk_count(length);
    if (index >= count) return 0;
    const uint32_t left = length - index * P4_CT_CHUNK_BYTES;
    return left < P4_CT_CHUNK_BYTES ? left : P4_CT_CHUNK_BYTES;
}
static unsigned window_width(uint32_t count, uint32_t base) {
    if (base >= count) return 0;
    const uint32_t left = count - base;
    return left < P4_CT_WINDOW ? (unsigned)left : P4_CT_WINDOW;
}
static uint8_t low_mask(unsigned width) { return (uint8_t)((1U << width) - 1U); }
static unsigned bit_count(uint8_t bits) {
    unsigned count = 0; while (bits) { count += bits & 1U; bits >>= 1; } return count;
}
static void encode_id(uint8_t *out, const p4_ct_identity *id, unsigned kind) {
    memcpy(out,"GCK1",4); out[4] = 1; out[5] = (uint8_t)kind; out[6] = out[7] = 0;
    put32(out + 8,id->schema); put64(out + 12,id->session);
    put64(out + 20,id->attempt); put64(out + 28,id->checkpoint);
}
static bool decode_id(const uint8_t *in, size_t n, unsigned kind, p4_ct_identity *id) {
    if (!in || n < P4_CT_ID_WIRE || memcmp(in,"GCK1",4) || in[4] != 1 || in[5] != kind || in[6] || in[7]) return false;
    id->schema = get32(in + 8); id->session = get64(in + 12);
    id->attempt = get64(in + 20); id->checkpoint = get64(in + 28);
    return identity_valid(id);
}
static bool packet_identity(const uint8_t *in, size_t n, unsigned kind, const p4_ct_identity *expected) {
    p4_ct_identity id = {0};
    return decode_id(in,n,kind,&id) && id.schema == expected->schema && id.session == expected->session &&
        id.attempt == expected->attempt && id.checkpoint == expected->checkpoint;
}
bool p4_ct_meta_encode(const p4_ct_meta *meta, uint8_t *out, size_t capacity, size_t *length) {
    if (length) *length = 0;
    if (!meta_valid(meta) || !out || !length || capacity < P4_CT_META_WIRE) return false;
    encode_id(out,&meta->identity,CT_META); put32(out + 36,meta->length);
    put32(out + 40,meta->next_tic); put32(out + 44,meta->map); out[48] = meta->members;
    out[49] = out[50] = out[51] = 0;
    memcpy(out + 52,meta->identity.content_id,32); memcpy(out + 84,meta->sha256,32);
    *length = P4_CT_META_WIRE; return true;
}
bool p4_ct_meta_decode(p4_ct_meta *meta, const uint8_t *in, size_t length) {
    p4_ct_meta value = {0};
    if (!meta || length != P4_CT_META_WIRE || !decode_id(in,length,CT_META,&value.identity) || in[49] || in[50] || in[51]) return false;
    value.length = get32(in + 36); value.next_tic = get32(in + 40); value.map = get32(in + 44); value.members = in[48];
    memcpy(value.identity.content_id,in + 52,32); memcpy(value.sha256,in + 84,32);
    if (!meta_valid(&value)) return false;
    *meta = value; return true;
}
static bool timer(bool *failed, bool active, bool verified, uint64_t started, uint64_t last_peer, uint64_t *last_now, uint64_t now) {
    if (!active || *failed) return false;
    if (verified) return true;
    if (now < *last_now || now < started || now < last_peer || now - started >= P4_CT_TOTAL_MS || now - last_peer >= P4_CT_SILENCE_MS) {
        *failed = true; return false;
    }
    *last_now = now; return true;
}
bool p4_ct_rx_tick(p4_ct_rx *rx, uint64_t now) {
    return rx && timer(&rx->failed,rx->active,rx->verified,rx->started_ms,rx->last_peer_ms,&rx->last_now_ms,now);
}
bool p4_ct_tx_tick(p4_ct_tx *tx, uint64_t now) {
    return tx && timer(&tx->failed,tx->active,tx->verified,tx->started_ms,tx->last_peer_ms,&tx->last_now_ms,now);
}
bool p4_ct_rx_start(p4_ct_rx *rx, const p4_ct_identity *expected, const p4_ct_meta *meta,
                    uint8_t *storage, size_t capacity, p4_ct_verify_fn verify, void *context, uint64_t now) {
    if (!rx) return false;
    memset(rx,0,sizeof(*rx));
    if (!identity_valid(expected) || !meta_valid(meta) || !identity_equal(expected,&meta->identity) ||
        !storage || capacity < meta->length || !verify) { rx->failed = true; return false; }
    rx->meta = *meta; rx->storage = storage; rx->capacity = capacity;
    rx->count = p4_ct_chunk_count(meta->length); rx->active = true;
    rx->verify = verify; rx->verify_context = context;
    rx->started_ms = rx->last_peer_ms = rx->last_now_ms = now; return true;
}
p4_ct_result p4_ct_rx_chunk(p4_ct_rx *rx, const uint8_t *in, size_t length, uint64_t now) {
    if (!p4_ct_rx_tick(rx,now) || length < P4_CT_CHUNK_HEADER || length > P4_CT_PACKET_MAX ||
        !packet_identity(in,length,CT_CHUNK,&rx->meta.identity) || in[42] || in[43]) return P4_CT_REJECT;
    const uint32_t index = get32(in + 36), bytes = get16(in + 40);
    const uint32_t expected = chunk_length(rx->meta.length,index);
    if (!expected || bytes != expected || length != P4_CT_CHUNK_HEADER + (size_t)bytes) return P4_CT_REJECT;
    /* index is already bounded by count <= 586, before offset multiplication. */
    const size_t offset = (size_t)index * P4_CT_CHUNK_BYTES;
    if (offset > rx->capacity || bytes > rx->capacity - offset) return P4_CT_REJECT;
    if (index < rx->base) {
        if (memcmp(rx->storage + offset,in + P4_CT_CHUNK_HEADER,bytes)) { rx->failed = true; return P4_CT_REJECT; }
        rx->last_peer_ms = now; return P4_CT_DUPLICATE;
    }
    const uint32_t delta = index - rx->base;
    if (delta >= P4_CT_WINDOW || rx->verified) return P4_CT_REJECT;
    const uint8_t bit = (uint8_t)(1U << delta);
    if (rx->seen & bit) {
        if (memcmp(rx->storage + offset,in + P4_CT_CHUNK_HEADER,bytes)) { rx->failed = true; return P4_CT_REJECT; }
        rx->last_peer_ms = now; return P4_CT_DUPLICATE;
    }
    memcpy(rx->storage + offset,in + P4_CT_CHUNK_HEADER,bytes); rx->seen |= bit;
    while (rx->seen & 1U) { ++rx->base; rx->seen >>= 1; }
    rx->last_peer_ms = now;
    if (rx->base == rx->count) {
        if (!rx->verify(rx->verify_context,rx->storage,rx->meta.length,rx->meta.sha256)) {
            rx->failed = true; return P4_CT_REJECT;
        }
        rx->verified = true; return P4_CT_READY;
    }
    return P4_CT_PROGRESS;
}
bool p4_ct_rx_ack(const p4_ct_rx *rx, uint8_t *out, size_t capacity, size_t *length) {
    if (length) *length = 0;
    if (!rx || !rx->active || rx->failed || !out || !length || capacity < P4_CT_ACK_WIRE) return false;
    encode_id(out,&rx->meta.identity,CT_ACK); put32(out + 36,rx->base);
    out[40] = rx->seen; out[41] = (uint8_t)(window_width(rx->count,rx->base) - bit_count(rx->seen));
    out[42] = rx->verified ? 1 : 0; out[43] = 0; *length = P4_CT_ACK_WIRE; return true;
}
const uint8_t *p4_ct_rx_data(const p4_ct_rx *rx, size_t *length) {
    if (length) *length = 0;
    if (!rx || !rx->active || !rx->verified || rx->failed || !length) return NULL;
    *length = rx->meta.length; return rx->storage;
}
bool p4_ct_tx_start(p4_ct_tx *tx, const p4_ct_meta *meta, const uint8_t *storage, size_t length,
                    p4_ct_verify_fn verify, void *context, uint64_t now) {
    if (!tx) return false;
    memset(tx,0,sizeof(*tx));
    if (!meta_valid(meta) || !storage || length != meta->length || !verify ||
        !verify(context,storage,length,meta->sha256)) { tx->failed = true; return false; }
    tx->meta = *meta; tx->storage = storage; tx->count = p4_ct_chunk_count(meta->length); tx->active = true;
    tx->started_ms = tx->last_peer_ms = tx->last_now_ms = now; return true;
}
p4_ct_result p4_ct_tx_ack(p4_ct_tx *tx, const uint8_t *in, size_t length, uint64_t now) {
    if (!p4_ct_tx_tick(tx,now) || length != P4_CT_ACK_WIRE ||
        !packet_identity(in,length,CT_ACK,&tx->meta.identity) || in[43]) return P4_CT_REJECT;
    const uint32_t base = get32(in + 36);
    const uint8_t bits = in[40], credit = in[41], complete = in[42];
    if (base > tx->count || complete > 1 || (complete != (base == tx->count)) ||
        (bits & (uint8_t)~low_mask(window_width(tx->count,base))) || (bits & 1U) ||
        credit != window_width(tx->count,base) - bit_count(bits)) return P4_CT_REJECT;
    if (base < tx->base) { tx->last_peer_ms = now; return P4_CT_DUPLICATE; }
    const uint32_t delta = base - tx->base;
    if (delta > P4_CT_WINDOW || (tx->sent & low_mask((unsigned)delta)) != low_mask((unsigned)delta) ||
        (bits & (uint8_t)~(tx->sent >> delta))) return P4_CT_REJECT;
    const uint8_t prior_ack = (uint8_t)(tx->acknowledged >> delta);
    for (unsigned i = 0; i < P4_CT_WINDOW; ++i)
        tx->sent_ms[i] = i + delta < P4_CT_WINDOW ? tx->sent_ms[i + delta] : 0;
    tx->sent = (uint8_t)(tx->sent >> delta); tx->acknowledged = (uint8_t)(prior_ack | bits);
    tx->base = base; tx->last_peer_ms = now;
    if (complete) { tx->verified = true; return P4_CT_READY; }
    return delta || (bits & (uint8_t)~prior_ack) ? P4_CT_PROGRESS : P4_CT_DUPLICATE;
}
bool p4_ct_tx_prepare(p4_ct_tx *tx, uint64_t now, uint8_t *out, size_t capacity, size_t *length, uint32_t *index) {
    if (length) *length = 0;
    if (!p4_ct_tx_tick(tx,now) || tx->verified || !out || !length || !index) return false;
    const unsigned width = window_width(tx->count,tx->base);
    unsigned chosen = P4_CT_WINDOW;
    /* Send each unsent window slot once before retrying missing ACKs. */
    for (unsigned i = 0; i < width; ++i)
        if (!(tx->sent & (1U << i))) { chosen = i; break; }
    if (chosen == P4_CT_WINDOW)
        for (unsigned i = 0; i < width; ++i)
            if (!(tx->acknowledged & (1U << i)) && now >= tx->sent_ms[i] && now - tx->sent_ms[i] >= P4_CT_RETRY_MS) { chosen = i; break; }
    if (chosen == P4_CT_WINDOW) return false;
    const uint32_t position = tx->base + chosen;
    const uint32_t bytes = chunk_length(tx->meta.length,position);
    if (!bytes || capacity < P4_CT_CHUNK_HEADER + (size_t)bytes) return false;
    encode_id(out,&tx->meta.identity,CT_CHUNK); put32(out + 36,position);
    put16(out + 40,(uint16_t)bytes); out[42] = out[43] = 0;
    memcpy(out + P4_CT_CHUNK_HEADER,tx->storage + (size_t)position * P4_CT_CHUNK_BYTES,bytes);
    *length = P4_CT_CHUNK_HEADER + (size_t)bytes; *index = position; return true;
}
bool p4_ct_tx_sent(p4_ct_tx *tx, uint32_t index, uint64_t now) {
    if (!p4_ct_tx_tick(tx,now) || tx->verified || index < tx->base || index >= tx->count || index - tx->base >= P4_CT_WINDOW) return false;
    const unsigned delta = (unsigned)(index - tx->base);
    if (tx->acknowledged & (1U << delta)) return false;
    tx->sent |= (uint8_t)(1U << delta); tx->sent_ms[delta] = now; return true;
}
unsigned p4_ct_host_poll(p4_ct_host *host, bool control, uint64_t now, p4_ct_send_fn send, void *context) {
    if (!host || !send) return 0;
    for (unsigned i = 0; i < P4_CT_MAX_GUESTS; ++i) if (host->guests[i]) (void)p4_ct_tx_tick(host->guests[i],now);
    if (control) return 0;
    unsigned attempts = 0;
    while (attempts < P4_CT_SEND_BUDGET) {
        bool prepared = false;
        for (unsigned scan = 0; scan < P4_CT_MAX_GUESTS; ++scan) {
            const unsigned slot = (unsigned)host->next_guest % P4_CT_MAX_GUESTS;
            host->next_guest = (uint8_t)((slot + 1U) % P4_CT_MAX_GUESTS);
            p4_ct_tx *tx = host->guests[slot];
            uint8_t packet[P4_CT_PACKET_MAX]; size_t length = 0; uint32_t index = 0;
            if (!tx || !p4_ct_tx_prepare(tx,now,packet,sizeof(packet),&length,&index)) continue;
            ++attempts; prepared = true;
            if (send(context,slot,packet,length)) (void)p4_ct_tx_sent(tx,index,now);
            break;
        }
        if (!prepared) break;
    }
    return attempts;
}
