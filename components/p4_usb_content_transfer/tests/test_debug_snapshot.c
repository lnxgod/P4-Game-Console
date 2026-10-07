// SPDX-License-Identifier: MIT
#include "p4/debug_snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 40, HEIGHT = 40, IMAGE_BYTES = WIDTH * HEIGHT * 2 };
typedef struct {
    uint8_t reply[P4_DEBUG_SNAPSHOT_MAX_RESPONSE];
    size_t reply_size;
    unsigned replies, captures, releases;
    bool available, capture_fails, invalid_metadata, oversized_metadata;
    bool lose_availability, send_fails, coordinate_source;
    uint32_t coordinate_seed;
    uint8_t source[IMAGE_BYTES];
    uint8_t *allocated;
    uint16_t capture_width, capture_height;
} fixture_t;

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8U));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U |
           (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}

static void put16(uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8U);
}

static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4U; ++i) p[i] = (uint8_t)(n >> (8U * i));
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8U; ++j) {
            crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return ~crc;
}

static bool send_reply(void *context, const uint8_t *bytes, size_t size)
{
    fixture_t *f = context;
    assert(size >= 36U && size <= sizeof(f->reply));
    assert(memcmp(bytes, "P4T1", 4) == 0 && bytes[4] == 1);
    assert(!bytes[7] && bytes[30] == 1 && bytes[31] == 1);
    assert(size == 36U + get16(bytes + 24));
    assert(get32(bytes + size - 4U) == crc32(bytes, size - 4U));
    memcpy(f->reply, bytes, size);
    f->reply_size = size;
    ++f->replies;
    return !f->send_fails;
}

static uint16_t coordinate_pixel(size_t x, size_t y, uint32_t seed)
{
    return (uint16_t)((x * 37U) ^ (y * 977U) ^ (y >> 8U) ^ seed);
}

static bool capture(void *context, uint8_t **data, size_t *size,
                    uint16_t *width, uint16_t *height)
{
    fixture_t *f = context;
    assert(!f->allocated);
    ++f->captures;
    const size_t bytes = (size_t)f->capture_width * f->capture_height * 2U;
    *data = malloc(bytes);
    assert(*data);
    f->allocated = *data;
    if (f->coordinate_source) {
        for (size_t y = 0; y < f->capture_height; ++y) {
            for (size_t x = 0; x < f->capture_width; ++x) {
                put16(*data + (y * f->capture_width + x) * 2U,
                      coordinate_pixel(x, y, f->coordinate_seed));
            }
        }
    } else {
        for (size_t i = 0; i < bytes; ++i) (*data)[i] = f->source[i % IMAGE_BYTES];
    }
    *size = f->invalid_metadata ? bytes - 1U : bytes;
    *width = f->capture_width;
    *height = f->capture_height;
    if (f->oversized_metadata) {
        *width = 1280;
        *height = 721;
        *size = 1280U * 721U * 2U; /* Deliberately exceeds the real allocation. */
    }
    if (f->lose_availability) f->available = false;
    return !f->capture_fails;
}

static void release(void *context, uint8_t *data)
{
    fixture_t *f = context;
    assert(data && data == f->allocated);
    ++f->releases;
    free(data);
    f->allocated = NULL;
}

static bool available(void *context)
{
    return ((fixture_t *)context)->available;
}

static void init(p4_debug_snapshot_t *snapshot, fixture_t *f)
{
    memset(f, 0, sizeof(*f));
    f->available = true;
    f->capture_width = WIDTH;
    f->capture_height = HEIGHT;
    for (size_t i = 0; i < IMAGE_BYTES; ++i) f->source[i] = (uint8_t)(i * 31U);
    p4_debug_snapshot_init(snapshot, send_reply, capture, release, available, f);
}

static void seal(uint8_t *bytes)
{
    put32(bytes + 28, crc32(bytes, 28));
}

static void request(uint8_t *bytes, uint8_t command, uint32_t id,
                    uint32_t sequence, uint32_t offset, uint16_t length)
{
    memset(bytes, 0, P4_DEBUG_SNAPSHOT_REQUEST_BYTES);
    memcpy(bytes, "P4S1", 4);
    bytes[4] = 1;
    bytes[5] = command;
    put32(bytes + 8, id);
    put32(bytes + 12, sequence);
    put32(bytes + 16, offset);
    put16(bytes + 20, length);
    seal(bytes);
}

static void run(p4_debug_snapshot_t *snapshot, fixture_t *f, const uint8_t *bytes,
                uint64_t now, p4_debug_snapshot_result_t expected)
{
    const unsigned replies = f->replies;
    assert(p4_debug_snapshot_consume(snapshot, bytes, P4_DEBUG_SNAPSHOT_REQUEST_BYTES, now));
    assert(f->replies == replies + 1U);
    assert(f->reply[6] == (uint8_t)expected && f->reply[5] == bytes[5]);
    assert(get32(f->reply + 8) == get32(bytes + 8));
    assert(get32(f->reply + 12) == get32(bytes + 12));
    assert(get32(f->reply + 16) == get32(bytes + 16));
    if (expected != P4_DEBUG_SNAPSHOT_OK || bytes[5] != P4_DEBUG_SNAPSHOT_READ) {
        assert(f->reply_size == 36U);
    }
}

static void test_capture_and_retries(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    f.send_fails = true;
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 42, 1, 0, 0);
    run(&s, &f, bytes, 100, P4_DEBUG_SNAPSHOT_OK);
    assert(f.captures == 1 && get32(f.reply + 20) == IMAGE_BYTES);
    assert(get16(f.reply + 26) == WIDTH && get16(f.reply + 28) == HEIGHT);
    memset(f.source, 0xff, IMAGE_BYTES); /* Rendering may change after capture. */
    f.send_fails = false;
    run(&s, &f, bytes, 200, P4_DEBUG_SNAPSHOT_OK);
    assert(f.captures == 1 && s.idle_until_ms == 30100);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 43, 2, 0, 0);
    run(&s, &f, bytes, 201, P4_DEBUG_SNAPSHOT_BUSY);
    assert(f.captures == 1 && !f.releases && !get32(f.reply + 20));
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 42, 3, 0, 2048);
    run(&s, &f, bytes, 300, P4_DEBUG_SNAPSHOT_OK);
    assert(f.reply_size == P4_DEBUG_SNAPSHOT_MAX_RESPONSE);
    for (size_t i = 0; i < 2048; ++i) assert(f.reply[32 + i] == (uint8_t)(i * 31U));
    uint8_t first[P4_DEBUG_SNAPSHOT_MAX_RESPONSE];
    memcpy(first, f.reply, sizeof(first));
    run(&s, &f, bytes, 301, P4_DEBUG_SNAPSHOT_OK);
    assert(memcmp(first, f.reply, sizeof(first)) == 0 && s.idle_until_ms == 30301);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 42, 2, 2048, 2048);
    run(&s, &f, bytes, 302, P4_DEBUG_SNAPSHOT_OK); /* Reordered sequence is valid. */
    assert(get16(f.reply + 24) == IMAGE_BYTES - 2048);
    for (size_t i = 0; i < IMAGE_BYTES - 2048; ++i) {
        assert(f.reply[32 + i] == (uint8_t)((i + 2048U) * 31U));
    }
    request(bytes, P4_DEBUG_SNAPSHOT_END, 43, 4, 0, 0);
    run(&s, &f, bytes, 303, P4_DEBUG_SNAPSHOT_EXPIRED);
    assert(!f.releases && s.capture_id == 42);
    request(bytes, P4_DEBUG_SNAPSHOT_END, 42, 5, 0, 0);
    run(&s, &f, bytes, 304, P4_DEBUG_SNAPSHOT_OK);
    run(&s, &f, bytes, 305, P4_DEBUG_SNAPSHOT_OK);
    assert(f.releases == 1 && !s.data && !get32(f.reply + 20));
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 42, 6, 0, 0);
    run(&s, &f, bytes, 306, P4_DEBUG_SNAPSHOT_EXPIRED);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 43, 1, 0, 0);
    run(&s, &f, bytes, 307, P4_DEBUG_SNAPSHOT_OK);
    request(bytes, P4_DEBUG_SNAPSHOT_END, 42, 5, 0, 0);
    run(&s, &f, bytes, 308, P4_DEBUG_SNAPSHOT_OK);
    assert(s.capture_id == 43 && f.releases == 1); /* Old END cannot free new capture. */
    p4_debug_snapshot_cancel(&s);
    p4_debug_snapshot_cancel(&s);
    assert(f.releases == 2);
}

static void test_invalid_and_bounds(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    const unsigned offsets[] = {4, 5, 6, 7, 16, 20, 22, 23, 24, 25, 26, 27};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 1, 0, 0);
        bytes[offsets[i]] = 0xff;
        seal(bytes);
        run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_INVALID);
    }
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 0, 1, 0, 0);
    run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_INVALID);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 0, 0, 0);
    run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_INVALID);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 1, 0, 0);
    bytes[31] ^= 1U;
    run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_INVALID);
    assert(!f.captures);
    seal(bytes);
    run(&s, &f, bytes, 1, P4_DEBUG_SNAPSHOT_OK);
    const uint32_t invalid_offsets[] = {IMAGE_BYTES, IMAGE_BYTES + 1U, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        request(bytes, P4_DEBUG_SNAPSHOT_READ, 1, 2, invalid_offsets[i], 2048);
        run(&s, &f, bytes, 2, P4_DEBUG_SNAPSHOT_INVALID);
    }
    const uint16_t invalid_lengths[] = {0, 2049, UINT16_MAX};
    for (size_t i = 0; i < sizeof(invalid_lengths) / sizeof(invalid_lengths[0]); ++i) {
        request(bytes, P4_DEBUG_SNAPSHOT_READ, 1, 2, 0, invalid_lengths[i]);
        run(&s, &f, bytes, 2, P4_DEBUG_SNAPSHOT_INVALID);
    }
    assert(s.idle_until_ms == 30001); /* Invalid reads do not renew. */
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 2, 2, 0, 1);
    run(&s, &f, bytes, 2, P4_DEBUG_SNAPSHOT_EXPIRED);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 1, 2, IMAGE_BYTES - 1U, 2048);
    run(&s, &f, bytes, 3, P4_DEBUG_SNAPSHOT_OK);
    assert(get16(f.reply + 24) == 1);
    assert(f.reply[32] == (uint8_t)((IMAGE_BYTES - 1U) * 31U));
    p4_debug_snapshot_cancel(&s);
}

static void test_expiry_and_conflicts(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    f.available = false;
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 1, 0, 0);
    run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_UNAVAILABLE);
    assert(!f.captures);
    f.available = true;
    run(&s, &f, bytes, 100, P4_DEBUG_SNAPSHOT_OK);
    run(&s, &f, bytes, 30099, P4_DEBUG_SNAPSHOT_OK);
    p4_debug_snapshot_poll(&s, 30100);
    assert(!s.data && f.releases == 1);
    run(&s, &f, bytes, 30101, P4_DEBUG_SNAPSHOT_EXPIRED);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 2, 1, 0, 0);
    run(&s, &f, bytes, 31000, P4_DEBUG_SNAPSHOT_OK);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 2, 2, 0, 1);
    for (uint64_t now = 51000; now < 151000; now += 20000U) {
        run(&s, &f, bytes, now, P4_DEBUG_SNAPSHOT_OK);
    }
    run(&s, &f, bytes, 151000, P4_DEBUG_SNAPSHOT_EXPIRED);
    assert(f.releases == 2); /* Continued reads cannot extend absolute lifetime. */
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 3, 1, 0, 0);
    run(&s, &f, bytes, 152000, P4_DEBUG_SNAPSHOT_OK);
    f.available = false;
    p4_debug_snapshot_poll(&s, 152001);
    assert(!s.data && f.releases == 3);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 3, 2, 0, 1);
    run(&s, &f, bytes, 152002, P4_DEBUG_SNAPSHOT_UNAVAILABLE);
    f.available = true;
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 3, 1, 0, 0);
    run(&s, &f, bytes, 152003, P4_DEBUG_SNAPSHOT_EXPIRED);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 4, 1, 0, 0);
    run(&s, &f, bytes, 160000, P4_DEBUG_SNAPSHOT_OK);
    p4_debug_snapshot_poll(&s, 159999);
    assert(!s.data && f.releases == 4); /* Clock rollback fails closed. */
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 5, 1, 0, 0);
    run(&s, &f, bytes, UINT64_MAX - 10U, P4_DEBUG_SNAPSHOT_OK);
    assert(s.absolute_until_ms == UINT64_MAX && s.idle_until_ms == UINT64_MAX);
    p4_debug_snapshot_poll(&s, UINT64_MAX);
    assert(!s.data && f.releases == 5);
}

static void test_callback_failures(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 1, 0, 0);
    f.capture_fails = true;
    run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_FAILED);
    assert(!s.data && f.releases == 1);
    f.capture_fails = false;
    f.invalid_metadata = true;
    run(&s, &f, bytes, 1, P4_DEBUG_SNAPSHOT_FAILED);
    assert(!s.data && f.releases == 2);
    f.invalid_metadata = false;
    f.oversized_metadata = true;
    run(&s, &f, bytes, 2, P4_DEBUG_SNAPSHOT_FAILED);
    assert(!s.data && f.releases == 3); /* Reject metadata without reading data. */
    f.oversized_metadata = false;
    f.lose_availability = true;
    run(&s, &f, bytes, 3, P4_DEBUG_SNAPSHOT_UNAVAILABLE);
    assert(!s.data && f.releases == 4);
    f.available = true;
    f.lose_availability = false;
    run(&s, &f, bytes, 4, P4_DEBUG_SNAPSHOT_EXPIRED);
}

static void test_framing(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 1, 1, 0, 0);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        assert(p4_debug_snapshot_consume(&s, bytes + i, 1, 10U + i) == (i >= 3U));
    }
    assert(f.replies == 1 && f.captures == 1);
    uint8_t pair[P4_DEBUG_SNAPSHOT_REQUEST_BYTES * 2U + 3U] = {'P', '4', 'P'};
    memcpy(pair + 3, bytes, sizeof(bytes));
    memcpy(pair + 3 + sizeof(bytes), bytes, sizeof(bytes));
    assert(p4_debug_snapshot_consume(&s, pair, sizeof(pair), 50));
    assert(f.replies == 3 && f.captures == 1);
    assert(p4_debug_snapshot_consume(&s, bytes, 8, 60));
    assert(p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 1061));
    assert(f.replies == 4);
    assert(p4_debug_snapshot_consume(&s, bytes, 8, 1070));
    assert(p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 1000));
    assert(f.replies == 5); /* Clock rollback drops only the partial frame. */
    const uint8_t other[] = "P4D1P4K1P4F1";
    assert(!p4_debug_snapshot_consume(&s, other, sizeof(other) - 1U, 1100));
    for (unsigned i = 0; i < 10000U; ++i) {
        const uint8_t noise = (uint8_t)(i * 37U);
        (void)p4_debug_snapshot_consume(&s, &noise, 1, 1101);
    }
    run(&s, &f, bytes, 2200, P4_DEBUG_SNAPSHOT_OK);
    assert(f.captures == 1);
    p4_debug_snapshot_cancel(&s);
    assert(!p4_debug_snapshot_consume(NULL, bytes, sizeof(bytes), 2201));
    assert(!p4_debug_snapshot_consume(&s, NULL, 1, 2201));
    p4_debug_snapshot_init(NULL, NULL, NULL, NULL, NULL, NULL);
    p4_debug_snapshot_poll(NULL, 0);
    p4_debug_snapshot_cancel(NULL);
    p4_debug_snapshot_init(&s, NULL, capture, release, available, &f);
    assert(!p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 2201));
    p4_debug_snapshot_init(&s, send_reply, NULL, release, available, &f);
    assert(!p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 2201));
    p4_debug_snapshot_init(&s, send_reply, capture, NULL, available, &f);
    assert(!p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 2201));
    p4_debug_snapshot_init(&s, send_reply, capture, release, NULL, &f);
    assert(!p4_debug_snapshot_consume(&s, bytes, sizeof(bytes), 2201));
}

static void test_quarter_capture(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    f.capture_width = 720;
    f.capture_height = 1280;
    f.coordinate_source = true;
    f.coordinate_seed = 0x35a9U;
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 70, 1, 0, 0);
    bytes[6] = 1; seal(bytes);
    run(&s, &f, bytes, 100, P4_DEBUG_SNAPSHOT_OK);
    assert(f.captures == 1 && s.size == 115200U);
    assert(get16(f.reply + 26) == 180 && get16(f.reply + 28) == 320);
    assert(get32(f.reply + 20) == 115200U && f.reply[31] == 1);
    for (size_t y = 0; y < 320U; ++y) {
        for (size_t x = 0; x < 180U; ++x) {
            const size_t dst = (y * 180U + x) * 2U;
            assert(get16(s.data + dst) ==
                   coordinate_pixel(x * 4U + 2U, y * 4U + 2U, f.coordinate_seed));
        }
    }
    memset(f.source, 0xff, sizeof(f.source));
    f.coordinate_seed ^= 0xffffU;
    uint8_t saved[2048]; memcpy(saved, s.data, sizeof(saved));
    run(&s, &f, bytes, 200, P4_DEBUG_SNAPSHOT_OK);
    assert(f.captures == 1 && s.idle_until_ms == 30100);
    bytes[6] = 0; seal(bytes);
    run(&s, &f, bytes, 201, P4_DEBUG_SNAPSHOT_INVALID);
    assert(s.size == 115200U && f.captures == 1 && !f.releases);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 70, 2, 0, 2048);
    run(&s, &f, bytes, 202, P4_DEBUG_SNAPSHOT_OK);
    assert(memcmp(f.reply + 32, saved, sizeof(saved)) == 0);
    run(&s, &f, bytes, 203, P4_DEBUG_SNAPSHOT_OK);
    assert(memcmp(f.reply + 32, saved, sizeof(saved)) == 0);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 70, 3, 115199, 2048);
    run(&s, &f, bytes, 204, P4_DEBUG_SNAPSHOT_OK);
    assert(get16(f.reply + 24) == 1 && f.reply[32] == s.data[115199]);
    request(bytes, P4_DEBUG_SNAPSHOT_READ, 70, 4, 115200, 1);
    run(&s, &f, bytes, 205, P4_DEBUG_SNAPSHOT_INVALID);
    for (unsigned command = P4_DEBUG_SNAPSHOT_READ; command <= P4_DEBUG_SNAPSHOT_END; ++command) {
        request(bytes, (uint8_t)command, 70, 5, 0, command == P4_DEBUG_SNAPSHOT_READ ? 1 : 0);
        bytes[6] = 1; seal(bytes);
        run(&s, &f, bytes, 206, P4_DEBUG_SNAPSHOT_INVALID);
    }
    assert(s.idle_until_ms == 30204 && !f.releases);
    f.available = false;
    p4_debug_snapshot_poll(&s, 207);
    assert(!s.data && f.releases == 1);
    f.available = true;
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 70, 6, 0, 0);
    bytes[6] = 1; seal(bytes);
    run(&s, &f, bytes, 208, P4_DEBUG_SNAPSHOT_EXPIRED);
    request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 71, 7, 0, 0);
    run(&s, &f, bytes, 209, P4_DEBUG_SNAPSHOT_OK);
    assert(s.width == 720 && s.height == 1280 && s.size == 1843200U);
    bytes[6] = 1; seal(bytes);
    run(&s, &f, bytes, 210, P4_DEBUG_SNAPSHOT_INVALID);
    p4_debug_snapshot_cancel(&s);
    assert(f.releases == 2);
}

static void test_quarter_metadata_and_flags(void)
{
    fixture_t f;
    p4_debug_snapshot_t s;
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
    init(&s, &f);
    for (unsigned flag = 2; flag < 256U; ++flag) {
        request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 80, 1, 0, 0);
        bytes[6] = (uint8_t)flag; seal(bytes);
        run(&s, &f, bytes, 0, P4_DEBUG_SNAPSHOT_INVALID);
    }
    assert(!f.captures);
    const uint16_t dimensions[][2] = {{38,40},{40,39},{1,4},{4,1}};
    for (size_t i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); ++i) {
        f.capture_width = dimensions[i][0]; f.capture_height = dimensions[i][1];
        request(bytes, P4_DEBUG_SNAPSHOT_BEGIN, 80, 1, 0, 0);
        bytes[6] = 1; seal(bytes);
        run(&s, &f, bytes, 1, P4_DEBUG_SNAPSHOT_FAILED);
        assert(!s.data && f.releases == i + 1U);
    }
    f.capture_width = 4; f.capture_height = 4;
    run(&s, &f, bytes, 2, P4_DEBUG_SNAPSHOT_OK);
    assert(s.width == 1 && s.height == 1 && s.size == 2);
    assert(s.data[0] == f.source[20] && s.data[1] == f.source[21]);
    request(bytes, P4_DEBUG_SNAPSHOT_END, 80, 2, 0, 0);
    run(&s, &f, bytes, 3, P4_DEBUG_SNAPSHOT_OK);
    assert(!s.data && f.releases == 5);
}

int main(void)
{
    test_capture_and_retries();
    test_invalid_and_bounds();
    test_expiry_and_conflicts();
    test_callback_failures();
    test_framing();
    test_quarter_capture();
    test_quarter_metadata_and_flags();
    puts("USB debug snapshot tests passed");
    return 0;
}
