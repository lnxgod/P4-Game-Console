// SPDX-License-Identifier: MIT

#include "byte_buddy_save.h"

#include <string.h>

enum {
    SAVE_CHECKSUM_OFFSET = 52,
    SAVE_KNOWN_ACHIEVEMENT_MASK = 0x0f,
    SAVE_DRAGON_RAISED_MASK = 0x08,
    SAVE_ELEMENTAL_GROWTH = 104,
};

static const uint8_t s_save_magic[8] = {
    'B', 'B', 'S', 'A', 'V', 'E', '1', '\0',
};

static const uint8_t s_style_max[BYTE_BUDDY_STYLE_COUNT] = {
    7U, 5U, 4U, 4U,
};

_Static_assert(BYTE_BUDDY_UPGRADE_COUNT == 4,
               "save layout requires four upgrades");
_Static_assert(BYTE_BUDDY_STYLE_COUNT == 4,
               "save layout requires four style channels");

static void store_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void store_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static uint16_t load_u16(const uint8_t *input)
{
    return (uint16_t)((uint16_t)input[0] |
                      (uint16_t)((uint16_t)input[1] << 8U));
}

static uint32_t load_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
        ((uint32_t)input[1] << 8U) |
        ((uint32_t)input[2] << 16U) |
        ((uint32_t)input[3] << 24U);
}

static uint32_t crc32(const uint8_t *data, size_t data_bytes)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t offset = 0U; offset < data_bytes; ++offset) {
        crc ^= data[offset];
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1U));
            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return ~crc;
}

bool byte_buddy_save_profile_valid(
    const byte_buddy_save_profile_t *profile)
{
    if (profile == NULL || profile->hunger > 100U ||
        profile->joy > 100U || profile->hygiene > 100U ||
        profile->energy > 100U ||
        (profile->achievement_mask &
         ~(uint32_t)SAVE_KNOWN_ACHIEVEMENT_MASK) != 0U ||
        ((profile->achievement_mask & SAVE_DRAGON_RAISED_MASK) != 0U &&
         profile->care_actions < SAVE_ELEMENTAL_GROWTH)) {
        return false;
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_UPGRADE_COUNT; ++index) {
        if (profile->upgrades[index] > 3U) {
            return false;
        }
    }
    for (unsigned index = 0U; index < BYTE_BUDDY_STYLE_COUNT; ++index) {
        if (profile->style_unlocked[index] > s_style_max[index] ||
            profile->style_selected[index] >
                profile->style_unlocked[index]) {
            return false;
        }
    }
    return true;
}

size_t byte_buddy_save_encode(
    const byte_buddy_save_profile_t *profile,
    uint8_t *output,
    size_t output_capacity)
{
    if (!byte_buddy_save_profile_valid(profile) || output == NULL ||
        output_capacity < BYTE_BUDDY_SAVE_PAYLOAD_BYTES) {
        return 0U;
    }
    memset(output, 0, BYTE_BUDDY_SAVE_PAYLOAD_BYTES);
    memcpy(output, s_save_magic, sizeof(s_save_magic));
    store_u16(output + 8U, BYTE_BUDDY_SAVE_SCHEMA_VERSION);
    store_u16(output + 10U, BYTE_BUDDY_SAVE_PAYLOAD_BYTES);
    output[16U] = profile->hunger;
    output[17U] = profile->joy;
    output[18U] = profile->hygiene;
    output[19U] = profile->energy;
    store_u16(output + 20U, profile->coins);
    store_u16(output + 22U, profile->care_actions);
    store_u16(output + 24U, profile->style_mix_count);
    store_u16(output + 26U, profile->pet_actions);
    for (unsigned index = 0U; index < 4U; ++index) {
        store_u16(output + 28U + index * 2U,
                  profile->action_counts[index]);
    }
    memcpy(output + 36U, profile->upgrades, BYTE_BUDDY_UPGRADE_COUNT);
    memcpy(output + 40U, profile->style_unlocked, BYTE_BUDDY_STYLE_COUNT);
    memcpy(output + 44U, profile->style_selected, BYTE_BUDDY_STYLE_COUNT);
    store_u32(output + 48U, profile->achievement_mask);
    store_u32(output + SAVE_CHECKSUM_OFFSET,
              crc32(output, SAVE_CHECKSUM_OFFSET));
    return BYTE_BUDDY_SAVE_PAYLOAD_BYTES;
}

bool byte_buddy_save_decode(
    byte_buddy_save_profile_t *profile,
    const uint8_t *data,
    size_t data_bytes)
{
    if (profile == NULL || data == NULL ||
        data_bytes != BYTE_BUDDY_SAVE_PAYLOAD_BYTES ||
        memcmp(data, s_save_magic, sizeof(s_save_magic)) != 0 ||
        load_u16(data + 8U) != BYTE_BUDDY_SAVE_SCHEMA_VERSION ||
        load_u16(data + 10U) != BYTE_BUDDY_SAVE_PAYLOAD_BYTES ||
        load_u32(data + 12U) != 0U ||
        load_u32(data + SAVE_CHECKSUM_OFFSET) !=
            crc32(data, SAVE_CHECKSUM_OFFSET)) {
        return false;
    }
    byte_buddy_save_profile_t loaded = {
        .hunger = data[16U],
        .joy = data[17U],
        .hygiene = data[18U],
        .energy = data[19U],
        .coins = load_u16(data + 20U),
        .care_actions = load_u16(data + 22U),
        .style_mix_count = load_u16(data + 24U),
        .pet_actions = load_u16(data + 26U),
        .achievement_mask = load_u32(data + 48U),
    };
    for (unsigned index = 0U; index < 4U; ++index) {
        loaded.action_counts[index] =
            load_u16(data + 28U + index * 2U);
    }
    memcpy(loaded.upgrades, data + 36U, BYTE_BUDDY_UPGRADE_COUNT);
    memcpy(loaded.style_unlocked, data + 40U, BYTE_BUDDY_STYLE_COUNT);
    memcpy(loaded.style_selected, data + 44U, BYTE_BUDDY_STYLE_COUNT);
    if (!byte_buddy_save_profile_valid(&loaded)) {
        return false;
    }
    *profile = loaded;
    return true;
}
