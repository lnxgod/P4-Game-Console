// SPDX-License-Identifier: MIT

#ifndef P4_SPACE_INVADERS_INTERNAL_H
#define P4_SPACE_INVADERS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SPACE_INVADER_ROWS = 4,
    SPACE_INVADER_COLUMNS = 8,
    SPACE_INVADER_COUNT = SPACE_INVADER_ROWS * SPACE_INVADER_COLUMNS,
    SPACE_PLAYER_PROJECTILE_COUNT = 2,
    SPACE_ENEMY_PROJECTILE_COUNT = 6,
    SPACE_SHIELD_COUNT = 3,
};

typedef struct {
    int16_t x;
    int16_t y;
    bool active;
} space_projectile_t;

typedef struct {
    uint32_t alive_mask;
    uint32_t score;
    uint32_t random_state;
    uint32_t simulation_accumulator_ms;
    uint32_t formation_accumulator_ms;
    uint32_t enemy_fire_accumulator_ms;
    uint32_t player_cooldown_ms;
    uint32_t respawn_ms;
    uint32_t wave_delay_ms;
    uint32_t held_buttons;
    int16_t formation_x;
    int16_t formation_y;
    int16_t player_x;
    int8_t formation_direction;
    uint8_t lives;
    uint8_t wave;
    uint8_t invaders_remaining;
    uint8_t animation_phase;
    uint8_t march_note;
    uint16_t shields[SPACE_SHIELD_COUNT];
    space_projectile_t player_projectiles[SPACE_PLAYER_PROJECTILE_COUNT];
    space_projectile_t enemy_projectiles[SPACE_ENEMY_PROJECTILE_COUNT];
    bool paused;
    bool game_over;
} space_invaders_state_t;

void space_invaders_reset(space_invaders_state_t *state);

#endif
