// SPDX-License-Identifier: MIT

#ifndef P4_BYTE_BUDDY_SAVE_H
#define P4_BYTE_BUDDY_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "byte_buddy_internal.h"

enum {
    BYTE_BUDDY_SAVE_SCHEMA_VERSION = 1,
    BYTE_BUDDY_SAVE_PAYLOAD_BYTES = 56,
};

/* Deliberately excludes signal labels, snapshots, RSSI, channels, tokens,
 * lineage entropy, and consumed-network history. Those remain session-only. */
typedef struct {
    uint8_t hunger;
    uint8_t joy;
    uint8_t hygiene;
    uint8_t energy;
    uint16_t coins;
    uint16_t care_actions;
    uint16_t style_mix_count;
    uint16_t pet_actions;
    uint16_t action_counts[4];
    uint8_t upgrades[BYTE_BUDDY_UPGRADE_COUNT];
    uint8_t style_unlocked[BYTE_BUDDY_STYLE_COUNT];
    uint8_t style_selected[BYTE_BUDDY_STYLE_COUNT];
    uint32_t achievement_mask;
} byte_buddy_save_profile_t;

bool byte_buddy_save_profile_valid(
    const byte_buddy_save_profile_t *profile);

size_t byte_buddy_save_encode(
    const byte_buddy_save_profile_t *profile,
    uint8_t *output,
    size_t output_capacity);

bool byte_buddy_save_decode(
    byte_buddy_save_profile_t *profile,
    const uint8_t *data,
    size_t data_bytes);

#endif
