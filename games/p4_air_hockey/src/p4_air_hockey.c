// SPDX-License-Identifier: MIT

#include "p4_air_hockey_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "table_presentation.h"
#include "rink_marks.h"
#include "generated/presentation_assets.inc"



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
    state->ui_screen = P4_AIR_HOCKEY_UI_PLAY;
    state->touch_on_rink = false;
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
        state->ui_screen = P4_AIR_HOCKEY_UI_TITLE;
    }
    (void)p4_game_play_tone(context, 196U, 55U, 2U, P4_WAVE_SQUARE);
    (void)p4_game_play_tone(context, 523U, 85U, 3U, P4_WAVE_TRIANGLE);
    (void)p4_game_play_tone(context, 1047U, 110U, 2U, P4_WAVE_TRIANGLE);
    return true;
}

static bool point_in(uint16_t x, uint16_t y, unsigned left, unsigned top,
                     unsigned width, unsigned height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
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
    const bool touch_down = touch_active && !state->touch_was_down;
    state->touch_was_down = touch_active;
    if (!touch_active) state->touch_on_rink = false;
    /* The OS also maps touch regions to virtual buttons. Direct-touch UI owns
     * this contact; otherwise crossing the old Back/Start zones would exit or
     * pause a rink drag. Physical controls remain usable without a contact. */
    const uint32_t pressed = touch_active ? 0U : input->pressed;
    const bool panel = state->ui_screen != P4_AIR_HOCKEY_UI_PLAY ||
        state->phase == P4_AIR_HOCKEY_GAME_OVER;
    const bool primary = touch_down && panel &&
        point_in(touch_x, touch_y, 88U, 118U, 88U, 24U);
    const bool panel_exit = touch_down && panel &&
        point_in(touch_x, touch_y, 184U, 118U, 48U, 24U);
    if ((pressed & P4_BUTTON_BACK) != 0U || panel_exit ||
        (touch_down && point_in(touch_x, touch_y, 4U, 3U, 43U, 20U))) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    const bool confirm = (pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U;
    if (state->mode == P4_AIR_HOCKEY_OFFLINE) {
        if (state->ui_screen != P4_AIR_HOCKEY_UI_PLAY) {
            if (primary || confirm) {
                state->ui_screen = P4_AIR_HOCKEY_UI_PLAY;
                state->touch_on_rink = false;
                (void)p4_game_play_tone(context, 784U, 55U, 2U, P4_WAVE_TRIANGLE);
            }
            /* No simulation, timer or remainder debt accumulates in menus. */
            return P4_GAME_CONTINUE;
        }
        if (state->phase != P4_AIR_HOCKEY_GAME_OVER &&
            (((pressed & P4_BUTTON_START) != 0U) ||
             (touch_down && point_in(touch_x, touch_y, 254U, 3U, 62U, 20U)))) {
            state->ui_screen = P4_AIR_HOCKEY_UI_PAUSED;
            state->touch_on_rink = false;
            p4_air_hockey_set_touch_target(state, 0U, false, 0U, 0U);
            p4_game_stop_audio(context);
            return P4_GAME_CONTINUE;
        }
    }
    if (touch_down && !panel) {
        state->touch_on_rink = point_in(touch_x, touch_y, 19U, 34U, 282U, 133U);
    }
    const bool rink_touch = touch_active && state->touch_on_rink;
    const bool restart = state->phase == P4_AIR_HOCKEY_GAME_OVER &&
        (primary || confirm);
    uint32_t events = P4_AIR_HOCKEY_EVENT_NONE;
    if (state->mode == P4_AIR_HOCKEY_NETWORK) {
        if (!p4_air_hockey_network_update(
                context, state, rink_touch, touch_x, touch_y,
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
            state, 0U, rink_touch, touch_x, touch_y);
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

static int view_fixed_x(const p4_air_hockey_state_t *state, int32_t world_x)
{
    const int32_t reflected = state->local_player_slot == 1U
        ? ((P4_GAME_SURFACE_WIDTH - 1) << P4_AIR_HOCKEY_FIXED_SHIFT) - world_x
        : world_x;
    return (int)reflected;
}

static void draw_object(p4_game_surface_t *surface,
                         const p4_air_hockey_state_t *state,
                         int32_t world_x, int32_t world_y,
                         int logical_size, unsigned sprite)
{
    /* Keep fractional physics coordinates until the final native-pixel mapping.
     * Truncating to a 320px position first made native movement jump 2-3 pixels. */
    const int x = view_fixed_x(state, world_x) -
        ((logical_size / 2) << P4_AIR_HOCKEY_FIXED_SHIFT);
    const int y = (int)world_y -
        ((logical_size / 2) << P4_AIR_HOCKEY_FIXED_SHIFT);
    const int px = x * (int)surface->width /
        (P4_GAME_SURFACE_WIDTH << P4_AIR_HOCKEY_FIXED_SHIFT);
    const int py = y * (int)surface->height /
        (P4_GAME_SURFACE_HEIGHT << P4_AIR_HOCKEY_FIXED_SHIFT);
    const int right = (x + (logical_size << P4_AIR_HOCKEY_FIXED_SHIFT)) *
        (int)surface->width / (P4_GAME_SURFACE_WIDTH << P4_AIR_HOCKEY_FIXED_SHIFT);
    const int bottom = (y + (logical_size << P4_AIR_HOCKEY_FIXED_SHIFT)) *
        (int)surface->height / (P4_GAME_SURFACE_HEIGHT << P4_AIR_HOCKEY_FIXED_SHIFT);
    p4_ui_sprite(surface, px, py, right - px, bottom - py, hockey_objects_hd[sprite],
                 66, 66, true, PRESENTATION_CHROMA);
}

static void centered_text(p4_game_surface_t *surface, int center, int y,
                          const char *text, uint16_t color, unsigned scale)
{
    size_t length = 0U;
    while (length < 40U && text[length] != '\0') ++length;
    if (surface->width == 320U) {
        table_text(surface, center - (int)(length * 6U * scale) / 2,
                   y, text, color, scale, length);
    } else {
        const unsigned height = scale > 1U ? 38U : 24U;
        p4_ui_text(surface, p4_ui_x(surface, center) -
            p4_ui_text_width(text, height, length) / 2,
            p4_ui_y(surface, y) - 3, text, color, height, length);
    }
}

static void draw_button(p4_game_surface_t *surface, int x, int y, int w, int h,
                        const char *label, bool primary)
{
    const uint16_t edge = primary ? 0x6e7fU : 0x4a90U;
    table_fill(surface, x, y + 2, w, h, 0x0004U);
    table_fill(surface, x, y, w, h - 1, primary ? 0x1254U : 0x10ccU);
    table_rect(surface, x, y, w, h - 1, edge);
    table_fill(surface, x + 2, y + 2, w - 4, 1, primary ? 0x3dbcU : 0x214fU);
    centered_text(surface, x + w / 2, y + (h - 8) / 2, label, COLOR_TEXT, 1U);
}

static void draw_header(p4_game_surface_t *surface,
                        const p4_air_hockey_state_t *state)
{
    const uint8_t local = state->local_player_slot < P4_AIR_HOCKEY_PLAYERS
        ? state->local_player_slot : 0U;
    const uint8_t rival = (uint8_t)(local ^ 1U);
    char scores[] = "YOU 0 : 0 RIVAL";
    scores[4] = (char)('0' + state->score[local]);
    scores[8] = (char)('0' + state->score[rival]);
    table_fill(surface, 0, 0, 320, 25, 0x0006U);
    table_fill(surface, 0, 24, 320, 1, 0x2cdbU);
    draw_button(surface, 4, 3, 43, 20, "EXIT", false);
    table_text(surface, 54, 9, "AIR HOCKEY", 0x9effU, 1U, 10U);
    centered_text(surface, 188, 9, scores, COLOR_TEXT, 1U);
    if (state->mode == P4_AIR_HOCKEY_NETWORK) {
        centered_text(surface, 285, 9, "LINKED", 0x9effU, 1U);
    } else if (state->ui_screen == P4_AIR_HOCKEY_UI_PLAY &&
               state->phase != P4_AIR_HOCKEY_GAME_OVER) {
        draw_button(surface, 254, 3, 62, 20, "PAUSE", false);
    } else {
        centered_text(surface, 285, 9, "SOLO", COLOR_MUTED, 1U);
    }
}

static void draw_menu(p4_game_surface_t *surface, const p4_air_hockey_state_t *state)
{
    const bool title = state->ui_screen == P4_AIR_HOCKEY_UI_TITLE;
    const bool paused = state->ui_screen == P4_AIR_HOCKEY_UI_PAUSED;
    const bool won = state->winner == state->local_player_slot;
    table_fill(surface, 69, 50, 188, 106, 0x0003U);
    table_fill(surface, 66, 47, 188, 106, 0x0008U);
    table_rect(surface, 66, 47, 188, 106, 0x2cdbU);
    table_fill(surface, 68, 49, 184, 2, 0x6e7fU);
    centered_text(surface, 160, 55,
        title ? "SOLO / FIRST TO 7" : paused ? "MATCH ON HOLD" : "MATCH COMPLETE",
        0x9effU, 1U);
    centered_text(surface, 160, 71,
        title ? "AIR HOCKEY" : paused ? "PAUSED" : won ? "YOU WIN!" : "RIVAL WINS",
        !title && !paused && won ? COLOR_GOAL : COLOR_TEXT, 2U);
    centered_text(surface, 160, 96,
        title ? "DRAG YOUR HALF TO STRIKE" : paused ? "RESUME WHEN YOU ARE READY" : "READY FOR ANOTHER ROUND?",
        COLOR_MUTED, 1U);
    if (title) centered_text(surface, 160, 106, "DEFEND LEFT. SCORE RIGHT.", COLOR_MUTED, 1U);
    draw_button(surface, 88, 118, 88, 24,
        title ? "PLAY" : paused ? "RESUME" : "REMATCH", true);
    draw_button(surface, 184, 118, 48, 24, "EXIT", false);
}

/* Crisp rink markings are drawn at native resolution over compact repeatable
 * ImageGen materials. These coordinates exactly match the physics surface. */
static void draw_rink(p4_game_surface_t *surface)
{
    /* Paint disjoint material regions, avoiding three full covered layers. */
    table_material(surface,0,175,320,25,hockey_material[2],64);
    table_material(surface,0,25,8,150,hockey_material[2],64);
    table_material(surface,312,25,8,150,hockey_material[2],64);
    table_material(surface,8,25,304,5,hockey_material[1],64);
    table_material(surface,8,170,304,5,hockey_material[1],64);
    table_material(surface,8,30,7,140,hockey_material[1],64);
    table_material(surface,305,30,7,140,hockey_material[1],64);
    table_fill(surface,15,30,290,4,0x0048U);
    table_fill(surface,15,167,290,3,0x0048U);
    table_fill(surface,15,34,4,133,0x0048U);
    table_fill(surface,301,34,4,133,0x0048U);
    table_material(surface,19,34,282,133,hockey_material[0],64);
    table_rect(surface,19,34,282,133,0x2cdbU);
    table_fill(surface,19,32,282,2,0x6e7fU);
    table_fill(surface,19,167,282,2,0x2cdbU);
    for(int y=36;y<166;y+=7) table_fill(surface,159,y,1,4,0x3d7bU);
    hockey_rink_ring(surface,160,100,23,0x6e7fU);
    hockey_rink_ring(surface,57,100,17,0x2d5cU);
    hockey_rink_ring(surface,263,100,17,0xa2f5U);
    table_circle(surface,160,100,2,0x9effU);
    table_fill(surface,9,80,11,41,0x0025U);
    table_fill(surface,300,80,11,41,0x1806U);
    for(int y=82;y<120;y+=6) {
        table_fill(surface,10,y,8,1,0x2c38U);
        table_fill(surface,302,y,8,1,0x694fU);
    }
    table_fill(surface,18,78,2,3,0x9effU);
    table_fill(surface,18,120,2,3,0x9effU);
    table_fill(surface,300,78,2,3,0xfbb7U);
    table_fill(surface,300,120,2,3,0xfbb7U);
    table_text(surface, 20, 186, "DRAG YOUR HALF", 0x9cd5U, 1U, 14U);
    table_text(surface, 231, 186, "FIRST TO 7", 0x9cd5U, 1U, 10U);
    table_fill(surface, 20, 179, 280, 1, 0x214cU);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const p4_air_hockey_state_t *const state = context->state;
    const p4_air_hockey_pose_t pose = p4_air_hockey_render_pose(state);
    draw_rink(surface);
    for (uint8_t player = 0U; player < P4_AIR_HOCKEY_PLAYERS; ++player) {
        const unsigned sprite = p4_air_hockey_striker_skin(player) ==
            P4_AIR_HOCKEY_STRIKER_CYAN ? 0U : 1U;
        draw_object(surface, state, pose.paddle_x[player],
                    pose.paddle_y[player], 27, sprite);
    }
    draw_object(surface, state, pose.puck_x, pose.puck_y, 17, 2U);
    draw_header(surface, state);
    if (state->ui_screen != P4_AIR_HOCKEY_UI_PLAY ||
        state->phase == P4_AIR_HOCKEY_GAME_OVER) {
        draw_menu(surface, state);
        return true;
    }

    if (state->phase == P4_AIR_HOCKEY_NETWORK_WAIT) {
        table_fill(surface, 68, 89, 184, 22, COLOR_HUD_BG);
        table_text(surface, 84, 96, "SYNCING WITH HOST",
                     COLOR_TEXT, 1U, 17U);
    } else if (state->phase == P4_AIR_HOCKEY_SERVE) {
        table_fill(surface, 126, 91, 68, 17, COLOR_HUD_BG);
        table_text(surface, 124, 96, "GET READY", COLOR_TEXT, 1U, 9U);
    } else if (state->phase == P4_AIR_HOCKEY_GOAL) {
        table_fill(surface, 136, 91, 48, 17, COLOR_HUD_BG);
        table_text(surface, 140, 96, "GOAL!", COLOR_GOAL, 1U, 5U);
    }
    if (state->disconnect_banner_ms != 0U) {
        table_fill(surface, 57, 122, 206, 18, COLOR_HUD_BG);
        table_text(surface, 60, 127, "PEER LEFT - CPU TAKES OVER",
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
    .title = "Air Hockey",
    .subtitle = "Touch hockey against the CPU",
    .accent_rgb565 = UINT16_C(0x5fea),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION | P4_GAME_CAP_VIDEO_HIGH_RES,
    .state_bytes = sizeof(p4_air_hockey_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
