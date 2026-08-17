// SPDX-License-Identifier: MIT
/*
 * Byte Buddy is an original, code-rendered virtual pet. It intentionally does
 * not emulate any commercial toy, import a ROM, or use third-party sprites.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"

enum {
    STAT_MAX = 100,
    DECAY_INTERVAL_MS = 5000,
    MINI_GAME_DURATION_MS = 15000,
    MINI_GAME_CATCH_Y = 126,
    PET_LEFT_MIN = 98,
    PET_LEFT_MAX = 202,
    ACTION_COUNT = 4,
    ACHIEVEMENT_FIRST_CARE = UINT32_C(1) << 0U,
    ACHIEVEMENT_CLEAN = UINT32_C(1) << 1U,
    ACHIEVEMENT_PLAY = UINT32_C(1) << 2U,
    ACHIEVEMENT_GROW = UINT32_C(1) << 3U,
};

typedef enum {
    ACTION_FEED = 0,
    ACTION_PLAY,
    ACTION_CLEAN,
    ACTION_REST,
} buddy_action_t;

typedef struct {
    uint8_t hunger;
    uint8_t joy;
    uint8_t hygiene;
    uint8_t energy;
    uint8_t stage;
    uint8_t selected_action;
    uint8_t clean_actions;
    uint8_t play_catches;
    uint16_t coins;
    uint16_t care_actions;
    uint32_t held_buttons;
    uint32_t decay_accumulator_ms;
    uint32_t animation_ms;
    uint32_t mini_elapsed_ms;
    int16_t catcher_x;
    int16_t star_x;
    int16_t star_y;
    bool mini_game;
    uint32_t achievement_mask;
    p4_game_audio_effect_player_t audio;
} byte_buddy_state_t;

static const char *const s_actions[ACTION_COUNT] = {
    "FEED", "PLAY", "CLEAN", "REST",
};

static uint8_t increase(uint8_t value, uint8_t amount)
{
    return value > STAT_MAX - amount ? STAT_MAX : (uint8_t)(value + amount);
}

static uint8_t decrease(uint8_t value, uint8_t amount)
{
    return value < amount ? 0U : (uint8_t)(value - amount);
}

static void play_tone(p4_game_context_t *context,
                      uint16_t frequency_hz, uint16_t duration_ms)
{
    (void)p4_game_play_tone(context, frequency_hz, duration_ms,
                            3U, P4_WAVE_TRIANGLE);
}

static void unlock(p4_game_context_t *context, byte_buddy_state_t *state,
                   uint32_t flag, const char *id, const char *title,
                   const char *description)
{
    if ((state->achievement_mask & flag) != 0U) {
        return;
    }
    state->achievement_mask |= flag;
    (void)p4_game_unlock_achievement(context, id, title, description);
}

static uint8_t health(const byte_buddy_state_t *state)
{
    const uint16_t total = (uint16_t)(
        (uint16_t)state->hunger + (uint16_t)state->joy +
        (uint16_t)state->hygiene + (uint16_t)state->energy);
    return (uint8_t)(total / 4U);
}

static const char *mood(const byte_buddy_state_t *state)
{
    const uint8_t value = health(state);
    if (value >= 80U) {
        return "RAD";
    }
    if (value >= 50U) {
        return "OK";
    }
    return "NEEDS CARE";
}

static void reset_star(byte_buddy_state_t *state)
{
    const uint32_t seed = (uint32_t)state->coins * 37U +
        (uint32_t)state->care_actions * 19U + state->animation_ms;
    state->star_x = (int16_t)(112 + seed % 96U);
    state->star_y = 38;
}

static void start_play(byte_buddy_state_t *state)
{
    state->mini_game = true;
    state->mini_elapsed_ms = 0U;
    state->catcher_x = 160;
    reset_star(state);
}

static void care_for_buddy(p4_game_context_t *context,
                           byte_buddy_state_t *state)
{
    switch ((buddy_action_t)state->selected_action) {
    case ACTION_FEED:
        state->hunger = increase(state->hunger, 24U);
        state->energy = increase(state->energy, 4U);
        play_tone(context, 523U, 100U);
        break;
    case ACTION_PLAY:
        if (state->energy >= 10U) {
            state->energy = decrease(state->energy, 8U);
            start_play(state);
            play_tone(context, 659U, 100U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
        } else {
            play_tone(context, 196U, 120U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_FAIL);
        }
        return;
    case ACTION_CLEAN:
        state->hygiene = increase(state->hygiene, 28U);
        state->joy = increase(state->joy, 4U);
        if (state->clean_actions < UINT8_MAX) {
            ++state->clean_actions;
        }
        play_tone(context, 784U, 80U);
        break;
    case ACTION_REST:
        state->energy = increase(state->energy, 30U);
        state->joy = increase(state->joy, 6U);
        play_tone(context, 392U, 140U);
        break;
    default:
        return;
    }
    (void)p4_game_audio_effect_play(
        context, &state->audio, P4_GAME_AUDIO_EFFECT_ACTION);
    if (state->care_actions < UINT16_MAX) {
        ++state->care_actions;
    }
    unlock(context, state, ACHIEVEMENT_FIRST_CARE, "first-care",
           "FIRST CARE", "GIVE BYTE BUDDY SOME LOVE");
    if (state->clean_actions >= 3U) {
        unlock(context, state, ACHIEVEMENT_CLEAN, "clean-sweep",
               "CLEAN SWEEP", "CLEAN BYTE BUDDY 3 TIMES");
    }
    if (state->care_actions >= 12U && state->stage < 1U) {
        state->stage = 1U;
    }
    if (state->care_actions >= 28U && state->stage < 2U) {
        state->stage = 2U;
        unlock(context, state, ACHIEVEMENT_GROW, "all-grown",
               "ALL GROWN", "RAISE A BRIGHT BUDDY");
    }
}

static void apply_decay(byte_buddy_state_t *state, uint32_t elapsed_ms)
{
    state->decay_accumulator_ms += elapsed_ms;
    while (state->decay_accumulator_ms >= DECAY_INTERVAL_MS) {
        state->decay_accumulator_ms -= DECAY_INTERVAL_MS;
        state->hunger = decrease(state->hunger, 1U);
        state->joy = decrease(state->joy, 1U);
        state->hygiene = decrease(state->hygiene, 1U);
        state->energy = decrease(state->energy, 1U);
    }
}

static void update_play(p4_game_context_t *context, byte_buddy_state_t *state,
                        const p4_game_input_t *input, uint32_t elapsed_ms)
{
    if ((input->held & P4_BUTTON_LEFT) != 0U &&
        state->catcher_x > PET_LEFT_MIN) {
        state->catcher_x = (int16_t)(state->catcher_x - 2);
    }
    if ((input->held & P4_BUTTON_RIGHT) != 0U &&
        state->catcher_x < PET_LEFT_MAX) {
        state->catcher_x = (int16_t)(state->catcher_x + 2);
    }
    state->mini_elapsed_ms += elapsed_ms;
    state->star_y = (int16_t)(state->star_y +
                               (int16_t)(elapsed_ms / 8U + 1U));
    if (state->star_y >= MINI_GAME_CATCH_Y) {
        const int16_t distance = state->star_x > state->catcher_x
            ? (int16_t)(state->star_x - state->catcher_x)
            : (int16_t)(state->catcher_x - state->star_x);
        if (distance <= 18) {
            if (state->coins < UINT16_MAX) {
                ++state->coins;
            }
            if (state->play_catches < UINT8_MAX) {
                ++state->play_catches;
            }
            state->joy = increase(state->joy, 5U);
            play_tone(context, 988U, 65U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_REWARD);
        } else {
            play_tone(context, 220U, 45U);
            (void)p4_game_audio_effect_play(
                context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
        }
        reset_star(state);
    }
    if (state->play_catches >= 3U) {
        unlock(context, state, ACHIEVEMENT_PLAY, "star-catcher",
               "STAR CATCHER", "CATCH 3 STARS WITH BUDDY");
    }
    if (state->mini_elapsed_ms >= MINI_GAME_DURATION_MS ||
        (input->pressed & P4_BUTTON_B) != 0U) {
        state->mini_game = false;
        state->joy = increase(state->joy, 8U);
        state->energy = decrease(state->energy, 3U);
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(byte_buddy_state_t)) {
        return false;
    }
    byte_buddy_state_t *const state = context->state;
    *state = (byte_buddy_state_t){
        .hunger = 72U,
        .joy = 68U,
        .hygiene = 75U,
        .energy = 70U,
        .catcher_x = 160,
    };
    play_tone(context, 523U, 90U);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || input == NULL || context->state == NULL) {
        return P4_GAME_ERROR;
    }
    byte_buddy_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    state->animation_ms += elapsed_ms;
    apply_decay(state, elapsed_ms);
    if (state->mini_game) {
        update_play(context, state, input, elapsed_ms);
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
        state->selected_action = state->selected_action == 0U
            ? ACTION_COUNT - 1U : (uint8_t)(state->selected_action - 1U);
    }
    if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
        state->selected_action = state->selected_action + 1U >= ACTION_COUNT
            ? 0U : (uint8_t)(state->selected_action + 1U);
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        care_for_buddy(context, state);
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        state->joy = increase(state->joy, 10U);
        play_tone(context, 880U, 70U);
    }
    return P4_GAME_CONTINUE;
}

static void draw_bar(p4_game_surface_t *surface, int top,
                     const char *label, uint8_t value, uint16_t color)
{
    p4_draw_text(surface, 8, top, label, UINT16_C(0xBDF7), 1U, 8U);
    p4_draw_rect(surface, 58, top, 72, 7, UINT16_C(0x7BEF));
    p4_draw_fill_rect(surface, 59, top + 1,
                      (int)((uint32_t)value * 70U / STAT_MAX), 5, color);
}

static void draw_buddy(p4_game_surface_t *surface,
                       const byte_buddy_state_t *state)
{
    const int bounce = (int)((state->animation_ms / 300U) % 2U);
    const int center_y = 101 - bounce;
    const uint16_t body = health(state) < 35U ? UINT16_C(0xF800)
                                              : UINT16_C(0xF81F);
    p4_draw_fill_circle(surface, 160, center_y, 24, body);
    p4_draw_fill_circle(surface, 151, center_y - 3, 4, UINT16_C(0xFFFF));
    p4_draw_fill_circle(surface, 169, center_y - 3, 4, UINT16_C(0xFFFF));
    p4_draw_fill_circle(surface, 151, center_y - 3, 2, UINT16_C(0x0000));
    p4_draw_fill_circle(surface, 169, center_y - 3, 2, UINT16_C(0x0000));
    p4_draw_fill_rect(surface, 153, center_y + 10, 15, 3,
                      health(state) < 35U ? UINT16_C(0x0000)
                                          : UINT16_C(0xFFFF));
    p4_draw_text(surface, 133, center_y + 34,
                 state->stage == 0U ? "SPROUT" :
                 state->stage == 1U ? "BUDDY" : "BRIGHT BUDDY",
                 UINT16_C(0x07FF), 1U, 15U);
}

static void draw_play_game(p4_game_surface_t *surface,
                           const byte_buddy_state_t *state)
{
    p4_draw_text(surface, 64, 34, "CATCH THE STARS", UINT16_C(0xFFFF),
                 1U, 20U);
    p4_draw_text(surface, 96, 46, "B TO STOP", UINT16_C(0xBDF7), 1U, 9U);
    p4_draw_fill_circle(surface, state->star_x, state->star_y, 5,
                        UINT16_C(0xFFE0));
    p4_draw_fill_rect(surface, state->catcher_x - 18, 132, 36, 5,
                      UINT16_C(0x07FF));
    p4_draw_text(surface, 116, 144, "COINS", UINT16_C(0xBDF7), 1U, 5U);
    p4_draw_text(surface, 116, 154,
                 state->play_catches >= 3U ? "STAR MASTER" : "KEEP GOING",
                 UINT16_C(0xF81F), 1U, 12U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL || !p4_surface_valid(surface)) {
        return false;
    }
    const byte_buddy_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0011));
    p4_draw_fill_rect(surface, 0, 24, P4_GAME_SURFACE_WIDTH, 1,
                      UINT16_C(0xF81F));
    p4_draw_text(surface, 59, 7, "BYTE BUDDY", UINT16_C(0xFFFF), 1U, 15U);
    p4_draw_text(surface, 220, 7, mood(state), UINT16_C(0x07FF), 1U, 11U);
    if (state->mini_game) {
        p4_game_feedback_draw_audio_effect(
            surface, &state->audio, state->star_x, state->star_y);
        draw_play_game(surface, state);
    } else {
        draw_bar(surface, 34, "FULL", state->hunger, UINT16_C(0x07E0));
        draw_bar(surface, 44, "JOY", state->joy, UINT16_C(0xFFE0));
        draw_bar(surface, 54, "CLEAN", state->hygiene, UINT16_C(0x07FF));
        draw_bar(surface, 64, "ENERGY", state->energy, UINT16_C(0xF81F));
        p4_game_feedback_draw_audio_effect(
            surface, &state->audio, 160, 101);
        draw_buddy(surface, state);
        p4_draw_text(surface, 118, 138, s_actions[state->selected_action],
                     UINT16_C(0xFFFF), 2U, 8U);
        p4_draw_text(surface, 116, 156, "L/R SELECT  A DO",
                     UINT16_C(0xBDF7), 1U, 20U);
        p4_draw_text(surface, 116, 166, "B = PET", UINT16_C(0xBDF7), 1U, 9U);
    }
    p4_draw_text(surface, 94, 184, "COINS", UINT16_C(0xFFE0), 1U, 5U);
    p4_draw_text(surface, 134, 184,
                 state->coins >= 10U ? "10+" :
                 state->coins >= 1U ? "1+" : "0",
                 UINT16_C(0xFFFF), 1U, 3U);
    p4_game_draw_standard_controls(
        surface, UINT16_C(0x7BEF), UINT16_C(0xF81F), state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_byte_buddy_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(108),
    .id = "org.p4console.byte-buddy",
    .title = "BYTE BUDDY",
    .subtitle = "VIRTUAL PET",
    .accent_rgb565 = UINT16_C(0xF81F),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(byte_buddy_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
