// SPDX-License-Identifier: MIT

#include "p4_yahtzee_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_SNAPSHOT_BYTES = 45,
    NET_REQUEST_BYTES = 7,
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

bool p4_yahtzee_network_available(const p4_game_context_t *context)
{
    return context != NULL && context->services != NULL &&
        (context->services->available_capabilities &
         P4_GAME_CAP_MULTIPLAYER_SESSION) != 0U;
}

bool p4_yahtzee_local_turn(const p4_yahtzee_state_t *state)
{
    return state != NULL && (state->mode == P4_YAHTZEE_LOCAL ||
        state->current_player == state->local_player_slot);
}

static bool encode_snapshot(const p4_yahtzee_state_t *state,
                            uint8_t bytes[NET_SNAPSHOT_BYTES])
{
    if (state == NULL || bytes == NULL) {
        return false;
    }
    memset(bytes, 0, NET_SNAPSHOT_BYTES);
    bytes[0] = P4_YAHTZEE_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    write_u32(bytes + 2U, state->network_revision);
    bytes[6] = state->current_player;
    bytes[7] = state->roll_count;
    bytes[8] = state->held_mask;
    bytes[9] = (uint8_t)state->phase;
    bytes[10] = state->selected_category;
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        bytes[11U + index] = state->dice[index];
    }
    size_t offset = 16U;
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const int score = state->scores[player][category];
            bytes[offset++] = score < 0 ? UINT8_C(0xff) : (uint8_t)score;
        }
    }
    bytes[offset++] = state->turns_scored[0];
    bytes[offset++] = state->turns_scored[1];
    bytes[offset] = state->roll_animation_ms != 0U ? 1U : 0U;
    return true;
}

static bool send_snapshot(p4_game_context_t *context,
                          p4_yahtzee_state_t *state)
{
    uint8_t bytes[NET_SNAPSHOT_BYTES];
    return encode_snapshot(state, bytes) &&
        p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static bool apply_snapshot(p4_yahtzee_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL || byte_count != NET_SNAPSHOT_BYTES ||
        bytes[0] != P4_YAHTZEE_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 2U);
    if (revision == 0U || revision < state->network_revision ||
        bytes[6] >= P4_YAHTZEE_PLAYERS ||
        bytes[7] > P4_YAHTZEE_ROLLS_PER_TURN ||
        (bytes[8] & UINT8_C(0xe0)) != 0U ||
        bytes[9] < (uint8_t)P4_YAHTZEE_TURN ||
        bytes[9] > (uint8_t)P4_YAHTZEE_GAME_OVER ||
        bytes[10] >= P4_YAHTZEE_CATEGORIES) {
        return false;
    }
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        if (bytes[11U + index] < 1U || bytes[11U + index] > 6U) {
            return false;
        }
    }
    size_t offset = 16U;
    for (size_t index = 0U; index <
         P4_YAHTZEE_PLAYERS * P4_YAHTZEE_CATEGORIES; ++index) {
        if (bytes[offset + index] != UINT8_C(0xff) &&
            bytes[offset + index] > 50U) {
            return false;
        }
    }
    state->network_revision = revision;
    state->current_player = bytes[6];
    state->roll_count = bytes[7];
    state->held_mask = bytes[8];
    state->phase = (p4_yahtzee_phase_t)bytes[9];
    state->selected_category = bytes[10];
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        state->dice[index] = bytes[11U + index];
        state->animation_dice[index] = bytes[11U + index];
    }
    offset = 16U;
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const uint8_t value = bytes[offset++];
            state->scores[player][category] = value == UINT8_C(0xff)
                ? -1 : (int16_t)value;
        }
    }
    state->turns_scored[0] = bytes[offset++];
    state->turns_scored[1] = bytes[offset++];
    if (bytes[offset] != 0U) {
        state->roll_animation_ms = 330U;
        state->animation_step_ms = 55U;
    }
    state->network_started = true;
    return true;
}

static bool send_request(p4_game_context_t *context,
                         const p4_yahtzee_state_t *state,
                         uint8_t kind, uint8_t argument)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        P4_YAHTZEE_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U, argument,
    };
    write_u32(bytes + 2U, state->network_revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void play_action_tone(p4_game_context_t *context, uint8_t kind,
                             bool held)
{
    if (kind == P4_YAHTZEE_NET_ROLL) {
        /* A short dissonant cluster reads as five dice clacking together. */
        (void)p4_game_play_tone(context, 170U, 38U, 3U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 247U, 52U, 3U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 359U, 31U, 2U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 503U, 24U, 2U, P4_WAVE_TRIANGLE);
    } else if (kind == P4_YAHTZEE_NET_HOLD) {
        (void)p4_game_play_tone(
            context, held ? 660U : 440U, 45U, 3U, P4_WAVE_SQUARE);
    } else if (kind == P4_YAHTZEE_NET_SCORE) {
        (void)p4_game_play_tone(context, 784U, 80U, 4U, P4_WAVE_TRIANGLE);
    }
}

static bool apply_host_action(p4_game_context_t *context,
                              p4_yahtzee_state_t *state,
                              uint8_t kind, uint8_t argument)
{
    bool changed = false;
    bool held = false;
    if (kind == P4_YAHTZEE_NET_ROLL) {
        changed = p4_yahtzee_roll(state);
    } else if (kind == P4_YAHTZEE_NET_HOLD &&
               argument < P4_YAHTZEE_DICE && state->roll_count != 0U &&
               state->roll_animation_ms == 0U) {
        state->held_mask ^= (uint8_t)(UINT8_C(1) << argument);
        held = (state->held_mask & (UINT8_C(1) << argument)) != 0U;
        changed = true;
    } else if (kind == P4_YAHTZEE_NET_SCORE &&
               argument < P4_YAHTZEE_CATEGORIES) {
        changed = p4_yahtzee_score_turn(
            state, (p4_yahtzee_category_t)argument);
    }
    if (!changed) {
        return false;
    }
    play_action_tone(context, kind, held);
    if (state->mode == P4_YAHTZEE_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        ++state->network_revision;
        (void)send_snapshot(context, state);
    }
    return true;
}

bool p4_yahtzee_perform_action(
    p4_game_context_t *context,
    p4_yahtzee_state_t *state,
    uint8_t kind,
    uint8_t argument)
{
    if (!p4_yahtzee_local_turn(state)) {
        return false;
    }
    if (state->mode == P4_YAHTZEE_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        return send_request(context, state, kind, argument);
    }
    return apply_host_action(context, state, kind, argument);
}

void p4_yahtzee_poll_network(
    p4_game_context_t *context,
    p4_yahtzee_state_t *state)
{
    p4_game_multiplayer_status_t status;
    if (!p4_game_multiplayer_read_status(context, &status)) {
        state->network_error = true;
        return;
    }
    if (status.state == P4_GAME_MULTIPLAYER_PEER_LEFT ||
        status.state == P4_GAME_MULTIPLAYER_ERROR ||
        status.state == P4_GAME_MULTIPLAYER_OFFLINE) {
        state->network_error = true;
        return;
    }
    if (status.state != P4_GAME_MULTIPLAYER_CONNECTED) {
        return;
    }
    state->local_player_slot = status.local_player_slot;
    state->network_role = status.role;
    state->network_seed = status.session_seed;
    if (!state->network_started &&
        status.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        p4_yahtzee_reset_match(state, (uint32_t)status.session_seed ^
                               (uint32_t)(status.session_seed >> 32U));
        state->mode = P4_YAHTZEE_NETWORK;
        state->local_player_slot = status.local_player_slot;
        state->network_role = status.role;
        state->network_seed = status.session_seed;
        state->network_revision = 1U;
        state->network_started = send_snapshot(context, state);
    }
    p4_game_multiplayer_message_t message;
    for (size_t received = 0U; received < 4U &&
         p4_game_multiplayer_receive(context, &message); ++received) {
        if (message.player_slot >= P4_YAHTZEE_PLAYERS ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (status.role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
            (void)apply_snapshot(state, message.data, message.bytes);
            continue;
        }
        if (message.bytes != NET_REQUEST_BYTES ||
            message.data[0] != P4_YAHTZEE_NETWORK_PROTOCOL ||
            read_u32(message.data + 2U) != state->network_revision ||
            message.player_slot != state->current_player) {
            continue;
        }
        (void)apply_host_action(
            context, state, message.data[1], message.data[6]);
    }
}
