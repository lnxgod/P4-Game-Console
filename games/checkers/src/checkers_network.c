// SPDX-License-Identifier: MIT

#include "checkers_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_REQUEST_BYTES = 8,
    NET_BOARD_OFFSET = 14,
    NET_BOARD_BYTES = 24,
    NET_SNAPSHOT_BYTES = NET_BOARD_OFFSET + NET_BOARD_BYTES,
    NET_RECEIVE_LIMIT = 4,
    NET_SYNC_INTERVAL_MS = 2000,
};

_Static_assert(NET_SNAPSHOT_BYTES == 38,
               "Checkers manifest message_bytes must match its snapshot");
_Static_assert(NET_SNAPSHOT_BYTES <= P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "Checkers snapshot exceeds the Game API message ceiling");

static void write_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
        (uint32_t)bytes[1] << 8U |
        (uint32_t)bytes[2] << 16U |
        (uint32_t)bytes[3] << 24U;
}

static void pack_piece(uint8_t bytes[NET_SNAPSHOT_BYTES],
                       size_t square, uint8_t piece)
{
    const size_t bit = square * 3U;
    const size_t offset = NET_BOARD_OFFSET + bit / 8U;
    const unsigned shift = (unsigned)(bit % 8U);
    const uint16_t value = (uint16_t)((uint16_t)piece << shift);
    bytes[offset] |= (uint8_t)value;
    if (shift > 5U) {
        bytes[offset + 1U] |= (uint8_t)(value >> 8U);
    }
}

static uint8_t unpack_piece(const uint8_t bytes[NET_SNAPSHOT_BYTES],
                            size_t square)
{
    const size_t bit = square * 3U;
    const size_t offset = NET_BOARD_OFFSET + bit / 8U;
    const unsigned shift = (unsigned)(bit % 8U);
    uint16_t value = bytes[offset];
    if (shift > 5U) {
        value |= (uint16_t)bytes[offset + 1U] << 8U;
    }
    return (uint8_t)((value >> shift) & UINT16_C(0x0007));
}

static bool encode_snapshot(const checkers_state_t *state,
                            uint8_t bytes[NET_SNAPSHOT_BYTES])
{
    if (state == NULL || bytes == NULL ||
        state->current_player >= CHECKERS_PLAYER_COUNT ||
        state->revision == 0U ||
        state->quiet_ply > CHECKERS_DRAW_QUIET_PLY) {
        return false;
    }
    memset(bytes, 0, NET_SNAPSHOT_BYTES);
    bytes[0] = CHECKERS_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    write_u32(bytes + 2U, state->revision);
    bytes[6] = state->current_player;
    bytes[7] = state->forced_piece;
    bytes[8] = state->winner;
    bytes[9] = (uint8_t)state->phase;
    bytes[10] = state->red_count;
    bytes[11] = state->white_count;
    bytes[12] = (uint8_t)state->quiet_ply;
    bytes[13] = (uint8_t)(state->quiet_ply >> 8U);
    for (size_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        if (state->board[square] > CHECKERS_WHITE_KING) {
            return false;
        }
        pack_piece(bytes, square, state->board[square]);
    }
    return true;
}

static bool send_snapshot(p4_game_context_t *context,
                          const checkers_state_t *state)
{
    uint8_t bytes[NET_SNAPSHOT_BYTES];
    return encode_snapshot(state, bytes) &&
        p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static bool snapshot_state_valid(const checkers_state_t *candidate)
{
    if (candidate->current_player >= CHECKERS_PLAYER_COUNT ||
        candidate->revision == 0U ||
        candidate->quiet_ply > CHECKERS_DRAW_QUIET_PLY ||
        candidate->phase > CHECKERS_PHASE_GAME_OVER) {
        return false;
    }
    uint8_t red_count = 0U;
    uint8_t white_count = 0U;
    for (uint8_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        const uint8_t piece = candidate->board[square];
        const int row = (int)square / CHECKERS_BOARD_SIDE;
        const int column = (int)square % CHECKERS_BOARD_SIDE;
        if (piece > CHECKERS_WHITE_KING ||
            (piece != CHECKERS_EMPTY && ((row + column) & 1) == 0)) {
            return false;
        }
        if (checkers_piece_belongs_to(piece, CHECKERS_PLAYER_RED)) {
            ++red_count;
        } else if (checkers_piece_belongs_to(piece, CHECKERS_PLAYER_WHITE)) {
            ++white_count;
        }
    }
    if (red_count != candidate->red_count ||
        white_count != candidate->white_count) {
        return false;
    }
    if (candidate->phase == CHECKERS_PHASE_PLAYING) {
        if (candidate->winner != CHECKERS_NO_WINNER ||
            red_count == 0U || white_count == 0U ||
            !checkers_player_has_move(candidate,
                                      candidate->current_player)) {
            return false;
        }
        if (candidate->forced_piece == CHECKERS_NO_SQUARE) {
            return true;
        }
        return candidate->forced_piece < CHECKERS_BOARD_SQUARES &&
            checkers_piece_belongs_to(
                candidate->board[candidate->forced_piece],
                candidate->current_player) &&
            checkers_piece_can_capture(candidate, candidate->forced_piece);
    }
    if (candidate->forced_piece != CHECKERS_NO_SQUARE ||
        candidate->selected != CHECKERS_NO_SQUARE) {
        return false;
    }
    if (candidate->winner == CHECKERS_WINNER_DRAW) {
        return candidate->quiet_ply == CHECKERS_DRAW_QUIET_PLY;
    }
    if (candidate->winner >= CHECKERS_PLAYER_COUNT ||
        candidate->current_player == candidate->winner) {
        return false;
    }
    const uint8_t loser_count =
        candidate->current_player == CHECKERS_PLAYER_RED
            ? red_count : white_count;
    return loser_count == 0U ||
        !checkers_player_has_move(candidate, candidate->current_player);
}

static bool apply_snapshot(checkers_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL ||
        byte_count != NET_SNAPSHOT_BYTES ||
        bytes[0] != CHECKERS_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT) {
        return false;
    }
    checkers_state_t candidate = *state;
    candidate.revision = read_u32(bytes + 2U);
    if (candidate.revision < state->revision) {
        return false;
    }
    candidate.current_player = bytes[6];
    candidate.forced_piece = bytes[7];
    candidate.winner = bytes[8];
    candidate.phase = (checkers_phase_t)bytes[9];
    candidate.red_count = bytes[10];
    candidate.white_count = bytes[11];
    candidate.quiet_ply = (uint16_t)bytes[12] |
        (uint16_t)((uint16_t)bytes[13] << 8U);
    candidate.selected = candidate.forced_piece;
    for (size_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        candidate.board[square] = unpack_piece(bytes, square);
    }
    if (!snapshot_state_valid(&candidate)) {
        return false;
    }
    if (state->network_started && candidate.revision == state->revision) {
        /* Periodic snapshots acknowledge unchanged authoritative state; they
         * must not cancel a local selection or an in-progress touch gesture.
         * A changed state requires a new revision, even if it is valid. */
        if (memcmp(state->board, candidate.board, sizeof(state->board)) != 0 ||
            state->current_player != candidate.current_player ||
            state->forced_piece != candidate.forced_piece ||
            state->winner != candidate.winner ||
            state->phase != candidate.phase ||
            state->red_count != candidate.red_count ||
            state->white_count != candidate.white_count ||
            state->quiet_ply != candidate.quiet_ply) {
            return false;
        }
        state->network_request_pending = false;
        state->network_retry_ms = 0U;
        return true;
    }
    memcpy(state->board, candidate.board, sizeof(state->board));
    state->current_player = candidate.current_player;
    state->forced_piece = candidate.forced_piece;
    state->selected = candidate.forced_piece;
    state->winner = candidate.winner;
    state->phase = candidate.phase;
    state->red_count = candidate.red_count;
    state->white_count = candidate.white_count;
    state->quiet_ply = candidate.quiet_ply;
    state->revision = candidate.revision;
    state->cursor = candidate.forced_piece != CHECKERS_NO_SQUARE
        ? candidate.forced_piece
        : checkers_default_cursor(state, state->current_player);
    state->network_started = true;
    state->network_request_pending = false;
    state->network_retry_ms = 0U;
    return true;
}

static bool send_request(p4_game_context_t *context,
                         const checkers_state_t *state,
                         uint8_t kind, uint8_t from, uint8_t to)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        CHECKERS_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U, from, to,
    };
    write_u32(bytes + 2U, state->revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void play_move_tone(p4_game_context_t *context, uint8_t flags)
{
    if ((flags & CHECKERS_MOVE_GAME_OVER) != 0U) {
        (void)p4_game_play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 784U, 150U, 4U, P4_WAVE_TRIANGLE);
    } else if ((flags & CHECKERS_MOVE_CROWNED) != 0U) {
        (void)p4_game_play_tone(context, 659U, 75U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 988U, 100U, 4U, P4_WAVE_TRIANGLE);
    } else if ((flags & CHECKERS_MOVE_CAPTURED) != 0U) {
        (void)p4_game_play_tone(context, 294U, 55U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 440U, 75U, 3U, P4_WAVE_TRIANGLE);
    } else {
        (void)p4_game_play_tone(context, 392U, 38U, 2U, P4_WAVE_TRIANGLE);
    }
}

bool checkers_local_turn(const checkers_state_t *state)
{
    return state != NULL && state->phase == CHECKERS_PHASE_PLAYING &&
        (!state->network_mode ||
         (state->network_started && !state->network_request_pending &&
          state->local_player_slot == state->current_player));
}

bool checkers_perform_move(p4_game_context_t *context,
                           checkers_state_t *state,
                           uint8_t from, uint8_t to,
                           uint8_t *event_flags)
{
    if (!checkers_local_turn(state) ||
        !checkers_move_is_legal(state, from, to)) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, CHECKERS_NET_MOVE, from, to)) {
            return false;
        }
        state->network_request_pending = true;
        if (event_flags != NULL) {
            *event_flags = 0U;
        }
        return true;
    }
    uint8_t flags = 0U;
    if (!checkers_apply_move(state, from, to, &flags)) {
        return false;
    }
    if (state->network_mode) {
        state->network_snapshot_dirty = true;
    }
    if (event_flags != NULL) {
        *event_flags = flags;
    }
    return true;
}

bool checkers_restart(p4_game_context_t *context, checkers_state_t *state)
{
    if (state == NULL || state->phase != CHECKERS_PHASE_GAME_OVER) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, CHECKERS_NET_RESTART, 0U, 0U)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    const uint32_t next_revision = state->revision == UINT32_MAX
        ? 1U : state->revision + 1U;
    checkers_reset_board(state, next_revision);
    if (state->network_mode) {
        state->network_snapshot_dirty = true;
    }
    return true;
}

bool checkers_network_begin(p4_game_context_t *context,
                            checkers_state_t *state)
{
    if (context == NULL || state == NULL || context->services == NULL ||
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) == 0U) {
        return false;
    }
    p4_game_multiplayer_profile_t profile;
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_profile(context, &profile) ||
        profile.style != P4_GAME_MULTIPLAYER_STYLE_TURN_BASED ||
        profile.min_players != CHECKERS_PLAYER_COUNT ||
        profile.max_players != CHECKERS_PLAYER_COUNT ||
        profile.message_bytes != NET_SNAPSHOT_BYTES ||
        profile.protocol != CHECKERS_NETWORK_PROTOCOL ||
        !p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count != CHECKERS_PLAYER_COUNT ||
        status.local_player_slot >= CHECKERS_PLAYER_COUNT ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        return false;
    }
    state->network_mode = true;
    state->network_role = status.role;
    state->local_player_slot = status.local_player_slot;
    state->network_started =
        status.role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    state->network_snapshot_dirty = state->network_started;
    state->network_retry_ms = NET_SYNC_INTERVAL_MS;
    state->network_request_pending = false;
    state->network_error = false;
    state->peer_lost_fallback = false;
    memset(state->last_network_sequence, 0,
           sizeof(state->last_network_sequence));
    return true;
}

static void fall_back_to_local(checkers_state_t *state)
{
    state->network_mode = false;
    state->network_started = false;
    state->network_role = P4_GAME_MULTIPLAYER_ROLE_NONE;
    state->local_player_slot = CHECKERS_PLAYER_RED;
    state->network_request_pending = false;
    state->network_snapshot_dirty = false;
    state->network_error = true;
    state->peer_lost_fallback = true;
    state->selected = state->forced_piece;
    state->cursor = state->forced_piece != CHECKERS_NO_SQUARE
        ? state->forced_piece
        : checkers_default_cursor(state, state->current_player);
}

static void host_handle_request(p4_game_context_t *context,
                                checkers_state_t *state,
                                const p4_game_multiplayer_message_t *message)
{
    if (message->bytes != NET_REQUEST_BYTES ||
        message->data[0] != CHECKERS_NETWORK_PROTOCOL) {
        return;
    }
    const uint8_t kind = message->data[1];
    if (kind == CHECKERS_NET_SYNC) {
        state->network_snapshot_dirty = true;
        return;
    }
    if (read_u32(message->data + 2U) != state->revision) {
        state->network_snapshot_dirty = true;
        return;
    }
    if (kind == CHECKERS_NET_MOVE &&
        message->player_slot == state->current_player) {
        uint8_t flags = 0U;
        if (checkers_apply_move(state, message->data[6], message->data[7],
                                &flags)) {
            state->network_snapshot_dirty = true;
            play_move_tone(context, flags);
        }
    } else if (kind == CHECKERS_NET_RESTART &&
               state->phase == CHECKERS_PHASE_GAME_OVER) {
        const uint32_t next_revision = state->revision == UINT32_MAX
            ? 1U : state->revision + 1U;
        checkers_reset_board(state, next_revision);
        state->network_snapshot_dirty = true;
        (void)p4_game_play_tone(
            context, 523U, 65U, 3U, P4_WAVE_TRIANGLE);
    }
}

void checkers_network_poll(p4_game_context_t *context,
                           checkers_state_t *state, uint32_t elapsed_ms)
{
    if (context == NULL || state == NULL || !state->network_mode) {
        return;
    }
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_status(context, &status) ||
        status.state == P4_GAME_MULTIPLAYER_PEER_LEFT ||
        status.state == P4_GAME_MULTIPLAYER_ERROR ||
        status.state == P4_GAME_MULTIPLAYER_OFFLINE) {
        fall_back_to_local(state);
        return;
    }
    if (status.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        return;
    }
    if (status.role != state->network_role ||
        status.player_count != CHECKERS_PLAYER_COUNT ||
        status.local_player_slot != state->local_player_slot) {
        fall_back_to_local(state);
        return;
    }
    if (UINT32_MAX - state->network_retry_ms < elapsed_ms) {
        state->network_retry_ms = NET_SYNC_INTERVAL_MS;
    } else {
        state->network_retry_ms += elapsed_ms;
    }

    p4_game_multiplayer_message_t message;
    for (size_t received = 0U;
         received < NET_RECEIVE_LIMIT &&
         p4_game_multiplayer_receive(context, &message);
         ++received) {
        if (message.player_slot >= CHECKERS_PLAYER_COUNT ||
            message.player_slot == state->local_player_slot ||
            message.sequence == 0U ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
            host_handle_request(context, state, &message);
        } else {
            (void)apply_snapshot(state, message.data, message.bytes);
        }
    }

    if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST &&
        (state->network_snapshot_dirty ||
         state->network_retry_ms >= NET_SYNC_INTERVAL_MS)) {
        if (send_snapshot(context, state)) {
            state->network_snapshot_dirty = false;
            state->network_retry_ms = 0U;
        }
    } else if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT &&
               (!state->network_started ||
                state->network_retry_ms >= NET_SYNC_INTERVAL_MS)) {
        if (send_request(context, state, CHECKERS_NET_SYNC, 0U, 0U)) {
            state->network_retry_ms = 0U;
        }
    }
}
