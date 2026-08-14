#include "gamepad_diag_arm_model.h"

#include <stdbool.h>
#include <string.h>

static const char s_arm_prefix[] = GAMEPAD_DIAG_ARM_PREFIX;

_Static_assert(sizeof(GAMEPAD_DIAG_AUTHORIZATION_ID) - 1U == 49U,
               "D1 authorization identity changed");
_Static_assert(sizeof(s_arm_prefix) - 1U == 68U,
               "D1 host-arm prefix changed");
_Static_assert((sizeof(s_arm_prefix) - 1U) +
                       GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES ==
                   GAMEPAD_DIAG_ARM_LINE_BYTES,
               "D1 host-arm frame identity changed");

static bool is_lower_hex(uint8_t value)
{
    return (value >= (uint8_t)'0' && value <= (uint8_t)'9') ||
           (value >= (uint8_t)'a' && value <= (uint8_t)'f');
}

void gamepad_diag_arm_model_init(gamepad_diag_arm_model_t *model)
{
    if (model != NULL) {
        memset(model, 0, sizeof(*model));
        model->result = GAMEPAD_DIAG_ARM_PENDING;
    }
}

gamepad_diag_arm_result_t gamepad_diag_arm_model_feed(
    gamepad_diag_arm_model_t *model, uint8_t byte)
{
    if (model == NULL) {
        return GAMEPAD_DIAG_ARM_FRAME;
    }
    if (model->result == GAMEPAD_DIAG_ARM_FRAME_COMPLETE ||
        model->result == GAMEPAD_DIAG_ARM_GUARDING) {
        model->result = GAMEPAD_DIAG_ARM_DUPLICATE;
        return model->result;
    }
    if (model->result != GAMEPAD_DIAG_ARM_PENDING) {
        return model->result;
    }
    if (byte == (uint8_t)'\n') {
        if (model->used != GAMEPAD_DIAG_ARM_LINE_BYTES ||
            memcmp(model->line, s_arm_prefix,
                   sizeof(s_arm_prefix) - 1U) != 0) {
            model->result = GAMEPAD_DIAG_ARM_FRAME;
            return model->result;
        }
        const size_t token_offset = sizeof(s_arm_prefix) - 1U;
        for (size_t index = 0U;
             index < GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES; ++index) {
            if (!is_lower_hex(model->line[token_offset + index])) {
                model->result = GAMEPAD_DIAG_ARM_TOKEN_ENCODING;
                return model->result;
            }
        }
        model->result = GAMEPAD_DIAG_ARM_FRAME_COMPLETE;
        return model->result;
    }
    if (byte < 0x20U || byte > 0x7eU) {
        model->result = GAMEPAD_DIAG_ARM_CONTROL_BYTE;
        return model->result;
    }
    if (model->used >= GAMEPAD_DIAG_ARM_LINE_BYTES) {
        model->result = GAMEPAD_DIAG_ARM_OVERFLOW;
        return model->result;
    }
    model->line[model->used++] = byte;
    return model->result;
}

gamepad_diag_arm_result_t gamepad_diag_arm_model_timeout(
    gamepad_diag_arm_model_t *model)
{
    if (model == NULL) {
        return GAMEPAD_DIAG_ARM_FRAME;
    }
    if (model->result == GAMEPAD_DIAG_ARM_PENDING) {
        model->result = GAMEPAD_DIAG_ARM_TIMEOUT;
    }
    return model->result;
}

gamepad_diag_arm_result_t gamepad_diag_arm_model_authorize(
    gamepad_diag_arm_model_t *model,
    const uint8_t expected_digest[GAMEPAD_DIAG_ARM_DIGEST_BYTES],
    gamepad_diag_arm_sha256_fn sha256_fn,
    gamepad_diag_arm_compare_fn compare_fn,
    gamepad_diag_arm_zeroize_fn zeroize_fn)
{
    uint8_t actual_digest[GAMEPAD_DIAG_ARM_DIGEST_BYTES] = {0};
    if (model == NULL || expected_digest == NULL || sha256_fn == NULL ||
        compare_fn == NULL || zeroize_fn == NULL ||
        model->result != GAMEPAD_DIAG_ARM_FRAME_COMPLETE) {
        if (model != NULL) {
            model->result = GAMEPAD_DIAG_ARM_CRYPTO;
        }
        return GAMEPAD_DIAG_ARM_CRYPTO;
    }

    const size_t token_offset = sizeof(s_arm_prefix) - 1U;
    if (sha256_fn(model->line + token_offset,
                  GAMEPAD_DIAG_ARM_TOKEN_HEX_BYTES,
                  actual_digest, 0) != 0) {
        model->result = GAMEPAD_DIAG_ARM_CRYPTO;
    } else if (compare_fn(actual_digest, expected_digest,
                          GAMEPAD_DIAG_ARM_DIGEST_BYTES) != 0) {
        model->result = GAMEPAD_DIAG_ARM_TOKEN_MISMATCH;
    } else {
        model->result = GAMEPAD_DIAG_ARM_GUARDING;
    }
    zeroize_fn(actual_digest, sizeof(actual_digest));
    return model->result;
}

gamepad_diag_arm_result_t gamepad_diag_arm_model_finish_guard(
    gamepad_diag_arm_model_t *model)
{
    if (model == NULL || model->result != GAMEPAD_DIAG_ARM_GUARDING) {
        return model != NULL ? model->result : GAMEPAD_DIAG_ARM_FRAME;
    }
    model->result = GAMEPAD_DIAG_ARM_ACCEPTED;
    return model->result;
}

void gamepad_diag_arm_model_clear(gamepad_diag_arm_model_t *model,
                                  gamepad_diag_arm_zeroize_fn zeroize_fn)
{
    if (model != NULL && zeroize_fn != NULL) {
        zeroize_fn(model, sizeof(*model));
    }
}

const char *gamepad_diag_arm_failure_reason(gamepad_diag_arm_result_t result)
{
    switch (result) {
    case GAMEPAD_DIAG_ARM_TIMEOUT:
        return "arm-timeout";
    case GAMEPAD_DIAG_ARM_CONTROL_BYTE:
        return "arm-control-byte";
    case GAMEPAD_DIAG_ARM_OVERFLOW:
        return "arm-overflow";
    case GAMEPAD_DIAG_ARM_FRAME:
        return "arm-frame";
    case GAMEPAD_DIAG_ARM_TOKEN_ENCODING:
        return "arm-token-encoding";
    case GAMEPAD_DIAG_ARM_DIGEST_CONFIG:
        return "arm-digest-config";
    case GAMEPAD_DIAG_ARM_TOKEN_MISMATCH:
        return "arm-token-mismatch";
    case GAMEPAD_DIAG_ARM_CRYPTO:
        return "arm-crypto";
    case GAMEPAD_DIAG_ARM_DUPLICATE:
        return "arm-duplicate";
    case GAMEPAD_DIAG_ARM_PENDING:
    case GAMEPAD_DIAG_ARM_FRAME_COMPLETE:
    case GAMEPAD_DIAG_ARM_GUARDING:
    case GAMEPAD_DIAG_ARM_ACCEPTED:
    default:
        return "arm-internal";
    }
}
