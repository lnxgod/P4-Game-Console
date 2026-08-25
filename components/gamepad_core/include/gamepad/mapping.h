// SPDX-License-Identifier: MIT

#ifndef GAMEPAD_MAPPING_H
#define GAMEPAD_MAPPING_H

#include <stdbool.h>
#include <stdint.h>

#include "gamepad/gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GAMEPAD_BUTTON_MAPPING_VERSION = 1,
};

/** User-facing logical controls that can be rebound console-wide. */
typedef enum {
    GAMEPAD_MAPPING_A = 0,
    GAMEPAD_MAPPING_B,
    GAMEPAD_MAPPING_X,
    GAMEPAD_MAPPING_Y,
    GAMEPAD_MAPPING_START,
    GAMEPAD_MAPPING_BACK,
    GAMEPAD_MAPPING_COUNT,
} gamepad_mapping_slot_t;

/**
 * For each logical slot, records the canonical raw button that drives it.
 * Sources must be unique so one physical press cannot trigger two actions.
 */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint8_t source[GAMEPAD_MAPPING_COUNT];
    uint8_t reserved[2];
} gamepad_button_mapping_t;

void gamepad_button_mapping_default(gamepad_button_mapping_t *mapping);
bool gamepad_button_mapping_valid(const gamepad_button_mapping_t *mapping);

/** Apply a validated mapping to one complete canonical state in place. */
gamepad_status_t gamepad_state_apply_button_mapping(
    gamepad_state_t *state, const gamepad_button_mapping_t *mapping);

#ifdef __cplusplus
}
#endif

#endif
