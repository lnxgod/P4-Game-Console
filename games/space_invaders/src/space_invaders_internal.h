// SPDX-License-Identifier: MIT

#ifndef P4_SPACE_INVADERS_INTERNAL_H
#define P4_SPACE_INVADERS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/audio_pack.h"

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
    int16_t previous_y;
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
    /* Presentation only: never sampled by firing, collisions, or the RNG. */
    uint32_t visual_clock_ms;
    uint32_t formation_tween_ms;
    uint32_t impact_ms;
    int16_t formation_from_x;
    int16_t formation_from_y;
    int16_t impact_x;
    int16_t impact_y;
    uint8_t impact_frame;
    p4_game_audio_effect_player_t audio;
    int16_t formation_x;
    int16_t formation_y;
    int16_t player_x;
    int16_t previous_player_x;
    int8_t formation_direction;
    uint8_t lives;
    uint8_t wave;
    uint8_t invaders_remaining;
    uint8_t animation_phase;
    uint8_t march_note;
    uint16_t shields[SPACE_SHIELD_COUNT];
    space_projectile_t player_projectiles[SPACE_PLAYER_PROJECTILE_COUNT];
    space_projectile_t enemy_projectiles[SPACE_ENEMY_PROJECTILE_COUNT];
    bool intro;
    bool launch_input_blocked;
    bool title_touch_down;
    bool paused;
    bool game_over;
} space_invaders_state_t;

void space_invaders_reset(space_invaders_state_t *state);
/* Presentation position in original world units; rules never consume this. */
int space_invaders_formation_visual_q8(const space_invaders_state_t *state, bool horizontal);

int space_invaders_motion_visual_q8(int previous, int current, uint32_t remainder_ms);
#endif
