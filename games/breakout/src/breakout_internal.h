// SPDX-License-Identifier: MIT

#ifndef P4_BREAKOUT_INTERNAL_H
#define P4_BREAKOUT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    BREAKOUT_COLUMNS = 10,
    BREAKOUT_ROWS = 5,
    BREAKOUT_BRICK_COUNT = BREAKOUT_COLUMNS * BREAKOUT_ROWS,
};

typedef struct {
    uint64_t bricks;
    uint32_t score;
    uint8_t lives;
    int16_t paddle_x;
    int16_t ball_x;
    int16_t ball_y;
    int8_t ball_dx;
    int8_t ball_dy;
    uint32_t simulation_accumulator_ms;
    uint32_t held_buttons;
    bool paused;
    bool game_over;
    bool won;
} breakout_state_t;

void breakout_reset(breakout_state_t *state);

#endif
