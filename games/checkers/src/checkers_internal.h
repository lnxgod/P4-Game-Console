// SPDX-License-Identifier: MIT

#ifndef P4_CHECKERS_INTERNAL_H
#define P4_CHECKERS_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    CHECKERS_BOARD_SIDE = 8,
    CHECKERS_BOARD_SQUARES = 64,
    CHECKERS_PLAYER_RED = 0,
    CHECKERS_PLAYER_WHITE = 1,
    CHECKERS_PLAYER_COUNT = 2,
    CHECKERS_WINNER_DRAW = 2,
    CHECKERS_NO_SQUARE = 0xff,
    CHECKERS_NO_WINNER = 0xff,
    CHECKERS_DRAW_QUIET_PLY = 80,
    CHECKERS_NETWORK_PROTOCOL = 1,
    CHECKERS_NET_MOVE = 1,
    CHECKERS_NET_RESTART = 2,
    CHECKERS_NET_SYNC = 3,
    CHECKERS_MOVE_CAPTURED = 1U << 0,
    CHECKERS_MOVE_CROWNED = 1U << 1,
    CHECKERS_MOVE_CONTINUES = 1U << 2,
    CHECKERS_MOVE_GAME_OVER = 1U << 3,
};

typedef enum {
    CHECKERS_EMPTY = 0,
    CHECKERS_RED_MAN,
    CHECKERS_RED_KING,
    CHECKERS_WHITE_MAN,
    CHECKERS_WHITE_KING,
} checkers_piece_t;

typedef enum {
    CHECKERS_PHASE_PLAYING = 0,
    CHECKERS_PHASE_GAME_OVER,
} checkers_phase_t;

typedef struct {
    uint8_t board[CHECKERS_BOARD_SQUARES];
    uint8_t cursor;
    uint8_t selected;
    uint8_t forced_piece;
    uint8_t current_player;
    uint8_t winner;
    uint8_t red_count;
    uint8_t white_count;
    uint16_t quiet_ply;
    checkers_phase_t phase;
    uint32_t revision;
    uint32_t last_network_sequence[CHECKERS_PLAYER_COUNT];
    uint32_t network_retry_ms;
    uint32_t held_buttons;
    p4_game_multiplayer_role_t network_role;
    uint8_t local_player_slot;
    bool network_mode;
    bool network_started;
    bool network_error;
    bool network_snapshot_dirty;
    bool network_request_pending;
    bool touch_was_down;
    bool peer_lost_fallback;
} checkers_state_t;

void checkers_reset_board(checkers_state_t *state, uint32_t revision);
bool checkers_piece_belongs_to(uint8_t piece, uint8_t player);
bool checkers_piece_can_capture(const checkers_state_t *state,
                                uint8_t square);
bool checkers_player_has_capture(const checkers_state_t *state,
                                 uint8_t player);
bool checkers_player_has_move(const checkers_state_t *state, uint8_t player);
bool checkers_move_is_legal(const checkers_state_t *state,
                            uint8_t from, uint8_t to);
bool checkers_apply_move(checkers_state_t *state, uint8_t from, uint8_t to,
                         uint8_t *event_flags);
uint8_t checkers_default_cursor(const checkers_state_t *state,
                                uint8_t player);

bool checkers_network_begin(p4_game_context_t *context,
                            checkers_state_t *state);
void checkers_network_poll(p4_game_context_t *context,
                           checkers_state_t *state, uint32_t elapsed_ms);
bool checkers_perform_move(p4_game_context_t *context,
                           checkers_state_t *state,
                           uint8_t from, uint8_t to,
                           uint8_t *event_flags);
bool checkers_restart(p4_game_context_t *context, checkers_state_t *state);
bool checkers_local_turn(const checkers_state_t *state);

#endif
