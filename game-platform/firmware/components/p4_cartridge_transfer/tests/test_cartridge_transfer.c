#include "p4/cartridge_transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                             \
        }                                                                                   \
    } while (0)

typedef struct {
    uint8_t storage[4096];
    uint8_t sha256[P4CT_SHA256_BYTES];
    uint32_t size;
    uint32_t begin_calls;
    uint32_t write_calls;
    uint32_t verify_calls;
    uint32_t commit_calls;
    uint32_t abort_calls;
    bool fail_begin;
    bool fail_write;
    bool fail_verify;
    bool fail_commit;
} fake_backend_t;

typedef struct {
    uint8_t bytes[P4CT_MAX_ENCODED_BYTES];
    size_t size;
    uint32_t count;
} capture_t;

static p4ct_result_t fake_begin(
    void *user,
    uint32_t size,
    const uint8_t sha256[P4CT_SHA256_BYTES])
{
    fake_backend_t *fake = user;
    fake->begin_calls++;
    if (fake->fail_begin || size > sizeof(fake->storage)) {
        return P4CT_RESULT_BACKEND_FAILED;
    }
    memset(fake->storage, 0, sizeof(fake->storage));
    fake->size = size;
    memcpy(fake->sha256, sha256, P4CT_SHA256_BYTES);
    return P4CT_RESULT_OK;
}

static p4ct_result_t fake_write(
    void *user,
    uint32_t offset,
    const uint8_t *data,
    uint16_t size)
{
    fake_backend_t *fake = user;
    fake->write_calls++;
    if (fake->fail_write || offset > fake->size || (uint32_t)size > fake->size - offset) {
        return P4CT_RESULT_BACKEND_FAILED;
    }
    memcpy(&fake->storage[offset], data, size);
    return P4CT_RESULT_OK;
}

static p4ct_result_t fake_verify(
    void *user,
    uint32_t size,
    const uint8_t sha256[P4CT_SHA256_BYTES])
{
    fake_backend_t *fake = user;
    fake->verify_calls++;
    if (fake->fail_verify || size != fake->size ||
        memcmp(fake->sha256, sha256, P4CT_SHA256_BYTES) != 0) {
        return P4CT_RESULT_BACKEND_FAILED;
    }
    return P4CT_RESULT_OK;
}

static p4ct_result_t fake_commit(void *user)
{
    fake_backend_t *fake = user;
    fake->commit_calls++;
    return fake->fail_commit ? P4CT_RESULT_BACKEND_FAILED : P4CT_RESULT_OK;
}

static void fake_abort(void *user)
{
    fake_backend_t *fake = user;
    fake->abort_calls++;
}

static bool capture_emit(void *user, const uint8_t *bytes, size_t size)
{
    capture_t *capture = user;
    if (size > sizeof(capture->bytes)) {
        return false;
    }
    memcpy(capture->bytes, bytes, size);
    capture->size = size;
    capture->count++;
    return true;
}

static bool reject_emit(void *user, const uint8_t *bytes, size_t size)
{
    (void)user;
    (void)bytes;
    (void)size;
    return false;
}

static p4ct_backend_t backend_functions(void)
{
    const p4ct_backend_t backend = {
        .stage_begin = fake_begin,
        .stage_write = fake_write,
        .stage_verify = fake_verify,
        .stage_commit = fake_commit,
        .stage_abort = fake_abort,
    };
    return backend;
}

static void init_context(p4ct_t *context, fake_backend_t *fake)
{
    const p4ct_config_t config = {
        .max_object_bytes = (uint32_t)sizeof(fake->storage),
        .inactivity_ms = P4CT_DEFAULT_INACTIVITY_MS,
        .feature_bits = 0U,
        .max_chunk_bytes = P4CT_MAX_CHUNK_BYTES,
    };
    const p4ct_backend_t backend = backend_functions();
    memset(fake, 0, sizeof(*fake));
    CHECK(p4ct_init(context, &config, &backend, fake) == P4CT_RESULT_OK);
}

static size_t make_frame(
    uint8_t type,
    uint32_t session,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size,
    uint8_t output[P4CT_MAX_ENCODED_BYTES])
{
    size_t size = 0U;
    CHECK(p4ct_encode_frame(
              type,
              session,
              sequence,
              payload,
              payload_size,
              output,
              P4CT_MAX_ENCODED_BYTES,
              &size) == P4CT_RESULT_OK);
    return size;
}

static void feed_frame(
    p4ct_t *context,
    const uint8_t *frame,
    size_t frame_size,
    uint32_t now_ms,
    capture_t *capture)
{
    CHECK(p4ct_feed(context, frame, frame_size, now_ms, capture_emit, capture) ==
          P4CT_RESULT_OK);
}

static void expect_response(
    const capture_t *capture,
    uint8_t type,
    uint32_t session,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_size)
{
    uint8_t expected[P4CT_MAX_ENCODED_BYTES];
    const size_t expected_size = make_frame(
        type,
        session,
        sequence,
        payload,
        payload_size,
        expected);
    CHECK(capture->size == expected_size);
    CHECK(memcmp(capture->bytes, expected, expected_size) == 0);
}

static void write_u32_test(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value & UINT32_C(0xff));
    bytes[1] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    bytes[2] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    bytes[3] = (uint8_t)(value >> 24U);
}

static void test_crc_check_value(void)
{
    static const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(p4ct_crc32(check, sizeof(check)) == UINT32_C(0xcbf43926));
}

static void test_maximum_frame_encoding(void)
{
    uint8_t payload[P4CT_MAX_FRAME_PAYLOAD_BYTES];
    uint8_t encoded[P4CT_MAX_ENCODED_BYTES];
    size_t encoded_size = 0U;

    memset(payload, UINT8_C(0xff), sizeof(payload));
    CHECK(p4ct_encode_frame(
              (uint8_t)P4CT_TYPE_DATA,
              1U,
              1U,
              payload,
              (uint16_t)sizeof(payload),
              encoded,
              sizeof(encoded),
              &encoded_size) == P4CT_RESULT_OK);
    CHECK(encoded_size <= P4CT_MAX_ENCODED_BYTES);
    CHECK(encoded[encoded_size - 1U] == 0U);
    CHECK(p4ct_encode_frame(
              (uint8_t)P4CT_TYPE_DATA,
              1U,
              1U,
              payload,
              (uint16_t)sizeof(payload),
              encoded,
              encoded_size - 1U,
              &encoded_size) == P4CT_RESULT_LIMIT_REACHED);
}

static void test_every_hello_split(void)
{
    uint8_t hello[P4CT_MAX_ENCODED_BYTES];
    const uint32_t session = UINT32_C(0x12345678);
    const size_t hello_size = make_frame(
        (uint8_t)P4CT_TYPE_HELLO,
        session,
        0U,
        NULL,
        0U,
        hello);
    size_t split;

    for (split = 0U; split <= hello_size; ++split) {
        p4ct_t context;
        fake_backend_t fake;
        capture_t capture;
        init_context(&context, &fake);
        memset(&capture, 0, sizeof(capture));
        CHECK(p4ct_feed(&context, hello, split, 1U, capture_emit, &capture) ==
              P4CT_RESULT_OK);
        CHECK(p4ct_feed(
                  &context,
                  &hello[split],
                  hello_size - split,
                  1U,
                  capture_emit,
                  &capture) == P4CT_RESULT_OK);
        CHECK(capture.count == 1U);
        CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    }
}

static void test_emit_failure_retries_cached_response(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t frame[P4CT_MAX_ENCODED_BYTES];
    const uint32_t session = 55U;
    const size_t frame_size = make_frame(
        (uint8_t)P4CT_TYPE_HELLO,
        session,
        0U,
        NULL,
        0U,
        frame);

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    CHECK(p4ct_feed(&context, frame, frame_size, 1U, reject_emit, NULL) ==
          P4CT_RESULT_EMIT_FAILED);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(context.last_request_valid);
    feed_frame(&context, frame, frame_size, 2U, &capture);
    CHECK(capture.count == 1U);
    CHECK(fake.abort_calls == 0U);
}

static void test_complete_transfer_and_duplicates(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t frame[P4CT_MAX_ENCODED_BYTES];
    uint8_t begin[40];
    uint8_t data[7];
    const uint32_t session = UINT32_C(0x89abcdef);
    size_t frame_size;
    uint32_t index;

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));

    frame_size = make_frame((uint8_t)P4CT_TYPE_HELLO, session, 0U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 10U, &capture);
    CHECK(capture.count == 1U);
    feed_frame(&context, frame, frame_size, 11U, &capture);
    CHECK(capture.count == 2U);
    CHECK(fake.abort_calls == 0U);

    memset(begin, 0, sizeof(begin));
    begin[0] = 1U;
    write_u32_test(&begin[4], 6U);
    for (index = 0U; index < P4CT_SHA256_BYTES; ++index) {
        begin[8U + index] = (uint8_t)(index + 1U);
    }
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        1U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 12U, &capture);
    CHECK(fake.begin_calls == 1U);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_RECEIVING);
    feed_frame(&context, frame, frame_size, 13U, &capture);
    CHECK(fake.begin_calls == 1U);

    write_u32_test(data, 0U);
    memcpy(&data[4], "abc", 3U);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_DATA,
        session,
        2U,
        data,
        (uint16_t)sizeof(data),
        frame);
    for (index = 0U; index < (uint32_t)frame_size; ++index) {
        CHECK(p4ct_feed(&context, &frame[index], 1U, 14U, capture_emit, &capture) ==
              P4CT_RESULT_OK);
    }
    CHECK(fake.write_calls == 1U);
    CHECK(p4ct_get_next_offset(&context) == 3U);

    data[4] = (uint8_t)'x';
    {
        uint8_t reused[P4CT_MAX_ENCODED_BYTES];
        const size_t reused_size = make_frame(
            (uint8_t)P4CT_TYPE_DATA,
            session,
            2U,
            data,
            (uint16_t)sizeof(data),
            reused);
        feed_frame(&context, reused, reused_size, 15U, &capture);
        CHECK(fake.write_calls == 1U);
    }
    data[4] = (uint8_t)'a';
    feed_frame(&context, frame, frame_size, 16U, &capture);
    CHECK(fake.write_calls == 1U);

    write_u32_test(data, 3U);
    memcpy(&data[4], "def", 3U);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_DATA,
        session,
        3U,
        data,
        (uint16_t)sizeof(data),
        frame);
    {
        uint8_t corrupt[P4CT_MAX_ENCODED_BYTES];
        const uint32_t before = capture.count;
        memcpy(corrupt, frame, frame_size);
        corrupt[frame_size / 2U] ^= UINT8_C(0x40);
        feed_frame(&context, corrupt, frame_size, 17U, &capture);
        CHECK(capture.count == before);
    }
    feed_frame(&context, frame, frame_size, 18U, &capture);
    CHECK(fake.write_calls == 2U);
    CHECK(memcmp(fake.storage, "abcdef", 6U) == 0);

    frame_size = make_frame((uint8_t)P4CT_TYPE_END, session, 4U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 19U, &capture);
    CHECK(fake.verify_calls == 1U);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_VERIFIED);
    feed_frame(&context, frame, frame_size, 20U, &capture);
    CHECK(fake.verify_calls == 1U);

    frame_size = make_frame((uint8_t)P4CT_TYPE_COMMIT, session, 5U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 21U, &capture);
    CHECK(fake.commit_calls == 1U);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    feed_frame(&context, frame, frame_size, 22U, &capture);
    CHECK(fake.commit_calls == 1U);
}

static void test_oversize_resync_timeout_and_failure(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t frame[P4CT_MAX_ENCODED_BYTES];
    uint8_t begin[40];
    uint8_t garbage[P4CT_MAX_ENCODED_BYTES + 20U];
    const uint32_t session = 99U;
    size_t frame_size;

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    memset(garbage, UINT8_C(0x55), sizeof(garbage));
    garbage[sizeof(garbage) - 1U] = 0U;
    CHECK(p4ct_feed(&context, garbage, sizeof(garbage), 1U, capture_emit, &capture) ==
          P4CT_RESULT_OK);
    CHECK(capture.count == 0U);

    frame_size = make_frame((uint8_t)P4CT_TYPE_HELLO, session, 0U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 2U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);

    memset(begin, 0, sizeof(begin));
    begin[0] = 1U;
    write_u32_test(&begin[4], 1U);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        1U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 3U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_RECEIVING);
    p4ct_tick(&context, 3U + P4CT_DEFAULT_INACTIVITY_MS);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_UNBOUND);
    CHECK(fake.abort_calls == 1U);

    frame_size = make_frame((uint8_t)P4CT_TYPE_HELLO, session, 0U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 40000U, &capture);
    fake.fail_begin = true;
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        1U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 40001U, &capture);
    CHECK(fake.begin_calls == 2U);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(fake.abort_calls == 2U);
}

static void test_timeout_clears_partial_framing(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t hello[P4CT_MAX_ENCODED_BYTES];
    uint8_t partial[] = {3U, (uint8_t)'P'};
    const uint32_t first_session = 101U;
    const uint32_t second_session = 102U;
    size_t hello_size;

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    hello_size = make_frame(
        (uint8_t)P4CT_TYPE_HELLO,
        first_session,
        0U,
        NULL,
        0U,
        hello);
    feed_frame(&context, hello, hello_size, 1U, &capture);
    CHECK(p4ct_feed(
              &context,
              partial,
              sizeof(partial),
              2U,
              capture_emit,
              &capture) == P4CT_RESULT_OK);
    CHECK(context.rx_length == sizeof(partial));

    p4ct_tick(&context, 1U + P4CT_DEFAULT_INACTIVITY_MS);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_UNBOUND);
    CHECK(context.rx_length == 0U);
    CHECK(!context.discarding_oversize);

    hello_size = make_frame(
        (uint8_t)P4CT_TYPE_HELLO,
        second_session,
        0U,
        NULL,
        0U,
        hello);
    feed_frame(&context, hello, hello_size, 40000U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(context.session_id == second_session);
}

static void test_backend_failures_preserve_ready_state(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t frame[P4CT_MAX_ENCODED_BYTES];
    uint8_t begin[40];
    uint8_t data[5];
    const uint32_t session = 303U;
    size_t frame_size;

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    memset(begin, 0, sizeof(begin));
    begin[0] = 1U;
    write_u32_test(&begin[4], 1U);
    write_u32_test(data, 0U);
    data[4] = UINT8_C(0x7a);

    frame_size = make_frame((uint8_t)P4CT_TYPE_HELLO, session, 0U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 1U, &capture);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        1U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 2U, &capture);
    fake.fail_write = true;
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_DATA,
        session,
        2U,
        data,
        (uint16_t)sizeof(data),
        frame);
    feed_frame(&context, frame, frame_size, 3U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(fake.abort_calls == 1U);

    fake.fail_write = false;
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        3U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 4U, &capture);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_DATA,
        session,
        4U,
        data,
        (uint16_t)sizeof(data),
        frame);
    feed_frame(&context, frame, frame_size, 5U, &capture);
    fake.fail_verify = true;
    frame_size = make_frame((uint8_t)P4CT_TYPE_END, session, 5U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 6U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(fake.abort_calls == 2U);

    fake.fail_verify = false;
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_BEGIN,
        session,
        6U,
        begin,
        (uint16_t)sizeof(begin),
        frame);
    feed_frame(&context, frame, frame_size, 7U, &capture);
    frame_size = make_frame(
        (uint8_t)P4CT_TYPE_DATA,
        session,
        7U,
        data,
        (uint16_t)sizeof(data),
        frame);
    feed_frame(&context, frame, frame_size, 8U, &capture);
    frame_size = make_frame((uint8_t)P4CT_TYPE_END, session, 8U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 9U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_VERIFIED);
    fake.fail_commit = true;
    frame_size = make_frame((uint8_t)P4CT_TYPE_COMMIT, session, 9U, NULL, 0U, frame);
    feed_frame(&context, frame, frame_size, 10U, &capture);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_READY);
    CHECK(fake.abort_calls == 3U);
}

static void test_concatenated_frames_and_response_shape(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t hello[P4CT_MAX_ENCODED_BYTES];
    uint8_t abort_frame[P4CT_MAX_ENCODED_BYTES];
    uint8_t both[P4CT_MAX_ENCODED_BYTES * 2U];
    uint8_t status_payload[8];
    const uint32_t session = 7U;
    const size_t hello_size = make_frame(
        (uint8_t)P4CT_TYPE_HELLO,
        session,
        0U,
        NULL,
        0U,
        hello);
    const size_t abort_size = make_frame(
        (uint8_t)P4CT_TYPE_ABORT,
        session,
        1U,
        NULL,
        0U,
        abort_frame);

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    memcpy(both, hello, hello_size);
    memcpy(&both[hello_size], abort_frame, abort_size);
    CHECK(p4ct_feed(
              &context,
              both,
              hello_size + abort_size,
              10U,
              capture_emit,
              &capture) == P4CT_RESULT_OK);
    CHECK(capture.count == 2U);

    status_payload[0] = (uint8_t)P4CT_TYPE_ABORT;
    status_payload[1] = (uint8_t)P4CT_STATE_READY;
    status_payload[2] = 0U;
    status_payload[3] = 0U;
    write_u32_test(&status_payload[4], 0U);
    expect_response(
        &capture,
        (uint8_t)P4CT_TYPE_ACK,
        session,
        1U,
        status_payload,
        (uint16_t)sizeof(status_payload));
}

static void test_arbitrary_input_is_bounded(void)
{
    p4ct_t context;
    fake_backend_t fake;
    capture_t capture;
    uint8_t bytes[257];
    uint32_t state = UINT32_C(0x5eed1234);
    uint32_t round;
    size_t index;

    init_context(&context, &fake);
    memset(&capture, 0, sizeof(capture));
    for (round = 0U; round < 200U; ++round) {
        for (index = 0U; index < sizeof(bytes); ++index) {
            state = state * UINT32_C(1664525) + UINT32_C(1013904223);
            bytes[index] = (uint8_t)(state >> 24U);
        }
        CHECK(p4ct_feed(&context, bytes, sizeof(bytes), round, capture_emit, &capture) ==
              P4CT_RESULT_OK);
    }
    p4ct_disconnect(&context);
    CHECK(p4ct_get_state(&context) == P4CT_STATE_UNBOUND);
}

int main(void)
{
    test_crc_check_value();
    test_maximum_frame_encoding();
    test_every_hello_split();
    test_emit_failure_retries_cached_response();
    test_complete_transfer_and_duplicates();
    test_oversize_resync_timeout_and_failure();
    test_timeout_clears_partial_framing();
    test_backend_failures_preserve_ready_state();
    test_concatenated_frames_and_response_shape();
    test_arbitrary_input_is_bounded();
    (void)puts("p4_cartridge_transfer_tests: PASS");
    return EXIT_SUCCESS;
}
