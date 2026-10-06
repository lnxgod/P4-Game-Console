// SPDX-License-Identifier: MIT
#ifndef P4_MAZE_CHASE_INTERNAL_H
#define P4_MAZE_CHASE_INTERNAL_H
#include <stdbool.h>
#include <stdint.h>
#include "p4/audio_pack.h"
enum { MAZE_CHASE_WIDTH=25, MAZE_CHASE_HEIGHT=13, MAZE_CHASE_ENEMY_COUNT=4 };
typedef enum { MAZE_DIRECTION_NONE=0, MAZE_DIRECTION_UP, MAZE_DIRECTION_DOWN,
               MAZE_DIRECTION_LEFT, MAZE_DIRECTION_RIGHT } maze_direction_t;
typedef struct {
    uint8_t x,y,target_x,target_y,color_index;
    maze_direction_t direction;
    uint32_t move_ms,release_ms,step_duration_ms;
    bool returning,reverse_pending,moving,power_immune;
} maze_enemy_t;
typedef struct {
    uint8_t pellets[MAZE_CHASE_HEIGHT][MAZE_CHASE_WIDTH];
    uint16_t pellets_remaining,level,round_pellets;
    uint32_t score,best_score,next_extra_life;
    uint8_t lives,player_x,player_y;
    maze_direction_t player_direction,desired_direction;
    maze_enemy_t enemies[MAZE_CHASE_ENEMY_COUNT];
    uint8_t player_target_x,player_target_y,touch_role;
    bool player_moving;
    uint32_t touch_lock;
    uint32_t player_move_accumulator_ms,frightened_ms,recovery_ms,protection_ms;
    uint32_t enemy_step,held_buttons,presentation_ms,mode_ms,level_clear_ms;
    uint32_t fruit_ms,popup_ms,popup_score,touch_buttons;
    uint8_t mode_phase,ghost_chain,fruit_stage,popup_x,popup_y;
    uint16_t touch_anchor_x,touch_anchor_y;
    bool awaiting_move,paused,game_over,won,intro,touch_was_down;
    maze_direction_t swipe_direction;
    p4_game_audio_effect_player_t audio;
} maze_chase_state_t;
void maze_chase_reset(maze_chase_state_t *state);
bool maze_chase_cell_open(int x,int y);
#endif
