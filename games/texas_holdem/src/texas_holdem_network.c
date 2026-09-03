// SPDX-License-Identifier: MIT

#include "texas_holdem_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_REQUEST_BYTES = 8,
    NET_STACKS_OFFSET = 26,
    NET_ROUND_BETS_OFFSET = 34,
    NET_CONTRIBUTIONS_OFFSET = 42,
    NET_HOLE_OFFSET = 50,
    NET_COMMUNITY_OFFSET = 58,
    NET_SNAPSHOT_BYTES = 63,
    NET_RECEIVE_LIMIT = 8,
    NET_SYNC_INTERVAL_MS = 1000,
};

_Static_assert(NET_SNAPSHOT_BYTES <= P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "Texas Hold'em snapshot exceeds the Game API ceiling");

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

static bool encode_snapshot(const texas_holdem_state_t *state,
                            uint8_t bytes[NET_SNAPSHOT_BYTES])
{
    if (state == NULL || bytes == NULL || state->revision == 0U ||
        state->player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        state->player_count > TEXAS_HOLDEM_PLAYERS ||
        state->phase > TEXAS_HOLDEM_PHASE_MATCH_OVER ||
        state->setup_index >= TEXAS_HOLDEM_STACK_CHOICES ||
        state->dealer >= state->player_count ||
        state->current_player >= state->player_count ||
        state->community_count > TEXAS_HOLDEM_COMMUNITY_CARDS) {
        return false;
    }
    memset(bytes, 0, NET_SNAPSHOT_BYTES);
    bytes[0] = TEXAS_HOLDEM_NETWORK_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    write_u32(bytes + 2U, state->revision);
    bytes[6] = (uint8_t)state->phase;
    bytes[7] = state->player_count;
    bytes[8] = state->dealer;
    bytes[9] = state->current_player;
    bytes[10] = state->community_count;
    bytes[11] = state->setup_index;
    bytes[12] = state->active_mask;
    bytes[13] = state->folded_mask;
    bytes[14] = state->all_in_mask;
    bytes[15] = state->acted_mask;
    bytes[16] = state->winner_mask;
    bytes[17] = state->cpu_mask;
    write_u16(bytes + 18U, state->current_bet);
    write_u16(bytes + 20U, state->min_raise);
    write_u16(bytes + 22U, state->pot);
    write_u16(bytes + 24U, state->starting_stack);
    for (uint8_t player = 0U; player < TEXAS_HOLDEM_PLAYERS; ++player) {
        write_u16(bytes + NET_STACKS_OFFSET + (size_t)player * 2U,
                  state->stacks[player]);
        write_u16(bytes + NET_ROUND_BETS_OFFSET + (size_t)player * 2U,
                  state->round_bet[player]);
        write_u16(bytes + NET_CONTRIBUTIONS_OFFSET + (size_t)player * 2U,
                  state->contribution[player]);
        for (uint8_t card = 0U; card < TEXAS_HOLDEM_HOLE_CARDS; ++card) {
            bytes[NET_HOLE_OFFSET + (size_t)player * 2U + card] =
                state->hole[player][card];
        }
    }
    memcpy(bytes + NET_COMMUNITY_OFFSET, state->community,
           TEXAS_HOLDEM_COMMUNITY_CARDS);
    return true;
}

static bool cards_valid(const uint8_t *bytes, uint8_t phase,
                        uint8_t player_count, uint8_t active_mask,
                        uint8_t community_count)
{
    uint64_t seen = 0U;
    for (uint8_t player = 0U; player < player_count; ++player) {
        for (uint8_t index = 0U; index < TEXAS_HOLDEM_HOLE_CARDS; ++index) {
            const uint8_t card = bytes[
                NET_HOLE_OFFSET + (size_t)player * 2U + index];
            const bool required = phase != TEXAS_HOLDEM_PHASE_SETUP &&
                phase != TEXAS_HOLDEM_PHASE_MATCH_OVER &&
                (active_mask & (UINT8_C(1) << player)) != 0U;
            if (card == TEXAS_HOLDEM_NO_CARD) {
                if (required) {
                    return false;
                }
                continue;
            }
            if (card >= TEXAS_HOLDEM_DECK_CARDS ||
                (seen & (UINT64_C(1) << card)) != 0U) {
                return false;
            }
            seen |= UINT64_C(1) << card;
        }
    }
    for (uint8_t index = 0U; index < TEXAS_HOLDEM_COMMUNITY_CARDS; ++index) {
        const uint8_t card = bytes[NET_COMMUNITY_OFFSET + index];
        if (index >= community_count) {
            if (card != TEXAS_HOLDEM_NO_CARD) {
                return false;
            }
            continue;
        }
        if (card >= TEXAS_HOLDEM_DECK_CARDS ||
            (seen & (UINT64_C(1) << card)) != 0U) {
            return false;
        }
        seen |= UINT64_C(1) << card;
    }
    return true;
}

static bool snapshot_valid(const texas_holdem_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (state == NULL || bytes == NULL || byte_count != NET_SNAPSHOT_BYTES ||
        bytes[0] != TEXAS_HOLDEM_NETWORK_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 2U);
    const uint8_t phase = bytes[6];
    const uint8_t count = bytes[7];
    const uint8_t community_count = bytes[10];
    const uint8_t setup_index = bytes[11];
    const uint8_t cpu_mask = bytes[17];
    if (revision == 0U || revision < state->revision ||
        count < TEXAS_HOLDEM_MIN_PLAYERS ||
        count > TEXAS_HOLDEM_PLAYERS ||
        state->network_player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        count < state->network_player_count ||
        phase > TEXAS_HOLDEM_PHASE_MATCH_OVER ||
        bytes[8] >= count || bytes[9] >= count ||
        community_count > TEXAS_HOLDEM_COMMUNITY_CARDS ||
        setup_index >= TEXAS_HOLDEM_STACK_CHOICES ||
        read_u16(bytes + 24U) !=
            texas_holdem_starting_stacks[setup_index]) {
        return false;
    }
    const uint8_t valid_bits = (uint8_t)((UINT8_C(1) << count) - 1U);
    const uint8_t human_bits = (uint8_t)(
        (UINT8_C(1) << state->network_player_count) - 1U);
    const uint8_t expected_cpu_mask = (uint8_t)(
        valid_bits & (uint8_t)~human_bits);
    const uint8_t active = bytes[12];
    const uint8_t folded = bytes[13];
    const uint8_t all_in = bytes[14];
    const uint8_t acted = bytes[15];
    const uint8_t winners = bytes[16];
    if (cpu_mask != expected_cpu_mask ||
        ((active | folded | all_in | acted | winners | cpu_mask) &
         (uint8_t)~valid_bits) != 0U ||
        (folded & (uint8_t)~active) != 0U ||
        (all_in & (uint8_t)~active) != 0U ||
        (winners & (uint8_t)~active) != 0U) {
        return false;
    }
    if ((phase == TEXAS_HOLDEM_PHASE_SETUP &&
         (active != 0U || community_count != 0U || winners != 0U)) ||
        (phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
         phase <= TEXAS_HOLDEM_PHASE_SHOWDOWN && active == 0U) ||
        (phase == TEXAS_HOLDEM_PHASE_PREFLOP && community_count != 0U) ||
        (phase == TEXAS_HOLDEM_PHASE_FLOP && community_count != 3U) ||
        (phase == TEXAS_HOLDEM_PHASE_TURN && community_count != 4U) ||
        (phase == TEXAS_HOLDEM_PHASE_RIVER && community_count != 5U)) {
        return false;
    }
    uint32_t conserved = 0U;
    uint32_t contributions = 0U;
    for (uint8_t player = 0U; player < TEXAS_HOLDEM_PLAYERS; ++player) {
        const uint16_t stack = read_u16(
            bytes + NET_STACKS_OFFSET + (size_t)player * 2U);
        const uint16_t round = read_u16(
            bytes + NET_ROUND_BETS_OFFSET + (size_t)player * 2U);
        const uint16_t total = read_u16(
            bytes + NET_CONTRIBUTIONS_OFFSET + (size_t)player * 2U);
        if ((player >= count && (stack != 0U || round != 0U || total != 0U)) ||
            round > total) {
            return false;
        }
        conserved += stack;
        conserved += total;
        contributions += total;
    }
    const uint32_t expected = (uint32_t)read_u16(bytes + 24U) * count;
    if (conserved != expected || expected > UINT16_MAX) {
        return false;
    }
    if (phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
        phase <= TEXAS_HOLDEM_PHASE_RIVER &&
        read_u16(bytes + 22U) != contributions) {
        return false;
    }
    return cards_valid(bytes, phase, count, active, community_count);
}

static bool apply_snapshot(texas_holdem_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (!snapshot_valid(state, bytes, byte_count)) {
        return false;
    }
    state->revision = read_u32(bytes + 2U);
    state->phase = (texas_holdem_phase_t)bytes[6];
    state->player_count = bytes[7];
    state->dealer = bytes[8];
    state->current_player = bytes[9];
    state->community_count = bytes[10];
    state->setup_index = bytes[11];
    state->active_mask = bytes[12];
    state->folded_mask = bytes[13];
    state->all_in_mask = bytes[14];
    state->acted_mask = bytes[15];
    state->winner_mask = bytes[16];
    state->cpu_mask = bytes[17];
    state->current_bet = read_u16(bytes + 18U);
    state->min_raise = read_u16(bytes + 20U);
    state->pot = read_u16(bytes + 22U);
    state->starting_stack = read_u16(bytes + 24U);
    for (uint8_t player = 0U; player < TEXAS_HOLDEM_PLAYERS; ++player) {
        state->stacks[player] = read_u16(
            bytes + NET_STACKS_OFFSET + (size_t)player * 2U);
        state->round_bet[player] = read_u16(
            bytes + NET_ROUND_BETS_OFFSET + (size_t)player * 2U);
        state->contribution[player] = read_u16(
            bytes + NET_CONTRIBUTIONS_OFFSET + (size_t)player * 2U);
        for (uint8_t card = 0U; card < TEXAS_HOLDEM_HOLE_CARDS; ++card) {
            state->hole[player][card] = bytes[
                NET_HOLE_OFFSET + (size_t)player * 2U + card];
        }
    }
    memcpy(state->community, bytes + NET_COMMUNITY_OFFSET,
           TEXAS_HOLDEM_COMMUNITY_CARDS);
    state->action_selection = TEXAS_HOLDEM_ACTION_CALL;
    state->network_started = true;
    state->network_request_pending = false;
    state->network_retry_ms = 0U;
    state->cpu_think_ms = 0U;
    state->pass_required = false;
    return true;
}

static bool send_snapshot(p4_game_context_t *context,
                          const texas_holdem_state_t *state)
{
    uint8_t bytes[NET_SNAPSHOT_BYTES];
    return encode_snapshot(state, bytes) &&
        p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static bool send_request(p4_game_context_t *context,
                         const texas_holdem_state_t *state,
                         uint8_t kind, uint8_t argument)
{
    uint8_t bytes[NET_REQUEST_BYTES] = {
        TEXAS_HOLDEM_NETWORK_PROTOCOL, kind, 0U, 0U, 0U, 0U,
        argument, 0U,
    };
    write_u32(bytes + 2U, state->revision);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void play_action_tone(p4_game_context_t *context,
                             texas_holdem_action_t action)
{
    if (action == TEXAS_HOLDEM_ACTION_FOLD) {
        (void)p4_game_play_tone(context, 220U, 55U, 2U, P4_WAVE_TRIANGLE);
    } else if (action == TEXAS_HOLDEM_ACTION_CALL) {
        (void)p4_game_play_tone(context, 440U, 45U, 3U, P4_WAVE_SQUARE);
    } else {
        (void)p4_game_play_tone(context, 659U, 55U, 4U, P4_WAVE_SQUARE);
    }
}

void texas_holdem_mark_snapshot_dirty(texas_holdem_state_t *state)
{
    if (state != NULL && state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        state->network_snapshot_dirty = true;
    }
}

bool texas_holdem_network_begin(p4_game_context_t *context,
                                texas_holdem_state_t *state)
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
        profile.min_players != TEXAS_HOLDEM_MIN_PLAYERS ||
        profile.max_players != TEXAS_HOLDEM_PLAYERS ||
        profile.message_bytes != NET_SNAPSHOT_BYTES ||
        profile.protocol != TEXAS_HOLDEM_NETWORK_PROTOCOL ||
        !p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        status.player_count > TEXAS_HOLDEM_PLAYERS ||
        status.local_player_slot >= status.player_count ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        return false;
    }
    const uint32_t seed = (uint32_t)status.session_seed ^
        (uint32_t)(status.session_seed >> 32U);
    texas_holdem_reset_lobby(state, status.player_count, seed);
    state->network_mode = true;
    state->network_role = status.role;
    state->local_player_slot = status.local_player_slot;
    state->session_seed = status.session_seed;
    state->network_player_count = status.player_count;
    state->network_started = status.role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    state->network_snapshot_dirty = state->network_started;
    return true;
}

static void fall_back_to_local(texas_holdem_state_t *state)
{
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        state->stacks[player] = (uint16_t)(
            state->stacks[player] + state->contribution[player]);
    }
    memset(state->round_bet, 0, sizeof(state->round_bet));
    memset(state->contribution, 0, sizeof(state->contribution));
    state->network_mode = false;
    state->network_started = false;
    state->network_role = P4_GAME_MULTIPLAYER_ROLE_NONE;
    state->network_request_pending = false;
    state->network_snapshot_dirty = false;
    state->network_error = true;
    state->network_player_count = 0U;
    state->peer_lost_fallback = true;
    state->pot = 0U;
    if (state->phase != TEXAS_HOLDEM_PHASE_SETUP &&
        state->phase != TEXAS_HOLDEM_PHASE_MATCH_OVER) {
        state->phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
        (void)texas_holdem_next_hand(state);
        state->peer_lost_fallback = true;
    }
}

bool texas_holdem_perform_action(p4_game_context_t *context,
                                 texas_holdem_state_t *state,
                                 texas_holdem_action_t action)
{
    if (state == NULL || !texas_holdem_local_turn(state) ||
        !texas_holdem_action_legal(state, state->current_player, action)) {
        return false;
    }
    if (state->network_mode &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        const uint8_t kind = action == TEXAS_HOLDEM_ACTION_FOLD
            ? TEXAS_HOLDEM_NET_FOLD
            : (action == TEXAS_HOLDEM_ACTION_CALL
                   ? TEXAS_HOLDEM_NET_CALL : TEXAS_HOLDEM_NET_RAISE);
        if (!send_request(context, state, kind, 0U)) {
            return false;
        }
        state->network_request_pending = true;
        return true;
    }
    if (!texas_holdem_apply_action(
            state, state->current_player, action)) {
        return false;
    }
    play_action_tone(context, action);
    texas_holdem_mark_snapshot_dirty(state);
    return true;
}

bool texas_holdem_perform_cpu_action(p4_game_context_t *context,
                                     texas_holdem_state_t *state)
{
    if (state == NULL ||
        !texas_holdem_player_is_cpu(state, state->current_player) ||
        (state->network_mode &&
         (!state->network_started ||
          state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST))) {
        return false;
    }
    const uint8_t player = state->current_player;
    const texas_holdem_action_t action =
        texas_holdem_choose_cpu_action(state, player);
    if (!texas_holdem_action_legal(state, player, action) ||
        !texas_holdem_apply_action(state, player, action)) {
        return false;
    }
    state->cpu_think_ms = 0U;
    play_action_tone(context, action);
    texas_holdem_mark_snapshot_dirty(state);
    return true;
}

bool texas_holdem_request_next_hand(p4_game_context_t *context,
                                    texas_holdem_state_t *state)
{
    (void)context;
    if (state == NULL || state->phase != TEXAS_HOLDEM_PHASE_SHOWDOWN ||
        (state->network_mode &&
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST) ||
        !texas_holdem_next_hand(state)) {
        return false;
    }
    texas_holdem_mark_snapshot_dirty(state);
    return true;
}

bool texas_holdem_request_new_match(p4_game_context_t *context,
                                    texas_holdem_state_t *state)
{
    (void)context;
    if (state == NULL || state->phase != TEXAS_HOLDEM_PHASE_MATCH_OVER ||
        (state->network_mode &&
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST) ||
        !texas_holdem_begin_match(state)) {
        return false;
    }
    texas_holdem_mark_snapshot_dirty(state);
    return true;
}

static void host_handle_request(p4_game_context_t *context,
                                texas_holdem_state_t *state,
                                const p4_game_multiplayer_message_t *message)
{
    if (message->bytes != NET_REQUEST_BYTES ||
        message->data[0] != TEXAS_HOLDEM_NETWORK_PROTOCOL) {
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
    texas_holdem_action_t action;
    if (kind == TEXAS_HOLDEM_NET_FOLD) {
        action = TEXAS_HOLDEM_ACTION_FOLD;
    } else if (kind == TEXAS_HOLDEM_NET_CALL) {
        action = TEXAS_HOLDEM_ACTION_CALL;
    } else if (kind == TEXAS_HOLDEM_NET_RAISE) {
        action = TEXAS_HOLDEM_ACTION_RAISE;
    } else {
        return;
    }
    if (texas_holdem_apply_action(state, message->player_slot, action)) {
        play_action_tone(context, action);
        state->network_snapshot_dirty = true;
    }
}

void texas_holdem_network_poll(p4_game_context_t *context,
                               texas_holdem_state_t *state,
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
        } else if (message.player_slot == 0U) {
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
        if (send_request(context, state, 0U, 0U)) {
            state->network_retry_ms = 0U;
        }
    }
}
