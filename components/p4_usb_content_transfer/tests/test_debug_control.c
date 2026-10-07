// SPDX-License-Identifier: MIT
#include "p4/debug_control.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t reply[P4_DEBUG_RESPONSE_BYTES];
    unsigned replies;
    bool fill_status;
} fixture_t;

static uint32_t get32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
           (uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i) bytes[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8U; ++j)
            crc = (crc >> 1U) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
    }
    return ~crc;
}

static bool send_reply(void *context, const uint8_t *bytes, size_t size)
{
    fixture_t *fixture = context;
    assert(size == P4_DEBUG_RESPONSE_BYTES);
    assert(memcmp(bytes, "P4E1", 4) == 0 && bytes[4] == 1);
    assert(get32(bytes + 124) == crc32(bytes, 124));
    assert(bytes[123] == 0);
    memcpy(fixture->reply, bytes, size);
    ++fixture->replies;
    return true;
}

static void status_text(void *context, char *buffer, size_t size)
{
    fixture_t *fixture = context;
    assert(size == P4_DEBUG_STATUS_BYTES);
    if (fixture->fill_status) memset(buffer, 'x', size);
    else (void)snprintf(buffer, size, "shell multiplayer game=arena");
}

static void seal(uint8_t *request)
{
    put32(request + 28, crc32(request, 28));
}

static void request(uint8_t *bytes, uint8_t command,
                    uint32_t session, uint32_t sequence)
{
    memset(bytes, 0, P4_DEBUG_REQUEST_BYTES);
    memcpy(bytes, "P4D1", 4);
    bytes[4] = 1;
    bytes[5] = command;
    put32(bytes + 8, session);
    put32(bytes + 12, sequence);
    seal(bytes);
}

static void input_request(uint8_t *bytes, uint32_t session, uint32_t sequence,
                          bool down, uint32_t buttons, uint16_t x,
                          uint16_t y, uint16_t hold_ms)
{
    request(bytes, P4_DEBUG_INPUT, session, sequence);
    bytes[6] = down ? 1U : 0U;
    put32(bytes + 16, buttons);
    put16(bytes + 20, x);
    put16(bytes + 22, y);
    put16(bytes + 24, hold_ms);
    seal(bytes);
}

static void run(p4_debug_control_t *control, fixture_t *fixture,
                const uint8_t *bytes, uint64_t now, p4_debug_result_t expected)
{
    const unsigned replies = fixture->replies;
    assert(p4_debug_control_consume(control, bytes, P4_DEBUG_REQUEST_BYTES, now));
    assert(fixture->replies == replies + 1U);
    assert(fixture->reply[6] == (uint8_t)expected);
    assert(fixture->reply[5] == bytes[5]);
    assert(get32(fixture->reply + 8) == get32(bytes + 8));
    assert(get32(fixture->reply + 12) == get32(bytes + 12));
}

static void test_framing(void)
{
    fixture_t fixture = {0};
    p4_debug_control_t control;
    uint8_t bytes[P4_DEBUG_REQUEST_BYTES];
    p4_debug_control_init(&control, send_reply, status_text, &fixture);
    request(bytes, P4_DEBUG_STATUS, 0, 0);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        const bool claimed = p4_debug_control_consume(&control, bytes + i, 1, 10U + i);
        assert(claimed == (i >= 3U));
    }
    assert(fixture.replies == 1 && fixture.reply[6] == P4_DEBUG_OK);
    assert(strcmp((char *)fixture.reply + 32, "shell multiplayer game=arena") == 0);
    uint8_t pair[P4_DEBUG_REQUEST_BYTES * 2U + 3U] = {'P', '4', 'P'};
    memcpy(pair + 3, bytes, sizeof(bytes));
    memcpy(pair + 3 + sizeof(bytes), bytes, sizeof(bytes));
    assert(p4_debug_control_consume(&control, pair, sizeof(pair), 50));
    assert(fixture.replies == 3);
    assert(p4_debug_control_consume(&control, bytes, 8, 60));
    assert(p4_debug_control_consume(&control, bytes, sizeof(bytes), 1061));
    assert(fixture.replies == 4);
    assert(p4_debug_control_consume(&control, bytes, 8, 1070));
    assert(p4_debug_control_consume(&control, bytes, sizeof(bytes), 1000));
    assert(fixture.replies == 5); /* Clock rollback also drops a partial frame. */
    for (unsigned i = 0; i < 10000U; ++i) {
        const uint8_t noise = (uint8_t)(i * 37U);
        (void)p4_debug_control_consume(&control, &noise, 1, 1100);
    }
    fixture.fill_status = true;
    run(&control, &fixture, bytes, 3000, P4_DEBUG_OK);
    for (size_t i = 32; i < 123; ++i) assert(fixture.reply[i] == 'x');
    assert(!p4_debug_control_consume(NULL, bytes, sizeof(bytes), 3001));
    assert(!p4_debug_control_consume(&control, NULL, 1, 3001));
    assert(!p4_debug_control_sample(NULL, 0).enabled);
    p4_debug_control_init(NULL, NULL, NULL, NULL);
    p4_debug_control_release_inputs(NULL);
    p4_debug_control_init(&control, NULL, NULL, NULL);
    assert(!p4_debug_control_consume(&control, bytes, sizeof(bytes), 3001));
}

static void test_sessions_and_expiry(void)
{
    fixture_t fixture = {0};
    p4_debug_control_t control;
    uint8_t bytes[P4_DEBUG_REQUEST_BYTES], duplicate[P4_DEBUG_REQUEST_BYTES];
    p4_debug_control_init(&control, send_reply, status_text, &fixture);
    input_request(bytes, 42, 1, true, 0x81, 1279, 719, 1000);
    run(&control, &fixture, bytes, 0, P4_DEBUG_SESSION);
    request(bytes, P4_DEBUG_OPEN, 42, 1);
    run(&control, &fixture, bytes, 10, P4_DEBUG_OK);
    assert(fixture.reply[7] == 1 && get32(fixture.reply + 24) == 30000);
    run(&control, &fixture, bytes, 20, P4_DEBUG_OK);
    assert(get32(fixture.reply + 24) == 29990); /* OPEN retries do not renew. */
    request(bytes, P4_DEBUG_OPEN, 43, 1);
    run(&control, &fixture, bytes, 30, P4_DEBUG_BUSY);
    input_request(bytes, 42, 2, true, 0x81, 1279, 719, 1000);
    memcpy(duplicate, bytes, sizeof(bytes));
    run(&control, &fixture, bytes, 40, P4_DEBUG_OK);
    assert(fixture.reply[7] == 3 && get32(fixture.reply + 16) == 0x81);
    assert(get32(fixture.reply + 24) == 30000 && get32(fixture.reply + 28) == 1000);
    p4_debug_input_t input = p4_debug_control_sample(&control, 1039);
    assert(input.enabled && input.touch_down && input.x == 1279 && input.y == 719);
    assert(input.buttons == 0x81);
    run(&control, &fixture, duplicate, 1039, P4_DEBUG_OK);
    assert(get32(fixture.reply + 28) == 1);
    input = p4_debug_control_sample(&control, 1040);
    assert(input.enabled && !input.touch_down && !input.buttons && !input.x && !input.y);
    run(&control, &fixture, duplicate, 1041, P4_DEBUG_OK);
    assert(get32(fixture.reply + 28) == 0 && !fixture.reply[16]);
    bytes[16] ^= 1;
    seal(bytes);
    run(&control, &fixture, bytes, 1042, P4_DEBUG_SEQUENCE);
    request(bytes, P4_DEBUG_RELEASE, 42, 1);
    run(&control, &fixture, bytes, 1043, P4_DEBUG_SEQUENCE);
    request(bytes, P4_DEBUG_STATUS, 0, 0);
    run(&control, &fixture, bytes, 30039, P4_DEBUG_OK);
    assert(get32(fixture.reply + 24) == 1);
    run(&control, &fixture, bytes, 30040, P4_DEBUG_OK);
    assert(fixture.reply[7] == 0 && get32(fixture.reply + 24) == 0);
    run(&control, &fixture, duplicate, 30041, P4_DEBUG_SESSION);
    request(bytes, P4_DEBUG_OPEN, 42, 1);
    run(&control, &fixture, bytes, 30042, P4_DEBUG_SEQUENCE);
    request(bytes, P4_DEBUG_OPEN, 43, 1);
    run(&control, &fixture, bytes, 30043, P4_DEBUG_OK);
    request(bytes, P4_DEBUG_CLOSE, 43, 2);
    run(&control, &fixture, bytes, 30044, P4_DEBUG_OK);
    run(&control, &fixture, bytes, 30045, P4_DEBUG_OK);
    assert(fixture.reply[7] == 0);
    request(bytes, P4_DEBUG_OPEN, 43, 3);
    run(&control, &fixture, bytes, 30046, P4_DEBUG_OK);
    input_request(bytes, 43, 4, true, 0xff, 1, 1, 1000);
    run(&control, &fixture, bytes, 30047, P4_DEBUG_OK);
    p4_debug_control_release_inputs(&control);
    run(&control, &fixture, bytes, 30048, P4_DEBUG_OK);
    assert(fixture.reply[7] == 1 && get32(fixture.reply + 16) == 0);
    assert(get32(fixture.reply + 28) == 0); /* Handoff release survives a retry. */
    request(bytes, P4_DEBUG_RELEASE, 43, UINT32_MAX);
    run(&control, &fixture, bytes, 30049, P4_DEBUG_OK);
    request(bytes, P4_DEBUG_RELEASE, 43, 1);
    run(&control, &fixture, bytes, 30050, P4_DEBUG_SEQUENCE);
    request(bytes, P4_DEBUG_RELEASE, 43, 0);
    run(&control, &fixture, bytes, 30051, P4_DEBUG_INVALID);
}

static void test_validation_and_release(void)
{
    fixture_t fixture = {0};
    p4_debug_control_t control;
    uint8_t bytes[P4_DEBUG_REQUEST_BYTES];
    p4_debug_control_init(&control, send_reply, NULL, &fixture);
    const unsigned invalid_offsets[] = {4, 5, 6, 7, 16, 20, 22, 24, 26, 27};
    for (size_t i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
        request(bytes, P4_DEBUG_OPEN, 1, 1);
        bytes[invalid_offsets[i]] = 0xff;
        seal(bytes);
        run(&control, &fixture, bytes, 0, P4_DEBUG_INVALID);
        assert(!p4_debug_control_sample(&control, 0).enabled);
    }
    request(bytes, P4_DEBUG_OPEN, 0, 1);
    run(&control, &fixture, bytes, 0, P4_DEBUG_INVALID);
    request(bytes, P4_DEBUG_OPEN, 1, 0);
    run(&control, &fixture, bytes, 0, P4_DEBUG_INVALID);
    request(bytes, P4_DEBUG_OPEN, 1, 1);
    bytes[31] ^= 1;
    run(&control, &fixture, bytes, 0, P4_DEBUG_INVALID);
    seal(bytes);
    run(&control, &fixture, bytes, 0, P4_DEBUG_OK);
    const uint16_t coordinates[][2] = {{1280, 0}, {0, 720}, {65535, 65535}};
    for (size_t i = 0; i < sizeof(coordinates) / sizeof(coordinates[0]); ++i) {
        input_request(bytes, 1, 2, true, 1, coordinates[i][0], coordinates[i][1], 1);
        run(&control, &fixture, bytes, 1, P4_DEBUG_INVALID);
    }
    input_request(bytes, 1, 2, true, 0x100, 0, 0, 1);
    run(&control, &fixture, bytes, 1, P4_DEBUG_INVALID);
    input_request(bytes, 1, 2, true, 0, 0, 0, 0);
    run(&control, &fixture, bytes, 1, P4_DEBUG_INVALID);
    input_request(bytes, 1, 2, true, 0, 0, 0, 1001);
    run(&control, &fixture, bytes, 1, P4_DEBUG_INVALID);
    input_request(bytes, 1, 2, true, 0, 0, 0, 1);
    bytes[6] = 2;
    seal(bytes);
    run(&control, &fixture, bytes, 1, P4_DEBUG_INVALID);
    assert(get32(fixture.reply + 24) == 29999); /* Errors never renew the lease. */
    input_request(bytes, 1, 2, true, 0x80, 10, 20, 1000);
    run(&control, &fixture, bytes, 2, P4_DEBUG_OK);
    request(bytes, P4_DEBUG_RELEASE, 1, 3);
    run(&control, &fixture, bytes, 3, P4_DEBUG_OK);
    assert(fixture.reply[7] == 1 && get32(fixture.reply + 16) == 0);
    input_request(bytes, 1, 4, true, 0x80, 10, 20, 1000);
    run(&control, &fixture, bytes, 4, P4_DEBUG_OK);
    input_request(bytes, 1, 5, false, 0, 0, 0, 1);
    run(&control, &fixture, bytes, 5, P4_DEBUG_OK);
    assert(fixture.reply[7] == 1 && get32(fixture.reply + 16) == 0);
    input_request(bytes, 1, 6, true, 0xff, 0, 0, 1000);
    run(&control, &fixture, bytes, 6, P4_DEBUG_OK);
    request(bytes, P4_DEBUG_CLOSE, 1, 7);
    run(&control, &fixture, bytes, 7, P4_DEBUG_OK);
    assert(fixture.reply[7] == 0 && get32(fixture.reply + 16) == 0);
    request(bytes, P4_DEBUG_OPEN, 2, 1);
    run(&control, &fixture, bytes, UINT64_MAX - 100U, P4_DEBUG_OK);
    input_request(bytes, 2, 2, true, 1, 1, 1, 1000);
    run(&control, &fixture, bytes, UINT64_MAX - 99U, P4_DEBUG_OK);
    assert(get32(fixture.reply + 24) == 99 && get32(fixture.reply + 28) == 99);
    assert(!p4_debug_control_sample(&control, UINT64_MAX).enabled);
}

int main(void)
{
    test_framing();
    test_sessions_and_expiry();
    test_validation_and_release();
    puts("USB debug framing, validation, session isolation, replay and input expiry passed");
    return 0;
}
