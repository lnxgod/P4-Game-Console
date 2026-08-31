// SPDX-License-Identifier: MIT

#ifndef P4_AIR_HOCKEY_INTERNAL_H
#define P4_AIR_HOCKEY_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/game.h"

enum {
    P4_AIR_HOCKEY_PLAYERS = 2,
    P4_AIR_HOCKEY_WIN_SCORE = 7,
    P4_AIR_HOCKEY_TABLE_LEFT = 19,
    P4_AIR_HOCKEY_TABLE_RIGHT = 300,
    P4_AIR_HOCKEY_TABLE_TOP = 34,
    P4_AIR_HOCKEY_TABLE_BOTTOM = 166,
    P4_AIR_HOCKEY_GOAL_TOP = 80,
    P4_AIR_HOCKEY_GOAL_BOTTOM = 120,
    P4_AIR_HOCKEY_PADDLE_RADIUS = 10,
    P4_AIR_HOCKEY_PUCK_RADIUS = 5,
    P4_AIR_HOCKEY_FIXED_SHIFT = 8,
    P4_AIR_HOCKEY_STEP_MS = 16,
    P4_AIR_HOCKEY_SNAPSHOT_BYTES = 32,
    P4_AIR_HOCKEY_INPUT_BYTES = 8,
    P4_AIR_HOCKEY_PROTOCOL = 4,
};

typedef enum {
    P4_AIR_HOCKEY_STRIKER_CYAN = 0,
    P4_AIR_HOCKEY_STRIKER_MAGENTA,
} p4_air_hockey_striker_skin_t;

typedef enum {
    P4_AIR_HOCKEY_OFFLINE = 0,
    P4_AIR_HOCKEY_NETWORK,
} p4_air_hockey_mode_t;

typedef enum {
    P4_AIR_HOCKEY_SERVE = 0,
    P4_AIR_HOCKEY_PLAY,
    P4_AIR_HOCKEY_GOAL,
    P4_AIR_HOCKEY_GAME_OVER,
    P4_AIR_HOCKEY_NETWORK_WAIT,
} p4_air_hockey_phase_t;

enum {
    P4_AIR_HOCKEY_EVENT_NONE = 0,
    P4_AIR_HOCKEY_EVENT_HIT = UINT32_C(1) << 0U,
    P4_AIR_HOCKEY_EVENT_WALL = UINT32_C(1) << 1U,
    P4_AIR_HOCKEY_EVENT_GOAL = UINT32_C(1) << 2U,
    P4_AIR_HOCKEY_EVENT_WIN = UINT32_C(1) << 3U,
    P4_AIR_HOCKEY_EVENT_SERVE = UINT32_C(1) << 4U,
    P4_AIR_HOCKEY_EVENT_AUDIO_MASK =
        P4_AIR_HOCKEY_EVENT_HIT | P4_AIR_HOCKEY_EVENT_WALL |
        P4_AIR_HOCKEY_EVENT_GOAL | P4_AIR_HOCKEY_EVENT_WIN |
        P4_AIR_HOCKEY_EVENT_SERVE,
};

typedef struct {
    p4_air_hockey_mode_t mode;
    p4_air_hockey_phase_t phase;
    int32_t paddle_x[P4_AIR_HOCKEY_PLAYERS];
    int32_t paddle_y[P4_AIR_HOCKEY_PLAYERS];
    int32_t puck_x;
    int32_t puck_y;
    int32_t puck_vx;
    int32_t puck_vy;
    int32_t target_x[P4_AIR_HOCKEY_PLAYERS];
    int32_t target_y[P4_AIR_HOCKEY_PLAYERS];
    uint32_t step_accumulator_ms;
    uint32_t phase_timer_ms;
    uint32_t rng;
    uint32_t simulation_tick;
    uint32_t snapshot_revision;
    uint32_t last_network_sequence[P4_AIR_HOCKEY_PLAYERS];
    uint32_t network_send_ms;
    uint32_t input_send_ms;
    uint32_t disconnect_banner_ms;
    uint32_t network_audio_events;
    uint64_t session_seed;
    p4_game_multiplayer_role_t network_role;
    uint8_t local_player_slot;
    uint8_t score[P4_AIR_HOCKEY_PLAYERS];
    uint8_t serving_player;
    uint8_t winner;
    uint16_t last_sent_touch_x;
    uint16_t last_sent_touch_y;
    bool target_active[P4_AIR_HOCKEY_PLAYERS];
    bool last_sent_touch_active;
    bool snapshot_received;
    bool restart_requested;
} p4_air_hockey_state_t;

void p4_air_hockey_reset_match(p4_air_hockey_state_t *state, uint32_t seed);
uint32_t p4_air_hockey_step(p4_air_hockey_state_t *state,
                            uint32_t elapsed_ms, bool cpu_opponent);
void p4_air_hockey_set_touch_target(p4_air_hockey_state_t *state,
                                    uint8_t player, bool active,
                                    uint16_t x, uint16_t y);
void p4_air_hockey_canonical_touch(uint8_t player_slot,
                                   uint16_t local_x, uint16_t local_y,
                                   uint16_t *world_x, uint16_t *world_y);
p4_air_hockey_striker_skin_t p4_air_hockey_striker_skin(
    uint8_t player_slot);
bool p4_air_hockey_network_begin(p4_game_context_t *context,
                                 p4_air_hockey_state_t *state);
bool p4_air_hockey_network_update(p4_game_context_t *context,
                                  p4_air_hockey_state_t *state,
                                  bool touch_active,
                                  uint16_t touch_x,
                                  uint16_t touch_y,
                                  bool restart_pressed,
                                  uint32_t elapsed_ms);
void p4_air_hockey_network_publish(p4_game_context_t *context,
                                   p4_air_hockey_state_t *state,
                                   bool force);

#endif
