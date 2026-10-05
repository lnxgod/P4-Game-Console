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
    COLOR_OPTION_PANEL = 0x48a8,
    COLOR_USED = 0x2109,
    COLOR_LINE = 0x3a4d,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xad55,
    COLOR_ACCENT = 0x5fea,
    COLOR_OPTION_PINK = 0xfb56,
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

static void adjust_player_count(p4_yahtzee_state_t *state, bool increase)
{
    if (state->player_count < P4_YAHTZEE_MIN_PLAYERS ||
        state->player_count > P4_YAHTZEE_PLAYERS) {
        state->player_count = P4_YAHTZEE_MIN_PLAYERS;
    }
    if (increase) {
        state->player_count = state->player_count >= P4_YAHTZEE_PLAYERS
            ? P4_YAHTZEE_MIN_PLAYERS
            : (uint8_t)(state->player_count + 1U);
    } else {
        state->player_count = state->player_count <= P4_YAHTZEE_MIN_PLAYERS
            ? P4_YAHTZEE_PLAYERS
            : (uint8_t)(state->player_count - 1U);
    }
}

static uint8_t display_player_count(const p4_yahtzee_state_t *state)
{
    return state != NULL &&
        state->player_count >= P4_YAHTZEE_MIN_PLAYERS &&
        state->player_count <= P4_YAHTZEE_PLAYERS
            ? state->player_count : P4_YAHTZEE_MIN_PLAYERS;
}

static void handle_touch(p4_game_context_t *context,
                         p4_yahtzee_state_t *state,
                         uint16_t x, uint16_t y)
{
    if (state->phase == P4_YAHTZEE_MENU) {
        if (point_in(x, y, 61, 105, 198, 25)) {
            if (x < 91U) {
                adjust_player_count(state, false);
            } else if (x >= 229U) {
                adjust_player_count(state, true);
            } else {
                state->mode = P4_YAHTZEE_LOCAL;
                p4_yahtzee_reset_match(state, state->rng);
            }
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
        (void)p4_yahtzee_perform_action(
            context, state, P4_YAHTZEE_NET_RESTART, 0U);
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
        .player_count = P4_YAHTZEE_MIN_PLAYERS,
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
        if (multiplayer.player_count >= P4_YAHTZEE_MIN_PLAYERS &&
            multiplayer.player_count <= P4_YAHTZEE_PLAYERS &&
            multiplayer.local_player_slot < multiplayer.player_count) {
            state->player_count = multiplayer.player_count;
        } else {
            state->network_error = true;
        }
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
        p4_yahtzee_poll_dice(context, state);
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
    p4_yahtzee_poll_dice(context, state);
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
        if (state->menu_selection == 0U &&
            (input->pressed & P4_BUTTON_LEFT) != 0U) {
            adjust_player_count(state, false);
        }
        if (state->menu_selection == 0U &&
            (input->pressed & P4_BUTTON_RIGHT) != 0U) {
            adjust_player_count(state, true);
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
            (void)p4_yahtzee_perform_action(
                context, state, P4_YAHTZEE_NET_RESTART, 0U);
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
                              uint8_t player, int x, int width)
{
    const bool active = state->current_player == player &&
        state->phase != P4_YAHTZEE_MENU;
    p4_draw_fill_rect(surface, x, 2, width, 25,
                      active ? COLOR_PANEL_ALT : COLOR_PANEL);
    p4_draw_rect(surface, x, 2, width, 25,
                 active ? COLOR_ACCENT : COLOR_LINE);
    char line[12] = "P1 ";
    line[1] = (char)('1' + player);
    (void)append_unsigned(line, sizeof(line), 3U,
                          (unsigned)p4_yahtzee_total(state, player));
    p4_draw_text(surface, x + 4, 5, line,
                 active ? COLOR_TEXT : COLOR_MUTED, 1U, 9U);
    char upper[12] = "U";
    const int upper_total = p4_yahtzee_upper_total(state, player);
    size_t length = append_unsigned(upper, sizeof(upper), 1U,
                                    (unsigned)upper_total);
    if (upper_total >= 63 &&
        length + 3U < sizeof(upper)) {
        upper[length++] = '+';
        upper[length++] = '3';
        upper[length++] = '5';
        upper[length] = '\0';
    }
    p4_draw_text(surface, x + 4, 16, upper, COLOR_MUTED, 1U, 9U);
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
    const bool selectable = stored < 0 &&
        state->phase == P4_YAHTZEE_TURN &&
        p4_yahtzee_local_turn(state) && state->roll_count != 0U &&
        state->roll_animation_ms == 0U;
    const int preview = state->roll_count == 0U ? 0 :
        p4_yahtzee_score_dice(state->dice,
                              (p4_yahtzee_category_t)category);
    const bool scoring_option = selectable && preview > 0;
    p4_draw_fill_rect(surface, x + 1, y + 1, SCORE_COL_W - 3,
                      SCORE_ROW_H - 1,
                      stored >= 0 ? COLOR_USED :
                      selected && selectable ? COLOR_PANEL_ALT :
                      scoring_option ? COLOR_OPTION_PANEL : COLOR_PANEL);
    if (stored >= 0) {
        p4_draw_fill_rect(surface, x + 1, y + 1, 4,
                          SCORE_ROW_H - 1, COLOR_GOLD);
    } else if (scoring_option) {
        p4_draw_fill_rect(surface, x + 1, y + 1, 4,
                          SCORE_ROW_H - 1, COLOR_OPTION_PINK);
    }
    if (selected) {
        p4_draw_rect(surface, x + 1, y + 1, SCORE_COL_W - 3,
                     SCORE_ROW_H - 1,
                     selectable ? COLOR_ACCENT : COLOR_LINE);
    }
    p4_draw_text(surface, x + 5, y + 3, s_category_names[category],
                 stored >= 0 ? COLOR_MUTED :
                 selected && selectable ? COLOR_ACCENT :
                 scoring_option ? COLOR_OPTION_PINK : COLOR_TEXT, 1U, 12U);
    char value[5];
    score_text(value, stored >= 0 ? stored : preview);
    p4_draw_text(surface, x + 130, y + 3, value,
                 stored >= 0 ? COLOR_MUTED :
                 state->roll_count != 0U ? COLOR_GOLD : COLOR_MUTED,
                 1U, 4U);
    if (stored >= 0) {
        p4_draw_text(surface, x + 106, y + 3, "SET", COLOR_GOLD, 1U, 3U);
    } else if (selectable) {
        p4_draw_text(surface, x + 102, y + 3, "PICK",
                     selected ? COLOR_ACCENT :
                     scoring_option ? COLOR_OPTION_PINK : COLOR_MUTED,
                     1U, 4U);
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
    p4_draw_text(surface, 92, 31, "2-4 PLAYER DICE", COLOR_MUTED, 1U, 18U);
    for (uint8_t die = 0U; die < P4_YAHTZEE_DICE; ++die) {
        draw_die(surface, 15 + (int)die * 61, 48,
                 (uint8_t)(die + 1U), false, false);
    }
    const bool network = p4_yahtzee_network_available(context);
    p4_draw_fill_rect(surface, 61, 105, 198, 25, COLOR_PANEL);
    p4_draw_rect(surface, 61, 105, 198, 25,
                 state->menu_selection == 0U ? COLOR_ACCENT : COLOR_LINE);
    char local_line[17] = "LOCAL 2 PLAYERS";
    local_line[6] = (char)('0' + display_player_count(state));
    p4_draw_text(surface, 105, 114, local_line, COLOR_TEXT, 1U, 16U);
    p4_draw_text(surface, 70, 114, "<", COLOR_GOLD, 1U, 1U);
    p4_draw_text(surface, 244, 114, ">", COLOR_GOLD, 1U, 1U);
    p4_draw_fill_rect(surface, 61, 136, 198, 25, COLOR_PANEL);
    p4_draw_rect(surface, 61, 136, 198, 25,
                 state->menu_selection == 1U ? COLOR_ACCENT : COLOR_LINE);
    p4_draw_text(surface, 101, 145, "NETWORK 2-4 PLAYERS",
                 network ? COLOR_TEXT : COLOR_MUTED, 1U, 18U);
    p4_draw_text(surface, 65, 172,
        network ? (state->menu_selection == 0U
            ? "LEFT/RIGHT PLAYERS  A SELECTS"
            : "A OPENS P4MP  BACK EXITS") :
            "NETWORK: OPEN FROM P4MP LOBBY",
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
    const uint8_t player_count = display_player_count(state);
    p4_draw_text(surface, 88, 22, "FINAL SCORES", COLOR_GOLD, 1U, 16U);
    int best_score = -1;
    uint8_t best_player = 0U;
    uint8_t leader_count = 0U;
    for (uint8_t player = 0U; player < player_count; ++player) {
        const int score = p4_yahtzee_total(state, player);
        char line[12] = "P1 ";
        line[1] = (char)('1' + player);
        (void)append_unsigned(line, sizeof(line), 3U, (unsigned)score);
        p4_draw_text(surface, 33 + (int)(player % 2U) * 155,
                     55 + (int)(player / 2U) * 30,
                     line, COLOR_TEXT, 2U, 9U);
        if (score > best_score) {
            best_score = score;
            best_player = player;
            leader_count = 1U;
        } else if (score == best_score) {
            ++leader_count;
        }
    }
    char winner[24] = "PLAYER 1 WINS";
    if (leader_count == 1U) {
        winner[7] = (char)('1' + best_player);
    } else {
        memcpy(winner, "TIE AT ", 7U);
        (void)append_unsigned(winner, sizeof(winner), 7U,
                              (unsigned)best_score);
    }
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
    const uint8_t player_count = display_player_count(state);
    const int tracker_x = EXIT_X + EXIT_W + 4;
    const int tracker_width = P4_GAME_SURFACE_WIDTH - tracker_x - 2;
    const int panel_width = tracker_width / player_count;
    for (uint8_t player = 0U; player < player_count; ++player) {
        const int x = tracker_x + (int)player * panel_width;
        const int width = player + 1U == player_count
            ? P4_GAME_SURFACE_WIDTH - 2 - x : panel_width;
        draw_player_panel(surface, state, player, x, width);
    }
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
        state->roll_animation_ms != 0U ? "ROLLING DICE..." :
        state->accessory_phase == P4_DICE_SEARCHING && state->accessory_request.enabled
            ? "TAP CONNECT ON CORE2" :
        state->accessory_phase == P4_DICE_SHAKING ? "SHAKING DICE..." :
        state->accessory_phase == P4_DICE_READY ? "SHAKE YOUR DICE" :
        state->accessory_phase == P4_DICE_WAITING && state->accessory_request.enabled
            ? "TAP READY ON YOUR DICE" :
        !p4_yahtzee_local_turn(state) ? "WAITING FOR OTHER PLAYER" :
        state->shared_accessory && state->roll_count == 0U
            ? "CORE2: READY + SHAKE OR ROLL" :
        state->roll_count == 0U ? "START ROLLS THE DICE" :
        state->focus == P4_YAHTZEE_FOCUS_DICE
            ? "PINK ROWS SCORE  B SCORES" :
              "A PICKS ROW  B DICE",
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
    .subtitle = "2-4 PLAYER DICE",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION | P4_GAME_CAP_DICE_ACCESSORY,
    .state_bytes = sizeof(p4_yahtzee_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
