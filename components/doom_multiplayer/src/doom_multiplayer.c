// SPDX-License-Identifier: MIT

#include "p4/doom_multiplayer.h"

#include <limits.h>
#include <string.h>

static const uint8_t s_engine_ready[P4_DOOM_MP_ENGINE_CONTROL_BYTES] = {
    'P', '4', 'D', 'R', 'E', 'A', 'D', 'Y',
};

static const uint8_t s_engine_ack[P4_DOOM_MP_ENGINE_CONTROL_BYTES] = {
    'P', '4', 'D', 'R', 'A', 'C', 'K', '!',
};

static uint8_t player_bit(uint8_t player_slot)
{
    return (uint8_t)(UINT8_C(1) << player_slot);
}

static bool tic_equal(
    const p4_doom_mp_tic_t *left,
    const p4_doom_mp_tic_t *right)
{
    return left->tick == right->tick &&
        left->forward_move == right->forward_move &&
        left->side_move == right->side_move &&
        left->angle_turn == right->angle_turn &&
        left->buttons == right->buttons &&
        left->consistency == right->consistency &&
        left->chat_char == right->chat_char;
}

void p4_doom_mp_tic_to_input(
    const p4_doom_mp_tic_t *tic,
    p4_mp_input_t *input_out)
{
    if (tic == NULL || input_out == NULL) {
        return;
    }
    *input_out = (p4_mp_input_t){
        .tick = tic->tick,
        .buttons = (uint32_t)tic->buttons |
            ((uint32_t)tic->consistency << 8U) |
            ((uint32_t)tic->chat_char << 16U),
        .left_x = tic->side_move,
        .left_y = tic->forward_move,
        .right_x = tic->angle_turn,
    };
}

bool p4_doom_mp_tic_from_input(
    const p4_mp_input_t *input,
    p4_doom_mp_tic_t *tic_out)
{
    if (input == NULL || tic_out == NULL) {
        return false;
    }
    *tic_out = (p4_doom_mp_tic_t){0};
    if ((input->buttons & UINT32_C(0xff000000)) != 0U ||
        input->left_x < INT8_MIN || input->left_x > INT8_MAX ||
        input->left_y < INT8_MIN || input->left_y > INT8_MAX ||
        input->right_y != 0 || input->left_trigger != 0U ||
        input->right_trigger != 0U || input->dpad != 0U ||
        input->flags != 0U) {
        return false;
    }
    *tic_out = (p4_doom_mp_tic_t){
        .tick = input->tick,
        .forward_move = (int8_t)input->left_y,
        .side_move = (int8_t)input->left_x,
        .angle_turn = input->right_x,
        .buttons = (uint8_t)input->buttons,
        .consistency = (uint8_t)(input->buttons >> 8U),
        .chat_char = (uint8_t)(input->buttons >> 16U),
    };
    return true;
}

bool p4_doom_mp_launch_config_valid(
    const p4_doom_mp_launch_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    if (!config->enabled) {
        return config->role == P4_MP_ROLE_NONE &&
            config->session_id == 0U && config->self_peer_id == 0U &&
            config->remote_peer_id == 0U && config->route_id == 0U &&
            config->local_player_slot == 0U && config->player_count == 0U &&
            config->input_delay_tics == 0U && config->start_tic == 0U &&
            config->session_seed == 0U;
    }
    return (config->role == P4_MP_ROLE_HOST ||
            config->role == P4_MP_ROLE_CLIENT) &&
        config->session_id != 0U && config->self_peer_id != 0U &&
        config->remote_peer_id != 0U &&
        config->self_peer_id != config->remote_peer_id &&
        config->route_id != 0U && config->player_count >= 2U &&
        config->player_count <= P4_MP_MAX_PLAYERS &&
        config->local_player_slot < config->player_count &&
        config->input_delay_tics <= 15U && config->session_seed != 0U;
}

void p4_doom_mp_engine_barrier_init(
    p4_doom_mp_engine_barrier_t *barrier)
{
    if (barrier != NULL) {
        *barrier = (p4_doom_mp_engine_barrier_t){0};
    }
}

void p4_doom_mp_engine_barrier_begin(
    p4_doom_mp_engine_barrier_t *barrier)
{
    if (barrier != NULL) {
        *barrier = (p4_doom_mp_engine_barrier_t){
            .local_ready = true,
        };
    }
}

static p4_doom_mp_engine_control_t engine_control_decode(
    const uint8_t *payload,
    size_t payload_length)
{
    if (payload == NULL ||
        payload_length != P4_DOOM_MP_ENGINE_CONTROL_BYTES) {
        return P4_DOOM_MP_ENGINE_CONTROL_NONE;
    }
    if (memcmp(payload, s_engine_ready, sizeof(s_engine_ready)) == 0) {
        return P4_DOOM_MP_ENGINE_CONTROL_READY;
    }
    if (memcmp(payload, s_engine_ack, sizeof(s_engine_ack)) == 0) {
        return P4_DOOM_MP_ENGINE_CONTROL_ACK;
    }
    return P4_DOOM_MP_ENGINE_CONTROL_NONE;
}

bool p4_doom_mp_engine_control_encode(
    p4_doom_mp_engine_control_t control,
    uint8_t payload[P4_DOOM_MP_ENGINE_CONTROL_BYTES])
{
    if (payload == NULL) {
        return false;
    }
    if (control == P4_DOOM_MP_ENGINE_CONTROL_READY) {
        memcpy(payload, s_engine_ready, sizeof(s_engine_ready));
        return true;
    }
    if (control == P4_DOOM_MP_ENGINE_CONTROL_ACK) {
        memcpy(payload, s_engine_ack, sizeof(s_engine_ack));
        return true;
    }
    return false;
}

p4_doom_mp_engine_control_t p4_doom_mp_engine_barrier_observe_ping(
    p4_doom_mp_engine_barrier_t *barrier,
    const uint8_t *payload,
    size_t payload_length)
{
    if (barrier == NULL || !barrier->local_ready ||
        engine_control_decode(payload, payload_length) !=
            P4_DOOM_MP_ENGINE_CONTROL_READY) {
        return P4_DOOM_MP_ENGINE_CONTROL_NONE;
    }
    barrier->peer_ready = true;
    return P4_DOOM_MP_ENGINE_CONTROL_ACK;
}

void p4_doom_mp_engine_barrier_observe_pong(
    p4_doom_mp_engine_barrier_t *barrier,
    const uint8_t *payload,
    size_t payload_length)
{
    if (barrier != NULL && barrier->local_ready &&
        engine_control_decode(payload, payload_length) ==
            P4_DOOM_MP_ENGINE_CONTROL_ACK) {
        barrier->peer_acknowledged = true;
    }
}

bool p4_doom_mp_engine_barrier_complete(
    const p4_doom_mp_engine_barrier_t *barrier)
{
    return barrier != NULL && barrier->local_ready &&
        barrier->peer_ready && barrier->peer_acknowledged;
}

bool p4_doom_mp_tic_queue_init(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_count,
    uint32_t start_tick)
{
    if (queue == NULL || player_count < 2U ||
        player_count > P4_MP_MAX_PLAYERS) {
        return false;
    }
    memset(queue, 0, sizeof(*queue));
    queue->next_tick = start_tick;
    queue->player_count = player_count;
    queue->connected_mask = (uint8_t)(
        (UINT8_C(1) << player_count) - UINT8_C(1));
    return true;
}

bool p4_doom_mp_tic_queue_submit(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_slot,
    const p4_doom_mp_tic_t *tic)
{
    if (queue == NULL || tic == NULL ||
        queue->player_count < 2U || player_slot >= queue->player_count ||
        (queue->connected_mask & player_bit(player_slot)) == 0U) {
        return false;
    }
    const uint32_t distance = tic->tick - queue->next_tick;
    if (distance >= P4_DOOM_MP_TIC_RING_SIZE) {
        return false;
    }
    const size_t index = tic->tick % P4_DOOM_MP_TIC_RING_SIZE;
    if (queue->valid[player_slot][index] != 0U) {
        return queue->tags[player_slot][index] == tic->tick &&
            tic_equal(&queue->tics[player_slot][index], tic);
    }
    queue->tics[player_slot][index] = *tic;
    queue->tags[player_slot][index] = tic->tick;
    queue->valid[player_slot][index] = 1U;
    return true;
}

bool p4_doom_mp_tic_queue_ready(const p4_doom_mp_tic_queue_t *queue)
{
    if (queue == NULL || queue->player_count < 2U ||
        queue->connected_mask == 0U) {
        return false;
    }
    const size_t index = queue->next_tick % P4_DOOM_MP_TIC_RING_SIZE;
    for (uint8_t slot = 0U; slot < queue->player_count; ++slot) {
        if ((queue->connected_mask & player_bit(slot)) != 0U &&
            (queue->valid[slot][index] == 0U ||
             queue->tags[slot][index] != queue->next_tick)) {
            return false;
        }
    }
    return true;
}

bool p4_doom_mp_tic_queue_pop(
    p4_doom_mp_tic_queue_t *queue,
    p4_doom_mp_tic_t output[P4_MP_MAX_PLAYERS],
    uint8_t *connected_mask_out)
{
    if (output == NULL || connected_mask_out == NULL ||
        !p4_doom_mp_tic_queue_ready(queue)) {
        return false;
    }
    const size_t index = queue->next_tick % P4_DOOM_MP_TIC_RING_SIZE;
    for (uint8_t slot = 0U; slot < P4_MP_MAX_PLAYERS; ++slot) {
        output[slot] = (p4_doom_mp_tic_t){.tick = queue->next_tick};
        if (slot < queue->player_count &&
            (queue->connected_mask & player_bit(slot)) != 0U) {
            output[slot] = queue->tics[slot][index];
        }
        if (slot < queue->player_count) {
            queue->valid[slot][index] = 0U;
            queue->tags[slot][index] = 0U;
            queue->tics[slot][index] = (p4_doom_mp_tic_t){0};
        }
    }
    *connected_mask_out = queue->connected_mask;
    ++queue->next_tick;
    return true;
}

bool p4_doom_mp_tic_queue_disconnect(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_slot)
{
    if (queue == NULL || player_slot >= queue->player_count ||
        (queue->connected_mask & player_bit(player_slot)) == 0U) {
        return false;
    }
    queue->connected_mask &= (uint8_t)~player_bit(player_slot);
    memset(queue->valid[player_slot], 0, sizeof(queue->valid[player_slot]));
    memset(queue->tags[player_slot], 0, sizeof(queue->tags[player_slot]));
    memset(queue->tics[player_slot], 0, sizeof(queue->tics[player_slot]));
    return true;
}
