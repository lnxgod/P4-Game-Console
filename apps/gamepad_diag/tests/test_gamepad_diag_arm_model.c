#include "gamepad_diag_arm_model.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            exit(EXIT_FAILURE);                                              \
        }                                                                    \
    } while (0)

#define VALID_FRAME_BYTES (GAMEPAD_DIAG_ARM_LINE_BYTES + 1U)

static const char TOKEN[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static unsigned s_usb_call_count;
static unsigned s_zeroize_count;

static int fake_sha256(const unsigned char *input, size_t input_bytes,
                       unsigned char output[GAMEPAD_DIAG_ARM_DIGEST_BYTES],
                       int is224)
{
    if (input == NULL || output == NULL ||
        input_bytes != GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES || is224 != 0) {
        return -1;
    }
    for (size_t index = 0U; index < GAMEPAD_DIAG_ARM_DIGEST_BYTES; ++index) {
        output[index] = (uint8_t)(input[index] ^
                                  input[index + GAMEPAD_DIAG_ARM_DIGEST_BYTES] ^
                                  (uint8_t)(index * 13U));
    }
    return 0;
}

static int constant_compare(const void *left, const void *right, size_t bytes)
{
    const uint8_t *const left_bytes = left;
    const uint8_t *const right_bytes = right;
    volatile uint8_t difference = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        difference = (uint8_t)(difference |
                               (uint8_t)(left_bytes[index] ^ right_bytes[index]));
    }
    return difference;
}

static void tracked_zeroize(void *buffer, size_t bytes)
{
    volatile uint8_t *cursor = buffer;
    for (size_t index = 0U; index < bytes; ++index) {
        cursor[index] = 0U;
    }
    ++s_zeroize_count;
}

static void expected_digest(uint8_t output[GAMEPAD_DIAG_ARM_DIGEST_BYTES])
{
    CHECK(fake_sha256((const unsigned char *)TOKEN,
                      GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES, output, 0) == 0);
}

static bool all_zero(const void *buffer, size_t bytes)
{
    const uint8_t *const cursor = buffer;
    uint8_t aggregate = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        aggregate = (uint8_t)(aggregate | cursor[index]);
    }
    return aggregate == 0U;
}

static gamepad_diag_arm_result_t run_attempt(
    const uint8_t *bytes, size_t byte_count, bool expire_wait,
    bool wrong_digest)
{
    gamepad_diag_arm_model_t model;
    gamepad_diag_arm_model_init(&model);
    uint8_t expected[GAMEPAD_DIAG_ARM_DIGEST_BYTES] = {0};
    expected_digest(expected);
    if (wrong_digest) {
        expected[0] ^= UINT8_C(0x80);
    }

    gamepad_diag_arm_result_t result = GAMEPAD_DIAG_ARM_PENDING;
    for (size_t index = 0U; index < byte_count; ++index) {
        result = gamepad_diag_arm_model_feed(&model, bytes[index]);
        if (result == GAMEPAD_DIAG_ARM_FRAME_COMPLETE) {
            result = gamepad_diag_arm_model_authorize(
                &model, expected, fake_sha256, constant_compare,
                tracked_zeroize);
        }
    }
    if (expire_wait && result == GAMEPAD_DIAG_ARM_PENDING) {
        result = gamepad_diag_arm_model_timeout(&model);
    }
    if (result == GAMEPAD_DIAG_ARM_GUARDING) {
        result = gamepad_diag_arm_model_finish_guard(&model);
    }
    if (result == GAMEPAD_DIAG_ARM_ACCEPTED) {
        ++s_usb_call_count;
    }

    gamepad_diag_arm_model_clear(&model, tracked_zeroize);
    CHECK(all_zero(&model, sizeof(model)));
    tracked_zeroize(expected, sizeof(expected));
    CHECK(all_zero(expected, sizeof(expected)));
    return result;
}

static void expect_rejected(const char *name, const uint8_t *bytes,
                            size_t bytes_count, bool expire_wait,
                            bool wrong_digest,
                            gamepad_diag_arm_result_t expected_result)
{
    s_usb_call_count = 0U;
    const gamepad_diag_arm_result_t result =
        run_attempt(bytes, bytes_count, expire_wait, wrong_digest);
    if (result != expected_result) {
        fprintf(stderr, "%s: expected %d, got %d (%s)\n", name,
                (int)expected_result, (int)result,
                gamepad_diag_arm_failure_reason(result));
        exit(EXIT_FAILURE);
    }
    CHECK(s_usb_call_count == 0U);
}

int main(void)
{
    _Static_assert(sizeof(TOKEN) - 1U == GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES,
                   "test token must be exact");
    uint8_t valid[VALID_FRAME_BYTES + 1U] = {0};
    const int valid_bytes = snprintf((char *)valid, sizeof(valid),
                                     "%s%s\n", GAMEPAD_DIAG_ARM_PREFIX,
                                     TOKEN);
    CHECK(valid_bytes == (int)VALID_FRAME_BYTES);

    s_usb_call_count = 0U;
    CHECK(run_attempt(valid, VALID_FRAME_BYTES, false, false) ==
          GAMEPAD_DIAG_ARM_ACCEPTED);
    CHECK(s_usb_call_count == 1U);

    uint8_t carriage_return[VALID_FRAME_BYTES + 1U] = {0};
    memcpy(carriage_return, valid, VALID_FRAME_BYTES - 1U);
    carriage_return[VALID_FRAME_BYTES - 1U] = (uint8_t)'\r';
    carriage_return[VALID_FRAME_BYTES] = (uint8_t)'\n';
    expect_rejected("CR", carriage_return, sizeof(carriage_return), false,
                    false, GAMEPAD_DIAG_ARM_CONTROL_BYTE);

    uint8_t uppercase[VALID_FRAME_BYTES];
    memcpy(uppercase, valid, sizeof(uppercase));
    uppercase[sizeof(GAMEPAD_DIAG_ARM_PREFIX) - 1U] = (uint8_t)'A';
    expect_rejected("uppercase", uppercase, sizeof(uppercase), false, false,
                    GAMEPAD_DIAG_ARM_TOKEN_ENCODING);

    uint8_t short_frame[VALID_FRAME_BYTES - 1U];
    memcpy(short_frame, valid, sizeof(short_frame) - 1U);
    short_frame[sizeof(short_frame) - 1U] = (uint8_t)'\n';
    expect_rejected("short", short_frame, sizeof(short_frame), false, false,
                    GAMEPAD_DIAG_ARM_FRAME);

    uint8_t long_frame[VALID_FRAME_BYTES + 1U];
    memcpy(long_frame, valid, VALID_FRAME_BYTES - 1U);
    long_frame[VALID_FRAME_BYTES - 1U] = (uint8_t)'0';
    long_frame[VALID_FRAME_BYTES] = (uint8_t)'\n';
    expect_rejected("long", long_frame, sizeof(long_frame), false, false,
                    GAMEPAD_DIAG_ARM_OVERFLOW);

    uint8_t extra_before_lf[VALID_FRAME_BYTES + 1U];
    memcpy(extra_before_lf, valid, VALID_FRAME_BYTES - 1U);
    extra_before_lf[VALID_FRAME_BYTES - 1U] = (uint8_t)' ';
    extra_before_lf[VALID_FRAME_BYTES] = (uint8_t)'\n';
    expect_rejected("extra-before-LF", extra_before_lf,
                    sizeof(extra_before_lf), false, false,
                    GAMEPAD_DIAG_ARM_OVERFLOW);

    uint8_t extra_after_lf[VALID_FRAME_BYTES + 1U];
    memcpy(extra_after_lf, valid, VALID_FRAME_BYTES);
    extra_after_lf[VALID_FRAME_BYTES] = (uint8_t)'X';
    expect_rejected("extra-after-LF", extra_after_lf,
                    sizeof(extra_after_lf), false, false,
                    GAMEPAD_DIAG_ARM_DUPLICATE);

    expect_rejected("timeout", NULL, 0U, true, false,
                    GAMEPAD_DIAG_ARM_TIMEOUT);
    expect_rejected("wrong-digest", valid, VALID_FRAME_BYTES, false, true,
                    GAMEPAD_DIAG_ARM_TOKEN_MISMATCH);

    uint8_t wrong_auth[VALID_FRAME_BYTES];
    memcpy(wrong_auth, valid, sizeof(wrong_auth));
    wrong_auth[0] = (uint8_t)'X';
    expect_rejected("wrong-auth", wrong_auth, sizeof(wrong_auth), false,
                    false, GAMEPAD_DIAG_ARM_FRAME);

    CHECK(s_zeroize_count >= 20U);
    puts("gamepad_diag ARM model tests PASS");
    return EXIT_SUCCESS;
}
