// SPDX-License-Identifier: MIT

#include "platform/save_seal.h"

#include <stdbool.h>
#include <string.h>

#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "p4/game_save.h"

static const char SAVE_SEAL_NAMESPACE[] = "p4_save_seal";
static const char SAVE_SEAL_KEY[] = "master_v1";
static const char LEGACY_REGISTRY_KEY[] = "legacy_v1";
static const uint8_t LEGACY_MARKER_DOMAIN[] =
    "P4SAVE2-LEGACY-CLOSED";

enum {
    LEGACY_REGISTRY_MAGIC = 0,
    LEGACY_REGISTRY_BYTES_FIELD = 8,
    LEGACY_REGISTRY_VERSION = 12,
    LEGACY_REGISTRY_COUNT = 16,
    LEGACY_REGISTRY_RESERVED = 20,
    LEGACY_REGISTRY_ENTRIES = 24,
    LEGACY_MARKER_DIGEST_BYTES = 32,
    LEGACY_REGISTRY_ENTRY_DIGEST = 0,
    LEGACY_REGISTRY_ENTRY_FLAGS = 32,
    LEGACY_REGISTRY_ENTRY_BYTES = 36,
    LEGACY_MARKER_MAX_ENTRIES = 32,
    LEGACY_REGISTRY_BYTES = LEGACY_REGISTRY_ENTRIES +
        LEGACY_MARKER_MAX_ENTRIES * LEGACY_REGISTRY_ENTRY_BYTES,
    LEGACY_REGISTRY_FORMAT_VERSION = 2,
    LEGACY_ENTRY_CLOSED = 1,
    LEGACY_ENTRY_ANCHOR_EXPECTED = 2,
    LEGACY_ENTRY_BASELINE_REQUIRED = 4,
    LEGACY_ENTRY_KNOWN_FLAGS = LEGACY_ENTRY_CLOSED |
        LEGACY_ENTRY_ANCHOR_EXPECTED | LEGACY_ENTRY_BASELINE_REQUIRED,

    LEGACY_V1_REGISTRY_ENTRIES = 24,
    LEGACY_V1_ENTRY_BYTES = 32,
    LEGACY_V1_REGISTRY_BYTES = LEGACY_V1_REGISTRY_ENTRIES +
        LEGACY_MARKER_MAX_ENTRIES * LEGACY_V1_ENTRY_BYTES,

    ANCHOR_MAGIC = 0,
    ANCHOR_BYTES_FIELD = 8,
    ANCHOR_VERSION = 12,
    ANCHOR_SEQUENCE = 16,
    ANCHOR_RESERVED = 20,
    ANCHOR_SLOT_DIGEST = 24,
    ANCHOR_OBJECT_SHA256 = 56,
    ANCHOR_BYTES = 88,
    ANCHOR_FORMAT_VERSION = 1,
    ANCHOR_KEY_DIGEST_BYTES = 7,
    ANCHOR_KEY_BYTES = 16,
};

static const uint8_t s_legacy_registry_magic[8] = {
    'P', '4', 'L', 'M', 'R', 'K', '2', 0,
};
static const uint8_t s_legacy_v1_registry_magic[8] = {
    'P', '4', 'L', 'M', 'R', 'K', '1', 0,
};
static const uint8_t s_anchor_magic[8] = {
    'P', '4', 'H', 'W', 'A', 'T', '1', 0,
};

_Static_assert(sizeof(SAVE_SEAL_NAMESPACE) <= 16U,
               "NVS namespace exceeds 15 characters");
_Static_assert(sizeof(SAVE_SEAL_KEY) <= 16U,
               "NVS key exceeds 15 characters");
_Static_assert(sizeof(LEGACY_REGISTRY_KEY) <= 16U,
               "NVS key exceeds 15 characters");
_Static_assert(ANCHOR_KEY_BYTES == 16U,
               "NVS anchor key must be 15 characters plus NUL");

static bool all_zero(const uint8_t *data, size_t bytes)
{
    uint8_t combined = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        combined |= data[index];
    }
    return combined == 0U;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right,
                                size_t bytes)
{
    volatile uint8_t difference = 0U;
    for (size_t index = 0U; index < bytes; ++index) {
        difference = (uint8_t)(difference |
            (uint8_t)(left[index] ^ right[index]));
    }
    return difference == 0U;
}

void platform_save_seal_clear(void *data, size_t bytes)
{
    volatile uint8_t *const output = data;
    if (output == NULL) {
        return;
    }
    for (size_t index = 0U; index < bytes; ++index) {
        output[index] = 0U;
    }
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
}

static void initialize_legacy_registry(
    uint8_t registry[LEGACY_REGISTRY_BYTES])
{
    memset(registry, 0, LEGACY_REGISTRY_BYTES);
    memcpy(registry + LEGACY_REGISTRY_MAGIC, s_legacy_registry_magic,
           sizeof(s_legacy_registry_magic));
    write_u32(registry + LEGACY_REGISTRY_BYTES_FIELD,
              LEGACY_REGISTRY_BYTES);
    write_u32(registry + LEGACY_REGISTRY_VERSION,
              LEGACY_REGISTRY_FORMAT_VERSION);
}

static bool legacy_registry_valid(
    const uint8_t registry[LEGACY_REGISTRY_BYTES], uint32_t *count_out)
{
    if (count_out == NULL ||
        memcmp(registry + LEGACY_REGISTRY_MAGIC, s_legacy_registry_magic,
               sizeof(s_legacy_registry_magic)) != 0 ||
        read_u32(registry + LEGACY_REGISTRY_BYTES_FIELD) !=
            LEGACY_REGISTRY_BYTES ||
        read_u32(registry + LEGACY_REGISTRY_VERSION) !=
            LEGACY_REGISTRY_FORMAT_VERSION ||
        read_u32(registry + LEGACY_REGISTRY_RESERVED) != 0U) {
        return false;
    }
    const uint32_t count = read_u32(registry + LEGACY_REGISTRY_COUNT);
    if (count > LEGACY_MARKER_MAX_ENTRIES) {
        return false;
    }
    for (uint32_t left = 0U; left < count; ++left) {
        const uint8_t *const left_entry = registry +
            LEGACY_REGISTRY_ENTRIES +
            (size_t)left * LEGACY_REGISTRY_ENTRY_BYTES;
        const uint8_t *const left_digest = left_entry +
            LEGACY_REGISTRY_ENTRY_DIGEST;
        const uint32_t flags = read_u32(
            left_entry + LEGACY_REGISTRY_ENTRY_FLAGS);
        if (all_zero(left_digest, LEGACY_MARKER_DIGEST_BYTES)) {
            return false;
        }
        if ((flags & LEGACY_ENTRY_CLOSED) == 0U ||
            (flags & ~(uint32_t)LEGACY_ENTRY_KNOWN_FLAGS) != 0U ||
            (flags & (uint32_t)(LEGACY_ENTRY_ANCHOR_EXPECTED |
                                LEGACY_ENTRY_BASELINE_REQUIRED)) ==
                (uint32_t)(LEGACY_ENTRY_ANCHOR_EXPECTED |
                           LEGACY_ENTRY_BASELINE_REQUIRED)) {
            return false;
        }
        for (uint32_t right = left + 1U; right < count; ++right) {
            const uint8_t *const right_digest = registry +
                LEGACY_REGISTRY_ENTRIES +
                (size_t)right * LEGACY_REGISTRY_ENTRY_BYTES +
                LEGACY_REGISTRY_ENTRY_DIGEST;
            if (constant_time_equal(
                    left_digest, right_digest,
                    LEGACY_MARKER_DIGEST_BYTES)) {
                return false;
            }
        }
    }
    const size_t used = LEGACY_REGISTRY_ENTRIES +
        (size_t)count * LEGACY_REGISTRY_ENTRY_BYTES;
    if (!all_zero(registry + used, LEGACY_REGISTRY_BYTES - used)) {
        return false;
    }
    *count_out = count;
    return true;
}

static bool legacy_v1_registry_valid(const uint8_t *registry, size_t bytes,
                                     uint32_t *count_out)
{
    if (registry == NULL || count_out == NULL ||
        bytes != LEGACY_V1_REGISTRY_BYTES ||
        memcmp(registry + LEGACY_REGISTRY_MAGIC,
               s_legacy_v1_registry_magic,
               sizeof(s_legacy_v1_registry_magic)) != 0 ||
        read_u32(registry + LEGACY_REGISTRY_BYTES_FIELD) !=
            LEGACY_V1_REGISTRY_BYTES ||
        read_u32(registry + LEGACY_REGISTRY_VERSION) != 1U ||
        read_u32(registry + LEGACY_REGISTRY_RESERVED) != 0U) {
        return false;
    }
    const uint32_t count = read_u32(registry + LEGACY_REGISTRY_COUNT);
    if (count > LEGACY_MARKER_MAX_ENTRIES) {
        return false;
    }
    for (uint32_t left = 0U; left < count; ++left) {
        const uint8_t *const left_digest = registry +
            LEGACY_V1_REGISTRY_ENTRIES +
            (size_t)left * LEGACY_V1_ENTRY_BYTES;
        if (all_zero(left_digest, LEGACY_MARKER_DIGEST_BYTES)) {
            return false;
        }
        for (uint32_t right = left + 1U; right < count; ++right) {
            const uint8_t *const right_digest = registry +
                LEGACY_V1_REGISTRY_ENTRIES +
                (size_t)right * LEGACY_V1_ENTRY_BYTES;
            if (constant_time_equal(
                    left_digest, right_digest,
                    LEGACY_MARKER_DIGEST_BYTES)) {
                return false;
            }
        }
    }
    const size_t used = LEGACY_V1_REGISTRY_ENTRIES +
        (size_t)count * LEGACY_V1_ENTRY_BYTES;
    if (!all_zero(registry + used, bytes - used)) {
        return false;
    }
    *count_out = count;
    return true;
}

static void migrate_legacy_v1_registry(
    const uint8_t old_registry[LEGACY_V1_REGISTRY_BYTES], uint32_t count,
    uint8_t registry[LEGACY_REGISTRY_BYTES])
{
    initialize_legacy_registry(registry);
    write_u32(registry + LEGACY_REGISTRY_COUNT, count);
    for (uint32_t index = 0U; index < count; ++index) {
        uint8_t *const entry = registry + LEGACY_REGISTRY_ENTRIES +
            (size_t)index * LEGACY_REGISTRY_ENTRY_BYTES;
        memcpy(entry + LEGACY_REGISTRY_ENTRY_DIGEST,
               old_registry + LEGACY_V1_REGISTRY_ENTRIES +
                   (size_t)index * LEGACY_V1_ENTRY_BYTES,
               LEGACY_MARKER_DIGEST_BYTES);
        write_u32(entry + LEGACY_REGISTRY_ENTRY_FLAGS,
                  LEGACY_ENTRY_CLOSED |
                  LEGACY_ENTRY_BASELINE_REQUIRED);
    }
}

static esp_err_t read_legacy_registry(
    nvs_handle_t handle,
    uint8_t registry[LEGACY_REGISTRY_BYTES],
    uint32_t *count_out)
{
    size_t bytes = LEGACY_REGISTRY_BYTES;
    esp_err_t result = nvs_get_blob(
        handle, LEGACY_REGISTRY_KEY, registry, &bytes);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        /* master_v1 and legacy_v1 are created as one logical state.  Once
         * initialization has happened, losing either half must fail closed;
         * treating a missing registry as empty would reopen every P4SAVE1
         * downgrade window. */
        return ESP_ERR_INVALID_STATE;
    }
    if (result != ESP_OK) {
        return result;
    }
    if (bytes != LEGACY_REGISTRY_BYTES ||
        !legacy_registry_valid(registry, count_out)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t derive_legacy_marker(
    const char *game_id,
    const char *slot_id,
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES])
{
    if (!p4_game_save_game_id_valid(game_id) ||
        !p4_game_save_slot_id_valid(slot_id) || digest == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t material[
        sizeof(LEGACY_MARKER_DOMAIN) + P4_GAME_ID_MAX_BYTES +
        P4_GAME_SAVE_SLOT_ID_BYTES] = {0};
    size_t used = 0U;
    memcpy(material + used, LEGACY_MARKER_DOMAIN,
           sizeof(LEGACY_MARKER_DOMAIN));
    used += sizeof(LEGACY_MARKER_DOMAIN);
    const size_t game_bytes = strlen(game_id) + 1U;
    memcpy(material + used, game_id, game_bytes);
    used += game_bytes;
    const size_t slot_bytes = strlen(slot_id) + 1U;
    memcpy(material + used, slot_id, slot_bytes);
    used += slot_bytes;
    const int crypto = mbedtls_sha256(material, used, digest, 0);
    platform_save_seal_clear(material, sizeof(material));
    return crypto == 0 ? ESP_OK : ESP_FAIL;
}

static int32_t legacy_registry_find(
    const uint8_t registry[LEGACY_REGISTRY_BYTES],
    uint32_t count,
    const uint8_t digest[LEGACY_MARKER_DIGEST_BYTES])
{
    for (uint32_t index = 0U; index < count; ++index) {
        const uint8_t *const candidate = registry +
            LEGACY_REGISTRY_ENTRIES +
            (size_t)index * LEGACY_REGISTRY_ENTRY_BYTES +
            LEGACY_REGISTRY_ENTRY_DIGEST;
        if (constant_time_equal(
                candidate, digest, LEGACY_MARKER_DIGEST_BYTES)) {
            return (int32_t)index;
        }
    }
    return -1;
}

static uint32_t legacy_registry_flags(
    const uint8_t registry[LEGACY_REGISTRY_BYTES], uint32_t index)
{
    return read_u32(registry + LEGACY_REGISTRY_ENTRIES +
        (size_t)index * LEGACY_REGISTRY_ENTRY_BYTES +
        LEGACY_REGISTRY_ENTRY_FLAGS);
}

static uint8_t *legacy_registry_entry(
    uint8_t registry[LEGACY_REGISTRY_BYTES], uint32_t index)
{
    return registry + LEGACY_REGISTRY_ENTRIES +
        (size_t)index * LEGACY_REGISTRY_ENTRY_BYTES;
}

static esp_err_t ensure_registry_entry(
    uint8_t registry[LEGACY_REGISTRY_BYTES], uint32_t *count,
    const uint8_t digest[LEGACY_MARKER_DIGEST_BYTES], uint32_t flags,
    uint32_t *index_out, bool *changed_out)
{
    if (count == NULL || index_out == NULL || changed_out == NULL ||
        (flags & ~(uint32_t)LEGACY_ENTRY_KNOWN_FLAGS) != 0U ||
        (flags & LEGACY_ENTRY_CLOSED) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    int32_t found = legacy_registry_find(registry, *count, digest);
    if (found < 0) {
        if (*count >= LEGACY_MARKER_MAX_ENTRIES) {
            return ESP_ERR_NO_MEM;
        }
        found = (int32_t)*count;
        ++*count;
        uint8_t *const entry = legacy_registry_entry(
            registry, (uint32_t)found);
        memcpy(entry + LEGACY_REGISTRY_ENTRY_DIGEST, digest,
               LEGACY_MARKER_DIGEST_BYTES);
        write_u32(entry + LEGACY_REGISTRY_ENTRY_FLAGS, flags);
        write_u32(registry + LEGACY_REGISTRY_COUNT, *count);
        *changed_out = true;
    } else {
        uint8_t *const entry = legacy_registry_entry(
            registry, (uint32_t)found);
        const uint32_t old_flags = read_u32(
            entry + LEGACY_REGISTRY_ENTRY_FLAGS);
        if ((old_flags | flags) != old_flags) {
            write_u32(entry + LEGACY_REGISTRY_ENTRY_FLAGS,
                      old_flags | flags);
            *changed_out = true;
        }
    }
    *index_out = (uint32_t)found;
    return ESP_OK;
}

static void anchor_key_for_digest(
    const uint8_t digest[LEGACY_MARKER_DIGEST_BYTES],
    char key[ANCHOR_KEY_BYTES])
{
    static const char hex[] = "0123456789abcdef";
    key[0] = 'h';
    for (size_t index = 0U; index < ANCHOR_KEY_DIGEST_BYTES; ++index) {
        key[1U + index * 2U] = hex[digest[index] >> 4U];
        key[2U + index * 2U] = hex[digest[index] & 0x0fU];
    }
    key[ANCHOR_KEY_BYTES - 1U] = '\0';
}

static void encode_anchor(
    uint8_t anchor[ANCHOR_BYTES],
    const uint8_t slot_digest[LEGACY_MARKER_DIGEST_BYTES],
    uint32_t sequence,
    const uint8_t object_sha256[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES])
{
    memset(anchor, 0, ANCHOR_BYTES);
    memcpy(anchor + ANCHOR_MAGIC, s_anchor_magic, sizeof(s_anchor_magic));
    write_u32(anchor + ANCHOR_BYTES_FIELD, ANCHOR_BYTES);
    write_u32(anchor + ANCHOR_VERSION, ANCHOR_FORMAT_VERSION);
    write_u32(anchor + ANCHOR_SEQUENCE, sequence);
    memcpy(anchor + ANCHOR_SLOT_DIGEST, slot_digest,
           LEGACY_MARKER_DIGEST_BYTES);
    memcpy(anchor + ANCHOR_OBJECT_SHA256, object_sha256,
           PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES);
}

static esp_err_t read_anchor(
    nvs_handle_t handle, const char *key,
    const uint8_t slot_digest[LEGACY_MARKER_DIGEST_BYTES],
    bool *present_out, uint32_t *sequence_out,
    uint8_t object_sha256_out[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES])
{
    if (key == NULL || present_out == NULL || sequence_out == NULL ||
        object_sha256_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *present_out = false;
    *sequence_out = 0U;
    memset(object_sha256_out, 0,
           PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES);
    uint8_t anchor[ANCHOR_BYTES] = {0};
    size_t bytes = sizeof(anchor);
    esp_err_t result = nvs_get_blob(handle, key, anchor, &bytes);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (result != ESP_OK) {
        return result;
    }
    const uint32_t sequence = read_u32(anchor + ANCHOR_SEQUENCE);
    const bool sentinel = sequence == 0U;
    if (bytes != sizeof(anchor) ||
        memcmp(anchor + ANCHOR_MAGIC, s_anchor_magic,
               sizeof(s_anchor_magic)) != 0 ||
        read_u32(anchor + ANCHOR_BYTES_FIELD) != ANCHOR_BYTES ||
        read_u32(anchor + ANCHOR_VERSION) != ANCHOR_FORMAT_VERSION ||
        read_u32(anchor + ANCHOR_RESERVED) != 0U ||
        !constant_time_equal(anchor + ANCHOR_SLOT_DIGEST, slot_digest,
                             LEGACY_MARKER_DIGEST_BYTES) ||
        (sentinel != all_zero(anchor + ANCHOR_OBJECT_SHA256,
                              PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES))) {
        platform_save_seal_clear(anchor, sizeof(anchor));
        return ESP_ERR_INVALID_STATE;
    }
    *present_out = true;
    *sequence_out = sequence;
    memcpy(object_sha256_out, anchor + ANCHOR_OBJECT_SHA256,
           PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES);
    platform_save_seal_clear(anchor, sizeof(anchor));
    return ESP_OK;
}

esp_err_t platform_save_seal_legacy_is_closed(
    const char *game_id, const char *slot_id, bool *closed_out)
{
    if (closed_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *closed_out = false;
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES];
    esp_err_t result = derive_legacy_marker(game_id, slot_id, digest);
    if (result != ESP_OK) {
        return result;
    }
    nvs_handle_t handle;
    result = nvs_open(SAVE_SEAL_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t registry[LEGACY_REGISTRY_BYTES];
    uint32_t count = 0U;
    result = read_legacy_registry(handle, registry, &count);
    if (result == ESP_OK) {
        *closed_out = legacy_registry_find(registry, count, digest) >= 0;
    }
    nvs_close(handle);
    return result;
}

esp_err_t platform_save_seal_close_legacy(
    const char *game_id, const char *slot_id)
{
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES];
    esp_err_t result = derive_legacy_marker(game_id, slot_id, digest);
    if (result != ESP_OK) {
        return result;
    }
    nvs_handle_t handle;
    result = nvs_open(SAVE_SEAL_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t registry[LEGACY_REGISTRY_BYTES];
    uint32_t count = 0U;
    result = read_legacy_registry(handle, registry, &count);
    bool registry_changed = false;
    uint32_t entry_index = 0U;
    if (result == ESP_OK) {
        result = ensure_registry_entry(
            registry, &count, digest, LEGACY_ENTRY_CLOSED,
            &entry_index, &registry_changed);
    }
    if (result == ESP_OK && registry_changed) {
        result = nvs_set_blob(
            handle, LEGACY_REGISTRY_KEY, registry, sizeof(registry));
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        if (result == ESP_OK) {
            memset(registry, 0, sizeof(registry));
            uint32_t readback_count = 0U;
            result = read_legacy_registry(
                handle, registry, &readback_count);
            const int32_t readback_index = result == ESP_OK
                ? legacy_registry_find(
                    registry, readback_count, digest) : -1;
            if (result == ESP_OK &&
                (readback_index < 0 ||
                 (legacy_registry_flags(
                     registry, (uint32_t)readback_index) &
                  LEGACY_ENTRY_CLOSED) == 0U)) {
                result = ESP_ERR_INVALID_STATE;
            }
        }
    }
    nvs_close(handle);
    return result;
}

esp_err_t platform_save_seal_object_is_allowed(
    const char *game_id, const char *slot_id, uint32_t sequence,
    const uint8_t
        object_sha256[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES],
    bool *allowed_out)
{
    if (allowed_out == NULL || sequence == 0U || object_sha256 == NULL ||
        all_zero(object_sha256,
                 PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES)) {
        return ESP_ERR_INVALID_ARG;
    }
    *allowed_out = false;
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES];
    esp_err_t result = derive_legacy_marker(game_id, slot_id, digest);
    if (result != ESP_OK) {
        return result;
    }
    char anchor_key[ANCHOR_KEY_BYTES];
    anchor_key_for_digest(digest, anchor_key);
    nvs_handle_t handle;
    result = nvs_open(SAVE_SEAL_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t registry[LEGACY_REGISTRY_BYTES];
    uint32_t count = 0U;
    result = read_legacy_registry(handle, registry, &count);
    const int32_t entry_index = result == ESP_OK
        ? legacy_registry_find(registry, count, digest) : -1;
    const bool anchor_expected = entry_index >= 0 &&
        (legacy_registry_flags(registry, (uint32_t)entry_index) &
         LEGACY_ENTRY_ANCHOR_EXPECTED) != 0U;
    bool anchor_present = false;
    uint32_t anchor_sequence = 0U;
    uint8_t anchor_sha[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    if (result == ESP_OK) {
        result = read_anchor(
            handle, anchor_key, digest, &anchor_present,
            &anchor_sequence, anchor_sha);
    }
    if (result == ESP_OK && anchor_present != anchor_expected) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK && !anchor_present) {
        /* Explicit baseline migration: a slot with no expected anchor may
         * accept its first authenticated P4SAVE2 object once. The caller then
         * advances the anchor before returning the payload. */
        *allowed_out = true;
    } else if (result == ESP_OK) {
        *allowed_out = sequence > anchor_sequence ||
            (sequence == anchor_sequence &&
             constant_time_equal(
                 object_sha256, anchor_sha,
                 PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES));
    }
    platform_save_seal_clear(anchor_sha, sizeof(anchor_sha));
    platform_save_seal_clear(registry, sizeof(registry));
    platform_save_seal_clear(digest, sizeof(digest));
    nvs_close(handle);
    return result;
}

esp_err_t platform_save_seal_object_sequence(
    const char *game_id, const char *slot_id, uint32_t *sequence_out)
{
    if (sequence_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *sequence_out = 0U;
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES];
    esp_err_t result = derive_legacy_marker(game_id, slot_id, digest);
    if (result != ESP_OK) {
        return result;
    }
    char anchor_key[ANCHOR_KEY_BYTES];
    anchor_key_for_digest(digest, anchor_key);
    nvs_handle_t handle;
    result = nvs_open(SAVE_SEAL_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t registry[LEGACY_REGISTRY_BYTES];
    uint32_t count = 0U;
    result = read_legacy_registry(handle, registry, &count);
    const int32_t entry_index = result == ESP_OK
        ? legacy_registry_find(registry, count, digest) : -1;
    const bool anchor_expected = entry_index >= 0 &&
        (legacy_registry_flags(registry, (uint32_t)entry_index) &
         LEGACY_ENTRY_ANCHOR_EXPECTED) != 0U;
    const bool baseline_required = entry_index >= 0 &&
        (legacy_registry_flags(registry, (uint32_t)entry_index) &
         LEGACY_ENTRY_BASELINE_REQUIRED) != 0U;
    bool anchor_present = false;
    uint32_t anchor_sequence = 0U;
    uint8_t anchor_sha[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    if (result == ESP_OK) {
        result = read_anchor(
            handle, anchor_key, digest, &anchor_present,
            &anchor_sequence, anchor_sha);
    }
    if (result == ESP_OK && anchor_present != anchor_expected) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK && !anchor_present && baseline_required) {
        /* A migrated P4LMRK1 entry proves that this slot was previously
         * sealed, but the old registry did not retain its sequence. Require
         * the existing authenticated object to establish the baseline; an
         * empty/replaced SD must not silently restart it at sequence zero. */
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK && anchor_present) {
        *sequence_out = anchor_sequence;
    }
    platform_save_seal_clear(anchor_sha, sizeof(anchor_sha));
    platform_save_seal_clear(registry, sizeof(registry));
    platform_save_seal_clear(digest, sizeof(digest));
    nvs_close(handle);
    return result;
}

esp_err_t platform_save_seal_advance_object(
    const char *game_id, const char *slot_id, uint32_t sequence,
    const uint8_t
        object_sha256[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES])
{
    if (sequence == 0U || object_sha256 == NULL ||
        all_zero(object_sha256,
                 PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES)) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t digest[LEGACY_MARKER_DIGEST_BYTES];
    esp_err_t result = derive_legacy_marker(game_id, slot_id, digest);
    if (result != ESP_OK) {
        return result;
    }
    char anchor_key[ANCHOR_KEY_BYTES];
    anchor_key_for_digest(digest, anchor_key);
    nvs_handle_t handle;
    result = nvs_open(SAVE_SEAL_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    uint8_t registry[LEGACY_REGISTRY_BYTES];
    uint32_t count = 0U;
    result = read_legacy_registry(handle, registry, &count);
    int32_t entry_index = result == ESP_OK
        ? legacy_registry_find(registry, count, digest) : -1;
    const bool anchor_expected = entry_index >= 0 &&
        (legacy_registry_flags(registry, (uint32_t)entry_index) &
         LEGACY_ENTRY_ANCHOR_EXPECTED) != 0U;
    bool anchor_present = false;
    uint32_t anchor_sequence = 0U;
    uint8_t anchor_sha[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
    if (result == ESP_OK) {
        result = read_anchor(
            handle, anchor_key, digest, &anchor_present,
            &anchor_sequence, anchor_sha);
    }
    if (result == ESP_OK && anchor_present != anchor_expected) {
        result = ESP_ERR_INVALID_STATE;
    }
    bool registry_changed = false;
    if (result == ESP_OK && anchor_present) {
        if (sequence < anchor_sequence ||
            (sequence == anchor_sequence &&
             !constant_time_equal(
                 object_sha256, anchor_sha,
                 PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES))) {
            result = ESP_ERR_INVALID_STATE;
        }
    }
    uint32_t ensured_index = 0U;
    if (result == ESP_OK) {
        result = ensure_registry_entry(
            registry, &count, digest,
            LEGACY_ENTRY_CLOSED | LEGACY_ENTRY_ANCHOR_EXPECTED,
            &ensured_index, &registry_changed);
        entry_index = result == ESP_OK ? (int32_t)ensured_index : -1;
    }
    if (result == ESP_OK && entry_index >= 0) {
        uint8_t *const entry = legacy_registry_entry(
            registry, (uint32_t)entry_index);
        const uint32_t flags = read_u32(
            entry + LEGACY_REGISTRY_ENTRY_FLAGS);
        if ((flags & LEGACY_ENTRY_BASELINE_REQUIRED) != 0U) {
            write_u32(entry + LEGACY_REGISTRY_ENTRY_FLAGS,
                      flags & ~(uint32_t)LEGACY_ENTRY_BASELINE_REQUIRED);
            registry_changed = true;
        }
    }
    uint8_t anchor[ANCHOR_BYTES];
    const bool advance_anchor = result == ESP_OK &&
        (!anchor_present || sequence > anchor_sequence);
    bool anchor_changed = false;
    if (advance_anchor) {
        encode_anchor(anchor, digest, sequence, object_sha256);
        result = nvs_set_blob(handle, anchor_key, anchor, sizeof(anchor));
        anchor_changed = result == ESP_OK;
    }
    if (result == ESP_OK && registry_changed) {
        result = nvs_set_blob(
            handle, LEGACY_REGISTRY_KEY, registry, sizeof(registry));
    }
    if (result == ESP_OK && (registry_changed || anchor_changed)) {
        result = nvs_commit(handle);
    }
    if (result == ESP_OK) {
        memset(registry, 0, sizeof(registry));
        uint32_t readback_count = 0U;
        result = read_legacy_registry(handle, registry, &readback_count);
        entry_index = result == ESP_OK
            ? legacy_registry_find(registry, readback_count, digest) : -1;
        bool readback_present = false;
        uint32_t readback_sequence = 0U;
        uint8_t readback_sha[PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES];
        if (result == ESP_OK) {
            result = read_anchor(
                handle, anchor_key, digest, &readback_present,
                &readback_sequence, readback_sha);
        }
        if (result == ESP_OK &&
            (entry_index < 0 ||
             (legacy_registry_flags(
                  registry, (uint32_t)entry_index) &
              (LEGACY_ENTRY_CLOSED | LEGACY_ENTRY_ANCHOR_EXPECTED)) !=
                 (LEGACY_ENTRY_CLOSED | LEGACY_ENTRY_ANCHOR_EXPECTED) ||
             !readback_present || readback_sequence != sequence ||
             !constant_time_equal(
                 readback_sha, object_sha256,
                 PLATFORM_SAVE_SEAL_OBJECT_SHA256_BYTES))) {
            result = ESP_ERR_INVALID_STATE;
        }
        platform_save_seal_clear(readback_sha, sizeof(readback_sha));
    }
    platform_save_seal_clear(anchor, sizeof(anchor));
    platform_save_seal_clear(anchor_sha, sizeof(anchor_sha));
    platform_save_seal_clear(registry, sizeof(registry));
    platform_save_seal_clear(digest, sizeof(digest));
    nvs_close(handle);
    return result;
}

esp_err_t platform_save_seal_load_key(
    uint8_t key_out[PLATFORM_SAVE_SEAL_KEY_BYTES])
{
    if (key_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(key_out, 0, PLATFORM_SAVE_SEAL_KEY_BYTES);

    nvs_handle_t handle;
    esp_err_t result = nvs_open(
        SAVE_SEAL_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    uint8_t candidate[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
    uint8_t registry[LEGACY_REGISTRY_BYTES] = {0};
    size_t stored_bytes = sizeof(candidate);
    const esp_err_t key_result = nvs_get_blob(
        handle, SAVE_SEAL_KEY, candidate, &stored_bytes);
    size_t registry_bytes = sizeof(registry);
    const esp_err_t registry_result = nvs_get_blob(
        handle, LEGACY_REGISTRY_KEY, registry, &registry_bytes);
    const bool key_missing = key_result == ESP_ERR_NVS_NOT_FOUND;
    const bool registry_missing =
        registry_result == ESP_ERR_NVS_NOT_FOUND;
    if ((key_result != ESP_OK && !key_missing) ||
        (registry_result != ESP_OK && !registry_missing)) {
        result = key_result != ESP_OK && !key_missing
            ? key_result : registry_result;
    } else if (key_missing != registry_missing) {
        /* Partial namespace loss must never mint a replacement key or reopen
         * the downgrade registry. */
        result = ESP_ERR_INVALID_STATE;
    } else if (key_missing) {
        esp_fill_random(candidate, sizeof(candidate));
        if (all_zero(candidate, sizeof(candidate))) {
            result = ESP_FAIL;
        } else {
            initialize_legacy_registry(registry);
            result = nvs_set_blob(
                handle, SAVE_SEAL_KEY, candidate, sizeof(candidate));
            if (result == ESP_OK) {
                result = nvs_set_blob(
                    handle, LEGACY_REGISTRY_KEY, registry,
                    sizeof(registry));
            }
            if (result == ESP_OK) {
                result = nvs_commit(handle);
            }
            if (result == ESP_OK) {
                uint8_t readback[PLATFORM_SAVE_SEAL_KEY_BYTES] = {0};
                size_t readback_bytes = sizeof(readback);
                result = nvs_get_blob(
                    handle, SAVE_SEAL_KEY, readback, &readback_bytes);
                if (result == ESP_OK &&
                    (readback_bytes != sizeof(readback) ||
                     !constant_time_equal(
                         candidate, readback, sizeof(readback)))) {
                    result = ESP_ERR_INVALID_STATE;
                }
                platform_save_seal_clear(readback, sizeof(readback));
            }
            if (result == ESP_OK) {
                memset(registry, 0, sizeof(registry));
                uint32_t readback_count = UINT32_MAX;
                result = read_legacy_registry(
                    handle, registry, &readback_count);
                if (result == ESP_OK && readback_count != 0U) {
                    result = ESP_ERR_INVALID_STATE;
                }
            }
        }
    } else {
        uint32_t count = 0U;
        if (stored_bytes != sizeof(candidate)) {
            result = ESP_ERR_INVALID_SIZE;
        } else if (registry_bytes == LEGACY_V1_REGISTRY_BYTES &&
                   legacy_v1_registry_valid(
                       registry, registry_bytes, &count)) {
            uint8_t migrated[LEGACY_REGISTRY_BYTES];
            migrate_legacy_v1_registry(registry, count, migrated);
            result = nvs_set_blob(
                handle, LEGACY_REGISTRY_KEY, migrated, sizeof(migrated));
            if (result == ESP_OK) {
                result = nvs_commit(handle);
            }
            uint32_t migrated_count = 0U;
            if (result == ESP_OK) {
                result = read_legacy_registry(
                    handle, migrated, &migrated_count);
            }
            if (result == ESP_OK && migrated_count != count) {
                result = ESP_ERR_INVALID_STATE;
            }
            platform_save_seal_clear(migrated, sizeof(migrated));
        } else if (registry_bytes != sizeof(registry) ||
                   !legacy_registry_valid(registry, &count)) {
            result = ESP_ERR_INVALID_STATE;
        } else {
            result = ESP_OK;
        }
    }

    if (result == ESP_OK && all_zero(candidate, sizeof(candidate))) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK) {
        memcpy(key_out, candidate, sizeof(candidate));
    }
    platform_save_seal_clear(registry, sizeof(registry));
    platform_save_seal_clear(candidate, sizeof(candidate));
    nvs_close(handle);
    if (result != ESP_OK) {
        platform_save_seal_clear(key_out, PLATFORM_SAVE_SEAL_KEY_BYTES);
    }
    return result;
}
