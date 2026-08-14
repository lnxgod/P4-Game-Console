// SPDX-License-Identifier: MIT

#ifndef P4_GAME_API_INPUT_H
#define P4_GAME_API_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_INPUT_PHYSICAL_WIDTH = 1024,
    P4_INPUT_PHYSICAL_HEIGHT = 600,
    P4_INPUT_VIEWPORT_LEFT = 32,
    P4_INPUT_VIEWPORT_SCALE = 3,
    P4_INPUT_MAX_TOUCHES = 5,
};

typedef struct {
    uint16_t x;
    uint16_t y;
} p4_physical_touch_t;

typedef struct {
    uint32_t previous_held;
} p4_game_input_mapper_t;

void p4_game_input_mapper_init(p4_game_input_mapper_t *mapper);

bool p4_game_map_physical_touch(uint16_t physical_x,
                                uint16_t physical_y,
                                p4_game_point_t *out_logical);

/**
 * Convert one complete GT911 snapshot plus optional already-sanitized digital
 * controls into the stable game input model. Invalid touch releases every
 * touch-derived button immediately. Unknown digital bits are discarded.
 */
void p4_game_input_mapper_update(
    p4_game_input_mapper_t *mapper,
    bool touch_valid,
    const p4_physical_touch_t *touches,
    size_t touch_count,
    uint32_t digital_buttons,
    p4_game_input_t *out_input);

/** Draw the standard Back/Start, D-pad, B, and A touch regions. */
void p4_game_draw_standard_controls(p4_game_surface_t *surface,
                                    uint16_t color,
                                    uint16_t active_color,
                                    uint32_t held_buttons);

#ifdef __cplusplus
}
#endif

#endif
