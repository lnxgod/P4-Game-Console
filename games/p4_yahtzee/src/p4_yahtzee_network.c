// SPDX-License-Identifier: MIT

#include "p4_yahtzee_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_SNAPSHOT = 16,
    NET_SCORE_VALUE_BITS = 6,
    NET_SCORE_COUNT = P4_YAHTZEE_PLAYERS * P4_YAHTZEE_CATEGORIES,
    NET_SCORE_BYTES =
        (NET_SCORE_COUNT * NET_SCORE_VALUE_BITS + 7) / 8,
    NET_SCORES_OFFSET = 17,
    NET_TURNS_OFFSET = NET_SCORES_OFFSET + NET_SCORE_BYTES,
    NET_ANIMATION_OFFSET = NET_TURNS_OFFSET + P4_YAHTZEE_PLAYERS,
    NET_SNAPSHOT_BYTES = NET_ANIMATION_OFFSET + 1,
    NET_REQUEST_BYTES = 7,
};

_Static_assert(NET_SNAPSHOT_BYTES <= P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES,
               "Yahtzee snapshot exceeds the Game API message ceiling");

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

static void write_score6(uint8_t bytes[NET_SNAPSHOT_BYTES],
                         size_t index, uint8_t value)
{
    const size_t bit = index * NET_SCORE_VALUE_BITS;
    const size_t offset = NET_SCORES_OFFSET + bit / 8U;
    const unsigned shift = (unsigned)(bit % 8U);
    const uint16_t shifted = (uint16_t)((uint16_t)value << shift);
    bytes[offset] |= (uint8_t)shifted;
    if (offset + 1U < NET_TURNS_OFFSET) {
        bytes[offset + 1U] |= (uint8_t)(shifted >> 8U);
    }
}

static uint8_t read_score6(const uint8_t bytes[NET_SNAPSHOT_BYTES],
                           size_t index)
{
    const size_t bit = index * NET_SCORE_VALUE_BITS;
    const size_t offset = NET_SCORES_OFFSET + bit / 8U;
    const unsigned shift = (unsigned)(bit % 8U);
    uint16_t packed = bytes[offset];
    if (offset + 1U < NET_TURNS_OFFSET) {
        packed |= (uint16_t)bytes[offset + 1U] << 8U;
    }
    return (uint8_t)((packed >> shift) & UINT16_C(0x003f));
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
        (!state->network_error && state->network_started &&
         state->local_player_slot < state->player_count &&
         state->current_player == state->local_player_slot));
}

static bool encode_snapshot(const p4_yahtzee_state_t *state,
                            uint8_t bytes[NET_SNAPSHOT_BYTES])
{
    if (state == NULL || bytes == NULL ||
        state->player_count < P4_YAHTZEE_MIN_PLAYERS ||
        state->player_count > P4_YAHTZEE_PLAYERS ||
        state->current_player >= state->player_count) {
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
    bytes[16] = state->player_count;
    size_t score_index = 0U;
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const int score = player < state->player_count
                ? state->scores[player][category] : -1;
            if (score < -1 || score > 50) {
                return false;
            }
            write_score6(bytes, score_index++,
                         score < 0 ? UINT8_C(0x3f) : (uint8_t)score);
        }
    }
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        if (player < state->player_count &&
            state->turns_scored[player] > P4_YAHTZEE_CATEGORIES) {
            return false;
        }
        bytes[NET_TURNS_OFFSET + player] = player < state->player_count
            ? state->turns_scored[player] : 0U;
    }
    bytes[NET_ANIMATION_OFFSET] =
        (uint8_t)((state->roll_animation_ms != 0U ? 1U : 0U) |
                  (state->shared_accessory ? 2U : 0U));
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
    const uint8_t player_count = bytes[16];
    if (revision == 0U || revision < state->network_revision ||
        player_count < P4_YAHTZEE_MIN_PLAYERS ||
        player_count > P4_YAHTZEE_PLAYERS ||
        (state->network_bound && state->player_count != player_count) ||
        bytes[6] >= player_count ||
        bytes[7] > P4_YAHTZEE_ROLLS_PER_TURN ||
        (bytes[8] & UINT8_C(0xe0)) != 0U ||
        bytes[9] < (uint8_t)P4_YAHTZEE_TURN ||
        bytes[9] > (uint8_t)P4_YAHTZEE_GAME_OVER ||
        bytes[10] >= P4_YAHTZEE_CATEGORIES ||
        bytes[NET_ANIMATION_OFFSET] > 3U) {
        return false;
    }
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        if (bytes[11U + index] < 1U || bytes[11U + index] > 6U) {
            return false;
        }
    }
    size_t score_index = 0U;
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const uint8_t score = read_score6(bytes, score_index++);
            if ((player < player_count && score != UINT8_C(0x3f) &&
                 score > 50U) ||
                (player >= player_count && score != UINT8_C(0x3f))) {
                return false;
            }
        }
        const uint8_t turns = bytes[NET_TURNS_OFFSET + player];
        if ((player < player_count && turns > P4_YAHTZEE_CATEGORIES) ||
            (player >= player_count && turns != 0U)) {
            return false;
        }
    }
    state->network_revision = revision;
    state->player_count = player_count;
    state->current_player = bytes[6];
    state->roll_count = bytes[7];
    state->held_mask = bytes[8];
    state->phase = (p4_yahtzee_phase_t)bytes[9];
    state->selected_category = bytes[10];
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        state->dice[index] = bytes[11U + index];
        state->animation_dice[index] = bytes[11U + index];
    }
    score_index = 0U;
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const uint8_t value = read_score6(bytes, score_index++);
            state->scores[player][category] = value == UINT8_C(0x3f)
                ? -1 : (int16_t)value;
        }
        state->turns_scored[player] = bytes[NET_TURNS_OFFSET + player];
    }
    state->roll_animation_ms = 0U;
    state->animation_step_ms = 0U;
    state->shared_accessory = (bytes[NET_ANIMATION_OFFSET] & 2U) != 0U;
    if ((bytes[NET_ANIMATION_OFFSET] & 1U) != 0U) {
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
    } else if (kind == P4_YAHTZEE_NET_RESTART) {
        (void)p4_game_play_tone(context, 523U, 65U, 3U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 659U, 90U, 3U, P4_WAVE_TRIANGLE);
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
    } else if (kind == P4_YAHTZEE_NET_RESTART &&
               state->phase == P4_YAHTZEE_GAME_OVER) {
        const uint32_t replay_seed = state->rng ^ state->network_revision ^
            UINT32_C(0x504c4159);
        p4_yahtzee_reset_match(state, replay_seed);
        changed = true;
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
    if (state == NULL ||
        (state->mode == P4_YAHTZEE_NETWORK &&
         (state->network_error || !state->network_started)) ||
        (kind == P4_YAHTZEE_NET_RESTART
            ? state->phase != P4_YAHTZEE_GAME_OVER
            : !p4_yahtzee_local_turn(state))) {
        return false;
    }
    if (state->mode == P4_YAHTZEE_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        return send_request(context, state, kind, argument);
    }
    return apply_host_action(context, state, kind, argument);
}

static void end_network_match(p4_yahtzee_state_t *state)
{
    state->network_error = true;
    state->phase = P4_YAHTZEE_NETWORK_WAIT;
    state->roll_animation_ms = 0U;
    state->animation_step_ms = 0U;
    state->accessory_pending = false;
}

bool p4_yahtzee_accept_network_status(
    p4_yahtzee_state_t *state,
    const p4_game_multiplayer_status_t *status)
{
    if (state->network_error) {
        end_network_match(state);
        return false;
    }
    if (status == NULL || status->state != P4_GAME_MULTIPLAYER_CONNECTED) {
        if (status == NULL || state->network_bound ||
            status->state != P4_GAME_MULTIPLAYER_WAITING) {
            end_network_match(state);
        }
        return false;
    }
    if (status->player_count < P4_YAHTZEE_MIN_PLAYERS ||
        status->player_count > P4_YAHTZEE_PLAYERS ||
        status->local_player_slot >= status->player_count ||
        (status->role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status->role != P4_GAME_MULTIPLAYER_ROLE_CLIENT) ||
        (state->network_bound &&
         (state->network_generation != status->generation ||
          state->network_seed != status->session_seed ||
          state->network_role != status->role ||
          state->local_player_slot != status->local_player_slot ||
          state->player_count != status->player_count))) {
        end_network_match(state);
        return false;
    }
    if (!state->network_bound) {
        state->network_generation = status->generation;
        state->network_seed = status->session_seed;
        state->network_role = status->role;
        state->local_player_slot = status->local_player_slot;
        state->player_count = status->player_count;
        state->network_bound = true;
    }
    return true;
}

void p4_yahtzee_poll_network(
    p4_game_context_t *context,
    p4_yahtzee_state_t *state)
{
    if (state->network_error) {
        end_network_match(state);
        return;
    }
    p4_game_multiplayer_status_t status;
    const bool status_ok = p4_game_multiplayer_read_status(context, &status);
    if (!p4_yahtzee_accept_network_status(state, status_ok ? &status : NULL)) {
        return;
    }
    if (!state->network_started &&
        status.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        p4_yahtzee_reset_match(state, (uint32_t)status.session_seed ^
                               (uint32_t)(status.session_seed >> 32U));
        state->mode = P4_YAHTZEE_NETWORK;
        state->player_count = status.player_count;
        state->local_player_slot = status.local_player_slot;
        state->network_role = status.role;
        state->network_seed = status.session_seed;
        state->network_revision = 1U;
        state->shared_accessory = context->services != NULL &&
            (context->services->available_capabilities & P4_GAME_CAP_DICE_ACCESSORY) != 0U;
        state->network_started = send_snapshot(context, state);
        if (!state->network_started) {
            state->phase = P4_YAHTZEE_NETWORK_WAIT;
            return;
        }
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
            message.player_slot >= state->player_count) {
            continue;
        }
        const uint8_t kind = message.data[1];
        if (kind != P4_YAHTZEE_NET_RESTART &&
            message.player_slot != state->current_player) {
            continue;
        }
        (void)apply_host_action(
            context, state, kind, message.data[6]);
    }
}

void p4_yahtzee_poll_dice(p4_game_context_t *context, p4_yahtzee_state_t *s)
{
    if (s->accessory_pending && (s->network_revision != s->accessory_pending_revision ||
        s->network_error || s->mode != P4_YAHTZEE_NETWORK ||
        context->elapsed_ms-s->accessory_pending_since_ms > 2000U)) s->accessory_pending=false;
    p4_dice_request_t request={0};
    request.token=s->accessory_request.token;
    request.player_slot=s->current_player;
    request.count=P4_YAHTZEE_DICE; request.sides=6;
    request.held_mask=s->held_mask;
    request.hold_ack=s->accessory_hold_ack;
    for (size_t i=0; i<P4_YAHTZEE_DICE; ++i)
        request.faces[i]=s->dice[i] >= 1 && s->dice[i] <= 6 ? s->dice[i] : 1;
    memcpy(request.player_name,"PLAYER 1",9);
    request.player_name[7]=(char)('1'+s->current_player);
    /* Console OS grants the shared accessory only to the host. Its validated
     * shake belongs to the displayed current player, including a remote turn.
     * Controller/touch actions still use the local-player authorization path. */
    const bool shared_host = s->mode == P4_YAHTZEE_NETWORK &&
        s->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST && s->network_started &&
        s->shared_accessory;
    const bool turn_active=s->phase == P4_YAHTZEE_TURN &&
        (p4_yahtzee_local_turn(s) || shared_host) &&
        !s->network_error && !s->accessory_pending && s->roll_animation_ms == 0 &&
        s->roll_count < P4_YAHTZEE_ROLLS_PER_TURN;
    request.enabled=turn_active && s->held_mask != 31;
    request.can_hold=turn_active && s->roll_count > 0;
    if (!request.token || memcmp(&request,&s->accessory_request,sizeof(request))) {
        ++request.token;
        if (!request.token) request.token=1;
        s->accessory_request=request;
    }
    p4_dice_status_t status={0};
    if (!p4_game_dice_exchange(context,&request,&status)) {
        s->accessory_phase=P4_DICE_OFFLINE; return;
    }
    s->accessory_phase=status.phase;
    if (status.hold_changed && request.can_hold &&
        status.phase == P4_DICE_WAITING && status.token == request.token &&
        status.player_slot == s->current_player && status.held_mask < 32 &&
        s->accessory_consumed_token != request.token &&
        (s->mode != P4_YAHTZEE_NETWORK || shared_host)) {
        /* One absolute selection is consumed once, then acknowledged by the
         * next request token. This also handles unholding all/any dice. */
        s->accessory_consumed_token=request.token;
        s->held_mask=status.held_mask;
        s->accessory_hold_ack=status.hold_sequence;
        play_action_tone(context,P4_YAHTZEE_NET_HOLD,s->held_mask != 0);
        if (shared_host) { ++s->network_revision; (void)send_snapshot(context,s); }
        return;
    }
    if (!request.enabled || status.phase != P4_DICE_ROLLED ||
        status.token != request.token || status.player_slot != s->current_player ||
        s->accessory_consumed_token == request.token) return;
    s->accessory_consumed_token=request.token;
    const bool rolled = shared_host
        ? apply_host_action(context,s,P4_YAHTZEE_NET_ROLL,0)
        : p4_yahtzee_perform_action(context,s,P4_YAHTZEE_NET_ROLL,0);
    if (rolled &&
        s->mode == P4_YAHTZEE_NETWORK &&
        s->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
        s->accessory_pending=true;
        s->accessory_pending_revision=s->network_revision;
        s->accessory_pending_since_ms=context->elapsed_ms;
    }
}
