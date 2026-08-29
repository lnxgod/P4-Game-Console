// SPDX-License-Identifier: LicenseRef-LORD-Permission

#ifndef P4_LORD_SYNC_IMPL_H
#define P4_LORD_SYNC_IMPL_H

#include "lord_internal.h"

#include <string.h>

enum {
    LORD_SYNC_HEADER_BYTES = 52,
};

static uint16_t sync_load_u16(const uint8_t *bytes, size_t offset)
{
    return (uint16_t)((uint16_t)bytes[offset] |
        (uint16_t)((uint16_t)bytes[offset + 1U] << 8U));
}

static uint32_t sync_load_u32(const uint8_t *bytes, size_t offset)
{
    return (uint32_t)bytes[offset] |
        (uint32_t)bytes[offset + 1U] << 8U |
        (uint32_t)bytes[offset + 2U] << 16U |
        (uint32_t)bytes[offset + 3U] << 24U;
}

static uint64_t sync_load_u64(const uint8_t *bytes, size_t offset)
{
    const uint64_t low = sync_load_u32(bytes, offset);
    const uint64_t high = sync_load_u32(bytes, offset + 4U);
    return low | high << 32U;
}

static void sync_store_u64(uint8_t *bytes, size_t offset, uint64_t value)
{
    save_store_u32(bytes, offset, (uint32_t)value);
    save_store_u32(bytes, offset + 4U, (uint32_t)(value >> 32U));
}

static bool sync_actor_valid(
    const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES])
{
    uint8_t combined = 0U;
    for (size_t index = 0U; index < LORD_SYNC_ACTOR_ID_BYTES; ++index) {
        combined = (uint8_t)(combined | actor_id[index]);
    }
    return combined != 0U;
}

size_t lord_sync_encode(const lord_state_t *state,
                        const uint8_t actor_id[LORD_SYNC_ACTOR_ID_BYTES],
                        uint64_t operation_nonce,
                        uint8_t *bytes, size_t capacity)
{
    if (state == NULL || actor_id == NULL || bytes == NULL ||
        operation_nonce == 0U || state->realm_revision == 0U ||
        !sync_actor_valid(actor_id) || capacity < LORD_SYNC_HEADER_BYTES ||
        capacity - LORD_SYNC_HEADER_BYTES < LORD_SAVE_MAX_BYTES) {
        return 0U;
    }
    const size_t save_bytes = lord_save_encode(
        state, bytes + LORD_SYNC_HEADER_BYTES,
        capacity - LORD_SYNC_HEADER_BYTES);
    if (save_bytes == 0U || save_bytes > LORD_SAVE_MAX_BYTES) {
        return 0U;
    }
    const size_t total_bytes = LORD_SYNC_HEADER_BYTES + save_bytes;
    bytes[0] = (uint8_t)'L';
    bytes[1] = (uint8_t)'R';
    bytes[2] = (uint8_t)'S';
    bytes[3] = (uint8_t)'Y';
    save_store_u16(bytes, 4U, LORD_SYNC_FORMAT_VERSION);
    save_store_u16(bytes, 6U, LORD_SYNC_HEADER_BYTES);
    save_store_u32(bytes, 8U, (uint32_t)total_bytes);
    save_store_u32(bytes, 12U, 0U);
    save_store_u32(bytes, 16U, state->realm_revision);
    save_store_u32(bytes, 20U, state->save_sequence);
    sync_store_u64(bytes, 24U, operation_nonce);
    memcpy(bytes + 32U, actor_id, LORD_SYNC_ACTOR_ID_BYTES);
    save_store_u32(bytes, 48U, (uint32_t)save_bytes);
    save_store_u32(bytes, 12U,
                   save_crc32(bytes + 16U, total_bytes - 16U));
    return total_bytes;
}

bool lord_sync_decode(lord_state_t *state,
                      lord_sync_metadata_t *metadata,
                      const uint8_t expected_actor_id[
                          LORD_SYNC_ACTOR_ID_BYTES],
                      uint32_t minimum_realm_revision,
                      const uint8_t *bytes, size_t length)
{
    if (state == NULL || metadata == NULL || expected_actor_id == NULL ||
        bytes == NULL || length < LORD_SYNC_HEADER_BYTES ||
        length > LORD_SYNC_MAX_BYTES || bytes[0] != (uint8_t)'L' ||
        bytes[1] != (uint8_t)'R' || bytes[2] != (uint8_t)'S' ||
        bytes[3] != (uint8_t)'Y' ||
        sync_load_u16(bytes, 4U) != LORD_SYNC_FORMAT_VERSION ||
        sync_load_u16(bytes, 6U) != LORD_SYNC_HEADER_BYTES ||
        sync_load_u32(bytes, 8U) != length) {
        return false;
    }
    const uint32_t stored_crc = sync_load_u32(bytes, 12U);
    const uint32_t realm_revision = sync_load_u32(bytes, 16U);
    const uint32_t save_sequence = sync_load_u32(bytes, 20U);
    const uint64_t operation_nonce = sync_load_u64(bytes, 24U);
    const uint32_t save_bytes = sync_load_u32(bytes, 48U);
    const uint8_t *const actor_id = bytes + 32U;
    if (stored_crc != save_crc32(bytes + 16U, length - 16U) ||
        realm_revision == 0U || realm_revision < minimum_realm_revision ||
        operation_nonce == 0U || !sync_actor_valid(actor_id) ||
        memcmp(actor_id, expected_actor_id, LORD_SYNC_ACTOR_ID_BYTES) != 0 ||
        save_bytes != length - LORD_SYNC_HEADER_BYTES ||
        save_bytes > LORD_SAVE_MAX_BYTES ||
        !lord_save_decode(state, bytes + LORD_SYNC_HEADER_BYTES,
                          save_bytes) ||
        state->realm_revision != realm_revision) {
        return false;
    }
    const bool migrated_legacy_empty_slots = state->save_dirty &&
        state->save_local_generation == 1U && save_sequence != UINT32_MAX &&
        state->save_sequence == save_sequence + 1U;
    if (state->save_sequence != save_sequence &&
        !migrated_legacy_empty_slots) {
        return false;
    }
    memcpy(metadata->actor_id, actor_id, LORD_SYNC_ACTOR_ID_BYTES);
    metadata->operation_nonce = operation_nonce;
    metadata->realm_revision = realm_revision;
    metadata->save_sequence = save_sequence;
    return true;
}

#endif
