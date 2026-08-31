// SPDX-License-Identifier: MIT

#include "p4_air_hockey_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    NET_KIND_INPUT = 1,
    NET_KIND_SNAPSHOT = 2,
    NET_INPUT_HEARTBEAT_MS = 50,
    NET_SNAPSHOT_MS = 33,
};

static void write_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
}

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8U);
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

static uint16_t encode_position(int32_t value)
{
    const int32_t pixels = value >> P4_AIR_HOCKEY_FIXED_SHIFT;
    return (uint16_t)(pixels < 0 ? 0 : pixels);
}

static void encode_snapshot(const p4_air_hockey_state_t *state,
                            uint8_t bytes[P4_AIR_HOCKEY_SNAPSHOT_BYTES])
{
    memset(bytes, 0, P4_AIR_HOCKEY_SNAPSHOT_BYTES);
    bytes[0] = P4_AIR_HOCKEY_PROTOCOL;
    bytes[1] = NET_KIND_SNAPSHOT;
    bytes[2] = (uint8_t)state->phase;
    bytes[3] = state->score[0];
    bytes[4] = state->score[1];
    bytes[5] = state->serving_player;
    bytes[6] = state->winner;
    bytes[7] = (uint8_t)(state->network_audio_events &
                         P4_AIR_HOCKEY_EVENT_AUDIO_MASK);
    write_u32(bytes + 8U, state->snapshot_revision);
    write_u16(bytes + 12U, encode_position(state->paddle_x[0]));
    write_u16(bytes + 14U, encode_position(state->paddle_y[0]));
    write_u16(bytes + 16U, encode_position(state->paddle_x[1]));
    write_u16(bytes + 18U, encode_position(state->paddle_y[1]));
    write_u16(bytes + 20U, encode_position(state->puck_x));
    write_u16(bytes + 22U, encode_position(state->puck_y));
    write_u16(bytes + 24U, (uint16_t)(int16_t)state->puck_vx);
    write_u16(bytes + 26U, (uint16_t)(int16_t)state->puck_vy);
    write_u16(bytes + 28U, (uint16_t)state->phase_timer_ms);
    write_u16(bytes + 30U, (uint16_t)state->simulation_tick);
}

static bool apply_snapshot(p4_air_hockey_state_t *state,
                           const uint8_t *bytes, size_t byte_count)
{
    if (bytes == NULL || byte_count != P4_AIR_HOCKEY_SNAPSHOT_BYTES ||
        bytes[0] != P4_AIR_HOCKEY_PROTOCOL ||
        bytes[1] != NET_KIND_SNAPSHOT ||
        bytes[2] > (uint8_t)P4_AIR_HOCKEY_NETWORK_WAIT ||
        bytes[3] > P4_AIR_HOCKEY_WIN_SCORE ||
        bytes[4] > P4_AIR_HOCKEY_WIN_SCORE || bytes[5] > 1U ||
        (bytes[6] > 1U && bytes[6] != UINT8_C(0xff)) ||
        (bytes[7] & UINT8_C(0xe0)) != 0U) {
        return false;
    }
    const uint32_t revision = read_u32(bytes + 8U);
    const uint16_t paddle_0_x = read_u16(bytes + 12U);
    const uint16_t paddle_0_y = read_u16(bytes + 14U);
    const uint16_t paddle_1_x = read_u16(bytes + 16U);
    const uint16_t paddle_1_y = read_u16(bytes + 18U);
    const uint16_t puck_x = read_u16(bytes + 20U);
    const uint16_t puck_y = read_u16(bytes + 22U);
    if (revision == 0U || revision <= state->snapshot_revision ||
        paddle_0_x > P4_GAME_SURFACE_WIDTH ||
        paddle_1_x > P4_GAME_SURFACE_WIDTH ||
        paddle_0_y > P4_GAME_SURFACE_HEIGHT ||
        paddle_1_y > P4_GAME_SURFACE_HEIGHT ||
        puck_x > P4_GAME_SURFACE_WIDTH || puck_y > P4_GAME_SURFACE_HEIGHT) {
        return false;
    }
    state->phase = (p4_air_hockey_phase_t)bytes[2];
    state->score[0] = bytes[3];
    state->score[1] = bytes[4];
    state->serving_player = bytes[5];
    state->winner = bytes[6];
    state->network_audio_events |= bytes[7];
    state->snapshot_revision = revision;
    state->paddle_x[0] = (int32_t)paddle_0_x << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->paddle_y[0] = (int32_t)paddle_0_y << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->paddle_x[1] = (int32_t)paddle_1_x << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->paddle_y[1] = (int32_t)paddle_1_y << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->puck_x = (int32_t)puck_x << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->puck_y = (int32_t)puck_y << P4_AIR_HOCKEY_FIXED_SHIFT;
    state->puck_vx = (int16_t)read_u16(bytes + 24U);
    state->puck_vy = (int16_t)read_u16(bytes + 26U);
    state->phase_timer_ms = read_u16(bytes + 28U);
    state->simulation_tick = read_u16(bytes + 30U);
    state->snapshot_received = true;
    return true;
}

bool p4_air_hockey_network_begin(p4_game_context_t *context,
                                 p4_air_hockey_state_t *state)
{
    p4_game_multiplayer_profile_t profile;
    p4_game_multiplayer_status_t status;
    if (state == NULL ||
        !p4_game_multiplayer_read_profile(context, &profile) ||
        profile.style != P4_GAME_MULTIPLAYER_STYLE_REALTIME ||
        profile.min_players != 2U || profile.max_players != 2U ||
        profile.protocol != P4_AIR_HOCKEY_PROTOCOL ||
        profile.message_bytes < P4_AIR_HOCKEY_SNAPSHOT_BYTES ||
        !p4_game_multiplayer_read_status(context, &status) ||
        status.state != P4_GAME_MULTIPLAYER_CONNECTED ||
        status.player_count != P4_AIR_HOCKEY_PLAYERS ||
        status.local_player_slot >= P4_AIR_HOCKEY_PLAYERS ||
        (status.role != P4_GAME_MULTIPLAYER_ROLE_HOST &&
         status.role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        return false;
    }
    state->mode = P4_AIR_HOCKEY_NETWORK;
    state->network_role = status.role;
    state->local_player_slot = status.local_player_slot;
    state->session_seed = status.session_seed;
    state->network_send_ms = NET_SNAPSHOT_MS;
    state->input_send_ms = NET_INPUT_HEARTBEAT_MS;
    state->last_sent_touch_x = UINT16_MAX;
    state->last_sent_touch_y = UINT16_MAX;
    state->last_sent_touch_active = true;
    if (status.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        p4_air_hockey_reset_match(
            state, (uint32_t)status.session_seed ^
                   (uint32_t)(status.session_seed >> 32U));
        state->snapshot_revision = 0U;
    } else {
        state->phase = P4_AIR_HOCKEY_NETWORK_WAIT;
        state->snapshot_received = false;
    }
    return true;
}

static bool status_connected(p4_game_context_t *context,
                             p4_air_hockey_state_t *state)
{
    p4_game_multiplayer_status_t status;
    return p4_game_multiplayer_read_status(context, &status) &&
        status.state == P4_GAME_MULTIPLAYER_CONNECTED &&
        status.role == state->network_role &&
        status.local_player_slot == state->local_player_slot &&
        status.session_seed == state->session_seed &&
        status.player_count == P4_AIR_HOCKEY_PLAYERS;
}

static bool send_input(p4_game_context_t *context,
                       bool touch_active, uint16_t touch_x,
                       uint16_t touch_y, bool restart_pressed)
{
    uint8_t bytes[P4_AIR_HOCKEY_INPUT_BYTES] = {
        P4_AIR_HOCKEY_PROTOCOL,
        NET_KIND_INPUT,
        0U, 0U, 0U, 0U,
        (uint8_t)((touch_active ? 1U : 0U) |
                  (restart_pressed ? 2U : 0U)),
        0U,
    };
    write_u16(bytes + 2U, touch_x);
    write_u16(bytes + 4U, touch_y);
    return p4_game_multiplayer_send(context, bytes, sizeof(bytes));
}

static void receive_messages(p4_game_context_t *context,
                             p4_air_hockey_state_t *state)
{
    p4_game_multiplayer_message_t message;
    for (size_t count = 0U; count < 8U &&
         p4_game_multiplayer_receive(context, &message); ++count) {
        if (message.player_slot >= P4_AIR_HOCKEY_PLAYERS ||
            message.player_slot == state->local_player_slot ||
            message.sequence <=
                state->last_network_sequence[message.player_slot]) {
            continue;
        }
        state->last_network_sequence[message.player_slot] = message.sequence;
        if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT) {
            (void)apply_snapshot(state, message.data, message.bytes);
            continue;
        }
        if (message.bytes != P4_AIR_HOCKEY_INPUT_BYTES ||
            message.data[0] != P4_AIR_HOCKEY_PROTOCOL ||
            message.data[1] != NET_KIND_INPUT ||
            (message.data[6] & UINT8_C(0xfc)) != 0U ||
            message.data[7] != 0U) {
            continue;
        }
        const uint16_t touch_x = read_u16(message.data + 2U);
        const uint16_t touch_y = read_u16(message.data + 4U);
        if (touch_x >= P4_GAME_SURFACE_WIDTH ||
            touch_y >= P4_GAME_SURFACE_HEIGHT) {
            continue;
        }
        p4_air_hockey_set_touch_target(
            state, message.player_slot,
            (message.data[6] & 1U) != 0U, touch_x, touch_y);
        state->restart_requested = (message.data[6] & 2U) != 0U;
    }
}

bool p4_air_hockey_network_update(p4_game_context_t *context,
                                  p4_air_hockey_state_t *state,
                                  bool touch_active,
                                  uint16_t touch_x,
                                  uint16_t touch_y,
                                  bool restart_pressed,
                                  uint32_t elapsed_ms)
{
    if (!status_connected(context, state)) {
        return false;
    }
    uint16_t world_x = touch_x < P4_GAME_SURFACE_WIDTH ? touch_x : 0U;
    uint16_t world_y = touch_y < P4_GAME_SURFACE_HEIGHT ? touch_y : 0U;
    p4_air_hockey_canonical_touch(state->local_player_slot,
                                  world_x, world_y, &world_x, &world_y);
    p4_air_hockey_set_touch_target(state, state->local_player_slot,
                                   touch_active, world_x, world_y);
    state->network_send_ms += elapsed_ms;
    state->input_send_ms += elapsed_ms;
    if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT &&
        (restart_pressed || world_x != state->last_sent_touch_x ||
         world_y != state->last_sent_touch_y ||
         touch_active != state->last_sent_touch_active ||
         state->input_send_ms >= NET_INPUT_HEARTBEAT_MS)) {
        if (send_input(context, touch_active, world_x, world_y,
                       restart_pressed)) {
            state->last_sent_touch_x = world_x;
            state->last_sent_touch_y = world_y;
            state->last_sent_touch_active = touch_active;
            state->input_send_ms = 0U;
        }
    } else if (state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST &&
               restart_pressed) {
        state->restart_requested = true;
    }
    receive_messages(context, state);
    return true;
}

void p4_air_hockey_network_publish(p4_game_context_t *context,
                                   p4_air_hockey_state_t *state,
                                   bool force)
{
    if (state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST ||
        (!force && state->network_send_ms < NET_SNAPSHOT_MS)) {
        return;
    }
    uint8_t bytes[P4_AIR_HOCKEY_SNAPSHOT_BYTES];
    ++state->snapshot_revision;
    encode_snapshot(state, bytes);
    if (p4_game_multiplayer_send(context, bytes, sizeof(bytes))) {
        state->network_send_ms = 0U;
        state->network_audio_events = P4_AIR_HOCKEY_EVENT_NONE;
    } else {
        --state->snapshot_revision;
    }
}
