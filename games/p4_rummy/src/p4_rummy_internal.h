// SPDX-License-Identifier: MIT

#ifndef P4_RUMMY_INTERNAL_H
#define P4_RUMMY_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    P4_RUMMY_MIN_PLAYERS = 2,
    P4_RUMMY_MAX_PLAYERS = 4,
    P4_RUMMY_DECK_CARDS = 52,
    P4_RUMMY_HAND_CARDS = 7,
    P4_RUMMY_DRAWN_CARDS = 8,
    P4_RUMMY_MAX_MELDS = 8,
    P4_RUMMY_MAX_MELD_CARDS = 13,
    P4_RUMMY_MATCH_TARGET = 500,
    P4_RUMMY_NO_CARD = 0xff,
    P4_RUMMY_NO_PLAYER = 0xff,
    P4_RUMMY_NETWORK_PROTOCOL = 3,
    P4_RUMMY_NETWORK_MESSAGE_BYTES = 64,
    P4_RUMMY_CPU_THINK_MS = 420,
    P4_RUMMY_TURN_LIMIT = 200,
    P4_RUMMY_NET_DRAW_STOCK = 1,
    P4_RUMMY_NET_DRAW_DISCARD = 2,
    P4_RUMMY_NET_DISCARD = 3,
    P4_RUMMY_NET_NEW_ROUND = 4,
    P4_RUMMY_NET_PLAY_MELD = 5,
};

typedef enum {
    P4_RUMMY_PHASE_SETUP = 0,
    P4_RUMMY_PHASE_DRAW,
    P4_RUMMY_PHASE_DISCARD,
    P4_RUMMY_PHASE_ROUND_OVER,
} p4_rummy_phase_t;

typedef enum {
    P4_RUMMY_DRAW_STOCK = 0,
    P4_RUMMY_DRAW_DISCARD,
} p4_rummy_draw_source_t;

typedef struct {
    uint8_t hands[P4_RUMMY_MAX_PLAYERS][P4_RUMMY_DRAWN_CARDS];
    uint8_t hand_counts[P4_RUMMY_MAX_PLAYERS];
    uint8_t melds[P4_RUMMY_MAX_MELDS][P4_RUMMY_MAX_MELD_CARDS];
    uint8_t meld_counts[P4_RUMMY_MAX_MELDS];
    uint8_t meld_owners[P4_RUMMY_MAX_MELDS];
    uint8_t deck[P4_RUMMY_DECK_CARDS];
    uint8_t discard[P4_RUMMY_DECK_CARDS];
    uint8_t deck_count;
    uint8_t deck_index;
    uint8_t discard_count;
    uint8_t player_count;
    uint8_t human_player_count;
    uint8_t network_player_count;
    uint8_t local_player_slot;
    uint8_t current_player;
    uint8_t winner;
    uint8_t cpu_mask;
    uint8_t selected_card;
    uint8_t selected_mask;
    uint8_t drawn_card_index;
    uint8_t meld_count;
    int16_t scores[P4_RUMMY_MAX_PLAYERS];
    int16_t round_scores[P4_RUMMY_MAX_PLAYERS];
    p4_rummy_phase_t phase;
    p4_rummy_draw_source_t draw_source;
    p4_game_multiplayer_role_t network_role;
    uint32_t rng;
    uint32_t revision;
    uint32_t cpu_think_ms;
    uint32_t network_retry_ms;
    uint32_t last_network_sequence[P4_RUMMY_MAX_PLAYERS];
    uint16_t turn_count;
    uint16_t round_number;
    uint64_t session_seed;
    bool network_mode;
    bool network_started;
    bool network_error;
    bool network_snapshot_dirty;
    bool network_request_pending;
    bool touch_was_down;
    bool peer_lost_fallback;
} p4_rummy_state_t;

void p4_rummy_reset_lobby(p4_rummy_state_t *state,
                          uint8_t human_players, uint32_t seed);
bool p4_rummy_adjust_offline_players(p4_rummy_state_t *state,
                                     bool increase);
bool p4_rummy_adjust_cpu_seats(p4_rummy_state_t *state, bool increase);
bool p4_rummy_begin_round(p4_rummy_state_t *state);
bool p4_rummy_draw(p4_rummy_state_t *state, uint8_t player,
                   p4_rummy_draw_source_t source);
bool p4_rummy_discard_card(p4_rummy_state_t *state, uint8_t player,
                           uint8_t hand_index);
bool p4_rummy_meld_is_valid(const uint8_t *cards, uint8_t count);
bool p4_rummy_play_meld(p4_rummy_state_t *state, uint8_t player,
                        uint8_t selection_mask);
bool p4_rummy_player_is_cpu(const p4_rummy_state_t *state,
                            uint8_t player);
bool p4_rummy_local_turn(const p4_rummy_state_t *state);
bool p4_rummy_cpu_step(p4_rummy_state_t *state);
uint16_t p4_rummy_hand_deadwood(const uint8_t *cards, uint8_t count);
uint16_t p4_rummy_hand_points(const uint8_t *cards, uint8_t count);
bool p4_rummy_hand_is_complete(const uint8_t *cards, uint8_t count);
uint8_t p4_rummy_match_winner(const p4_rummy_state_t *state);

bool p4_rummy_network_begin(p4_game_context_t *context,
                            p4_rummy_state_t *state);
void p4_rummy_network_poll(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           uint32_t elapsed_ms);
bool p4_rummy_perform_draw(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           p4_rummy_draw_source_t source);
bool p4_rummy_perform_discard(p4_game_context_t *context,
                              p4_rummy_state_t *state,
                              uint8_t hand_index);
bool p4_rummy_perform_meld(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           uint8_t selection_mask);
bool p4_rummy_request_new_round(p4_game_context_t *context,
                                p4_rummy_state_t *state);
void p4_rummy_mark_snapshot_dirty(p4_rummy_state_t *state);

extern const p4_game_descriptor_t p4_p4_rummy_game;

#endif
