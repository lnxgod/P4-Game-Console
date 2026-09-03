// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

enum {
    COLOR_BACKGROUND = 0x08a4,
    COLOR_FELT = 0x0448,
    COLOR_FELT_DARK = 0x02c5,
    COLOR_PANEL = 0x18e7,
    COLOR_PANEL_EDGE = 0x4a8d,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xb596,
    COLOR_ACCENT = 0x5fea,
    COLOR_GOLD = 0xfe60,
    COLOR_RED = 0xf986,
    COLOR_BLACK = 0x0000,
    COLOR_CARD = 0xffdf,
    COLOR_CARD_BACK = 0x51bf,
    EXIT_X = 2,
    EXIT_Y = 2,
    EXIT_W = 42,
    EXIT_H = 14,
    STOCK_X = 109,
    DISCARD_X = 181,
    PILE_Y = 66,
    CARD_W = 30,
    CARD_H = 42,
    SETUP_X = 52,
    SETUP_Y = 91,
    SETUP_W = 216,
    SETUP_H = 28,
    START_X = 91,
    START_Y = 137,
    START_W = 138,
    START_H = 27,
};

static bool point_in(uint16_t px, uint16_t py,
                     int x, int y, int width, int height)
{
    return (int)px >= x && (int)px < x + width &&
        (int)py >= y && (int)py < y + height;
}

static size_t append_text(char *text, size_t capacity, size_t length,
                          const char *value)
{
    while (*value != '\0' && length + 1U < capacity) {
        text[length++] = *value++;
    }
    text[length] = '\0';
    return length;
}

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

static char rank_character(uint8_t card)
{
    const uint8_t rank = (uint8_t)(card % 13U);
    if (rank <= 7U) {
        return (char)('2' + rank);
    }
    static const char high[] = {'T', 'J', 'Q', 'K', 'A'};
    return high[rank - 8U];
}

static char suit_character(uint8_t card)
{
    static const char suits[] = {'C', 'D', 'H', 'S'};
    return suits[card / 13U];
}

static uint16_t suit_color(uint8_t card)
{
    const uint8_t suit = (uint8_t)(card / 13U);
    return suit == 1U || suit == 2U ? COLOR_RED : COLOR_BLACK;
}

static void draw_card(p4_game_surface_t *surface, int x, int y,
                      uint8_t card, bool visible, bool selected)
{
    const uint16_t edge = selected ? COLOR_GOLD : COLOR_TEXT;
    if (!visible || card == P4_RUMMY_NO_CARD) {
        p4_draw_fill_rect(surface, x, y, CARD_W, CARD_H, COLOR_CARD_BACK);
        p4_draw_rect(surface, x, y, CARD_W, CARD_H, edge);
        p4_draw_rect(surface, x + 4, y + 4, CARD_W - 8,
                     CARD_H - 8, COLOR_ACCENT);
        p4_draw_text(surface, x + 10, y + 16, "R", COLOR_TEXT, 1U, 1U);
        return;
    }
    p4_draw_fill_rect(surface, x, y, CARD_W, CARD_H, COLOR_CARD);
    p4_draw_rect(surface, x, y, CARD_W, CARD_H, edge);
    if (selected) {
        p4_draw_rect(surface, x + 1, y + 1, CARD_W - 2,
                     CARD_H - 2, COLOR_GOLD);
    }
    char rank[2] = {rank_character(card), '\0'};
    char suit[2] = {suit_character(card), '\0'};
    const uint16_t color = suit_color(card);
    p4_draw_text(surface, x + 3, y + 3, rank, color, 1U, 1U);
    p4_draw_text(surface, x + 17, y + 25, suit, color, 1U, 1U);
}

static void draw_exit(p4_game_surface_t *surface)
{
    p4_draw_fill_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_PANEL);
    p4_draw_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_MUTED);
    p4_draw_text(surface, 7, 5, "EXIT", COLOR_TEXT, 1U, 4U);
}

static void player_label(char text[12], const p4_rummy_state_t *state,
                         uint8_t player)
{
    size_t length = append_text(text, 12U, 0U,
                                p4_rummy_player_is_cpu(state, player)
                                    ? "CPU" : "P");
    length = append_unsigned(text, 12U, length, (unsigned)player + 1U);
    length = append_text(text, 12U, length, "  ");
    (void)append_unsigned(text, 12U, length, state->hand_counts[player]);
}

static void draw_player_strip(p4_game_surface_t *surface,
                              const p4_rummy_state_t *state)
{
    const int width = 74;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const int x = 8 + (int)player * 77;
        const bool current = player == state->current_player &&
            state->phase != P4_RUMMY_PHASE_SETUP &&
            state->phase != P4_RUMMY_PHASE_ROUND_OVER;
        p4_draw_fill_rect(surface, x, 22, width, 16, COLOR_PANEL);
        p4_draw_rect(surface, x, 22, width, 16,
                     current ? COLOR_GOLD : COLOR_PANEL_EDGE);
        char label[12] = {0};
        player_label(label, state, player);
        p4_draw_text(surface, x + 6, 27, label,
                     current ? COLOR_GOLD : COLOR_TEXT, 1U, 7U);
    }
}

static uint8_t visible_hand_player(const p4_rummy_state_t *state)
{
    if (state->phase == P4_RUMMY_PHASE_ROUND_OVER &&
        state->winner < state->player_count) {
        return state->winner;
    }
    if (state->network_mode &&
        state->local_player_slot < state->player_count) {
        return state->local_player_slot;
    }
    if (state->human_player_count == 1U) {
        return 0U;
    }
    return state->current_player;
}

static int hand_start_x(uint8_t count)
{
    if (count == 0U) {
        return 160;
    }
    const int width = CARD_W + ((int)count - 1) * 33;
    return (320 - width) / 2;
}

static void draw_hand(p4_game_surface_t *surface,
                      const p4_rummy_state_t *state)
{
    const uint8_t player = visible_hand_player(state);
    if (player >= state->player_count || state->hand_counts[player] == 0U) {
        return;
    }
    const uint8_t count = state->hand_counts[player];
    const int start_x = hand_start_x(count);
    const bool hide = !state->network_mode &&
        state->human_player_count > 1U && state->pass_required;
    for (uint8_t index = 0U; index < count; ++index) {
        const bool selected = player == state->current_player &&
            p4_rummy_local_turn(state) &&
            state->phase == P4_RUMMY_PHASE_DISCARD &&
            index == state->selected_card;
        draw_card(surface, start_x + (int)index * 33,
                  selected ? 142 : 147, state->hands[player][index],
                  !hide, selected);
    }
}

static void draw_setup(p4_game_surface_t *surface,
                       const p4_rummy_state_t *state)
{
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_draw_fill_rect(surface, 24, 21, 272, 158, COLOR_FELT_DARK);
    p4_draw_rect(surface, 24, 21, 272, 158, COLOR_ACCENT);
    p4_draw_text(surface, 88, 35, "P4 RUMMY", COLOR_GOLD, 2U, 8U);
    p4_draw_text(surface, 68, 62, "MELD YOUR WHOLE HAND TO WIN",
                 COLOR_MUTED, 1U, 27U);

    p4_draw_fill_rect(surface, SETUP_X, SETUP_Y,
                      SETUP_W, SETUP_H, COLOR_PANEL);
    p4_draw_rect(surface, SETUP_X, SETUP_Y,
                 SETUP_W, SETUP_H, COLOR_GOLD);
    p4_draw_text(surface, 59, 101, "<", COLOR_ACCENT, 1U, 1U);
    char setting[28] = {0};
    if (state->network_mode) {
        size_t length = append_text(setting, sizeof(setting), 0U,
                                    "CPU SEATS  ");
        (void)append_unsigned(setting, sizeof(setting), length,
            (unsigned)(state->player_count - state->network_player_count));
    } else {
        size_t length = append_text(setting, sizeof(setting), 0U,
                                    "HUMAN PLAYERS  ");
        (void)append_unsigned(setting, sizeof(setting), length,
                              state->human_player_count);
    }
    p4_draw_text(surface, state->network_mode ? 103 : 88, 101,
                 setting, COLOR_TEXT, 1U,
                 state->network_mode ? 12U : 16U);
    p4_draw_text(surface, 253, 101, ">", COLOR_ACCENT, 1U, 1U);

    const bool can_start = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    p4_draw_fill_rect(surface, START_X, START_Y,
                      START_W, START_H, can_start ? COLOR_ACCENT : COLOR_PANEL);
    p4_draw_rect(surface, START_X, START_Y,
                 START_W, START_H, COLOR_TEXT);
    p4_draw_text(surface, can_start ? 119 : 103, 147,
                 can_start ? "A  DEAL" : "WAIT FOR HOST",
                 can_start ? COLOR_BLACK : COLOR_MUTED, 1U,
                 can_start ? 7U : 13U);
    if (state->human_player_count == 1U && !state->network_mode) {
        p4_draw_text(surface, 100, 171, "SOLO = YOU VS CPU",
                     COLOR_MUTED, 1U, 17U);
    } else if (state->network_mode) {
        char linked[24] = {0};
        size_t length = append_text(linked, sizeof(linked), 0U,
                                    "CONNECTED  ");
        length = append_unsigned(linked, sizeof(linked), length,
                                 state->network_player_count);
        (void)append_text(linked, sizeof(linked), length, " HUMANS");
        p4_draw_text(surface, 98, 171, linked, COLOR_MUTED, 1U, 19U);
    }
    draw_exit(surface);
}

static void draw_table(p4_game_surface_t *surface,
                       const p4_rummy_state_t *state)
{
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_draw_fill_rect(surface, 0, 18, 320, 182, COLOR_FELT);
    p4_draw_text(surface, 133, 6, "RUMMY", COLOR_GOLD, 1U, 5U);
    draw_exit(surface);
    draw_player_strip(surface, state);

    const bool stock_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_STOCK &&
        p4_rummy_local_turn(state);
    const bool discard_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_DISCARD &&
        p4_rummy_local_turn(state);
    draw_card(surface, STOCK_X, PILE_Y, P4_RUMMY_NO_CARD,
              false, stock_selected);
    const uint8_t discard_top = state->discard_count == 0U
        ? P4_RUMMY_NO_CARD : state->discard[state->discard_count - 1U];
    draw_card(surface, DISCARD_X, PILE_Y, discard_top,
              discard_top != P4_RUMMY_NO_CARD, discard_selected);
    p4_draw_text(surface, 107, 112, "STOCK", COLOR_MUTED, 1U, 5U);
    p4_draw_text(surface, 175, 112, "DISCARD", COLOR_MUTED, 1U, 7U);

    char action[30] = {0};
    uint16_t action_color = COLOR_TEXT;
    if (state->network_request_pending) {
        (void)append_text(action, sizeof(action), 0U, "SENDING MOVE...");
        action_color = COLOR_GOLD;
    } else if (p4_rummy_player_is_cpu(state, state->current_player)) {
        (void)append_text(action, sizeof(action), 0U, "CPU IS THINKING...");
        action_color = COLOR_MUTED;
    } else if (state->phase == P4_RUMMY_PHASE_DRAW) {
        (void)append_text(action, sizeof(action), 0U,
                          "LEFT/RIGHT CHOOSE  A DRAW");
    } else if (state->selected_card == state->drawn_card_index) {
        (void)append_text(action, sizeof(action), 0U,
                          "NEW PICK LOCKED - CHOOSE CARD");
        action_color = COLOR_GOLD;
    } else {
        (void)append_text(action, sizeof(action), 0U,
                          "LEFT/RIGHT CARD  A DISCARD");
    }
    p4_draw_text(surface, 66, 126, action, action_color, 1U, 28U);
    draw_hand(surface, state);
}

static void draw_pass_overlay(p4_game_surface_t *surface,
                              const p4_rummy_state_t *state)
{
    p4_draw_fill_rect(surface, 39, 53, 242, 94, COLOR_BACKGROUND);
    p4_draw_rect(surface, 39, 53, 242, 94, COLOR_GOLD);
    char player[22] = {0};
    size_t length = append_text(player, sizeof(player), 0U, "PASS TO PLAYER ");
    (void)append_unsigned(player, sizeof(player), length,
                          (unsigned)state->current_player + 1U);
    p4_draw_text(surface, 86, 75, player, COLOR_TEXT, 1U, 16U);
    p4_draw_text(surface, 101, 102, "A  REVEAL HAND",
                 COLOR_GOLD, 1U, 14U);
    p4_draw_text(surface, 85, 125, "KEEP CARDS PRIVATE",
                 COLOR_MUTED, 1U, 18U);
}

static void draw_round_over(p4_game_surface_t *surface,
                            const p4_rummy_state_t *state)
{
    draw_table(surface, state);
    p4_draw_fill_rect(surface, 52, 48, 216, 88, COLOR_BACKGROUND);
    p4_draw_rect(surface, 52, 48, 216, 88, COLOR_GOLD);
    char winner[24] = {0};
    size_t length = append_text(winner, sizeof(winner), 0U,
        p4_rummy_player_is_cpu(state, state->winner) ? "CPU" : "PLAYER ");
    length = append_unsigned(winner, sizeof(winner), length,
                             (unsigned)state->winner + 1U);
    (void)append_text(winner, sizeof(winner), length, " WINS!");
    p4_draw_text(surface, 103, 68, winner, COLOR_GOLD, 1U, 16U);
    p4_draw_text(surface, 77, 92, "HAND IS ALL SETS + RUNS",
                 COLOR_TEXT, 1U, 23U);
    const bool can_restart = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    p4_draw_text(surface, can_restart ? 106 : 104, 116,
                 can_restart ? "A  NEW ROUND" : "WAIT FOR HOST",
                 COLOR_MUTED, 1U, can_restart ? 12U : 13U);
}

static bool adjust_setup(p4_rummy_state_t *state, bool increase)
{
    const bool changed = state->network_mode
        ? p4_rummy_adjust_cpu_seats(state, increase)
        : p4_rummy_adjust_human_players(state, increase);
    if (changed) {
        p4_rummy_mark_snapshot_dirty(state);
    }
    return changed;
}

static void move_selection(p4_rummy_state_t *state, bool right)
{
    const uint8_t count = state->hand_counts[state->current_player];
    if (count == 0U) {
        return;
    }
    for (uint8_t step = 0U; step < count; ++step) {
        state->selected_card = right
            ? (uint8_t)((state->selected_card + 1U) % count)
            : (uint8_t)((state->selected_card + count - 1U) % count);
        if (state->selected_card != state->drawn_card_index) {
            return;
        }
    }
}

static void handle_touch(p4_game_context_t *context,
                         p4_rummy_state_t *state,
                         uint16_t x, uint16_t y)
{
    if (state->phase == P4_RUMMY_PHASE_SETUP) {
        if (point_in(x, y, SETUP_X, SETUP_Y, SETUP_W / 2, SETUP_H)) {
            (void)adjust_setup(state, false);
        } else if (point_in(x, y, SETUP_X + SETUP_W / 2,
                            SETUP_Y, SETUP_W / 2, SETUP_H)) {
            (void)adjust_setup(state, true);
        } else if (point_in(x, y, START_X, START_Y, START_W, START_H) &&
                   (!state->network_mode ||
                    state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST)) {
            if (p4_rummy_begin_round(state)) {
                p4_rummy_mark_snapshot_dirty(state);
            }
        }
        return;
    }
    if (state->pass_required) {
        if (point_in(x, y, 39, 53, 242, 94)) {
            state->pass_required = false;
        }
        return;
    }
    if (state->phase == P4_RUMMY_PHASE_ROUND_OVER) {
        if (point_in(x, y, 52, 48, 216, 88)) {
            (void)p4_rummy_request_new_round(context, state);
        }
        return;
    }
    if (!p4_rummy_local_turn(state) || state->network_request_pending) {
        return;
    }
    if (state->phase == P4_RUMMY_PHASE_DRAW) {
        if (point_in(x, y, STOCK_X - 8, PILE_Y - 5,
                     CARD_W + 16, CARD_H + 24)) {
            state->draw_source = P4_RUMMY_DRAW_STOCK;
            (void)p4_rummy_perform_draw(
                context, state, P4_RUMMY_DRAW_STOCK);
        } else if (point_in(x, y, DISCARD_X - 8, PILE_Y - 5,
                            CARD_W + 16, CARD_H + 24)) {
            state->draw_source = P4_RUMMY_DRAW_DISCARD;
            (void)p4_rummy_perform_draw(
                context, state, P4_RUMMY_DRAW_DISCARD);
        }
        return;
    }
    const uint8_t count = state->hand_counts[state->current_player];
    const int start_x = hand_start_x(count);
    for (uint8_t index = 0U; index < count; ++index) {
        if (point_in(x, y, start_x + (int)index * 33, 137,
                     CARD_W, 58)) {
            if (index == state->drawn_card_index) {
                return;
            }
            if (index == state->selected_card) {
                (void)p4_rummy_perform_discard(context, state, index);
            } else {
                state->selected_card = index;
            }
            return;
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(p4_rummy_state_t)) {
        return false;
    }
    p4_rummy_state_t *const state = context->state;
    p4_rummy_reset_lobby(state, 1U, UINT32_C(0x5034524d));
    (void)p4_rummy_network_begin(context, state);
    (void)p4_game_play_tone(
        context, 523U, 70U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || context->state == NULL || input == NULL) {
        return P4_GAME_ERROR;
    }
    p4_rummy_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    const bool touch_down = input->touch_valid && input->touch_count != 0U;
    const bool touch_pressed = touch_down && !state->touch_was_down;
    if (touch_pressed && point_in(input->touches[0].x, input->touches[0].y,
                                  EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        state->touch_was_down = touch_down;
        return P4_GAME_EXIT_TO_LAUNCHER;
    }

    p4_rummy_network_poll(context, state, elapsed_ms);
    if (touch_pressed) {
        handle_touch(context, state,
                     input->touches[0].x, input->touches[0].y);
    }

    const uint32_t pressed = input->pressed;
    if (state->phase == P4_RUMMY_PHASE_SETUP) {
        if ((pressed & (P4_BUTTON_LEFT | P4_BUTTON_DOWN)) != 0U) {
            (void)adjust_setup(state, false);
        }
        if ((pressed & (P4_BUTTON_RIGHT | P4_BUTTON_UP)) != 0U) {
            (void)adjust_setup(state, true);
        }
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U &&
            (!state->network_mode ||
             state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST) &&
            p4_rummy_begin_round(state)) {
            p4_rummy_mark_snapshot_dirty(state);
        }
    } else if (state->pass_required) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            state->pass_required = false;
        }
    } else if (state->phase == P4_RUMMY_PHASE_ROUND_OVER) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            (void)p4_rummy_request_new_round(context, state);
        }
    } else if (p4_rummy_local_turn(state) &&
               !state->network_request_pending) {
        if (state->phase == P4_RUMMY_PHASE_DRAW) {
            if ((pressed & (P4_BUTTON_LEFT | P4_BUTTON_RIGHT |
                            P4_BUTTON_UP | P4_BUTTON_DOWN |
                            P4_BUTTON_B)) != 0U) {
                state->draw_source = state->draw_source == P4_RUMMY_DRAW_STOCK
                    ? P4_RUMMY_DRAW_DISCARD : P4_RUMMY_DRAW_STOCK;
            }
            if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
                (void)p4_rummy_perform_draw(
                    context, state, state->draw_source);
            }
        } else if (state->phase == P4_RUMMY_PHASE_DISCARD) {
            if ((pressed & P4_BUTTON_LEFT) != 0U) {
                move_selection(state, false);
            }
            if ((pressed & P4_BUTTON_RIGHT) != 0U) {
                move_selection(state, true);
            }
            if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
                (void)p4_rummy_perform_discard(
                    context, state, state->selected_card);
            }
        }
    }

    const bool cpu_authority = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    if (cpu_authority &&
        p4_rummy_player_is_cpu(state, state->current_player) &&
        (state->phase == P4_RUMMY_PHASE_DRAW ||
         state->phase == P4_RUMMY_PHASE_DISCARD)) {
        if (UINT32_MAX - state->cpu_think_ms < elapsed_ms) {
            state->cpu_think_ms = P4_RUMMY_CPU_THINK_MS;
        } else {
            state->cpu_think_ms += elapsed_ms;
        }
        if (state->cpu_think_ms >= P4_RUMMY_CPU_THINK_MS) {
            const p4_rummy_phase_t old_phase = state->phase;
            state->cpu_think_ms = 0U;
            if (p4_rummy_cpu_step(state)) {
                if (old_phase == P4_RUMMY_PHASE_DRAW) {
                    (void)p4_game_play_tone(
                        context, 392U, 30U, 2U, P4_WAVE_TRIANGLE);
                } else {
                    (void)p4_game_play_tone(
                        context,
                        state->phase == P4_RUMMY_PHASE_ROUND_OVER
                            ? 880U : 523U,
                        state->phase == P4_RUMMY_PHASE_ROUND_OVER
                            ? 140U : 40U,
                        3U, P4_WAVE_SQUARE);
                }
                p4_rummy_mark_snapshot_dirty(state);
            }
        }
    }
    state->touch_was_down = touch_down;
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL ||
        !p4_surface_valid(surface)) {
        return false;
    }
    const p4_rummy_state_t *const state = context->state;
    if (state->phase == P4_RUMMY_PHASE_SETUP) {
        draw_setup(surface, state);
    } else if (state->phase == P4_RUMMY_PHASE_ROUND_OVER) {
        draw_round_over(surface, state);
    } else {
        draw_table(surface, state);
        if (state->pass_required) {
            draw_pass_overlay(surface, state);
        }
    }
    if (state->peer_lost_fallback) {
        p4_draw_fill_rect(surface, 57, 181, 206, 15, COLOR_BACKGROUND);
        p4_draw_rect(surface, 57, 181, 206, 15, COLOR_RED);
        p4_draw_text(surface, 69, 185, "LINK LOST - LOCAL TABLE",
                     COLOR_RED, 1U, 23U);
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_p4_rummy_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(118),
    .id = "org.p4console.p4-rummy",
    .title = "P4 RUMMY",
    .subtitle = "1-4 PLAYER CARD GAME",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(p4_rummy_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
