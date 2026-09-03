// SPDX-License-Identifier: MIT

#ifndef P4_TEXAS_HOLDEM_INTERNAL_H
#define P4_TEXAS_HOLDEM_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    TEXAS_HOLDEM_MIN_PLAYERS = 2,
    TEXAS_HOLDEM_PLAYERS = 4,
    TEXAS_HOLDEM_DECK_CARDS = 52,
    TEXAS_HOLDEM_HOLE_CARDS = 2,
    TEXAS_HOLDEM_COMMUNITY_CARDS = 5,
    TEXAS_HOLDEM_STACK_CHOICES = 4,
    TEXAS_HOLDEM_SMALL_BLIND = 10,
    TEXAS_HOLDEM_BIG_BLIND = 20,
    TEXAS_HOLDEM_NO_CARD = 0xff,
    TEXAS_HOLDEM_NETWORK_PROTOCOL = 2,
    TEXAS_HOLDEM_CPU_THINK_MS = 600,
    TEXAS_HOLDEM_NET_FOLD = 1,
    TEXAS_HOLDEM_NET_CALL = 2,
    TEXAS_HOLDEM_NET_RAISE = 3,
    TEXAS_HOLDEM_NET_NEXT_HAND = 4,
    TEXAS_HOLDEM_NET_NEW_MATCH = 5,
};

typedef enum {
    TEXAS_HOLDEM_PHASE_SETUP = 0,
    TEXAS_HOLDEM_PHASE_PREFLOP,
    TEXAS_HOLDEM_PHASE_FLOP,
    TEXAS_HOLDEM_PHASE_TURN,
    TEXAS_HOLDEM_PHASE_RIVER,
    TEXAS_HOLDEM_PHASE_SHOWDOWN,
    TEXAS_HOLDEM_PHASE_MATCH_OVER,
} texas_holdem_phase_t;

typedef enum {
    TEXAS_HOLDEM_ACTION_FOLD = 0,
    TEXAS_HOLDEM_ACTION_CALL,
    TEXAS_HOLDEM_ACTION_RAISE,
    TEXAS_HOLDEM_ACTION_COUNT,
} texas_holdem_action_t;

typedef struct {
    uint16_t stacks[TEXAS_HOLDEM_PLAYERS];
    uint16_t round_bet[TEXAS_HOLDEM_PLAYERS];
    uint16_t contribution[TEXAS_HOLDEM_PLAYERS];
    uint16_t starting_stack;
    uint16_t pot;
    uint16_t current_bet;
    uint16_t min_raise;
    uint8_t deck[TEXAS_HOLDEM_DECK_CARDS];
    uint8_t hole[TEXAS_HOLDEM_PLAYERS][TEXAS_HOLDEM_HOLE_CARDS];
    uint8_t community[TEXAS_HOLDEM_COMMUNITY_CARDS];
    uint8_t deck_index;
    uint8_t community_count;
    uint8_t dealer;
    uint8_t current_player;
    uint8_t player_count;
    uint8_t network_player_count;
    uint8_t setup_index;
    uint8_t setup_focus;
    uint8_t action_selection;
    uint8_t cpu_mask;
    uint8_t active_mask;
    uint8_t folded_mask;
    uint8_t all_in_mask;
    uint8_t acted_mask;
    uint8_t winner_mask;
    texas_holdem_phase_t phase;
    p4_game_multiplayer_role_t network_role;
    uint8_t local_player_slot;
    uint32_t rng;
    uint32_t revision;
    uint32_t hand_number;
    uint32_t last_network_sequence[TEXAS_HOLDEM_PLAYERS];
    uint32_t network_retry_ms;
    uint32_t cpu_think_ms;
    uint32_t held_buttons;
    uint64_t session_seed;
    bool network_mode;
    bool network_started;
    bool network_error;
    bool network_snapshot_dirty;
    bool network_request_pending;
    bool touch_was_down;
    bool pass_required;
    bool peer_lost_fallback;
} texas_holdem_state_t;

extern const uint16_t
    texas_holdem_starting_stacks[TEXAS_HOLDEM_STACK_CHOICES];

void texas_holdem_reset_lobby(texas_holdem_state_t *state,
                              uint8_t player_count, uint32_t seed);
bool texas_holdem_adjust_stack(texas_holdem_state_t *state, bool increase);
bool texas_holdem_adjust_cpu_players(texas_holdem_state_t *state,
                                     bool increase);
bool texas_holdem_player_is_cpu(const texas_holdem_state_t *state,
                                uint8_t player);
bool texas_holdem_begin_match(texas_holdem_state_t *state);
bool texas_holdem_next_hand(texas_holdem_state_t *state);
bool texas_holdem_action_legal(const texas_holdem_state_t *state,
                               uint8_t player, texas_holdem_action_t action);
bool texas_holdem_apply_action(texas_holdem_state_t *state,
                               uint8_t player, texas_holdem_action_t action);
uint32_t texas_holdem_best_hand_rank(
    const uint8_t hole[TEXAS_HOLDEM_HOLE_CARDS],
    const uint8_t community[TEXAS_HOLDEM_COMMUNITY_CARDS]);
uint32_t texas_holdem_five_card_rank(const uint8_t cards[5]);
void texas_holdem_resolve_showdown(texas_holdem_state_t *state);
bool texas_holdem_local_turn(const texas_holdem_state_t *state);
texas_holdem_action_t texas_holdem_choose_cpu_action(
    texas_holdem_state_t *state, uint8_t player);

bool texas_holdem_network_begin(p4_game_context_t *context,
                                texas_holdem_state_t *state);
void texas_holdem_network_poll(p4_game_context_t *context,
                               texas_holdem_state_t *state,
                               uint32_t elapsed_ms);
bool texas_holdem_perform_action(p4_game_context_t *context,
                                 texas_holdem_state_t *state,
                                 texas_holdem_action_t action);
bool texas_holdem_perform_cpu_action(p4_game_context_t *context,
                                     texas_holdem_state_t *state);
bool texas_holdem_request_next_hand(p4_game_context_t *context,
                                    texas_holdem_state_t *state);
bool texas_holdem_request_new_match(p4_game_context_t *context,
                                    texas_holdem_state_t *state);
void texas_holdem_mark_snapshot_dirty(texas_holdem_state_t *state);

#endif
