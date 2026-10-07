// SPDX-License-Identifier: MIT
#include "p4/debug_snapshot.h"

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

void p4_debug_snapshot_init(p4_debug_snapshot_t *snapshot,
                            p4_debug_snapshot_send_fn send,
                            p4_debug_snapshot_capture_fn capture,
                            p4_debug_snapshot_release_fn release,
                            p4_debug_snapshot_available_fn available,
                            void *context)
{
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->send = send;
    snapshot->capture = capture;
    snapshot->release = release;
    snapshot->available = available;
    snapshot->context = context;
}

static void release_snapshot(p4_debug_snapshot_t *snapshot)
{
    if (snapshot->data && snapshot->release) {
        snapshot->release(snapshot->context, snapshot->data);
    }
    snapshot->data = NULL;
    snapshot->size = 0;
    snapshot->width = 0;
    snapshot->height = 0;
    snapshot->flags = 0;
    snapshot->capture_id = 0;
    snapshot->started_ms = 0;
    snapshot->idle_until_ms = 0;
    snapshot->absolute_until_ms = 0;
}

void p4_debug_snapshot_cancel(p4_debug_snapshot_t *snapshot)
{
    if (!snapshot) return;
    if (snapshot->capture_id) snapshot->last_cancelled_id = snapshot->capture_id;
    release_snapshot(snapshot);
    snapshot->used = 0;
}

void p4_debug_snapshot_poll(p4_debug_snapshot_t *snapshot, uint64_t now_ms)
{
    if (!snapshot || !snapshot->data) return;
    if (now_ms < snapshot->started_ms || now_ms >= snapshot->idle_until_ms ||
        now_ms >= snapshot->absolute_until_ms || !snapshot->available ||
        !snapshot->available(snapshot->context)) {
        p4_debug_snapshot_cancel(snapshot);
    }
}

static bool valid_request(const uint8_t *request)
{
    const uint8_t command = request[5];
    if (request[4] != 1U || request[7] ||
        (command == P4_DEBUG_SNAPSHOT_BEGIN
            ? (request[6] & (uint8_t)~P4_DEBUG_SNAPSHOT_QUARTER) != 0U
            : request[6] != 0U) ||
        !get32(request + 8) || !get32(request + 12) ||
        get32(request + 28) != crc32(request, 28) ||
        command < P4_DEBUG_SNAPSHOT_BEGIN || command > P4_DEBUG_SNAPSHOT_END) {
        return false;
    }
    for (size_t i = 22; i < 28; ++i) if (request[i]) return false;
    if (command == P4_DEBUG_SNAPSHOT_READ) {
        return get16(request + 20) >= 1U &&
               get16(request + 20) <= P4_DEBUG_SNAPSHOT_MAX_PAYLOAD;
    }
    return !get32(request + 16) && !get16(request + 20);
}

/* This runs only on the owned immutable copy, after the display mutex has
 * been released. Forward compaction cannot overwrite a future source pixel:
 * every selected source offset is strictly ahead of its destination offset. */
static void quarter_snapshot(uint8_t *data, uint16_t width, uint16_t height)
{
    const size_t output_width = (size_t)width / 4U;
    const size_t output_height = (size_t)height / 4U;
    for (size_t y = 0; y < output_height; ++y) {
        for (size_t x = 0; x < output_width; ++x) {
            const size_t source = ((y * 4U + 2U) * width + x * 4U + 2U) * 2U;
            const size_t destination = (y * output_width + x) * 2U;
            data[destination] = data[source];
            data[destination + 1U] = data[source + 1U];
        }
    }
}

static p4_debug_snapshot_result_t apply_request(p4_debug_snapshot_t *snapshot,
                                                uint64_t now)
{
    const uint8_t *request = snapshot->request;
    const uint8_t command = request[5];
    const uint32_t id = get32(request + 8);
    if (!valid_request(request)) return P4_DEBUG_SNAPSHOT_INVALID;

    if (!snapshot->available(snapshot->context)) {
        p4_debug_snapshot_cancel(snapshot);
        return P4_DEBUG_SNAPSHOT_UNAVAILABLE;
    }
    if (command == P4_DEBUG_SNAPSHOT_BEGIN) {
        if (snapshot->data) {
            if (id != snapshot->capture_id) return P4_DEBUG_SNAPSHOT_BUSY;
            return request[6] == snapshot->flags ? P4_DEBUG_SNAPSHOT_OK :
                                                  P4_DEBUG_SNAPSHOT_INVALID;
        }
        if (id == snapshot->last_ended_id || id == snapshot->last_cancelled_id) {
            return P4_DEBUG_SNAPSHOT_EXPIRED;
        }
        uint8_t *data = NULL;
        size_t size = 0;
        uint16_t width = 0, height = 0;
        const bool captured = snapshot->capture(snapshot->context, &data, &size,
                                                &width, &height);
        const uint64_t expected = (uint64_t)width * (uint64_t)height * 2U;
        if (!captured || !data || !width || !height ||
            expected > P4_DEBUG_SNAPSHOT_MAX_IMAGE_BYTES ||
            size != expected || (request[6] == P4_DEBUG_SNAPSHOT_QUARTER &&
                                 (width % 4U != 0U || height % 4U != 0U))) {
            if (data) snapshot->release(snapshot->context, data);
            return P4_DEBUG_SNAPSHOT_FAILED;
        }
        if (!snapshot->available(snapshot->context)) {
            snapshot->release(snapshot->context, data);
            snapshot->last_cancelled_id = id;
            return P4_DEBUG_SNAPSHOT_UNAVAILABLE;
        }
        if (request[6] == P4_DEBUG_SNAPSHOT_QUARTER) {
            quarter_snapshot(data, width, height);
            width = (uint16_t)(width / 4U);
            height = (uint16_t)(height / 4U);
            size /= 16U;
        }
        snapshot->data = data;
        snapshot->size = size;
        snapshot->width = width;
        snapshot->height = height;
        snapshot->flags = request[6];
        snapshot->capture_id = id;
        snapshot->started_ms = now;
        snapshot->idle_until_ms = deadline(now, P4_DEBUG_SNAPSHOT_IDLE_MS);
        snapshot->absolute_until_ms = deadline(now, P4_DEBUG_SNAPSHOT_LIFETIME_MS);
        return P4_DEBUG_SNAPSHOT_OK;
    }
    if (command == P4_DEBUG_SNAPSHOT_END && id == snapshot->last_ended_id &&
        id != snapshot->capture_id) return P4_DEBUG_SNAPSHOT_OK;
    if (!snapshot->data || id != snapshot->capture_id) {
        return P4_DEBUG_SNAPSHOT_EXPIRED;
    }
    if (command == P4_DEBUG_SNAPSHOT_END) {
        snapshot->last_ended_id = id;
        release_snapshot(snapshot);
        return P4_DEBUG_SNAPSHOT_OK;
    }
    if (get32(request + 16) >= snapshot->size) return P4_DEBUG_SNAPSHOT_INVALID;
    snapshot->idle_until_ms = deadline(now, P4_DEBUG_SNAPSHOT_IDLE_MS);
    return P4_DEBUG_SNAPSHOT_OK;
}

static void handle(p4_debug_snapshot_t *snapshot, uint64_t now)
{
    const p4_debug_snapshot_result_t result = apply_request(snapshot, now);
    const uint8_t *request = snapshot->request;
    uint8_t reply[P4_DEBUG_SNAPSHOT_MAX_RESPONSE] = {0};
    memcpy(reply, "P4T1", 4);
    reply[4] = 1;
    reply[5] = request[5];
    reply[6] = (uint8_t)result;
    put32(reply + 8, get32(request + 8));
    put32(reply + 12, get32(request + 12));
    const uint32_t offset = get32(request + 16);
    put32(reply + 16, offset);
    size_t payload = 0;
    if (snapshot->data && get32(request + 8) == snapshot->capture_id) {
        put32(reply + 20, (uint32_t)snapshot->size);
        put16(reply + 26, snapshot->width);
        put16(reply + 28, snapshot->height);
        if (result == P4_DEBUG_SNAPSHOT_OK && request[5] == P4_DEBUG_SNAPSHOT_READ) {
            payload = get16(request + 20);
            if (payload > snapshot->size - offset) payload = snapshot->size - offset;
            memcpy(reply + P4_DEBUG_SNAPSHOT_HEADER_BYTES,
                   snapshot->data + offset, payload);
        }
    }
    put16(reply + 24, (uint16_t)payload);
    reply[30] = 1;
    reply[31] = 1;
    const size_t crc_offset = P4_DEBUG_SNAPSHOT_HEADER_BYTES + payload;
    put32(reply + crc_offset, crc32(reply, crc_offset));
    (void)snapshot->send(snapshot->context, reply, crc_offset + 4U);
}

bool p4_debug_snapshot_consume(p4_debug_snapshot_t *snapshot,
                               const uint8_t *bytes, size_t size,
                               uint64_t now_ms)
{
    if (!snapshot || !snapshot->send || !snapshot->capture || !snapshot->release ||
        !snapshot->available || (!bytes && size)) return false;
    p4_debug_snapshot_poll(snapshot, now_ms);
    if (snapshot->used && (now_ms < snapshot->last_byte_ms ||
        now_ms - snapshot->last_byte_ms > 1000U)) snapshot->used = 0;
    bool claimed = snapshot->used >= 4U;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = bytes[i];
        if (snapshot->used < 4U && byte != (uint8_t)"P4S1"[snapshot->used]) {
            snapshot->used = byte == 'P' ? 1U : 0U;
            if (snapshot->used) snapshot->request[0] = byte;
        } else {
            snapshot->request[snapshot->used++] = byte;
            if (snapshot->used >= 4U) claimed = true;
            if (snapshot->used == P4_DEBUG_SNAPSHOT_REQUEST_BYTES) {
                handle(snapshot, now_ms);
                snapshot->used = 0;
            }
        }
        snapshot->last_byte_ms = now_ms;
    }
    return claimed;
}
