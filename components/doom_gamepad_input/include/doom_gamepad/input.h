#ifndef DOOM_GAMEPAD_INPUT_H
#define DOOM_GAMEPAD_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "gamepad/gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_GAMEPAD_INPUT_VERSION 1U
#define DOOM_GAMEPAD_EVENT_CAPACITY 32U
#define DOOM_GAMEPAD_STICK_THRESHOLD 12000
#define DOOM_GAMEPAD_TRIGGER_THRESHOLD 32768U

typedef enum {
    DOOM_GAMEPAD_ACTION_UP = 0,
    DOOM_GAMEPAD_ACTION_DOWN,
    DOOM_GAMEPAD_ACTION_LEFT,
    DOOM_GAMEPAD_ACTION_RIGHT,
    DOOM_GAMEPAD_ACTION_FIRE,
    DOOM_GAMEPAD_ACTION_USE,
    DOOM_GAMEPAD_ACTION_RUN,
    DOOM_GAMEPAD_ACTION_STRAFE,
    DOOM_GAMEPAD_ACTION_STRAFE_LEFT,
    DOOM_GAMEPAD_ACTION_STRAFE_RIGHT,
    DOOM_GAMEPAD_ACTION_MENU_ACCEPT,
    DOOM_GAMEPAD_ACTION_MENU_BACK,
    DOOM_GAMEPAD_ACTION_MAP,
    DOOM_GAMEPAD_ACTION_WEAPON_NEXT,
    DOOM_GAMEPAD_ACTION_WEAPON_PREVIOUS,
    DOOM_GAMEPAD_ACTION_PAUSE,
    DOOM_GAMEPAD_ACTION_COUNT
} doom_gamepad_action_t;

typedef struct {
    uint8_t action;
    uint8_t pressed;
    uint8_t reserved[2];
} doom_gamepad_event_t;

typedef struct {
    uint16_t version;
    uint16_t size;
    uint32_t active_actions;
    doom_gamepad_event_t events[DOOM_GAMEPAD_EVENT_CAPACITY];
    uint8_t read_index;
    uint8_t event_count;
    uint8_t reserved[2];
} doom_gamepad_input_t;

void doom_gamepad_input_init(doom_gamepad_input_t *input);

/**
 * Publish one latest canonical controller snapshot as Doom actions.
 *
 * Call only after draining prior events. Releases are queued before presses,
 * so a disconnect or remap cannot leave a Doom key held. Invalid snapshots
 * are treated as neutral and return an error after queuing required releases.
 */
gamepad_status_t doom_gamepad_input_update(
    doom_gamepad_input_t *input,
    const gamepad_state_t *state
);

bool doom_gamepad_input_next(
    doom_gamepad_input_t *input,
    doom_gamepad_event_t *event
);

bool doom_gamepad_input_idle(const doom_gamepad_input_t *input);

#ifdef __cplusplus
}
#endif

#endif
