// SPDX-License-Identifier: MIT

#include "p4_yahtzee_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

#include "generated/p4_yahtzee_dice_atlas.inc"

enum {
    COLOR_BG = 0x0844,
    COLOR_PANEL = 0x10c7,
    COLOR_PANEL_ALT = 0x1949,
    COLOR_USED = 0x2109,
    COLOR_LINE = 0x3a4d,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xad55,
    COLOR_ACCENT = 0x5fea,
    COLOR_GOLD = 0xfe60,
    COLOR_DANGER = 0xf986,
    DICE_Y = 30,
    DICE_GAP = 15,
    DICE_X = 15,
    SCORE_Y = 88,
    SCORE_ROW_H = 12,
    SCORE_COL_W = 158,
    EXIT_X = 2,
    EXIT_Y = 2,
    EXIT_W = 48,
    EXIT_H = 25,
};

static const char *const s_category_names[P4_YAHTZEE_CATEGORIES] = {
    "ONES", "TWOS", "THREES", "FOURS", "FIVES", "SIXES",
    "3 KIND", "4 KIND", "FULL HOUSE", "SM STRAIGHT", "LG STRAIGHT",
    "YAHTZEE", "CHANCE",
};

static size_t append_unsigned(char *text, size_t capacity, size_t length,
                              unsigned value)
{
    char reversed[10];
    size_t digits = 0U;
    do {
        reversed[digits++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && digits < sizeof(reversed));
    while (digits != 0U && length + 1U < capacity) {
        text[length++] = reversed[--digits];
    }
    text[length] = '\0';
    return length;
}

static void score_text(char text[5], int score)
{
    text[0] = '\0';
    if (score < 0) {
        text[0] = '-';
        text[1] = '\0';
        return;
    }
    (void)append_unsigned(text, 5U, 0U, (unsigned)score);
}

static bool point_in(uint16_t px, uint16_t py,
                     int x, int y, int width, int height)
{
    return (int)px >= x && (int)px < x + width &&
        (int)py >= y && (int)py < y + height;
}

static void handle_touch(p4_game_context_t *context,
                         p4_yahtzee_state_t *state,
                         uint16_t x, uint16_t y)
{
    if (state->phase == P4_YAHTZEE_MENU) {
        if (point_in(x, y, 61, 105, 198, 25)) {
            state->mode = P4_YAHTZEE_LOCAL;
            p4_yahtzee_reset_match(state, state->rng);
        } else if (point_in(x, y, 61, 136, 198, 25)) {
            state->menu_selection = 1U;
            if (p4_yahtzee_network_available(context)) {
                state->mode = P4_YAHTZEE_NETWORK;
                state->phase = P4_YAHTZEE_NETWORK_WAIT;
            }
        }
        return;
    }
    if (state->phase == P4_YAHTZEE_PASS) {
        state->phase = P4_YAHTZEE_TURN;
        return;
    }
    if (state->phase == P4_YAHTZEE_GAME_OVER) {
        state->phase = P4_YAHTZEE_MENU;
        state->menu_selection = 0U;
        return;
    }
    if (state->phase != P4_YAHTZEE_TURN ||
        !p4_yahtzee_local_turn(state)) {
        return;
    }
    for (uint8_t die = 0U; die < P4_YAHTZEE_DICE; ++die) {
        const int die_x = DICE_X + (int)die *
            (P4_YAHTZEE_DIE_SPRITE_SIZE + DICE_GAP);
        if (point_in(x, y, die_x, DICE_Y,
                     P4_YAHTZEE_DIE_SPRITE_SIZE,
                     P4_YAHTZEE_DIE_SPRITE_SIZE)) {
            state->focus = P4_YAHTZEE_FOCUS_DICE;
            state->selected_die = die;
            (void)p4_yahtzee_perform_action(
                context, state, P4_YAHTZEE_NET_HOLD, die);
            return;
        }
    }
    if (point_in(x, y, 230, 185, 86, 13)) {
        (void)p4_yahtzee_perform_action(
            context, state, P4_YAHTZEE_NET_ROLL, 0U);
        return;
    }
    if (y >= SCORE_Y && y < SCORE_Y + SCORE_ROW_H * 7) {
        const uint8_t row = (uint8_t)((y - SCORE_Y) / SCORE_ROW_H);
        uint8_t category = row;
        if (x >= SCORE_COL_W) {
            category = (uint8_t)(P4_YAHTZEE_THREE_KIND + row);
        }
        if (category < P4_YAHTZEE_CATEGORIES) {
            state->focus = P4_YAHTZEE_FOCUS_SCORE;
            state->selected_category = category;
            (void)p4_yahtzee_perform_action(
                context, state, P4_YAHTZEE_NET_SCORE, category);
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(p4_yahtzee_state_t)) {
        return false;
    }
    p4_yahtzee_state_t *const state = context->state;
    *state = (p4_yahtzee_state_t){
        .phase = P4_YAHTZEE_MENU,
        .mode = P4_YAHTZEE_LOCAL,
        .rng = UINT32_C(0x5034595a),
    };
    p4_game_multiplayer_status_t multiplayer;
    if (p4_game_multiplayer_read_status(context, &multiplayer) &&
        multiplayer.state == P4_GAME_MULTIPLAYER_CONNECTED) {
        state->mode = P4_YAHTZEE_NETWORK;
        state->phase = P4_YAHTZEE_NETWORK_WAIT;
        state->local_player_slot = multiplayer.local_player_slot;
        state->network_role = multiplayer.role;
        state->network_seed = multiplayer.session_seed;
    }
    (void)p4_game_play_tone(context, 523U, 70U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    p4_yahtzee_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        if (state->phase == P4_YAHTZEE_MENU) {
            return P4_GAME_EXIT_TO_LAUNCHER;
        }
        state->phase = P4_YAHTZEE_MENU;
        state->mode = P4_YAHTZEE_LOCAL;
        state->network_started = false;
        state->network_error = false;
        return P4_GAME_CONTINUE;
    }
    if (input->touch_valid && input->touch_count != 0U &&
        !state->touch_was_down &&
        point_in(input->touches[0].x, input->touches[0].y,
                 EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->rng ^= (uint32_t)context->elapsed_ms ^
        context->frame_index * UINT32_C(33);
    p4_yahtzee_update_animation(state, elapsed_ms);
    if (state->mode == P4_YAHTZEE_NETWORK &&
        state->phase != P4_YAHTZEE_MENU) {
        p4_yahtzee_poll_network(context, state);
    }
    if (input->touch_valid && input->touch_count != 0U &&
        !state->touch_was_down) {
        handle_touch(context, state, input->touches[0].x,
                     input->touches[0].y);
    }
    state->touch_was_down = input->touch_valid && input->touch_count != 0U;

    if (state->phase == P4_YAHTZEE_MENU) {
        if ((input->pressed & (P4_BUTTON_UP | P4_BUTTON_DOWN)) != 0U) {
            state->menu_selection ^= 1U;
        }
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            if (state->menu_selection == 0U) {
                state->mode = P4_YAHTZEE_LOCAL;
                p4_yahtzee_reset_match(state, state->rng);
            } else if (p4_yahtzee_network_available(context)) {
                state->mode = P4_YAHTZEE_NETWORK;
                state->phase = P4_YAHTZEE_NETWORK_WAIT;
            }
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase == P4_YAHTZEE_PASS) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->phase = P4_YAHTZEE_TURN;
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase == P4_YAHTZEE_GAME_OVER) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->phase = P4_YAHTZEE_MENU;
            state->menu_selection = 0U;
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase != P4_YAHTZEE_TURN ||
        !p4_yahtzee_local_turn(state) || state->roll_animation_ms != 0U) {
        return P4_GAME_CONTINUE;
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        state->focus = state->focus == P4_YAHTZEE_FOCUS_DICE
            ? P4_YAHTZEE_FOCUS_SCORE : P4_YAHTZEE_FOCUS_DICE;
    }
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        (void)p4_yahtzee_perform_action(
            context, state, P4_YAHTZEE_NET_ROLL, 0U);
    }
    if (state->focus == P4_YAHTZEE_FOCUS_DICE) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            state->selected_die = (uint8_t)((state->selected_die +
                P4_YAHTZEE_DICE - 1U) % P4_YAHTZEE_DICE);
        }
        if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            state->selected_die = (uint8_t)((state->selected_die + 1U) %
                                             P4_YAHTZEE_DICE);
        }
        if ((input->pressed & P4_BUTTON_DOWN) != 0U) {
            state->focus = P4_YAHTZEE_FOCUS_SCORE;
        }
        if ((input->pressed & P4_BUTTON_A) != 0U) {
            (void)p4_yahtzee_perform_action(
                context, state, P4_YAHTZEE_NET_HOLD, state->selected_die);
        }
    } else {
        if ((input->pressed & (P4_BUTTON_UP | P4_BUTTON_LEFT)) != 0U) {
            state->selected_category = (uint8_t)(
                (state->selected_category + P4_YAHTZEE_CATEGORIES - 1U) %
                P4_YAHTZEE_CATEGORIES);
        }
        if ((input->pressed & (P4_BUTTON_DOWN | P4_BUTTON_RIGHT)) != 0U) {
            state->selected_category = (uint8_t)(
                (state->selected_category + 1U) % P4_YAHTZEE_CATEGORIES);
        }
        if ((input->pressed & P4_BUTTON_A) != 0U) {
            (void)p4_yahtzee_perform_action(
                context, state, P4_YAHTZEE_NET_SCORE,
                state->selected_category);
        }
    }
    return P4_GAME_CONTINUE;
}

static void draw_die(p4_game_surface_t *surface, int x, int y, uint8_t value,
                     bool held, bool selected)
{
    if (value < 1U || value > 6U) {
        value = 1U;
    }
    p4_draw_sprite_rgb565(
        surface, x, y, s_p4_yahtzee_dice[value - 1U],
        P4_YAHTZEE_DIE_SPRITE_SIZE, P4_YAHTZEE_DIE_SPRITE_SIZE,
        P4_YAHTZEE_DIE_SPRITE_SIZE, true, P4_YAHTZEE_DIE_CHROMA);
    if (held) {
        p4_draw_rect(surface, x, y, P4_YAHTZEE_DIE_SPRITE_SIZE,
                     P4_YAHTZEE_DIE_SPRITE_SIZE, COLOR_GOLD);
        p4_draw_rect(surface, x + 2, y + 2,
                     P4_YAHTZEE_DIE_SPRITE_SIZE - 4,
                     P4_YAHTZEE_DIE_SPRITE_SIZE - 4, COLOR_GOLD);
    } else if (selected) {
        p4_draw_rect(surface, x, y, P4_YAHTZEE_DIE_SPRITE_SIZE,
                     P4_YAHTZEE_DIE_SPRITE_SIZE, COLOR_ACCENT);
    }
}

static void draw_player_panel(p4_game_surface_t *surface,
                              const p4_yahtzee_state_t *state,
                              uint8_t player, int x)
{
    const bool active = state->current_player == player &&
        state->phase != P4_YAHTZEE_MENU;
    p4_draw_fill_rect(surface, x, 2, 156, 25,
                      active ? COLOR_PANEL_ALT : COLOR_PANEL);
    p4_draw_rect(surface, x, 2, 156, 25,
                 active ? COLOR_ACCENT : COLOR_LINE);
    char line[20] = "PLAYER ";
    size_t length = 7U;
    line[length++] = (char)('1' + player);
    line[length++] = ' ';
    line[length++] = ' ';
    line[length] = '\0';
    (void)append_unsigned(line, sizeof(line), length,
                          (unsigned)p4_yahtzee_total(state, player));
    const int text_x = x + (player == 0U ? EXIT_W + 4 : 6);
    p4_draw_text(surface, text_x, 5, line,
                 active ? COLOR_TEXT : COLOR_MUTED, 1U, 18U);
    char upper[20] = "UPPER ";
    length = append_unsigned(upper, sizeof(upper), 6U,
        (unsigned)p4_yahtzee_upper_total(state, player));
    if (p4_yahtzee_upper_total(state, player) >= 63 &&
        length + 3U < sizeof(upper)) {
        upper[length++] = '+';
        upper[length++] = '3';
        upper[length++] = '5';
        upper[length] = '\0';
    }
    p4_draw_text(surface, text_x, 16, upper, COLOR_MUTED, 1U, 18U);
}

static void draw_exit_button(p4_game_surface_t *surface)
{
    p4_draw_fill_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_DANGER);
    p4_draw_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_GOLD);
    p4_draw_text(surface, EXIT_X + 12, EXIT_Y + 9, "EXIT",
                 COLOR_TEXT, 1U, 4U);
}

static void draw_score_row(p4_game_surface_t *surface,
                           const p4_yahtzee_state_t *state,
                           uint8_t category, int x, int row)
{
    const int y = SCORE_Y + row * SCORE_ROW_H;
    const bool selected = state->focus == P4_YAHTZEE_FOCUS_SCORE &&
        state->selected_category == category &&
        state->phase == P4_YAHTZEE_TURN;
    const int stored = state->scores[state->current_player][category];
    const int preview = state->roll_count == 0U ? 0 :
        p4_yahtzee_score_dice(state->dice,
                              (p4_yahtzee_category_t)category);
    p4_draw_fill_rect(surface, x + 1, y + 1, SCORE_COL_W - 3,
                      SCORE_ROW_H - 1,
                      stored >= 0 ? COLOR_USED :
                      selected ? COLOR_PANEL_ALT : COLOR_PANEL);
    if (stored >= 0) {
        p4_draw_fill_rect(surface, x + 1, y + 1, 4,
                          SCORE_ROW_H - 1, COLOR_GOLD);
    }
    p4_draw_text(surface, x + 5, y + 3, s_category_names[category],
                 stored >= 0 ? COLOR_MUTED :
                 selected ? COLOR_ACCENT : COLOR_TEXT, 1U, 12U);
    char value[5];
    score_text(value, stored >= 0 ? stored : preview);
    p4_draw_text(surface, x + 130, y + 3, value,
                 stored >= 0 ? COLOR_MUTED :
                 state->roll_count != 0U ? COLOR_GOLD : COLOR_MUTED,
                 1U, 4U);
    if (stored >= 0) {
        p4_draw_text(surface, x + 106, y + 3, "SET", COLOR_GOLD, 1U, 3U);
    }
}

static void draw_summary_row(p4_game_surface_t *surface, const char *label,
                             int value, int x, int row, uint16_t color)
{
    const int y = SCORE_Y + row * SCORE_ROW_H;
    p4_draw_fill_rect(surface, x + 1, y + 1, SCORE_COL_W - 3,
                      SCORE_ROW_H - 1, COLOR_PANEL_ALT);
    p4_draw_text(surface, x + 5, y + 3, label, color, 1U, 12U);
    char text[5];
    score_text(text, value);
    p4_draw_text(surface, x + 130, y + 3, text, color, 1U, 4U);
}

static void draw_menu(p4_game_context_t *context,
                      p4_game_surface_t *surface,
                      const p4_yahtzee_state_t *state)
{
    p4_draw_text(surface, 80, 9, "P4 YAHTZEE", COLOR_TEXT, 2U, 10U);
    p4_draw_text(surface, 92, 31, "TWO PLAYER DICE", COLOR_MUTED, 1U, 18U);
    for (uint8_t die = 0U; die < P4_YAHTZEE_DICE; ++die) {
        draw_die(surface, 15 + (int)die * 61, 48,
                 (uint8_t)(die + 1U), false, false);
    }
    const bool network = p4_yahtzee_network_available(context);
    p4_draw_fill_rect(surface, 61, 105, 198, 25, COLOR_PANEL);
    p4_draw_rect(surface, 61, 105, 198, 25,
                 state->menu_selection == 0U ? COLOR_ACCENT : COLOR_LINE);
    p4_draw_text(surface, 105, 114, "LOCAL 2 PLAYERS", COLOR_TEXT, 1U, 16U);
    p4_draw_fill_rect(surface, 61, 136, 198, 25, COLOR_PANEL);
    p4_draw_rect(surface, 61, 136, 198, 25,
                 state->menu_selection == 1U ? COLOR_ACCENT : COLOR_LINE);
    p4_draw_text(surface, 101, 145, "NETWORK 2 PLAYERS",
                 network ? COLOR_TEXT : COLOR_MUTED, 1U, 18U);
    p4_draw_text(surface, 65, 172,
        network ? "A SELECTS  BACK EXITS" : "NETWORK: OPEN FROM P4MP LOBBY",
        network ? COLOR_MUTED : COLOR_DANGER, 1U, 31U);
}

static void draw_pass(p4_game_surface_t *surface,
                      const p4_yahtzee_state_t *state)
{
    p4_draw_fill_rect(surface, 31, 45, 258, 110, COLOR_PANEL);
    p4_draw_rect(surface, 31, 45, 258, 110, COLOR_ACCENT);
    p4_draw_text(surface, 86, 59, "PASS THE DEVICE", COLOR_GOLD, 1U, 18U);
    char line[9] = "PLAYER 1";
    line[7] = (char)('1' + state->current_player);
    p4_draw_text(surface, 97, 84, line, COLOR_TEXT, 2U, 8U);
    p4_draw_text(surface, 74, 124, "PRESS A WHEN READY", COLOR_MUTED, 1U, 20U);
}

static void draw_game_over(p4_game_surface_t *surface,
                           const p4_yahtzee_state_t *state)
{
    const int p1 = p4_yahtzee_total(state, 0U);
    const int p2 = p4_yahtzee_total(state, 1U);
    p4_draw_text(surface, 88, 22, "FINAL SCORES", COLOR_GOLD, 1U, 16U);
    char first[24] = "PLAYER 1  ";
    char second[24] = "PLAYER 2  ";
    (void)append_unsigned(first, sizeof(first), 10U, (unsigned)p1);
    (void)append_unsigned(second, sizeof(second), 10U, (unsigned)p2);
    p4_draw_text(surface, 72, 56, first, COLOR_TEXT, 2U, 20U);
    p4_draw_text(surface, 72, 83, second, COLOR_TEXT, 2U, 20U);
    const char *winner = p1 == p2 ? "DRAW GAME" :
        p1 > p2 ? "PLAYER 1 WINS" : "PLAYER 2 WINS";
    p4_draw_text(surface, 80, 125, winner, COLOR_ACCENT, 1U, 20U);
    p4_draw_text(surface, 57, 166, "A: PLAY AGAIN  BACK: MENU",
                 COLOR_MUTED, 1U, 28U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const p4_yahtzee_state_t *const state = context->state;
    p4_draw_clear(surface, COLOR_BG);
    if (state->phase == P4_YAHTZEE_MENU) {
        draw_menu(context, surface, state);
        draw_exit_button(surface);
        return true;
    }
    if (state->phase == P4_YAHTZEE_NETWORK_WAIT) {
        p4_draw_text(surface, 64, 42, "NETWORK MATCH", COLOR_TEXT, 2U, 13U);
        p4_draw_text(surface, 61, 91,
            state->network_error ? "SESSION LOST - BACK" :
            "WAITING FOR P4MP PEER...",
            state->network_error ? COLOR_DANGER : COLOR_ACCENT, 1U, 28U);
        p4_draw_text(surface, 41, 132,
            "ENCRYPTED LINK / OS-OWNED SESSION", COLOR_MUTED, 1U, 34U);
        draw_exit_button(surface);
        return true;
    }
    if (state->phase == P4_YAHTZEE_GAME_OVER) {
        draw_game_over(surface, state);
        draw_exit_button(surface);
        return true;
    }
    draw_player_panel(surface, state, 0U, 2);
    draw_player_panel(surface, state, 1U, 162);
    for (uint8_t die = 0U; die < P4_YAHTZEE_DICE; ++die) {
        const bool held = (state->held_mask & (UINT8_C(1) << die)) != 0U;
        const bool selected = state->focus == P4_YAHTZEE_FOCUS_DICE &&
            state->selected_die == die && state->phase == P4_YAHTZEE_TURN;
        draw_die(surface, DICE_X + (int)die *
                     (P4_YAHTZEE_DIE_SPRITE_SIZE + DICE_GAP), DICE_Y,
                 state->roll_animation_ms != 0U
                    ? state->animation_dice[die] : state->dice[die],
                 held, selected);
    }
    char roll_line[28] = "ROLL ";
    size_t length = append_unsigned(roll_line, sizeof(roll_line), 5U,
                                    state->roll_count);
    if (length + 12U < sizeof(roll_line)) {
        memcpy(roll_line + length, "/3  PLAYER ", 11U);
        length += 11U;
        roll_line[length++] = (char)('1' + state->current_player);
        roll_line[length] = '\0';
    }
    p4_draw_text(surface, 8, 78, roll_line, COLOR_MUTED, 1U, 27U);
    for (uint8_t row = 0U; row < 6U; ++row) {
        draw_score_row(surface, state, row, 1, row);
    }
    const int upper = p4_yahtzee_upper_total(state, state->current_player);
    draw_summary_row(surface, upper >= 63 ? "BONUS +35" : "BONUS AT 63",
                     upper >= 63 ? 35 : 0, 1, 6, COLOR_GOLD);
    draw_summary_row(surface, "UPPER TOTAL", upper, 1, 7, COLOR_TEXT);
    for (uint8_t row = 0U; row < 7U; ++row) {
        draw_score_row(surface, state,
                       (uint8_t)(P4_YAHTZEE_THREE_KIND + row),
                       SCORE_COL_W + 1, row);
    }
    draw_summary_row(surface, "GRAND TOTAL",
                     p4_yahtzee_total(state, state->current_player),
                     SCORE_COL_W + 1, 7, COLOR_TEXT);
    p4_draw_fill_rect(surface, 2, 185, 224, 13, COLOR_PANEL);
    p4_draw_text(surface, 6, 188,
        !p4_yahtzee_local_turn(state) ? "WAITING FOR OTHER PLAYER" :
        state->focus == P4_YAHTZEE_FOCUS_DICE
            ? "A HOLD  B SCORES  START ROLL" :
              "A SCORE  B DICE  START ROLL",
        !p4_yahtzee_local_turn(state) ? COLOR_GOLD : COLOR_MUTED, 1U, 28U);
    p4_draw_fill_rect(surface, 230, 185, 86, 13,
                      state->roll_count < P4_YAHTZEE_ROLLS_PER_TURN
                        ? COLOR_ACCENT : COLOR_LINE);
    p4_draw_text(surface, 250, 188,
        state->roll_count == 0U ? "ROLL" : "REROLL",
        COLOR_BG, 1U, 8U);
    if (state->phase == P4_YAHTZEE_PASS) {
        draw_pass(surface, state);
    }
    draw_exit_button(surface);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_p4_yahtzee_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(113),
    .id = "org.p4console.p4-yahtzee",
    .title = "P4 YAHTZEE",
    .subtitle = "TWO-PLAYER DICE",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(p4_yahtzee_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
