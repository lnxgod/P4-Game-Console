#ifndef GAMEPAD_DIAG_ARM_MODEL_H
#define GAMEPAD_DIAG_ARM_MODEL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GAMEPAD_DIAG_AUTHORIZATION_ID                                      \
    "gamepad-diag-d1-one-shot-authorization-2026-08-13"
#define GAMEPAD_DIAG_ARM_PREFIX                                             \
    "P4_GAMEPAD_D1_ARM " GAMEPAD_DIAG_AUTHORIZATION_ID " "

#define GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES 64U
#define GAMEPAD_DIAG_ARM_DIGEST_BYTES 32U
#define GAMEPAD_DIAG_ARM_LINE_BYTES 132U

typedef enum {
    GAMEPAD_DIAG_ARM_PENDING = 0,
    GAMEPAD_DIAG_ARM_FRAME_COMPLETE,
    GAMEPAD_DIAG_ARM_GUARDING,
    GAMEPAD_DIAG_ARM_ACCEPTED,
    GAMEPAD_DIAG_ARM_TIMEOUT,
    GAMEPAD_DIAG_ARM_CONTROL_BYTE,
    GAMEPAD_DIAG_ARM_OVERFLOW,
    GAMEPAD_DIAG_ARM_FRAME,
    GAMEPAD_DIAG_ARM_TOKEN_ENCODING,
    GAMEPAD_DIAG_ARM_DIGEST_CONFIG,
    GAMEPAD_DIAG_ARM_TOKEN_MISMATCH,
    GAMEPAD_DIAG_ARM_CRYPTO,
    GAMEPAD_DIAG_ARM_DUPLICATE,
} gamepad_diag_arm_result_t;

typedef int (*gamepad_diag_arm_sha256_fn)(
    const unsigned char *input, size_t input_bytes,
    unsigned char output[GAMEPAD_DIAG_ARM_DIGEST_BYTES], int is224);
typedef int (*gamepad_diag_arm_compare_fn)(const void *left,
                                           const void *right,
                                           size_t bytes);
typedef void (*gamepad_diag_arm_zeroize_fn)(void *buffer, size_t bytes);

typedef struct {
    uint8_t line[GAMEPAD_DIAG_ARM_LINE_BYTES + 1U];
    size_t used;
    gamepad_diag_arm_result_t result;
} gamepad_diag_arm_model_t;

void gamepad_diag_arm_model_init(gamepad_diag_arm_model_t *model);

gamepad_diag_arm_result_t gamepad_diag_arm_model_feed(
    gamepad_diag_arm_model_t *model, uint8_t byte);

gamepad_diag_arm_result_t gamepad_diag_arm_model_timeout(
    gamepad_diag_arm_model_t *model);

gamepad_diag_arm_result_t gamepad_diag_arm_model_authorize(
    gamepad_diag_arm_model_t *model,
    const uint8_t expected_digest[GAMEPAD_DIAG_ARM_DIGEST_BYTES],
    gamepad_diag_arm_sha256_fn sha256_fn,
    gamepad_diag_arm_compare_fn compare_fn,
    gamepad_diag_arm_zeroize_fn zeroize_fn);

gamepad_diag_arm_result_t gamepad_diag_arm_model_finish_guard(
    gamepad_diag_arm_model_t *model);

void gamepad_diag_arm_model_clear(gamepad_diag_arm_model_t *model,
                                  gamepad_diag_arm_zeroize_fn zeroize_fn);

const char *gamepad_diag_arm_failure_reason(gamepad_diag_arm_result_t result);

#ifdef __cplusplus
}
#endif

#endif
