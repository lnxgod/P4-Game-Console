// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_REQUEST_BYTES = 8,
    NET_HAND_COUNTS_OFFSET = 14,
    NET_HANDS_OFFSET = 18,
    NET_TURN_OFFSET = 50,
    NET_ROUND_OFFSET = 52,
    NET_RECEIVE_LIMIT = 8,
    NET_SYNC_INTERVAL_MS = 1000,
};

_Static_assert(P4_RUMMY_NETWORK_MESSAGE_BYTES <=
                   P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "P4 Rummy snapshot exceeds the Game API ceiling");

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)bytes[0] |
        (uint16_t)((uint16_t)bytes[1] << 8U);
}

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

static uint8_t stock_remaining(const p4_rummy_state_t *state)
{
    return state->deck_index <= state->deck_count
        ? (uint8_t)(state->deck_count - state->deck_index) : 0U;
}

static bool encode_snapshot(
    const p4_rummy_state_t *state,
    uint8_t bytes[P4_RUMMY_NETWORK_MESSAGE_BYTES])
{
    if (state == NULL || bytes == NULL || state->revision == 0U ||
        state->phase > P4_RUMMY_PHASE_ROUND_OVER ||
        state->player_count < P4_RUMMY_MIN_PLAYERS ||
        state->player_count > P4_RUMMY_MAX_PLAYERS ||
        state->network_player_count < P4_RUMMY_MIN_PLAYERS ||
        state->network_player_count > state->player_count ||
        state->current_player >= state->player_count) {
        return false;
    }
    memset(bytes, 0, P4_RUMMY_NETWORK_MESSAGE_BYTES);
    bytes[0] = P4_RUMMY_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    write_u32(bytes + 2U, state->revision);
    bytes[6] = (uint8_t)state->phase;
    bytes[7] = state->player_count;
    bytes[8] = state->network_player_count;
    bytes[9] = state->current_player;
    bytes[10] = state->cpu_mask;
    bytes[11] = state->winner;
    bytes[12] = state->discard_count == 0U ? P4_RUMMY_NO_CARD
        : state->discard[state->discard_count - 1U];
    bytes[13] = stock_remaining(state);
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        bytes[NET_HAND_COUNTS_OFFSET + player] = state->hand_counts[player];
        memcpy(bytes + NET_HANDS_OFFSET +
                   (size_t)player * P4_RUMMY_DRAWN_CARDS,
               state->hands[player], P4_RUMMY_DRAWN_CARDS);
    }
    write_u16(bytes + NET_TURN_OFFSET, state->turn_count);
    write_u16(bytes + NET_ROUND_OFFSET, state->round_number);
    bytes[54] = state->drawn_card_index;
    return true;
}

static bool snapshot_cards_valid(const uint8_t *bytes,
                                 p4_rummy_phase_t phase,
                                 uint8_t player_count,
                                 uint8_t current_player)
{
    uint64_t seen = 0U;
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        const uint8_t count = bytes[NET_HAND_COUNTS_OFFSET + player];
        uint8_t expected = 0U;
        if (phase != P4_RUMMY_PHASE_SETUP && player < player_count) {
            expected = phase == P4_RUMMY_PHASE_DISCARD &&
                player == current_player ? P4_RUMMY_DRAWN_CARDS
                                          : P4_RUMMY_HAND_CARDS;
        }
        if (count != expected) {
            return false;
        }
        for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
            const uint8_t card = bytes[NET_HANDS_OFFSET +
                (size_t)player * P4_RUMMY_DRAWN_CARDS + index];
            if (index >= count) {
                if (card != P4_RUMMY_NO_CARD) {
                    return false;
                }
                continue;
            }
            if (card >= P4_RUMMY_DECK_CARDS ||
                (seen & (UINT64_C(1) << card)) != 0U) {
                return false;
            }
            seen |= UINT64_C(1) << card;
        }
    }
    const uint8_t top = bytes[12];
    if (phase == P4_RUMMY_PHASE_SETUP) {
        return top == P4_RUMMY_NO_CARD;
    }
    if (top == P4_RUMMY_NO_CARD) {
        return phase == P4_RUMMY_PHASE_DISCARD;
    }
    return top < P4_RUMMY_DECK_CARDS &&
        (seen & (UINT64_C(1) << top)) == 0U;
}

static bool snapshot_valid(const p4_rummy_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL ||
        byte_count != P4_RUMMY_NETWORK_MESSAGE_BYTES ||
        bytes[0] != P4_RUMMY_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 2U);
    const p4_rummy_phase_t phase = (p4_rummy_phase_t)bytes[6];
    const uint8_t players = bytes[7];
    const uint8_t network_players = bytes[8];
    const uint8_t current = bytes[9];
    const uint8_t cpu_mask = bytes[10];
    const uint8_t winner = bytes[11];
    const uint8_t drawn_index = bytes[54];
    if (revision == 0U || revision < state->revision ||
        phase > P4_RUMMY_PHASE_ROUND_OVER ||
        players < P4_RUMMY_MIN_PLAYERS ||
        players > P4_RUMMY_MAX_PLAYERS ||
        network_players != state->network_player_count ||
        players < network_players || current >= players ||
        bytes[13] > P4_RUMMY_DECK_CARDS ||
        read_u16(bytes + NET_TURN_OFFSET) > P4_RUMMY_TURN_LIMIT ||
        (phase == P4_RUMMY_PHASE_DISCARD
             ? (drawn_index != P4_RUMMY_NO_CARD &&
                drawn_index != P4_RUMMY_HAND_CARDS)
             : drawn_index != P4_RUMMY_NO_CARD)) {
        return false;
    }
    const uint8_t valid_bits = (uint8_t)((UINT8_C(1) << players) - 1U);
    const uint8_t human_bits = (uint8_t)(
        (UINT8_C(1) << network_players) - 1U);
    const uint8_t expected_cpus = (uint8_t)(
        valid_bits & (uint8_t)~human_bits);
    if (cpu_mask != expected_cpus ||
        (phase == P4_RUMMY_PHASE_ROUND_OVER
             ? winner >= players : winner != P4_RUMMY_NO_PLAYER)) {
        return false;
    }
    return snapshot_cards_valid(bytes, phase, players, current);
}

static bool apply_snapshot(p4_rummy_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (!snapshot_valid(state, bytes, byte_count)) {
        return false;
    }
    state->revision = read_u32(bytes + 2U);
    state->phase = (p4_rummy_phase_t)bytes[6];
    state->player_count = bytes[7];
    state->current_player = bytes[9];
    state->cpu_mask = bytes[10];
    state->winner = bytes[11];
    state->discard_count = bytes[12] == P4_RUMMY_NO_CARD ? 0U : 1U;
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    if (state->discard_count != 0U) {
        state->discard[0] = bytes[12];
    }
    state->deck_count = P4_RUMMY_DECK_CARDS;
    state->deck_index = (uint8_t)(P4_RUMMY_DECK_CARDS - bytes[13]);
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        state->hand_counts[player] = bytes[NET_HAND_COUNTS_OFFSET + player];
        memcpy(state->hands[player], bytes + NET_HANDS_OFFSET +
                   (size_t)player * P4_RUMMY_DRAWN_CARDS,
               P4_RUMMY_DRAWN_CARDS);
    }
    state->turn_count = read_u16(bytes + NET_TURN_OFFSET);
    state->round_number = read_u16(bytes + NET_ROUND_OFFSET);
    state->drawn_card_index = bytes[54];
    state->selected_card = 0U;
    if (state->phase == P4_RUMMY_PHASE_DISCARD &&
        state->current_player == state->local_player_slot) {
        state->selected_card = (uint8_t)(
            state->hand_counts[state->current_player] - 1U);
    }
    state->network_started = true;
    state->network_request_pending = false;
    state->network_retry_ms = 0U;
    state->cpu_think_ms = 0U;
    state->pass_required = false;
    return true;
}

static bool send_snapshot(p4_game_context_t *context,
                          const p4_rummy_state_t *state)
{
    uint8_t bytes[P4_RUMMY_NETWORK_MESSAGE_BYTES];
    return encode_snapshot(state, bytes) &&
        p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static bool send_request(p4_game_context_t *context,
                         const p4_rummy_state_t *state,
                         uint8_t kind, uint8_t argument)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        P4_RUMMY_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U,
        argument, 0U,
    };
    write_u32(bytes + 2U, state->revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void play_draw_tone(p4_game_context_t *context)
{
    (void)p4_game_play_tone(context, 440U, 35U, 2U, P4_WAVE_TRIANGLE);
}

static void play_discard_tone(p4_game_context_t *context, bool won)
{
    (void)p4_game_play_tone(context, won ? 880U : 587U,
                            won ? 140U : 45U, won ? 5U : 3U,
                            P4_WAVE_SQUARE);
}

void p4_rummy_mark_snapshot_dirty(p4_rummy_state_t *state)
{
    if (state != NULL && state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        state->network_snapshot_dirty = true;
    }
}

bool p4_rummy_network_begin(p4_game_context_t *context,
                            p4_rummy_state_t *state)
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
        profile.min_players != P4_RUMMY_MIN_PLAYERS ||
        profile.max_players != P4_RUMMY_MAX_PLAYERS ||
        profile.message_bytes != P4_RUMMY_NETWORK_MESSAGE_BYTES ||
        profile.protocol != P4_RUMMY_NETWORK_PROTOCOL ||
        !p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count < P4_RUMMY_MIN_PLAYERS ||
        status.player_count > P4_RUMMY_MAX_PLAYERS ||
        status.local_player_slot >= status.player_count ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        return false;
    }
    const uint32_t seed = (uint32_t)status.session_seed ^
        (uint32_t)(status.session_seed >> 32U);
    p4_rummy_reset_lobby(state, status.player_count, seed);
    state->network_mode = true;
    state->network_role = status.role;
    state->network_player_count = status.player_count;
    state->local_player_slot = status.local_player_slot;
    state->session_seed = status.session_seed;
    state->network_started = status.role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    state->network_snapshot_dirty = state->network_started;
    state->human_player_count = status.player_count;
    state->cpu_mask = 0U;
    return true;
}

static void fall_back_to_local(p4_rummy_state_t *state)
{
    const uint8_t humans = state->network_player_count >= 1U
        ? state->network_player_count : 1U;
    const uint32_t seed = state->rng ^ UINT32_C(0x4c4f5354);
    p4_rummy_reset_lobby(state, humans, seed);
    state->network_error = true;
    state->peer_lost_fallback = true;
}

bool p4_rummy_perform_draw(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           p4_rummy_draw_source_t source)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DRAW) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        const uint8_t kind = source == P4_RUMMY_DRAW_DISCARD
            ? P4_RUMMY_NET_DRAW_DISCARD : P4_RUMMY_NET_DRAW_STOCK;
        if (!send_request(context, state, kind, 0U)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_draw(state, state->current_player, source)) {
        return false;
    }
    play_draw_tone(context);
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

bool p4_rummy_perform_discard(p4_game_context_t *context,
                              p4_rummy_state_t *state,
                              uint8_t hand_index)
{
    if (state == NULL || !p4_rummy_local_turn(state) ||
        state->phase != P4_RUMMY_PHASE_DISCARD ||
        hand_index >= state->hand_counts[state->current_player] ||
        hand_index == state->drawn_card_index) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        if (!send_request(context, state, P4_RUMMY_NET_DISCARD,
                          hand_index)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!p4_rummy_discard_card(
            state, state->current_player, hand_index)) {
        return false;
    }
    play_discard_tone(context,
                      state->phase == P4_RUMMY_PHASE_ROUND_OVER);
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

bool p4_rummy_request_new_round(p4_game_context_t *context,
                                p4_rummy_state_t *state)
{
    (void)context;
    if (state == NULL || state->phase != P4_RUMMY_PHASE_ROUND_OVER ||
        (state->network_mode &&
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST) ||
        !p4_rummy_begin_round(state)) {
        return false;
    }
    p4_rummy_mark_snapshot_dirty(state);
    return true;
}

static void host_handle_request(
    p4_game_context_t *context, p4_rummy_state_t *state,
    const p4_game_multiplayer_message_t *message)
{
    if (message->bytes != NET_REQUEST_BYTES ||
        message->data[0] != P4_RUMMY_NETWORK_PROTOCOL) {
        return;
    }
    const uint8_t kind = message->data[1];
    if (kind == 0U) {
        state->network_snapshot_dirty = true;
        return;
    }
    if (read_u32(message->data + 2U) != state->revision ||
        message->player_slot != state->current_player) {
        state->network_snapshot_dirty = true;
        return;
    }
    bool changed = false;
    if (kind == P4_RUMMY_NET_DRAW_STOCK) {
        changed = p4_rummy_draw(state, message->player_slot,
                                P4_RUMMY_DRAW_STOCK);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DRAW_DISCARD) {
        changed = p4_rummy_draw(state, message->player_slot,
                                P4_RUMMY_DRAW_DISCARD);
        if (changed) {
            play_draw_tone(context);
        }
    } else if (kind == P4_RUMMY_NET_DISCARD) {
        changed = p4_rummy_discard_card(
            state, message->player_slot, message->data[6]);
        if (changed) {
            play_discard_tone(
                context, state->phase == P4_RUMMY_PHASE_ROUND_OVER);
        }
    }
    if (changed) {
        state->network_snapshot_dirty = true;
    }
}

void p4_rummy_network_poll(p4_game_context_t *context,
                           p4_rummy_state_t *state,
                           uint32_t elapsed_ms)
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
        status.player_count != state->network_player_count ||
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
        if (message.player_slot >= state->network_player_count ||
            message.player_slot == state->local_player_slot ||
            message.sequence == 0U ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
            host_handle_request(context, state, &message);
        } else if (message.bytes == P4_RUMMY_NETWORK_MESSAGE_BYTES) {
            (void)apply_snapshot(state, message.data, message.bytes);
        }
    }

    if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        if ((state->network_snapshot_dirty ||
             state->network_retry_ms >= NET_SYNC_INTERVAL_MS) &&
            send_snapshot(context, state)) {
            state->network_snapshot_dirty = false;
            state->network_retry_ms = 0U;
        }
    } else if ((!state->network_started || state->network_request_pending) &&
               state->network_retry_ms >= NET_SYNC_INTERVAL_MS &&
               send_request(context, state, 0U, 0U)) {
        state->network_retry_ms = 0U;
    }
}
