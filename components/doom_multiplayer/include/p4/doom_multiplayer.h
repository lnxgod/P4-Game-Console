// SPDX-License-Identifier: MIT

#ifndef P4_DOOM_MULTIPLAYER_H
#define P4_DOOM_MULTIPLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/multiplayer.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_DOOM_MP_TICK_RATE_HZ = 35,
    P4_DOOM_MP_TIC_RING_SIZE = 128,
    P4_DOOM_MP_TX_WINDOW_SIZE = 32,
    P4_DOOM_MP_ENGINE_CONTROL_BYTES = 8,
    P4_DOOM_MP_SETUP_SCHEMA = 2,
    P4_DOOM_MP_SETUP_BYTES = P4_MP_GAME_SETTINGS_BYTES,
    P4_DOOM_MP_MAX_EPISODE = 4,
    P4_DOOM_MP_MAX_MAP = 9,
    P4_DOOM_MP_MAX_CHEX_MAP = 5,
    P4_DOOM_MP_MAX_SKILL = 5,
    P4_DOOM_MP_MAX_TIME_LIMIT_MINUTES = 60,
};

typedef enum {
    P4_DOOM_MP_GAME_DOOM = 0,
    P4_DOOM_MP_GAME_CHEX_QUEST,
    P4_DOOM_MP_GAME_COUNT,
} p4_doom_mp_game_t;

typedef enum {
    P4_DOOM_MP_MODE_COOPERATIVE = 0,
    P4_DOOM_MP_MODE_DEATHMATCH,
    P4_DOOM_MP_MODE_ALTDEATH,
} p4_doom_mp_mode_t;

typedef struct {
    p4_doom_mp_game_t game;
    p4_doom_mp_mode_t mode;
    uint8_t episode;
    uint8_t map;
    uint8_t skill;
    uint8_t time_limit_minutes;
    bool no_monsters;
    bool fast_monsters;
    bool respawn_monsters;
} p4_doom_mp_setup_t;

typedef enum {
    P4_DOOM_MP_ENGINE_CONTROL_NONE = 0,
    P4_DOOM_MP_ENGINE_CONTROL_READY,
    P4_DOOM_MP_ENGINE_CONTROL_ACK,
} p4_doom_mp_engine_control_t;

typedef struct {
    bool local_ready;
    bool peer_ready;
    bool peer_acknowledged;
} p4_doom_mp_engine_barrier_t;

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
    p4_doom_mp_setup_t setup;
} p4_doom_mp_launch_config_t;

typedef struct {
    p4_doom_mp_tic_t tics[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint32_t tags[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint8_t valid[P4_MP_MAX_PLAYERS][P4_DOOM_MP_TIC_RING_SIZE];
    uint32_t next_tick;
    uint8_t player_count;
    uint8_t connected_mask;
} p4_doom_mp_tic_queue_t;

/**
 * Bounded local-input history used to recover a lost lockstep packet. The
 * peer's packet ack is its next required tic, so every older entry can be
 * discarded and the oldest remaining entry can be retransmitted safely.
 */
typedef struct {
    p4_doom_mp_tic_t tics[P4_DOOM_MP_TX_WINDOW_SIZE];
    uint32_t tags[P4_DOOM_MP_TX_WINDOW_SIZE];
    uint8_t valid[P4_DOOM_MP_TX_WINDOW_SIZE];
    uint32_t peer_ack;
    uint32_t next_local_tick;
    uint8_t pending_count;
} p4_doom_mp_tx_window_t;

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

/** Validate one bounded Doom/DeathManager-style lobby setup. */
bool p4_doom_mp_setup_valid(const p4_doom_mp_setup_t *setup);

/** Encode a setup into the generic host-authored lobby settings field. */
bool p4_doom_mp_setup_encode(
    const p4_doom_mp_setup_t *setup,
    uint8_t bytes[P4_DOOM_MP_SETUP_BYTES]);

/** Decode and reject unknown schemas, flags, or out-of-range values. */
bool p4_doom_mp_setup_decode(
    const uint8_t *bytes,
    size_t bytes_length,
    p4_doom_mp_setup_t *setup_out);

/** Clear every engine-start handshake flag. */
void p4_doom_mp_engine_barrier_init(
    p4_doom_mp_engine_barrier_t *barrier);

/** Mark the local Doom engine ready and start a fresh two-way handshake. */
void p4_doom_mp_engine_barrier_begin(
    p4_doom_mp_engine_barrier_t *barrier);

/** Encode one fixed-size engine READY or ACK control payload. */
bool p4_doom_mp_engine_control_encode(
    p4_doom_mp_engine_control_t control,
    uint8_t payload[P4_DOOM_MP_ENGINE_CONTROL_BYTES]);

/**
 * Observe a PING payload. ACK is returned only after the local engine called
 * begin; launcher teardown can therefore never signal engine readiness.
 */
p4_doom_mp_engine_control_t p4_doom_mp_engine_barrier_observe_ping(
    p4_doom_mp_engine_barrier_t *barrier,
    const uint8_t *payload,
    size_t payload_length);

/** Observe a PONG payload and remember a valid acknowledgment of local READY. */
void p4_doom_mp_engine_barrier_observe_pong(
    p4_doom_mp_engine_barrier_t *barrier,
    const uint8_t *payload,
    size_t payload_length);

/** True only after both engines advertised READY and acknowledged the peer. */
bool p4_doom_mp_engine_barrier_complete(
    const p4_doom_mp_engine_barrier_t *barrier);

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

void p4_doom_mp_tx_window_init(
    p4_doom_mp_tx_window_t *window,
    uint32_t start_tick);

/** Track the next sequential local tic, accepting an identical retry. */
bool p4_doom_mp_tx_window_track(
    p4_doom_mp_tx_window_t *window,
    const p4_doom_mp_tic_t *tic);

/**
 * Apply a monotonic peer next-required-tic acknowledgment. Stale, future, or
 * ambiguous half-range acknowledgments are rejected without changing state.
 */
bool p4_doom_mp_tx_window_acknowledge(
    p4_doom_mp_tx_window_t *window,
    uint32_t peer_ack);

/** Return the oldest unacknowledged local tic for a paced retransmission. */
bool p4_doom_mp_tx_window_oldest(
    const p4_doom_mp_tx_window_t *window,
    p4_doom_mp_tic_t *tic_out);

size_t p4_doom_mp_tx_window_pending(
    const p4_doom_mp_tx_window_t *window);

#ifdef __cplusplus
}
#endif

#endif
