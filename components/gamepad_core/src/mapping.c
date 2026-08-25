// SPDX-License-Identifier: MIT

#include "gamepad/mapping.h"

#include <stddef.h>

static const gamepad_button_t s_logical_buttons[GAMEPAD_MAPPING_COUNT] = {
    GAMEPAD_BUTTON_SOUTH,
    GAMEPAD_BUTTON_EAST,
    GAMEPAD_BUTTON_WEST,
    GAMEPAD_BUTTON_NORTH,
    GAMEPAD_BUTTON_START,
    GAMEPAD_BUTTON_BACK,
};

void gamepad_button_mapping_default(gamepad_button_mapping_t *mapping)
{
    if (mapping == NULL) {
        return;
    }
    *mapping = (gamepad_button_mapping_t) {
        .version = GAMEPAD_BUTTON_MAPPING_VERSION,
        .size = (uint16_t)sizeof(*mapping),
        .source = {
            GAMEPAD_BUTTON_SOUTH,
            GAMEPAD_BUTTON_EAST,
            GAMEPAD_BUTTON_WEST,
            GAMEPAD_BUTTON_NORTH,
            GAMEPAD_BUTTON_START,
            GAMEPAD_BUTTON_BACK,
        },
        .reserved = {0U, 0U},
    };
}

bool gamepad_button_mapping_valid(const gamepad_button_mapping_t *mapping)
{
    if (mapping == NULL ||
        mapping->version != GAMEPAD_BUTTON_MAPPING_VERSION ||
        mapping->size != sizeof(*mapping) || mapping->reserved[0] != 0U ||
        mapping->reserved[1] != 0U) {
        return false;
    }
    uint64_t used = 0U;
    for (size_t slot = 0U; slot < GAMEPAD_MAPPING_COUNT; ++slot) {
        const uint8_t source = mapping->source[slot];
        if (source >= GAMEPAD_BUTTON_COUNT) {
            return false;
        }
        const uint64_t bit = GAMEPAD_BUTTON_MASK(source);
        if ((used & bit) != 0U) {
            return false;
        }
        used |= bit;
    }
    return true;
}

gamepad_status_t gamepad_state_apply_button_mapping(
    gamepad_state_t *state, const gamepad_button_mapping_t *mapping)
{
    if (state == NULL || state->version != GAMEPAD_STATE_VERSION ||
        state->size != sizeof(*state) || state->connected > 1U ||
        !gamepad_button_mapping_valid(mapping)) {
        return GAMEPAD_ERR_INVALID_ARGUMENT;
    }

    const uint64_t raw = state->buttons;
    uint64_t logical_mask = 0U;
    uint64_t source_mask = 0U;
    for (size_t slot = 0U; slot < GAMEPAD_MAPPING_COUNT; ++slot) {
        logical_mask |= GAMEPAD_BUTTON_MASK(s_logical_buttons[slot]);
        source_mask |= GAMEPAD_BUTTON_MASK(mapping->source[slot]);
    }
    state->buttons = raw & ~(logical_mask | source_mask);
    for (size_t slot = 0U; slot < GAMEPAD_MAPPING_COUNT; ++slot) {
        if ((raw & GAMEPAD_BUTTON_MASK(mapping->source[slot])) != 0U) {
            state->buttons |= GAMEPAD_BUTTON_MASK(s_logical_buttons[slot]);
        }
    }
    return GAMEPAD_OK;
}
