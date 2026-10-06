// SPDX-License-Identifier: MIT

#include "p4_air_hockey_internal.h"

#include <stddef.h>
#include <stdint.h>

#define FP(value) ((int32_t)(value) << P4_AIR_HOCKEY_FIXED_SHIFT)

enum {
    PADDLE_SPEED = FP(6),
    CPU_SPEED = FP(3),
    PUCK_START_X_SPEED = 510,
    PUCK_MAX_X_SPEED = FP(5),
    PUCK_MAX_Y_SPEED = FP(4),
};

static p4_air_hockey_pose_t current_pose(const p4_air_hockey_state_t *state)
{
    return (p4_air_hockey_pose_t){
        .paddle_x = {state->paddle_x[0], state->paddle_x[1]},
        .paddle_y = {state->paddle_y[0], state->paddle_y[1]},
        .puck_x = state->puck_x, .puck_y = state->puck_y,
    };
}
void p4_air_hockey_visual_snap(p4_air_hockey_state_t *state)
{
    state->previous_pose = current_pose(state);
    state->client_visual_ms = 0U;
    state->visual_ready = true;
}
static int32_t interpolate(int32_t previous, int32_t current, uint32_t part, uint32_t whole)
{
    /* Legal world/snapshot positions are <=320*256; this bounded product fits
     * 32 bits and needs no 64-bit arithmetic on the P4. Never extrapolate. */
    return previous + (current - previous) * (int32_t)part / (int32_t)whole;
}
p4_air_hockey_pose_t p4_air_hockey_render_pose(const p4_air_hockey_state_t *state)
{
    p4_air_hockey_pose_t pose = current_pose(state);
    if (!state->visual_ready) return pose;
    const bool client = state->mode == P4_AIR_HOCKEY_NETWORK &&
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT;
    const uint32_t whole = client ? P4_AIR_HOCKEY_SNAPSHOT_MS : P4_AIR_HOCKEY_STEP_MS;
    uint32_t part = client ? state->client_visual_ms : state->step_accumulator_ms;
    if (part > whole) part = whole;
    for (unsigned player = 0U; player < P4_AIR_HOCKEY_PLAYERS; ++player) {
        pose.paddle_x[player] = interpolate(state->previous_pose.paddle_x[player], pose.paddle_x[player], part, whole);
        pose.paddle_y[player] = interpolate(state->previous_pose.paddle_y[player], pose.paddle_y[player], part, whole);
    }
    pose.puck_x = interpolate(state->previous_pose.puck_x, pose.puck_x, part, whole);
    pose.puck_y = interpolate(state->previous_pose.puck_y, pose.puck_y, part, whole);
    return pose;
}

static uint32_t next_random(p4_air_hockey_state_t *state)
{
    uint32_t value = state->rng;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    state->rng = value == 0U ? UINT32_C(0x5041484b) : value;
    return state->rng;
}

static int32_t clamp_i32(int32_t value, int32_t low, int32_t high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

static int32_t absolute_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void reset_positions(p4_air_hockey_state_t *state)
{
    state->paddle_x[0] = FP(58);
    state->paddle_y[0] = FP(100);
    state->paddle_x[1] = FP(262);
    state->paddle_y[1] = FP(100);
    state->target_x[0] = state->paddle_x[0];
    state->target_y[0] = state->paddle_y[0];
    state->target_x[1] = state->paddle_x[1];
    state->target_y[1] = state->paddle_y[1];
    state->puck_x = FP(160);
    state->puck_y = FP(100);
    state->puck_vx = 0;
    state->puck_vy = 0;
    state->target_active[0] = false;
    state->target_active[1] = false;
}

void p4_air_hockey_reset_match(p4_air_hockey_state_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    state->score[0] = 0U;
    state->score[1] = 0U;
    state->serving_player = 0U;
    state->winner = UINT8_C(0xff);
    state->rng = seed == 0U ? UINT32_C(0x5041484b) : seed;
    state->simulation_tick = 0U;
    state->step_accumulator_ms = 0U;
    state->phase_timer_ms = 700U;
    state->network_audio_events = P4_AIR_HOCKEY_EVENT_NONE;
    state->phase = P4_AIR_HOCKEY_SERVE;
    reset_positions(state);
    p4_air_hockey_visual_snap(state);
}

void p4_air_hockey_canonical_touch(uint8_t player_slot,
                                   uint16_t local_x, uint16_t local_y,
                                   uint16_t *world_x, uint16_t *world_y)
{
    if (world_x != NULL) {
        *world_x = player_slot == 1U
            ? (uint16_t)(P4_GAME_SURFACE_WIDTH - 1U - local_x) : local_x;
    }
    if (world_y != NULL) {
        *world_y = local_y;
    }
}

p4_air_hockey_striker_skin_t p4_air_hockey_striker_skin(
    uint8_t player_slot)
{
    return player_slot == 1U ? P4_AIR_HOCKEY_STRIKER_MAGENTA
                             : P4_AIR_HOCKEY_STRIKER_CYAN;
}

void p4_air_hockey_set_touch_target(p4_air_hockey_state_t *state,
                                    uint8_t player, bool active,
                                    uint16_t x, uint16_t y)
{
    if (state == NULL || player >= P4_AIR_HOCKEY_PLAYERS) {
        return;
    }
    const int32_t min_x = player == 0U ? FP(31) : FP(170);
    const int32_t max_x = player == 0U ? FP(150) : FP(289);
    const int32_t min_y = FP(P4_AIR_HOCKEY_TABLE_TOP +
                             P4_AIR_HOCKEY_PADDLE_RADIUS + 1);
    const int32_t max_y = FP(P4_AIR_HOCKEY_TABLE_BOTTOM -
                             P4_AIR_HOCKEY_PADDLE_RADIUS - 1);
    state->target_active[player] = active;
    if (active) {
        state->target_x[player] = clamp_i32(FP(x), min_x, max_x);
        state->target_y[player] = clamp_i32(FP(y), min_y, max_y);
    } else {
        state->target_x[player] = state->paddle_x[player];
        state->target_y[player] = state->paddle_y[player];
    }
}

static int32_t move_toward(int32_t current, int32_t target, int32_t limit)
{
    const int32_t difference = target - current;
    return current + clamp_i32(difference, -limit, limit);
}

static void move_paddle(p4_air_hockey_state_t *state, uint8_t player,
                        int32_t speed)
{
    state->paddle_x[player] = move_toward(
        state->paddle_x[player], state->target_x[player], speed);
    state->paddle_y[player] = move_toward(
        state->paddle_y[player], state->target_y[player], speed);
}

static uint32_t collide_paddle(p4_air_hockey_state_t *state, uint8_t player)
{
    const int32_t dx = (state->puck_x - state->paddle_x[player]) >>
        P4_AIR_HOCKEY_FIXED_SHIFT;
    const int32_t dy = (state->puck_y - state->paddle_y[player]) >>
        P4_AIR_HOCKEY_FIXED_SHIFT;
    const int32_t radius = P4_AIR_HOCKEY_PADDLE_RADIUS +
        P4_AIR_HOCKEY_PUCK_RADIUS;
    if (dx * dx + dy * dy > radius * radius ||
        (player == 0U ? state->puck_vx >= 0 : state->puck_vx <= 0)) {
        return P4_AIR_HOCKEY_EVENT_NONE;
    }
    const int32_t speed_x = absolute_i32(state->puck_vx) + 32;
    state->puck_vx = player == 0U
        ? clamp_i32(speed_x, FP(2), PUCK_MAX_X_SPEED)
        : -clamp_i32(speed_x, FP(2), PUCK_MAX_X_SPEED);
    state->puck_vy = clamp_i32(
        state->puck_vy + dy * 38, -PUCK_MAX_Y_SPEED, PUCK_MAX_Y_SPEED);
    state->puck_x = state->paddle_x[player] +
        (player == 0U ? FP(radius) : -FP(radius));
    return P4_AIR_HOCKEY_EVENT_HIT;
}

static void begin_serve(p4_air_hockey_state_t *state)
{
    const uint32_t random = next_random(state);
    state->puck_x = FP(160);
    state->puck_y = FP(100);
    state->puck_vx = state->serving_player == 0U
        ? PUCK_START_X_SPEED : -PUCK_START_X_SPEED;
    state->puck_vy = (int32_t)(random % 513U) - 256;
    state->phase = P4_AIR_HOCKEY_PLAY;
    state->phase_timer_ms = 0U;
}

static uint32_t score_goal(p4_air_hockey_state_t *state, uint8_t player)
{
    ++state->score[player];
    state->serving_player = (uint8_t)(player ^ 1U);
    state->puck_vx = 0;
    state->puck_vy = 0;
    if (state->score[player] >= P4_AIR_HOCKEY_WIN_SCORE) {
        state->phase = P4_AIR_HOCKEY_GAME_OVER;
        state->winner = player;
        state->phase_timer_ms = 0U;
        return P4_AIR_HOCKEY_EVENT_GOAL | P4_AIR_HOCKEY_EVENT_WIN;
    }
    state->phase = P4_AIR_HOCKEY_GOAL;
    state->phase_timer_ms = 850U;
    return P4_AIR_HOCKEY_EVENT_GOAL;
}

static uint32_t simulate_puck(p4_air_hockey_state_t *state)
{
    uint32_t events = P4_AIR_HOCKEY_EVENT_NONE;
    state->puck_x += state->puck_vx;
    state->puck_y += state->puck_vy;
    const int32_t top = FP(P4_AIR_HOCKEY_TABLE_TOP +
                           P4_AIR_HOCKEY_PUCK_RADIUS + 1);
    const int32_t bottom = FP(P4_AIR_HOCKEY_TABLE_BOTTOM -
                              P4_AIR_HOCKEY_PUCK_RADIUS - 1);
    if (state->puck_y < top) {
        state->puck_y = top;
        state->puck_vy = absolute_i32(state->puck_vy);
        events |= P4_AIR_HOCKEY_EVENT_WALL;
    } else if (state->puck_y > bottom) {
        state->puck_y = bottom;
        state->puck_vy = -absolute_i32(state->puck_vy);
        events |= P4_AIR_HOCKEY_EVENT_WALL;
    }
    events |= collide_paddle(state, 0U);
    events |= collide_paddle(state, 1U);

    const int puck_y = (int)(state->puck_y >> P4_AIR_HOCKEY_FIXED_SHIFT);
    if (state->puck_x < FP(P4_AIR_HOCKEY_TABLE_LEFT)) {
        if (puck_y >= P4_AIR_HOCKEY_GOAL_TOP &&
            puck_y <= P4_AIR_HOCKEY_GOAL_BOTTOM) {
            return events | score_goal(state, 1U);
        }
        state->puck_x = FP(P4_AIR_HOCKEY_TABLE_LEFT);
        state->puck_vx = absolute_i32(state->puck_vx);
        events |= P4_AIR_HOCKEY_EVENT_WALL;
    } else if (state->puck_x > FP(P4_AIR_HOCKEY_TABLE_RIGHT)) {
        if (puck_y >= P4_AIR_HOCKEY_GOAL_TOP &&
            puck_y <= P4_AIR_HOCKEY_GOAL_BOTTOM) {
            return events | score_goal(state, 0U);
        }
        state->puck_x = FP(P4_AIR_HOCKEY_TABLE_RIGHT);
        state->puck_vx = -absolute_i32(state->puck_vx);
        events |= P4_AIR_HOCKEY_EVENT_WALL;
    }
    return events;
}

static void update_cpu_target(p4_air_hockey_state_t *state)
{
    state->target_active[1] = true;
    state->target_x[1] = state->phase == P4_AIR_HOCKEY_PLAY &&
        state->puck_x > FP(145) ? clamp_i32(state->puck_x, FP(178), FP(274))
                                : FP(262);
    state->target_y[1] = state->phase == P4_AIR_HOCKEY_PLAY
        ? clamp_i32(state->puck_y, FP(47), FP(153)) : FP(100);
}

uint32_t p4_air_hockey_step(p4_air_hockey_state_t *state,
                            uint32_t elapsed_ms, bool cpu_opponent)
{
    if (state == NULL) {
        return P4_AIR_HOCKEY_EVENT_NONE;
    }
    uint32_t events = P4_AIR_HOCKEY_EVENT_NONE;
    state->step_accumulator_ms += elapsed_ms;
    while (state->step_accumulator_ms >= P4_AIR_HOCKEY_STEP_MS) {
        state->step_accumulator_ms -= P4_AIR_HOCKEY_STEP_MS;
        ++state->simulation_tick;
        state->previous_pose = current_pose(state);
        state->visual_ready = true;
        const p4_air_hockey_phase_t previous_phase = state->phase;
        if (state->phase == P4_AIR_HOCKEY_GAME_OVER ||
            state->phase == P4_AIR_HOCKEY_NETWORK_WAIT) {
            continue;
        }
        if (cpu_opponent) {
            update_cpu_target(state);
        }
        move_paddle(state, 0U, PADDLE_SPEED);
        move_paddle(state, 1U, cpu_opponent ? CPU_SPEED : PADDLE_SPEED);
        if (state->phase == P4_AIR_HOCKEY_SERVE ||
            state->phase == P4_AIR_HOCKEY_GOAL) {
            if (state->phase_timer_ms <= P4_AIR_HOCKEY_STEP_MS) {
                begin_serve(state);
                events |= P4_AIR_HOCKEY_EVENT_SERVE;
            } else {
                state->phase_timer_ms -= P4_AIR_HOCKEY_STEP_MS;
            }
        } else if (state->phase == P4_AIR_HOCKEY_PLAY) {
            events |= simulate_puck(state);
        }
        if (state->phase != previous_phase) p4_air_hockey_visual_snap(state);
    }
    return events;
}
