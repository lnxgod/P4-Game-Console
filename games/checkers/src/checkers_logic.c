// SPDX-License-Identifier: MIT

#include "checkers_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int square_row(uint8_t square)
{
    return (int)square / CHECKERS_BOARD_SIDE;
}

static int square_column(uint8_t square)
{
    return (int)square % CHECKERS_BOARD_SIDE;
}

static bool coordinates_valid(int row, int column)
{
    return row >= 0 && row < CHECKERS_BOARD_SIDE &&
        column >= 0 && column < CHECKERS_BOARD_SIDE;
}

static uint8_t square_at(int row, int column)
{
    return (uint8_t)(row * CHECKERS_BOARD_SIDE + column);
}

static bool piece_is_king(uint8_t piece)
{
    return piece == CHECKERS_RED_KING || piece == CHECKERS_WHITE_KING;
}

bool checkers_piece_belongs_to(uint8_t piece, uint8_t player)
{
    if (player == CHECKERS_PLAYER_RED) {
        return piece == CHECKERS_RED_MAN || piece == CHECKERS_RED_KING;
    }
    if (player == CHECKERS_PLAYER_WHITE) {
        return piece == CHECKERS_WHITE_MAN || piece == CHECKERS_WHITE_KING;
    }
    return false;
}

static bool direction_allowed(uint8_t piece, int row_delta)
{
    if (piece_is_king(piece)) {
        return true;
    }
    if (piece == CHECKERS_RED_MAN) {
        return row_delta > 0;
    }
    if (piece == CHECKERS_WHITE_MAN) {
        return row_delta < 0;
    }
    return false;
}

static bool piece_can_step(const checkers_state_t *state, uint8_t square)
{
    if (state == NULL || square >= CHECKERS_BOARD_SQUARES) {
        return false;
    }
    const uint8_t piece = state->board[square];
    if (piece == CHECKERS_EMPTY) {
        return false;
    }
    const int row = square_row(square);
    const int column = square_column(square);
    static const int directions[2] = {-1, 1};
    for (size_t row_index = 0U; row_index < 2U; ++row_index) {
        const int row_delta = directions[row_index];
        if (!direction_allowed(piece, row_delta)) {
            continue;
        }
        for (size_t column_index = 0U; column_index < 2U; ++column_index) {
            const int next_row = row + row_delta;
            const int next_column = column + directions[column_index];
            if (coordinates_valid(next_row, next_column) &&
                state->board[square_at(next_row, next_column)] ==
                    CHECKERS_EMPTY) {
                return true;
            }
        }
    }
    return false;
}

bool checkers_piece_can_capture(const checkers_state_t *state,
                                uint8_t square)
{
    if (state == NULL || square >= CHECKERS_BOARD_SQUARES) {
        return false;
    }
    const uint8_t piece = state->board[square];
    uint8_t player = CHECKERS_PLAYER_RED;
    if (checkers_piece_belongs_to(piece, CHECKERS_PLAYER_WHITE)) {
        player = CHECKERS_PLAYER_WHITE;
    } else if (!checkers_piece_belongs_to(piece, CHECKERS_PLAYER_RED)) {
        return false;
    }
    const int row = square_row(square);
    const int column = square_column(square);
    static const int directions[2] = {-1, 1};
    for (size_t row_index = 0U; row_index < 2U; ++row_index) {
        const int row_delta = directions[row_index] * 2;
        if (!direction_allowed(piece, row_delta)) {
            continue;
        }
        for (size_t column_index = 0U; column_index < 2U; ++column_index) {
            const int column_delta = directions[column_index] * 2;
            const int landing_row = row + row_delta;
            const int landing_column = column + column_delta;
            const int middle_row = row + row_delta / 2;
            const int middle_column = column + column_delta / 2;
            if (!coordinates_valid(landing_row, landing_column)) {
                continue;
            }
            const uint8_t middle = state->board[
                square_at(middle_row, middle_column)];
            if (state->board[square_at(landing_row, landing_column)] ==
                    CHECKERS_EMPTY &&
                middle != CHECKERS_EMPTY &&
                !checkers_piece_belongs_to(middle, player)) {
                return true;
            }
        }
    }
    return false;
}

bool checkers_player_has_capture(const checkers_state_t *state,
                                 uint8_t player)
{
    if (state == NULL || player >= CHECKERS_PLAYER_COUNT) {
        return false;
    }
    for (uint8_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        if (checkers_piece_belongs_to(state->board[square], player) &&
            checkers_piece_can_capture(state, square)) {
            return true;
        }
    }
    return false;
}

bool checkers_player_has_move(const checkers_state_t *state, uint8_t player)
{
    if (state == NULL || player >= CHECKERS_PLAYER_COUNT) {
        return false;
    }
    if (checkers_player_has_capture(state, player)) {
        return true;
    }
    for (uint8_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        if (checkers_piece_belongs_to(state->board[square], player) &&
            piece_can_step(state, square)) {
            return true;
        }
    }
    return false;
}

bool checkers_move_is_legal(const checkers_state_t *state,
                            uint8_t from, uint8_t to)
{
    if (state == NULL || state->phase != CHECKERS_PHASE_PLAYING ||
        from >= CHECKERS_BOARD_SQUARES || to >= CHECKERS_BOARD_SQUARES ||
        state->board[to] != CHECKERS_EMPTY ||
        !checkers_piece_belongs_to(state->board[from],
                                   state->current_player) ||
        (state->forced_piece != CHECKERS_NO_SQUARE &&
         state->forced_piece != from)) {
        return false;
    }
    const int from_row = square_row(from);
    const int from_column = square_column(from);
    const int row_delta = square_row(to) - from_row;
    const int column_delta = square_column(to) - from_column;
    const int row_distance = row_delta < 0 ? -row_delta : row_delta;
    const int column_distance = column_delta < 0
        ? -column_delta : column_delta;
    if (row_distance != column_distance ||
        !direction_allowed(state->board[from], row_delta)) {
        return false;
    }
    if (row_distance == 1) {
        return state->forced_piece == CHECKERS_NO_SQUARE &&
            !checkers_player_has_capture(state, state->current_player);
    }
    if (row_distance != 2) {
        return false;
    }
    const uint8_t middle = state->board[square_at(
        from_row + row_delta / 2, from_column + column_delta / 2)];
    return middle != CHECKERS_EMPTY &&
        !checkers_piece_belongs_to(middle, state->current_player);
}

uint8_t checkers_default_cursor(const checkers_state_t *state,
                                uint8_t player)
{
    if (state == NULL || player >= CHECKERS_PLAYER_COUNT) {
        return 0U;
    }
    const bool capture_required = checkers_player_has_capture(state, player);
    for (uint8_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        if (!checkers_piece_belongs_to(state->board[square], player)) {
            continue;
        }
        if ((capture_required && checkers_piece_can_capture(state, square)) ||
            (!capture_required && piece_can_step(state, square))) {
            return square;
        }
    }
    return 0U;
}

static void finish_turn(checkers_state_t *state, uint8_t mover)
{
    state->forced_piece = CHECKERS_NO_SQUARE;
    state->selected = CHECKERS_NO_SQUARE;
    state->current_player = (uint8_t)(mover ^ 1U);
    state->cursor = checkers_default_cursor(state, state->current_player);
    const uint8_t next_count = state->current_player == CHECKERS_PLAYER_RED
        ? state->red_count : state->white_count;
    if (next_count == 0U ||
        !checkers_player_has_move(state, state->current_player)) {
        state->winner = mover;
        state->phase = CHECKERS_PHASE_GAME_OVER;
    } else if (state->quiet_ply >= CHECKERS_DRAW_QUIET_PLY) {
        state->winner = CHECKERS_WINNER_DRAW;
        state->phase = CHECKERS_PHASE_GAME_OVER;
    }
}

bool checkers_apply_move(checkers_state_t *state, uint8_t from, uint8_t to,
                         uint8_t *event_flags)
{
    if (!checkers_move_is_legal(state, from, to)) {
        return false;
    }
    uint8_t flags = 0U;
    const uint8_t mover = state->current_player;
    uint8_t piece = state->board[from];
    const int row_delta = square_row(to) - square_row(from);
    const bool captured = row_delta == 2 || row_delta == -2;
    state->board[from] = CHECKERS_EMPTY;
    if (captured) {
        const int middle_row = (square_row(from) + square_row(to)) / 2;
        const int middle_column =
            (square_column(from) + square_column(to)) / 2;
        const uint8_t middle = square_at(middle_row, middle_column);
        if (checkers_piece_belongs_to(state->board[middle],
                                      CHECKERS_PLAYER_RED)) {
            --state->red_count;
        } else {
            --state->white_count;
        }
        state->board[middle] = CHECKERS_EMPTY;
        state->quiet_ply = 0U;
        flags |= CHECKERS_MOVE_CAPTURED;
    } else if (state->quiet_ply < CHECKERS_DRAW_QUIET_PLY) {
        ++state->quiet_ply;
    }

    const int destination_row = square_row(to);
    const bool crowned =
        (piece == CHECKERS_RED_MAN && destination_row == 7) ||
        (piece == CHECKERS_WHITE_MAN && destination_row == 0);
    if (crowned) {
        piece = mover == CHECKERS_PLAYER_RED
            ? CHECKERS_RED_KING : CHECKERS_WHITE_KING;
        state->quiet_ply = 0U;
        flags |= CHECKERS_MOVE_CROWNED;
    }
    state->board[to] = piece;
    state->cursor = to;
    ++state->revision;

    if (captured && !crowned && checkers_piece_can_capture(state, to)) {
        state->forced_piece = to;
        state->selected = to;
        flags |= CHECKERS_MOVE_CONTINUES;
    } else {
        finish_turn(state, mover);
    }
    if (state->phase == CHECKERS_PHASE_GAME_OVER) {
        flags |= CHECKERS_MOVE_GAME_OVER;
    }
    if (event_flags != NULL) {
        *event_flags = flags;
    }
    return true;
}

void checkers_reset_board(checkers_state_t *state, uint32_t revision)
{
    if (state == NULL) {
        return;
    }
    memset(state->board, 0, sizeof(state->board));
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < CHECKERS_BOARD_SIDE; ++column) {
            if (((row + column) & 1) != 0) {
                state->board[square_at(row, column)] = CHECKERS_RED_MAN;
            }
        }
    }
    for (int row = 5; row < CHECKERS_BOARD_SIDE; ++row) {
        for (int column = 0; column < CHECKERS_BOARD_SIDE; ++column) {
            if (((row + column) & 1) != 0) {
                state->board[square_at(row, column)] = CHECKERS_WHITE_MAN;
            }
        }
    }
    state->red_count = 12U;
    state->white_count = 12U;
    state->current_player = CHECKERS_PLAYER_RED;
    state->winner = CHECKERS_NO_WINNER;
    state->quiet_ply = 0U;
    state->phase = CHECKERS_PHASE_PLAYING;
    state->revision = revision == 0U ? 1U : revision;
    state->forced_piece = CHECKERS_NO_SQUARE;
    state->selected = CHECKERS_NO_SQUARE;
    state->cursor = checkers_default_cursor(state, state->current_player);
    state->network_request_pending = false;
}
