// SPDX-License-Identifier: MIT

#ifndef P4_YAHTZEE_INTERNAL_H
#define P4_YAHTZEE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    P4_YAHTZEE_PLAYERS = 2,
    P4_YAHTZEE_DICE = 5,
    P4_YAHTZEE_CATEGORIES = 13,
    P4_YAHTZEE_ROLLS_PER_TURN = 3,
    P4_YAHTZEE_NETWORK_PROTOCOL = 1,
    P4_YAHTZEE_NET_ROLL = 1,
    P4_YAHTZEE_NET_HOLD = 2,
    P4_YAHTZEE_NET_SCORE = 3,
};

typedef enum {
    P4_YAHTZEE_ONES = 0,
    P4_YAHTZEE_TWOS,
    P4_YAHTZEE_THREES,
    P4_YAHTZEE_FOURS,
    P4_YAHTZEE_FIVES,
    P4_YAHTZEE_SIXES,
    P4_YAHTZEE_THREE_KIND,
    P4_YAHTZEE_FOUR_KIND,
    P4_YAHTZEE_FULL_HOUSE,
    P4_YAHTZEE_SMALL_STRAIGHT,
    P4_YAHTZEE_LARGE_STRAIGHT,
    P4_YAHTZEE_FIVE_KIND,
    P4_YAHTZEE_CHANCE,
} p4_yahtzee_category_t;

typedef enum {
    P4_YAHTZEE_MENU = 0,
    P4_YAHTZEE_NETWORK_WAIT,
    P4_YAHTZEE_TURN,
    P4_YAHTZEE_PASS,
    P4_YAHTZEE_GAME_OVER,
} p4_yahtzee_phase_t;

typedef enum {
    P4_YAHTZEE_LOCAL = 0,
    P4_YAHTZEE_NETWORK,
} p4_yahtzee_mode_t;

typedef enum {
    P4_YAHTZEE_FOCUS_DICE = 0,
    P4_YAHTZEE_FOCUS_SCORE,
} p4_yahtzee_focus_t;

typedef struct {
    int16_t scores[P4_YAHTZEE_PLAYERS][P4_YAHTZEE_CATEGORIES];
    uint8_t dice[P4_YAHTZEE_DICE];
    uint8_t animation_dice[P4_YAHTZEE_DICE];
    uint8_t held_mask;
    uint8_t current_player;
    uint8_t roll_count;
    uint8_t selected_die;
    uint8_t selected_category;
    uint8_t menu_selection;
    uint8_t turns_scored[P4_YAHTZEE_PLAYERS];
    uint8_t local_player_slot;
    p4_yahtzee_phase_t phase;
    p4_yahtzee_mode_t mode;
    p4_yahtzee_focus_t focus;
    p4_game_multiplayer_role_t network_role;
    uint32_t rng;
    uint32_t roll_animation_ms;
    uint32_t animation_step_ms;
    uint32_t network_revision;
    uint32_t last_network_sequence[P4_YAHTZEE_PLAYERS];
    uint64_t network_seed;
    bool touch_was_down;
    bool network_started;
    bool network_error;
} p4_yahtzee_state_t;

int p4_yahtzee_score_dice(
    const uint8_t dice[P4_YAHTZEE_DICE],
    p4_yahtzee_category_t category);
int p4_yahtzee_upper_total(const p4_yahtzee_state_t *state, uint8_t player);
int p4_yahtzee_total(const p4_yahtzee_state_t *state, uint8_t player);
void p4_yahtzee_reset_match(p4_yahtzee_state_t *state, uint32_t seed);
bool p4_yahtzee_roll(p4_yahtzee_state_t *state);
bool p4_yahtzee_score_turn(
    p4_yahtzee_state_t *state, p4_yahtzee_category_t category);
void p4_yahtzee_update_animation(
    p4_yahtzee_state_t *state, uint32_t elapsed_ms);
bool p4_yahtzee_network_available(const p4_game_context_t *context);
bool p4_yahtzee_local_turn(const p4_yahtzee_state_t *state);
bool p4_yahtzee_perform_action(
    p4_game_context_t *context,
    p4_yahtzee_state_t *state,
    uint8_t kind,
    uint8_t argument);
void p4_yahtzee_poll_network(
    p4_game_context_t *context,
    p4_yahtzee_state_t *state);

#endif
