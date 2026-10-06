// SPDX-License-Identifier: MIT
#ifndef FROG_HOP_INTERNAL_H
#define FROG_HOP_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
#include "p4/audio_pack.h"
typedef struct {
    int16_t frog_x;
    int16_t splash_x;
    int32_t visual_from_x_q8;
    int32_t visual_from_y_q8;
    uint16_t hop_ms;
    uint16_t scenery_ms;
    uint8_t road_variants[3];
    uint8_t log_variants[4];
    bool world_advanced;
    int16_t road_offsets[3];
    int16_t log_offsets[4];
    uint32_t score;
    uint32_t simulation_ms;
    uint32_t held_buttons;
    uint16_t respawn_ms;
    uint16_t splash_ms;
    uint16_t animation_ms;
    uint8_t frog_row;
    uint8_t splash_row;
    uint8_t lives;
    uint8_t level;
    uint8_t homes;
    uint8_t animation_frame;
    p4_game_audio_effect_player_t audio;
    bool intro;
    bool paused;
    bool game_over;
} frog_hop_state_t;
int32_t frog_hop_visual_q8(const frog_hop_state_t *state, bool vertical);
int32_t frog_hop_lane_visual_q8(const frog_hop_state_t *state, bool water, unsigned lane);
#endif
