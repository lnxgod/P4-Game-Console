// SPDX-License-Identifier: MIT

#ifndef P4_PLATFORM_SAVE_SEAL_H
#define P4_PLATFORM_SAVE_SEAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLATFORM_SAVE_SEAL_KEY_BYTES = 32,
    PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES = 32,
};

/**
 * Load or create the single device-local save key.
 *
 * NVS must already be initialized by Console OS. Existing malformed key
 * records fail closed and are never erased or regenerated here.
 */
esp_err_t platform_save_seal_load_key(
    uint8_t key_out[PLATFORM_SAVE_SEAL_KEY_BYTES]);

/** Query the persistent downgrade gate for one validated game/slot. */
esp_err_t platform_save_seal_legacy_is_closed(
    const char *game_id, const char *slot_id, bool *closed_out);

/** Permanently close P4SAVE1 acceptance for one validated game/slot. */
esp_err_t platform_save_seal_close_legacy(
    const char *game_id, const char *slot_id);

/**
 * Check one authenticated P4SAVE2 object against the device-local freshness
 * anchor. A newer sequence is eligible to complete an interrupted commit;
 * an equal sequence must have the exact anchored object digest.
 */
esp_err_t platform_save_seal_object_is_allowed(
    const char *game_id, const char *slot_id, uint32_t sequence,
    const uint8_t
        object_sha256[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES],
    bool *allowed_out);

/** Return the exact anchored sequence, or zero when no anchor exists yet. */
esp_err_t platform_save_seal_object_sequence(
    const char *game_id, const char *slot_id, uint32_t *sequence_out);

/**
 * Advance one slot's freshness anchor after the authenticated object has been
 * durably installed and read back. The anchor never moves backward or to a
 * different object at the same sequence.
 */
esp_err_t platform_save_seal_advance_object(
    const char *game_id, const char *slot_id, uint32_t sequence,
    const uint8_t
        object_sha256[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES]);

/** Volatile wipe for temporary key copies. */
void platform_save_seal_clear(void *data, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
