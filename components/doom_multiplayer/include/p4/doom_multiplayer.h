// SPDX-License-Identifier: MIT

#ifndef P4_DOOM_MULTIPLAYER_H
#define P4_DOOM_MULTIPLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_DOOM_MP_TICK_RATE_HZ = 35,
    P4_DOOM_MP_TIC_RING_SIZE = 128,
};

/** Padding-free subset of Doom's ticcmd_t used by Doom/Heretic-style play. */
typedef struct {
    uint32_t tick;
    int8_t forward_move;
    int8_t side_move;
    int16_t angle_turn;
    uint8_t buttons;
    uint8_t consistency;
    uint8_t chat_char;
} p4_doom_mp_tic_t;

typedef struct {
    bool enabled;
    p4_mp_role_t role;
    uint32_t session_id;
    uint32_t self_peer_id;
    uint32_t remote_peer_id;
    uint64_t route_id;
    uint8_t local_player_slot;
    uint8_t player_count;
    uint8_t input_delay_tics;
    uint32_t start_tic;
    uint64_t session_seed;
} p4_doom_mp_launch_config_t;

typedef struct {
    p4_doom_mp_tic_t tics[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint32_t tags[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint8_t valid[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint32_t next_tick;
    uint8_t player_count;
    uint8_t connected_mask;
} p4_doom_mp_tic_queue_t;

/** Map one canonical Doom command into the fixed P4MP Input payload model. */
void p4_doom_mp_tic_to_input(
    const p4_doom_mp_tic_t *tic,
    p4_mp_input_t *input_out);

/** Reject noncanonical fields before exposing a remote command to Doom. */
bool p4_doom_mp_tic_from_input(
    const p4_mp_input_t *input,
    p4_doom_mp_tic_t *tic_out);

bool p4_doom_mp_launch_config_valid(
    const p4_doom_mp_launch_config_t *config);

bool p4_doom_mp_tic_queue_init(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_count,
    uint32_t start_tick);

/** Accept one idempotent command at most 127 tics ahead of next_tick. */
bool p4_doom_mp_tic_queue_submit(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_slot,
    const p4_doom_mp_tic_t *tic);

/** True only when every still-connected player has the next command. */
bool p4_doom_mp_tic_queue_ready(const p4_doom_mp_tic_queue_t *queue);

/**
 * Pop one complete lockstep tic. Disconnected slots are returned neutral and
 * cleared in connected_mask_out so the engine can remove the player safely.
 */
bool p4_doom_mp_tic_queue_pop(
    p4_doom_mp_tic_queue_t *queue,
    p4_doom_mp_tic_t output[P4_MP_MAX_PLAYERS],
    uint8_t *connected_mask_out);

/** Stop waiting for a disconnected slot immediately and erase queued input. */
bool p4_doom_mp_tic_queue_disconnect(
    p4_doom_mp_tic_queue_t *queue,
    uint8_t player_slot);

#ifdef __cplusplus
}
#endif

#endif
