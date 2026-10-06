// SPDX-License-Identifier: MIT

#ifndef COLOR_CLASH_INTERNAL_H
#define COLOR_CLASH_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    COLOR_CLASH_MAX_PLAYERS = 4,
    COLOR_CLASH_DECK_CARDS = 109,
    COLOR_CLASH_HAND_CAPACITY = 108,
    COLOR_CLASH_STARTING_HAND = 7,
    COLOR_CLASH_VISIBLE_CARDS = 9,
    COLOR_CLASH_NETWORK_PROTOCOL = 5,
    COLOR_CLASH_NET_PLAY = 1,
    COLOR_CLASH_NET_DRAW = 2,
    COLOR_CLASH_NET_COLOR = 3,
    COLOR_CLASH_NET_RESTART = 4,
    COLOR_CLASH_NET_HAND = 5,
    COLOR_CLASH_NET_PASS = 6,
    COLOR_CLASH_NET_UNO = 7,
    COLOR_CLASH_NET_PLAY_UNO = 8,
};

typedef enum {
    COLOR_CLASH_RED = 0,
    COLOR_CLASH_GOLD,
    COLOR_CLASH_TIFFANY,
    COLOR_CLASH_VIOLET,
    COLOR_CLASH_COLOR_COUNT,
} color_clash_color_t;

typedef enum {
    COLOR_CLASH_ZERO = 0,
    COLOR_CLASH_ONE,
    COLOR_CLASH_TWO,
    COLOR_CLASH_THREE,
    COLOR_CLASH_FOUR,
    COLOR_CLASH_FIVE,
    COLOR_CLASH_SIX,
    COLOR_CLASH_SEVEN,
    COLOR_CLASH_EIGHT,
    COLOR_CLASH_NINE,
    COLOR_CLASH_SKIP,
    COLOR_CLASH_REVERSE,
    COLOR_CLASH_DRAW_TWO,
    COLOR_CLASH_WILD,
    COLOR_CLASH_GAMECHANGER,
    COLOR_CLASH_WILD_DRAW_FOUR,
} color_clash_rank_t;

typedef enum {
    COLOR_CLASH_MENU = 0,
    COLOR_CLASH_NETWORK_WAIT,
    COLOR_CLASH_TURN,
    COLOR_CLASH_DRAWN_CARD,
    COLOR_CLASH_CHOOSE_COLOR,
    COLOR_CLASH_CHOOSE_HAND,
    COLOR_CLASH_GAME_OVER,
    COLOR_CLASH_NETWORK_LOST,
} color_clash_phase_t;

typedef enum {
    COLOR_CLASH_PRACTICE = 0,
    COLOR_CLASH_NETWORK,
} color_clash_mode_t;

typedef enum {
    COLOR_CLASH_COLOR_AID_STANDARD = 0,
    COLOR_CLASH_COLOR_AID_SYMBOLS,
    COLOR_CLASH_COLOR_AID_HIGH_CONTRAST,
    COLOR_CLASH_COLOR_AID_COUNT,
} color_clash_color_aid_t;

typedef enum {
    COLOR_CLASH_NOTICE_NONE = 0,
    COLOR_CLASH_NOTICE_INVALID_PLAY,
    COLOR_CLASH_NOTICE_DRAW_FOUR_BLOCKED,
    COLOR_CLASH_NOTICE_UNO_CALLED,
    COLOR_CLASH_NOTICE_UNO_CAUGHT,
} color_clash_notice_t;

typedef struct {
    uint8_t hands[COLOR_CLASH_MAX_PLAYERS][COLOR_CLASH_HAND_CAPACITY];
    uint8_t hand_counts[COLOR_CLASH_MAX_PLAYERS];
    uint8_t deck[COLOR_CLASH_DECK_CARDS];
    uint8_t discard[COLOR_CLASH_DECK_CARDS];
    uint8_t deck_count;
    uint8_t discard_count;
    uint8_t player_count;
    uint8_t human_player_count;
    uint8_t current_player;
    uint8_t local_player_slot;
    uint8_t direction;
    uint8_t active_color;
    uint8_t selected_card;
    uint8_t hand_window_start;
    uint8_t hand_touch_card;
    uint8_t selected_color;
    uint8_t selected_target;
    uint8_t menu_players;
    uint8_t color_aid;
    uint8_t winner;
    uint8_t uno_pending_player;
    uint8_t notice_player;
    color_clash_phase_t phase;
    color_clash_mode_t mode;
    color_clash_notice_t notice;
    p4_game_multiplayer_role_t network_role;
    uint32_t rng;
    uint32_t bot_wait_ms;
    uint32_t notice_ms;
    uint32_t network_revision;
    uint32_t last_network_sequence[COLOR_CLASH_MAX_PLAYERS];
    uint64_t network_seed;
    uint16_t hand_touch_last_x;
    uint16_t hand_touch_start_y;
    uint16_t hand_touch_start_x;
    uint16_t hand_touch_x;
    uint16_t hand_touch_y;
    int16_t hand_touch_offset_x;
    int16_t hand_touch_offset_y;
    uint8_t hand_touch_value;
    uint8_t hand_touch_count;
    uint32_t hand_touch_revision;
    bool hand_dragging;
    bool touch_was_down;
    bool hand_touch_active;
    bool hand_touch_scrolled;
    bool network_started;
    bool network_error;
    bool network_sync_pending;
    uint8_t network_sync_cursor;
    uint8_t network_sync_stage;
    uint8_t network_sync_offset;
    uint8_t network_hand_received;
} color_clash_state_t;

uint8_t color_clash_make_card(color_clash_color_t color,
                              color_clash_rank_t rank);
color_clash_color_t color_clash_card_color(uint8_t card);
color_clash_rank_t color_clash_card_rank(uint8_t card);
bool color_clash_card_valid(uint8_t card);
bool color_clash_card_playable(const color_clash_state_t *state,
                               uint8_t card);
bool color_clash_card_playable_for_player(
    const color_clash_state_t *state, uint8_t player, uint8_t hand_index);
bool color_clash_hand_card_playable_now(
    const color_clash_state_t *state, uint8_t player, uint8_t hand_index);
uint8_t color_clash_hand_index_at_visual(
    const color_clash_state_t *state, uint8_t player, uint8_t visual_index);
uint8_t color_clash_hand_visual_index(
    const color_clash_state_t *state, uint8_t player, uint8_t hand_index);
void color_clash_reset_match(color_clash_state_t *state,
                             uint8_t player_count, uint32_t seed);
bool color_clash_play_card(color_clash_state_t *state,
                           uint8_t player, uint8_t hand_index);
bool color_clash_play_card_and_call_uno(color_clash_state_t *state,
                                        uint8_t player,
                                        uint8_t hand_index);
bool color_clash_draw_card(color_clash_state_t *state, uint8_t player);
bool color_clash_pass_drawn_card(color_clash_state_t *state, uint8_t player);
bool color_clash_call_uno(color_clash_state_t *state, uint8_t caller);
bool color_clash_choose_color(color_clash_state_t *state,
                              uint8_t player, uint8_t color);
bool color_clash_choose_hand(color_clash_state_t *state,
                             uint8_t player, uint8_t target);
void color_clash_update_bot(p4_game_context_t *context,
                            color_clash_state_t *state,
                            uint32_t elapsed_ms);

bool color_clash_network_available(const p4_game_context_t *context);
bool color_clash_local_turn(const color_clash_state_t *state);
bool color_clash_perform_action(p4_game_context_t *context,
                                color_clash_state_t *state,
                                uint8_t kind, uint8_t argument);
bool color_clash_perform_bot_action(p4_game_context_t *context,
                                    color_clash_state_t *state,
                                    uint8_t kind, uint8_t argument);
void color_clash_poll_network(p4_game_context_t *context,
                              color_clash_state_t *state);

#endif
