// SPDX-License-Identifier: MIT

#ifndef P4_MAZE_CHASE_INTERNAL_H
#define P4_MAZE_CHASE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/audio_pack.h"

enum {
    MAZE_CHASE_WIDTH = 19,
    MAZE_CHASE_HEIGHT = 13,
    MAZE_CHASE_ENEMY_COUNT = 3,
};

typedef enum {
    MAZE_DIRECTION_NONE = 0,
    MAZE_DIRECTION_UP,
    MAZE_DIRECTION_DOWN,
    MAZE_DIRECTION_LEFT,
    MAZE_DIRECTION_RIGHT,
} maze_direction_t;

typedef struct {
    uint8_t x;
    uint8_t y;
    maze_direction_t direction;
    uint8_t color_index;
} maze_enemy_t;

typedef struct {
    uint8_t pellets[MAZE_CHASE_HEIGHT][MAZE_CHASE_WIDTH];
    uint16_t pellets_remaining;
    uint32_t score;
    uint8_t lives;
    uint8_t player_x;
    uint8_t player_y;
    maze_direction_t player_direction;
    maze_direction_t desired_direction;
    maze_enemy_t enemies[MAZE_CHASE_ENEMY_COUNT];
    uint32_t player_move_accumulator_ms;
    uint32_t enemy_move_accumulator_ms;
    uint32_t frightened_ms;
    uint32_t enemy_step;
    uint32_t held_buttons;
    p4_game_audio_effect_player_t audio;
    bool paused;
    bool game_over;
    bool won;
} maze_chase_state_t;

void maze_chase_reset(maze_chase_state_t *state);
bool maze_chase_cell_open(int x, int y);

#endif
