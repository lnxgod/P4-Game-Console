// SPDX-License-Identifier: MIT

#include "p4_air_hockey_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"

#include "generated/p4_air_hockey_rink.inc"
#include "generated/p4_air_hockey_objects.inc"

#define PIXELS(value) ((int)((value) >> P4_AIR_HOCKEY_FIXED_SHIFT))

enum {
    COLOR_HUD_BG = 0x0008,
    COLOR_GOAL = 0xffc0,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0x9cf3,
};

static void play_events(p4_game_context_t *context, uint32_t events)
{
    if ((events & P4_AIR_HOCKEY_EVENT_WIN) != 0U) {
        (void)p4_game_play_tone(context, 196U, 260U, 3U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 523U, 220U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 784U, 260U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 1568U, 180U, 3U, P4_WAVE_SQUARE);
    } else if ((events & P4_AIR_HOCKEY_EVENT_GOAL) != 0U) {
        (void)p4_game_play_tone(context, 110U, 170U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 659U, 140U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 988U, 95U, 3U, P4_WAVE_TRIANGLE);
    } else if ((events & P4_AIR_HOCKEY_EVENT_HIT) != 0U) {
        (void)p4_game_play_tone(context, 280U, 34U, 3U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 840U, 24U, 2U, P4_WAVE_TRIANGLE);
    } else if ((events & P4_AIR_HOCKEY_EVENT_WALL) != 0U) {
        (void)p4_game_play_tone(context, 196U, 20U, 2U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 1568U, 14U, 1U, P4_WAVE_TRIANGLE);
    } else if ((events & P4_AIR_HOCKEY_EVENT_SERVE) != 0U) {
        (void)p4_game_play_tone(context, 392U, 55U, 2U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 784U, 90U, 3U, P4_WAVE_TRIANGLE);
    }
}

static void fall_back_to_cpu(p4_air_hockey_state_t *state)
{
    const uint32_t seed = state->rng ^ state->snapshot_revision ^
        UINT32_C(0x43505521);
    state->mode = P4_AIR_HOCKEY_OFFLINE;
    state->network_role = P4_GAME_MULTIPLAYER_ROLE_NONE;
    state->local_player_slot = 0U;
    p4_air_hockey_reset_match(state, seed);
    state->disconnect_banner_ms = 1800U;
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(p4_air_hockey_state_t)) {
        return false;
    }
    p4_air_hockey_state_t *const state = context->state;
    *state = (p4_air_hockey_state_t){
        .winner = UINT8_C(0xff),
        .rng = UINT32_C(0x5041484b),
    };
    if (!p4_air_hockey_network_begin(context, state)) {
        state->mode = P4_AIR_HOCKEY_OFFLINE;
        state->local_player_slot = 0U;
        p4_air_hockey_reset_match(state, UINT32_C(0x5041484b));
    }
    (void)p4_game_play_tone(context, 196U, 55U, 2U, P4_WAVE_SQUARE);
    (void)p4_game_play_tone(context, 523U, 85U, 3U, P4_WAVE_TRIANGLE);
    (void)p4_game_play_tone(context, 1047U, 110U, 2U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    p4_air_hockey_state_t *const state = context->state;
    const bool touch_active = input->touch_valid && input->touch_count != 0U;
    const uint16_t touch_x = touch_active ? input->touches[0].x : 0U;
    const uint16_t touch_y = touch_active ? input->touches[0].y : 0U;
    if ((input->pressed & P4_BUTTON_BACK) != 0U ||
        (touch_active && touch_x < 50U && touch_y < 22U)) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    const bool restart = touch_active &&
        state->phase == P4_AIR_HOCKEY_GAME_OVER;
    uint32_t events = P4_AIR_HOCKEY_EVENT_NONE;
    if (state->mode == P4_AIR_HOCKEY_NETWORK) {
        if (!p4_air_hockey_network_update(
                context, state, touch_active, touch_x, touch_y,
                restart, elapsed_ms)) {
            fall_back_to_cpu(state);
        } else if (state->network_role ==
                   P4_GAME_MULTIPLAYER_ROLE_HOST) {
            if (state->phase == P4_AIR_HOCKEY_GAME_OVER &&
                state->restart_requested) {
                p4_air_hockey_reset_match(
                    state, state->rng ^ state->snapshot_revision);
                state->restart_requested = false;
                p4_air_hockey_network_publish(context, state, true);
            } else {
                events = p4_air_hockey_step(state, elapsed_ms, false);
                state->network_audio_events |= events &
                    P4_AIR_HOCKEY_EVENT_AUDIO_MASK;
                p4_air_hockey_network_publish(
                    context, state,
                    (events & (P4_AIR_HOCKEY_EVENT_GOAL |
                               P4_AIR_HOCKEY_EVENT_WIN)) != 0U);
            }
        } else {
            events = state->network_audio_events;
            state->network_audio_events = P4_AIR_HOCKEY_EVENT_NONE;
        }
    }
    if (state->mode == P4_AIR_HOCKEY_OFFLINE) {
        p4_air_hockey_set_touch_target(
            state, 0U, touch_active, touch_x, touch_y);
        if (state->phase == P4_AIR_HOCKEY_GAME_OVER && restart) {
            p4_air_hockey_reset_match(
                state, state->rng ^ state->simulation_tick);
        } else {
            events = p4_air_hockey_step(state, elapsed_ms, true);
        }
    }
    if (state->disconnect_banner_ms > elapsed_ms) {
        state->disconnect_banner_ms -= elapsed_ms;
    } else {
        state->disconnect_banner_ms = 0U;
    }
    play_events(context, events);
    return P4_GAME_CONTINUE;
}

static int view_x(const p4_air_hockey_state_t *state, int world_x)
{
    return state->local_player_slot == 1U
        ? P4_GAME_SURFACE_WIDTH - 1 - world_x : world_x;
}

static int view_y(const p4_air_hockey_state_t *state, int world_y)
{
    (void)state;
    return world_y;
}

static void draw_score(p4_game_surface_t *surface,
                       const p4_air_hockey_state_t *state)
{
    const uint8_t local = state->local_player_slot < P4_AIR_HOCKEY_PLAYERS
        ? state->local_player_slot : 0U;
    const uint8_t rival = (uint8_t)(local ^ 1U);
    char scores[] = "YOU 0     0 RIVAL";
    scores[4] = (char)('0' + state->score[local]);
    scores[10] = (char)('0' + state->score[rival]);
    p4_draw_fill_rect(surface, 88, 2, 144, 13, COLOR_HUD_BG);
    p4_draw_text(surface, 92, 5, scores, COLOR_TEXT, 1U, 17U);
}

static void draw_mallet(p4_game_surface_t *surface, int x, int y,
                        uint8_t player_slot)
{
    const uint16_t *const pixels =
        p4_air_hockey_striker_skin(player_slot) ==
                P4_AIR_HOCKEY_STRIKER_CYAN
            ? p4_air_hockey_local_striker_rgb565
            : p4_air_hockey_rival_striker_rgb565;
    p4_draw_sprite_rgb565(
        surface, x - P4_AIR_HOCKEY_LOCAL_STRIKER_WIDTH / 2,
        y - P4_AIR_HOCKEY_LOCAL_STRIKER_HEIGHT / 2, pixels,
        P4_AIR_HOCKEY_LOCAL_STRIKER_WIDTH,
        P4_AIR_HOCKEY_LOCAL_STRIKER_HEIGHT,
        P4_AIR_HOCKEY_LOCAL_STRIKER_WIDTH, true,
        P4_AIR_HOCKEY_OBJECT_TRANSPARENT_KEY);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const p4_air_hockey_state_t *const state = context->state;
    p4_draw_sprite_rgb565(surface, 0, 0, p4_air_hockey_rink_rgb565,
                          P4_AIR_HOCKEY_RINK_WIDTH,
                          P4_AIR_HOCKEY_RINK_HEIGHT,
                          P4_AIR_HOCKEY_RINK_WIDTH, false, 0U);
    p4_draw_fill_rect(surface, 3, 2, 45, 15, COLOR_HUD_BG);
    p4_draw_rect(surface, 3, 2, 45, 15, COLOR_MUTED);
    p4_draw_text(surface, 10, 6, "EXIT", COLOR_TEXT, 1U, 4U);
    const uint8_t local = state->local_player_slot < P4_AIR_HOCKEY_PLAYERS
        ? state->local_player_slot : 0U;
    for (uint8_t player = 0U; player < P4_AIR_HOCKEY_PLAYERS; ++player) {
        draw_mallet(
            surface,
            view_x(state, PIXELS(state->paddle_x[player])),
            view_y(state, PIXELS(state->paddle_y[player])),
            player);
    }
    const int puck_x = view_x(state, PIXELS(state->puck_x));
    const int puck_y = view_y(state, PIXELS(state->puck_y));
    p4_draw_sprite_rgb565(
        surface, puck_x - P4_AIR_HOCKEY_PUCK_WIDTH / 2,
        puck_y - P4_AIR_HOCKEY_PUCK_HEIGHT / 2,
        p4_air_hockey_puck_rgb565, P4_AIR_HOCKEY_PUCK_WIDTH,
        P4_AIR_HOCKEY_PUCK_HEIGHT, P4_AIR_HOCKEY_PUCK_WIDTH, true,
        P4_AIR_HOCKEY_OBJECT_TRANSPARENT_KEY);
    draw_score(surface, state);

    if (state->phase == P4_AIR_HOCKEY_NETWORK_WAIT) {
        p4_draw_fill_rect(surface, 68, 89, 184, 22, COLOR_HUD_BG);
        p4_draw_text(surface, 84, 96, "SYNCING WITH HOST",
                     COLOR_TEXT, 1U, 17U);
    } else if (state->phase == P4_AIR_HOCKEY_SERVE) {
        p4_draw_fill_rect(surface, 126, 91, 68, 17, COLOR_HUD_BG);
        p4_draw_text(surface, 124, 96, "GET READY", COLOR_TEXT, 1U, 9U);
    } else if (state->phase == P4_AIR_HOCKEY_GOAL) {
        p4_draw_fill_rect(surface, 136, 91, 48, 17, COLOR_HUD_BG);
        p4_draw_text(surface, 140, 96, "GOAL!", COLOR_GOAL, 1U, 5U);
    } else if (state->phase == P4_AIR_HOCKEY_GAME_OVER) {
        const bool local_won = state->winner == local;
        p4_draw_fill_rect(surface, 80, 82, 160, 39, COLOR_HUD_BG);
        p4_draw_text(surface, local_won ? 124 : 116, 89,
                     local_won ? "YOU WIN!" : "RIVAL WINS",
                     local_won ? COLOR_GOAL : COLOR_TEXT, 1U,
                     local_won ? 8U : 10U);
        p4_draw_text(surface, 92, 104, "TOUCH TO REMATCH",
                     COLOR_MUTED, 1U, 16U);
    }
    if (state->disconnect_banner_ms != 0U) {
        p4_draw_fill_rect(surface, 57, 122, 206, 18, COLOR_HUD_BG);
        p4_draw_text(surface, 60, 127, "PEER LEFT - CPU TAKES OVER",
                     COLOR_GOAL, 1U, 26U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_p4_air_hockey_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(115),
    .id = "org.p4console.p4-air-hockey",
    .title = "P4 AIR HOCKEY",
    .subtitle = "TWO-CONSOLE TOUCH DUEL",
    .accent_rgb565 = UINT16_C(0x5fea),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(p4_air_hockey_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
