// SPDX-License-Identifier: MIT

#include "color_clash_internal.h"

#include <string.h>

enum {
    NET_KIND_PUBLIC_SNAPSHOT = 16,
    NET_KIND_HAND_CHUNK = 17,
    NET_SNAPSHOT_HEADER_BYTES = 27,
    NET_HAND_HEADER_BYTES = 10,
    NET_HAND_PAYLOAD_BYTES =
        P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES - NET_HAND_HEADER_BYTES,
    NET_REQUEST_BYTES = 7,
    NET_MAX_RECEIVE_PER_UPDATE = 16,
    NO_WINNER = 0xff,
};

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

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8U);
}

bool color_clash_network_available(const p4_game_context_t *context)
{
    return context != NULL && context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) != 0U;
}

bool color_clash_local_turn(const color_clash_state_t *state)
{
    return state != NULL &&
        (state->mode != COLOR_CLASH_NETWORK || state->network_started) &&
        state->current_player == state->local_player_slot;
}

static void play_feedback(p4_game_context_t *context, uint8_t kind,
                          bool accepted)
{
    if (!accepted) {
        (void)p4_game_play_tone(context, 150U, 80U, 3U, P4_WAVE_SQUARE);
    } else if (kind == COLOR_CLASH_NET_PLAY) {
        (void)p4_game_play_tone(context, 659U, 50U, 3U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 880U, 70U, 3U, P4_WAVE_TRIANGLE);
    } else if (kind == COLOR_CLASH_NET_DRAW) {
        (void)p4_game_play_tone(context, 220U, 70U, 3U, P4_WAVE_TRIANGLE);
    } else if (kind == COLOR_CLASH_NET_COLOR) {
        (void)p4_game_play_tone(context, 988U, 85U, 4U, P4_WAVE_TRIANGLE);
    } else if (kind == COLOR_CLASH_NET_HAND) {
        (void)p4_game_play_tone(context, 392U, 70U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 659U, 100U, 4U, P4_WAVE_TRIANGLE);
    } else if (kind == COLOR_CLASH_NET_RESTART) {
        (void)p4_game_play_tone(context, 523U, 60U, 3U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 784U, 90U, 3U, P4_WAVE_TRIANGLE);
    } else if (kind == COLOR_CLASH_NET_UNO) {
        (void)p4_game_play_tone(context, 784U, 70U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 1047U, 100U, 4U,
                               P4_WAVE_TRIANGLE);
    }
}

static void queue_sync(color_clash_state_t *state)
{
    state->network_sync_pending = true;
    state->network_sync_cursor = 0U;
    state->network_sync_stage = 0U;
    state->network_sync_offset = 0U;
}

static bool apply_authoritative_action(p4_game_context_t *context,
                                       color_clash_state_t *state,
                                       uint8_t player, uint8_t kind,
                                       uint8_t argument)
{
    bool changed = false;
    if (kind == COLOR_CLASH_NET_PLAY) {
        changed = color_clash_play_card(state, player, argument);
    } else if (kind == COLOR_CLASH_NET_DRAW) {
        changed = color_clash_draw_card(state, player);
    } else if (kind == COLOR_CLASH_NET_COLOR) {
        changed = color_clash_choose_color(state, player, argument);
    } else if (kind == COLOR_CLASH_NET_HAND) {
        changed = color_clash_choose_hand(state, player, argument);
    } else if (kind == COLOR_CLASH_NET_PASS) {
        changed = color_clash_pass_drawn_card(state, player);
    } else if (kind == COLOR_CLASH_NET_UNO) {
        changed = color_clash_call_uno(state, player);
    } else if (kind == COLOR_CLASH_NET_RESTART &&
               state->phase == COLOR_CLASH_GAME_OVER) {
        const uint32_t seed = state->rng ^ state->network_revision ^
            UINT32_C(0x52454d41);
        color_clash_reset_match(state, state->player_count, seed);
        changed = true;
    }
    play_feedback(context, kind, changed);
    if (changed && state->mode == COLOR_CLASH_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        ++state->network_revision;
        queue_sync(state);
    }
    return changed;
}

static bool send_request(p4_game_context_t *context,
                         const color_clash_state_t *state,
                         uint8_t kind, uint8_t argument)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        COLOR_CLASH_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U, argument,
    };
    write_u32(bytes + 2U, state->network_revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

bool color_clash_perform_action(p4_game_context_t *context,
                                color_clash_state_t *state,
                                uint8_t kind, uint8_t argument)
{
    if (context == NULL || state == NULL) {
        return false;
    }
    if (kind != COLOR_CLASH_NET_RESTART && kind != COLOR_CLASH_NET_UNO &&
        state->mode == COLOR_CLASH_NETWORK &&
        !color_clash_local_turn(state)) {
        return false;
    }
    if (state->mode == COLOR_CLASH_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        return send_request(context, state, kind, argument);
    }
    const uint8_t player = kind == COLOR_CLASH_NET_UNO
        ? state->local_player_slot : state->current_player;
    return apply_authoritative_action(context, state, player, kind, argument);
}

bool color_clash_perform_bot_action(p4_game_context_t *context,
                                    color_clash_state_t *state,
                                    uint8_t kind, uint8_t argument)
{
    if (context == NULL || state == NULL ||
        state->current_player >= state->player_count ||
        (state->mode == COLOR_CLASH_NETWORK &&
         (state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST ||
          state->current_player < state->human_player_count))) {
        return false;
    }
    return apply_authoritative_action(
        context, state, state->current_player, kind, argument);
}

static size_t encode_public_snapshot(const color_clash_state_t *state,
                                     uint8_t target, uint8_t bytes[64])
{
    if (state == NULL || bytes == NULL || target >= state->player_count ||
        state->hand_counts[target] > COLOR_CLASH_HAND_CAPACITY ||
        state->discard_count == 0U) {
        return 0U;
    }
    memset(bytes, 0, NET_SNAPSHOT_HEADER_BYTES);
    bytes[0] = COLOR_CLASH_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_PUBLIC_SNAPSHOT;
    write_u32(bytes + 2U, state->network_revision);
    bytes[6] = target;
    bytes[7] = state->player_count;
    bytes[8] = (uint8_t)state->phase;
    bytes[9] = state->current_player;
    bytes[10] = state->direction;
    bytes[11] = state->active_color;
    bytes[12] = state->discard[state->discard_count - 1U];
    bytes[13] = state->winner;
    bytes[14] = state->deck_count;
    bytes[15] = state->discard_count;
    for (uint8_t player = 0U; player < COLOR_CLASH_MAX_PLAYERS; ++player) {
        bytes[16U + player] = state->hand_counts[player];
    }
    bytes[20] = state->hand_counts[target];
    bytes[21] = state->phase == COLOR_CLASH_CHOOSE_HAND
        ? state->selected_target : state->selected_color;
    bytes[22] = state->uno_pending_player;
    bytes[23] = (uint8_t)state->notice;
    bytes[24] = state->notice_player;
    write_u16(bytes + 25U,
              state->notice_ms > UINT16_MAX
                  ? UINT16_MAX : (uint16_t)state->notice_ms);
    return NET_SNAPSHOT_HEADER_BYTES;
}

static size_t encode_hand_chunk(const color_clash_state_t *state,
                                uint8_t target, uint8_t offset,
                                uint8_t bytes[64])
{
    if (state == NULL || bytes == NULL || target >= state->player_count ||
        offset >= state->hand_counts[target]) {
        return 0U;
    }
    const uint8_t remaining = (uint8_t)(
        state->hand_counts[target] - offset);
    const uint8_t count = remaining > NET_HAND_PAYLOAD_BYTES
        ? NET_HAND_PAYLOAD_BYTES : remaining;
    const size_t byte_count = NET_HAND_HEADER_BYTES + count;
    memset(bytes, 0, byte_count);
    bytes[0] = COLOR_CLASH_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_HAND_CHUNK;
    write_u32(bytes + 2U, state->network_revision);
    bytes[6] = target;
    bytes[7] = offset;
    bytes[8] = count;
    bytes[9] = state->hand_counts[target];
    memcpy(bytes + NET_HAND_HEADER_BYTES,
           state->hands[target] + offset, count);
    return byte_count;
}

static bool apply_public_snapshot(color_clash_state_t *state,
                                  const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL ||
        byte_count != NET_SNAPSHOT_HEADER_BYTES ||
        bytes[0] != COLOR_CLASH_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_PUBLIC_SNAPSHOT ||
        bytes[6] != state->local_player_slot ||
        bytes[7] < 2U || bytes[7] > COLOR_CLASH_MAX_PLAYERS ||
        bytes[9] >= bytes[7] || bytes[10] > 1U ||
        bytes[11] >= COLOR_CLASH_COLOR_COUNT ||
        !color_clash_card_valid(bytes[12]) ||
        (bytes[13] != NO_WINNER && bytes[13] >= bytes[7]) ||
        bytes[14] > COLOR_CLASH_DECK_CARDS ||
        bytes[15] == 0U || bytes[15] > COLOR_CLASH_DECK_CARDS ||
        bytes[20] > COLOR_CLASH_HAND_CAPACITY ||
        (bytes[22] != NO_WINNER && bytes[22] >= bytes[7]) ||
        bytes[23] > COLOR_CLASH_NOTICE_UNO_CAUGHT ||
        (bytes[24] != NO_WINNER && bytes[24] >= bytes[7]) ||
        ((bytes[23] == COLOR_CLASH_NOTICE_UNO_CALLED ||
          bytes[23] == COLOR_CLASH_NOTICE_UNO_CAUGHT) &&
         bytes[24] == NO_WINNER)) {
        return false;
    }
    const color_clash_phase_t phase = (color_clash_phase_t)bytes[8];
    if (phase != COLOR_CLASH_TURN &&
        phase != COLOR_CLASH_DRAWN_CARD &&
        phase != COLOR_CLASH_CHOOSE_COLOR &&
        phase != COLOR_CLASH_CHOOSE_HAND &&
        phase != COLOR_CLASH_GAME_OVER) {
        return false;
    }
    if ((phase == COLOR_CLASH_CHOOSE_COLOR &&
         bytes[21] >= COLOR_CLASH_COLOR_COUNT) ||
        (phase == COLOR_CLASH_CHOOSE_HAND &&
         (bytes[21] >= bytes[7] || bytes[21] == bytes[9]))) {
        return false;
    }
    for (uint8_t player = 0U; player < COLOR_CLASH_MAX_PLAYERS; ++player) {
        if (bytes[16U + player] > COLOR_CLASH_HAND_CAPACITY ||
            (player >= bytes[7] && bytes[16U + player] != 0U)) {
            return false;
        }
    }
    if (bytes[20] != bytes[16U + state->local_player_slot]) {
        return false;
    }
    if (bytes[22] != NO_WINNER && bytes[16U + bytes[22]] != 1U) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 2U);
    if (revision == 0U || revision < state->network_revision) {
        return false;
    }
    state->network_revision = revision;
    state->player_count = bytes[7];
    state->phase = phase;
    state->current_player = bytes[9];
    state->direction = bytes[10];
    state->active_color = bytes[11];
    state->discard[0] = bytes[12];
    state->discard_count = 1U;
    state->winner = bytes[13];
    state->deck_count = bytes[14];
    memcpy(state->hand_counts, bytes + 16U, COLOR_CLASH_MAX_PLAYERS);
    state->uno_pending_player = bytes[22];
    state->notice = (color_clash_notice_t)bytes[23];
    state->notice_player = bytes[24];
    state->notice_ms = read_u16(bytes + 25U);
    memset(state->hands, 0, sizeof(state->hands));
    const uint8_t local_count = state->hand_counts[state->local_player_slot];
    if (phase == COLOR_CLASH_CHOOSE_HAND) {
        state->selected_target = bytes[21];
    } else if (phase == COLOR_CLASH_CHOOSE_COLOR) {
        state->selected_color = bytes[21];
    }
    if (local_count == 0U) {
        state->selected_card = 0U;
    } else if (state->selected_card >= local_count) {
        state->selected_card = (uint8_t)(local_count - 1U);
    }
    state->network_hand_received = 0U;
    state->network_started = local_count == 0U;
    state->network_error = false;
    return true;
}

static bool apply_hand_chunk(color_clash_state_t *state,
                             const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL ||
        byte_count < NET_HAND_HEADER_BYTES ||
        byte_count > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        bytes[0] != COLOR_CLASH_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_HAND_CHUNK ||
        bytes[6] != state->local_player_slot || bytes[8] == 0U ||
        bytes[8] > NET_HAND_PAYLOAD_BYTES ||
        bytes[9] != state->hand_counts[state->local_player_slot] ||
        bytes[7] != state->network_hand_received ||
        (uint16_t)bytes[7] + bytes[8] > bytes[9] ||
        byte_count != NET_HAND_HEADER_BYTES + bytes[8] ||
        read_u32(bytes + 2U) != state->network_revision) {
        return false;
    }
    for (uint8_t index = 0U; index < bytes[8]; ++index) {
        if (!color_clash_card_valid(
                bytes[NET_HAND_HEADER_BYTES + index])) {
            return false;
        }
    }
    memcpy(state->hands[state->local_player_slot] + bytes[7],
           bytes + NET_HAND_HEADER_BYTES, bytes[8]);
    state->network_hand_received = (uint8_t)(
        state->network_hand_received + bytes[8]);
    if (state->network_hand_received == bytes[9]) {
        const uint8_t local_count = bytes[9];
        if (local_count == 0U) {
            state->selected_card = 0U;
        } else if (state->phase == COLOR_CLASH_DRAWN_CARD ||
                   state->selected_card >= local_count) {
            state->selected_card = (uint8_t)(local_count - 1U);
        }
        state->network_started = true;
    }
    return true;
}

static void service_sync(p4_game_context_t *context,
                         color_clash_state_t *state)
{
    if (!state->network_sync_pending) {
        return;
    }
    const uint8_t human_players =
        state->human_player_count >= 2U &&
        state->human_player_count <= state->player_count
            ? state->human_player_count : state->player_count;
    while (state->network_sync_cursor < human_players) {
        const uint8_t target = state->network_sync_cursor;
        if (target == state->local_player_slot) {
            ++state->network_sync_cursor;
            state->network_sync_stage = 0U;
            state->network_sync_offset = 0U;
            continue;
        }
        uint8_t bytes[P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES];
        if (state->network_sync_stage == 0U) {
            const size_t byte_count = encode_public_snapshot(
                state, target, bytes);
            if (byte_count == 0U ||
                !p4_game_multiplayer_send(context, bytes, byte_count)) {
                return;
            }
            state->network_sync_stage = 1U;
            state->network_sync_offset = 0U;
        }
        while (state->network_sync_offset < state->hand_counts[target]) {
            const size_t byte_count = encode_hand_chunk(
                state, target, state->network_sync_offset, bytes);
            if (byte_count == 0U ||
                !p4_game_multiplayer_send(context, bytes, byte_count)) {
                return;
            }
            state->network_sync_offset = (uint8_t)(
                state->network_sync_offset +
                (uint8_t)(byte_count - NET_HAND_HEADER_BYTES));
        }
        ++state->network_sync_cursor;
        state->network_sync_stage = 0U;
        state->network_sync_offset = 0U;
    }
    state->network_sync_pending = false;
}

void color_clash_poll_network(p4_game_context_t *context,
                              color_clash_state_t *state)
{
    p4_game_multiplayer_status_t status;
    if (state == NULL ||
        !p4_game_multiplayer_read_status(context, &status)) {
        if (state != NULL) {
            state->network_error = true;
            state->phase = COLOR_CLASH_NETWORK_LOST;
        }
        return;
    }
    if (status.state == P4_GAME_MULTIPLAYER_PEER_LEFT ||
        status.state == P4_GAME_MULTIPLAYER_ERROR ||
        status.state == P4_GAME_MULTIPLAYER_OFFLINE) {
        state->network_error = true;
        state->phase = COLOR_CLASH_NETWORK_LOST;
        return;
    }
    if (status.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        return;
    }
    if (status.player_count < 2U ||
        status.player_count > COLOR_CLASH_MAX_PLAYERS ||
        status.local_player_slot >= status.player_count ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        state->network_error = true;
        state->phase = COLOR_CLASH_NETWORK_LOST;
        return;
    }
    state->local_player_slot = status.local_player_slot;
    state->network_role = status.role;
    state->network_seed = status.session_seed;
    state->human_player_count = status.player_count;
    if (!state->network_started &&
        status.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        color_clash_reset_match(
            state, COLOR_CLASH_MAX_PLAYERS,
            (uint32_t)status.session_seed ^
                (uint32_t)(status.session_seed >> 32U));
        state->mode = COLOR_CLASH_NETWORK;
        state->local_player_slot = status.local_player_slot;
        state->network_role = status.role;
        state->network_seed = status.session_seed;
        state->network_revision = 1U;
        state->network_started = true;
        queue_sync(state);
    }
    p4_game_multiplayer_message_t message;
    for (size_t received = 0U; received < NET_MAX_RECEIVE_PER_UPDATE &&
         p4_game_multiplayer_receive(context, &message); ++received) {
        if (message.player_slot >= status.player_count ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (status.role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
            if (message.player_slot == 0U && message.bytes >= 2U) {
                if (message.data[1] == NET_KIND_PUBLIC_SNAPSHOT) {
                    (void)apply_public_snapshot(
                        state, message.data, message.bytes);
                } else if (message.data[1] == NET_KIND_HAND_CHUNK) {
                    (void)apply_hand_chunk(
                        state, message.data, message.bytes);
                }
            }
            continue;
        }
        if (message.bytes != NET_REQUEST_BYTES ||
            message.data[0] != COLOR_CLASH_NETWORK_PROTOCOL ||
            read_u32(message.data + 2U) != state->network_revision) {
            continue;
        }
        const uint8_t kind = message.data[1];
        if (kind != COLOR_CLASH_NET_RESTART &&
            kind != COLOR_CLASH_NET_UNO &&
            message.player_slot != state->current_player) {
            continue;
        }
        (void)apply_authoritative_action(
            context, state, message.player_slot, kind, message.data[6]);
    }
    if (status.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        service_sync(context, state);
    }
}
