// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

enum {
    COLOR_BACKGROUND = 0x0865,
    COLOR_FELT = 0x0448,
    COLOR_FELT_DARK = 0x02c5,
    COLOR_FELT_LIGHT = 0x0549,
    COLOR_RAIL = 0x69e6,
    COLOR_RAIL_DARK = 0x30c3,
    COLOR_PANEL = 0x18e7,
    COLOR_PANEL_LIGHT = 0x29aa,
    COLOR_PANEL_EDGE = 0x4a8d,
    COLOR_TEXT = 0xffff,
    COLOR_CREAM = 0xffbd,
    COLOR_MUTED = 0xb596,
    COLOR_ACCENT = 0x5fea,
    COLOR_GOLD = 0xfe60,
    COLOR_GOLD_DARK = 0xa3c0,
    COLOR_RED = 0xf986,
    COLOR_CLUB = 0x04ad,
    COLOR_BLACK = 0x0000,
    COLOR_CARD = 0xffdf,
    COLOR_CARD_SHADOW = 0x0183,
    COLOR_CARD_BACK = 0x51bf,
    COLOR_CARD_BACK_DARK = 0x2812,
    SUIT_HEART_GLYPH = 0x03,
    SUIT_DIAMOND_GLYPH = 0x04,
    SUIT_CLUB_GLYPH = 0x05,
    SUIT_SPADE_GLYPH = 0x06,
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

static void rank_text(uint8_t card, char text[3])
{
    const uint8_t rank = (uint8_t)(card % 13U);
    if (rank <= 7U) {
        text[0] = (char)('2' + rank);
        text[1] = '\0';
        return;
    }
    if (rank == 8U) {
        text[0] = '1';
        text[1] = '0';
        text[2] = '\0';
        return;
    }
    static const char high[] = {'J', 'Q', 'K', 'A'};
    text[0] = high[rank - 9U];
    text[1] = '\0';
}

static uint8_t suit_glyph(uint8_t card)
{
    static const uint8_t suits[] = {
        SUIT_CLUB_GLYPH,
        SUIT_DIAMOND_GLYPH,
        SUIT_HEART_GLYPH,
        SUIT_SPADE_GLYPH,
    };
    return suits[card / 13U];
}

static uint16_t suit_color(uint8_t card)
{
    const uint8_t suit = (uint8_t)(card / 13U);
    if (suit == 0U) {
        return COLOR_CLUB;
    }
    return suit == 1U || suit == 2U ? COLOR_RED : COLOR_BLACK;
}

static void fill_rounded_rect(p4_game_surface_t *surface,
                              int x, int y, int width, int height,
                              int radius, uint16_t color)
{
    if (radius <= 0 || width <= radius * 2 || height <= radius * 2) {
        p4_draw_fill_rect(surface, x, y, width, height, color);
        return;
    }
    p4_draw_fill_rect(surface, x + radius, y,
                      width - radius * 2, height, color);
    p4_draw_fill_rect(surface, x, y + radius,
                      width, height - radius * 2, color);
    p4_draw_fill_circle(surface, x + radius, y + radius, radius, color);
    p4_draw_fill_circle(surface, x + width - radius - 1,
                        y + radius, radius, color);
    p4_draw_fill_circle(surface, x + radius,
                        y + height - radius - 1, radius, color);
    p4_draw_fill_circle(surface, x + width - radius - 1,
                        y + height - radius - 1, radius, color);
}

static void draw_suit(p4_game_surface_t *surface, int x, int y,
                      uint8_t glyph, uint16_t color,
                      uint16_t background, unsigned height)
{
    const bool large = height == P4_DRAW_CP437_FULL_HEIGHT;
    const int width = large ? 12 : 8;
    p4_draw_fill_rect(surface, x, y, width, (int)height, background);

    if (!large) {
        switch (glyph) {
        case SUIT_HEART_GLYPH:
            p4_draw_fill_circle(surface, x + 2, y + 2, 2, color);
            p4_draw_fill_circle(surface, x + 5, y + 2, 2, color);
            p4_draw_fill_rect(surface, x + 1, y + 2, 6, 2, color);
            p4_draw_fill_rect(surface, x + 2, y + 4, 4, 2, color);
            p4_draw_fill_rect(surface, x + 3, y + 6, 2, 1, color);
            p4_draw_pixel(surface, x + 3, y, background);
            p4_draw_pixel(surface, x + 4, y, background);
            p4_draw_pixel(surface, x + 3, y + 1, background);
            p4_draw_pixel(surface, x + 4, y + 1, background);
            break;
        case SUIT_DIAMOND_GLYPH:
            p4_draw_pixel(surface, x + 3, y + 1, color);
            p4_draw_fill_rect(surface, x + 2, y + 2, 3, 1, color);
            p4_draw_fill_rect(surface, x + 1, y + 3, 5, 2, color);
            p4_draw_fill_rect(surface, x + 2, y + 5, 3, 1, color);
            p4_draw_pixel(surface, x + 3, y + 6, color);
            break;
        case SUIT_CLUB_GLYPH:
            p4_draw_fill_circle(surface, x + 4, y + 1, 1, color);
            p4_draw_fill_circle(surface, x + 2, y + 4, 1, color);
            p4_draw_fill_circle(surface, x + 6, y + 4, 1, color);
            p4_draw_fill_rect(surface, x + 4, y + 2, 1, 4, color);
            p4_draw_fill_rect(surface, x + 3, y + 5, 3, 2, color);
            p4_draw_fill_rect(surface, x + 2, y + 7, 5, 1, color);
            break;
        case SUIT_SPADE_GLYPH:
        default:
            p4_draw_pixel(surface, x + 3, y, color);
            p4_draw_fill_rect(surface, x + 2, y + 1, 3, 1, color);
            p4_draw_fill_rect(surface, x + 1, y + 2, 5, 2, color);
            p4_draw_fill_rect(surface, x, y + 4, 7, 1, color);
            p4_draw_fill_rect(surface, x + 3, y + 4, 2, 3, color);
            p4_draw_fill_rect(surface, x + 2, y + 6, 4, 1, color);
            break;
        }
        return;
    }

    switch (glyph) {
    case SUIT_HEART_GLYPH:
        p4_draw_fill_circle(surface, x + 3, y + 4, 3, color);
        p4_draw_fill_circle(surface, x + 8, y + 4, 3, color);
        p4_draw_fill_rect(surface, x + 1, y + 4, 10, 3, color);
        p4_draw_fill_rect(surface, x + 2, y + 7, 8, 3, color);
        p4_draw_fill_rect(surface, x + 3, y + 10, 6, 2, color);
        p4_draw_fill_rect(surface, x + 4, y + 12, 4, 2, color);
        p4_draw_fill_rect(surface, x + 5, y + 14, 2, 1, color);
        p4_draw_fill_rect(surface, x + 5, y + 1, 2, 3, background);
        break;
    case SUIT_DIAMOND_GLYPH:
        p4_draw_fill_rect(surface, x + 5, y + 1, 2, 1, color);
        p4_draw_fill_rect(surface, x + 4, y + 2, 4, 2, color);
        p4_draw_fill_rect(surface, x + 3, y + 4, 6, 2, color);
        p4_draw_fill_rect(surface, x + 1, y + 6, 10, 3, color);
        p4_draw_fill_rect(surface, x + 3, y + 9, 6, 2, color);
        p4_draw_fill_rect(surface, x + 4, y + 11, 4, 2, color);
        p4_draw_fill_rect(surface, x + 5, y + 13, 2, 1, color);
        break;
    case SUIT_CLUB_GLYPH:
        p4_draw_fill_circle(surface, x + 6, y + 3, 2, color);
        p4_draw_fill_circle(surface, x + 3, y + 8, 2, color);
        p4_draw_fill_circle(surface, x + 9, y + 8, 2, color);
        p4_draw_fill_rect(surface, x + 5, y + 5, 3, 8, color);
        p4_draw_fill_rect(surface, x + 3, y + 13, 7, 2, color);
        break;
    case SUIT_SPADE_GLYPH:
    default:
        p4_draw_fill_rect(surface, x + 5, y + 1, 2, 1, color);
        p4_draw_fill_rect(surface, x + 4, y + 2, 4, 2, color);
        p4_draw_fill_rect(surface, x + 3, y + 4, 6, 2, color);
        p4_draw_fill_circle(surface, x + 3, y + 8, 3, color);
        p4_draw_fill_circle(surface, x + 8, y + 8, 3, color);
        p4_draw_fill_rect(surface, x + 1, y + 7, 10, 2, color);
        p4_draw_fill_rect(surface, x + 5, y + 9, 3, 5, color);
        p4_draw_fill_rect(surface, x + 3, y + 13, 7, 2, color);
        break;
    }
}

static void draw_card(p4_game_surface_t *surface, int x, int y,
                      uint8_t card, bool visible, bool selected)
{
    const uint16_t edge = selected ? COLOR_GOLD : COLOR_TEXT;
    fill_rounded_rect(surface, x + 2, y + 3,
                      CARD_W, CARD_H, 3, COLOR_CARD_SHADOW);
    if (!visible || card == P4_RUMMY_NO_CARD) {
        fill_rounded_rect(surface, x, y,
                          CARD_W, CARD_H, 3, edge);
        fill_rounded_rect(surface, x + 1, y + 1,
                          CARD_W - 2, CARD_H - 2, 2, COLOR_CARD_BACK_DARK);
        p4_draw_rect(surface, x + 4, y + 4,
                     CARD_W - 8, CARD_H - 8, COLOR_ACCENT);
        p4_draw_rect(surface, x + 6, y + 6,
                     CARD_W - 12, CARD_H - 12, COLOR_GOLD_DARK);
        for (int dot_y = y + 9; dot_y <= y + 31; dot_y += 7) {
            p4_draw_pixel(surface, x + 8, dot_y, COLOR_CARD_BACK);
            p4_draw_pixel(surface, x + 21, dot_y + 2, COLOR_CARD_BACK);
        }
        draw_suit(surface, x + 11, y + 13, SUIT_DIAMOND_GLYPH,
                  COLOR_CREAM, COLOR_CARD_BACK_DARK,
                  P4_DRAW_CP437_FULL_HEIGHT);
        return;
    }
    fill_rounded_rect(surface, x, y, CARD_W, CARD_H, 3, edge);
    fill_rounded_rect(surface, x + 1, y + 1,
                      CARD_W - 2, CARD_H - 2, 2, COLOR_CARD);
    if (selected) {
        p4_draw_rect(surface, x + 1, y + 1, CARD_W - 2,
                     CARD_H - 2, COLOR_GOLD);
    }
    char rank[3] = {0};
    rank_text(card, rank);
    const bool ten = (card % 13U) == 8U;
    const uint16_t color = suit_color(card);
    p4_draw_text(surface, x + (ten ? 3 : 4), y + 3,
                 rank, color, 1U, ten ? 2U : 1U);
    draw_suit(surface, x + (ten ? 19 : 17), y + 3,
              suit_glyph(card), color,
              COLOR_CARD, P4_DRAW_CP437_COMPACT_HEIGHT);
    draw_suit(surface, x + 11, y + 15, suit_glyph(card), color,
              COLOR_CARD, P4_DRAW_CP437_FULL_HEIGHT);
    p4_draw_text(surface, x + (ten ? 16 : 21), y + 32,
                 rank, color, 1U, ten ? 2U : 1U);
}

static void draw_exit(p4_game_surface_t *surface)
{
    fill_rounded_rect(surface, EXIT_X, EXIT_Y,
                      EXIT_W, EXIT_H, 2, COLOR_GOLD_DARK);
    fill_rounded_rect(surface, EXIT_X + 1, EXIT_Y + 1,
                      EXIT_W - 2, EXIT_H - 2, 1, COLOR_PANEL);
    p4_draw_text(surface, 8, 5, "EXIT", COLOR_CREAM, 1U, 4U);
}

static void draw_table_base(p4_game_surface_t *surface)
{
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_draw_fill_rect(surface, 0, 18, 320, 182, COLOR_RAIL_DARK);
    p4_draw_fill_rect(surface, 3, 21, 314, 176, COLOR_RAIL);
    p4_draw_fill_rect(surface, 6, 24, 308, 170, COLOR_FELT);
    for (int y = 31; y < 194; y += 14) {
        p4_draw_fill_rect(surface, 7, y, 306, 1, COLOR_FELT_LIGHT);
    }
    for (int x = 14; x < 310; x += 22) {
        p4_draw_pixel(surface, x, 27 + (x % 3), COLOR_GOLD_DARK);
        p4_draw_pixel(surface, x + 7, 190 - (x % 4), COLOR_GOLD_DARK);
    }
    p4_draw_rect(surface, 5, 23, 310, 172, COLOR_GOLD_DARK);
}

static void draw_title_bar(p4_game_surface_t *surface)
{
    draw_suit(surface, 116, 1, SUIT_HEART_GLYPH,
              COLOR_RED, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    p4_draw_text(surface, 130, 6, "P4 RUMMY", COLOR_GOLD, 1U, 8U);
    draw_suit(surface, 191, 1, SUIT_SPADE_GLYPH,
              COLOR_CREAM, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    draw_exit(surface);
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
        const uint16_t panel = current ? COLOR_PANEL_LIGHT : COLOR_PANEL;
        fill_rounded_rect(surface, x + 1, 23, width, 16,
                          2, COLOR_CARD_SHADOW);
        fill_rounded_rect(surface, x, 22, width, 16,
                          2, current ? COLOR_GOLD : COLOR_PANEL_EDGE);
        fill_rounded_rect(surface, x + 1, 23, width - 2, 14,
                          1, panel);
        if (current) {
            p4_draw_fill_rect(surface, x + 2, 25, 2, 10, COLOR_GOLD);
        }
        char label[12] = {0};
        player_label(label, state, player);
        p4_draw_text(surface, x + 7, 27, label,
                     current ? COLOR_GOLD : COLOR_TEXT, 1U, 7U);
        static const uint8_t player_suits[] = {
            SUIT_HEART_GLYPH, SUIT_SPADE_GLYPH,
            SUIT_DIAMOND_GLYPH, SUIT_CLUB_GLYPH,
        };
        const uint16_t icon_color =
            player == 0U || player == 2U ? COLOR_RED : COLOR_CREAM;
        draw_suit(surface, x + 62, 26, player_suits[player],
                  icon_color, panel, P4_DRAW_CP437_COMPACT_HEIGHT);
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
    for (uint8_t index = 0U; index < count; ++index) {
        const bool selected = player == state->current_player &&
            p4_rummy_local_turn(state) &&
            state->phase == P4_RUMMY_PHASE_DISCARD &&
            index == state->selected_card;
        draw_card(surface, start_x + (int)index * 33,
                  selected ? 142 : 147, state->hands[player][index],
                  true, selected);
    }
}

static void draw_setup(p4_game_surface_t *surface,
                       const p4_rummy_state_t *state)
{
    draw_table_base(surface);
    draw_title_bar(surface);
    fill_rounded_rect(surface, 19, 29, 282, 157,
                      5, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, 17, 27, 282, 157,
                      5, COLOR_GOLD_DARK);
    fill_rounded_rect(surface, 19, 29, 278, 153,
                      4, COLOR_FELT_DARK);

    draw_card(surface, 35, 38, 12U, true, false);
    draw_card(surface, 255, 38, 51U, true, false);
    p4_draw_text(surface, 112, 38, "P4 RUMMY", COLOR_GOLD, 2U, 8U);
    draw_suit(surface, 105, 66, SUIT_CLUB_GLYPH,
              COLOR_CREAM, COLOR_FELT_DARK, P4_DRAW_CP437_COMPACT_HEIGHT);
    draw_suit(surface, 207, 66, SUIT_SPADE_GLYPH,
              COLOR_CREAM, COLOR_FELT_DARK, P4_DRAW_CP437_COMPACT_HEIGHT);
    p4_draw_text(surface, 116, 67, "SETS  +  RUNS",
                 COLOR_MUTED, 1U, 13U);

    fill_rounded_rect(surface, SETUP_X + 1, SETUP_Y + 2,
                      SETUP_W, SETUP_H, 3, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, SETUP_X, SETUP_Y,
                      SETUP_W, SETUP_H, 3, COLOR_GOLD);
    fill_rounded_rect(surface, SETUP_X + 1, SETUP_Y + 1,
                      SETUP_W - 2, SETUP_H - 2, 2, COLOR_PANEL);
    p4_draw_text(surface, 61, 101, "<", COLOR_GOLD, 1U, 1U);
    char setting[28] = {0};
    if (state->network_mode) {
        size_t length = append_text(setting, sizeof(setting), 0U,
                                    "CPU SEATS  ");
        (void)append_unsigned(setting, sizeof(setting), length,
            (unsigned)(state->player_count - state->network_player_count));
    } else {
        size_t length = append_text(setting, sizeof(setting), 0U,
                                    "CPU OPPONENTS  ");
        (void)append_unsigned(setting, sizeof(setting), length,
                              (unsigned)(state->player_count - 1U));
    }
    p4_draw_text(surface, state->network_mode ? 103 : 88, 101,
                 setting, COLOR_TEXT, 1U,
                 state->network_mode ? 12U : 16U);
    p4_draw_text(surface, 253, 101, ">", COLOR_GOLD, 1U, 1U);

    const bool can_start = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    fill_rounded_rect(surface, START_X + 2, START_Y + 2,
                      START_W, START_H, 4, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, START_X, START_Y,
                      START_W, START_H, 4,
                      can_start ? COLOR_GOLD : COLOR_PANEL_EDGE);
    fill_rounded_rect(surface, START_X + 2, START_Y + 2,
                      START_W - 4, START_H - 4, 2,
                      can_start ? COLOR_ACCENT : COLOR_PANEL);
    p4_draw_text(surface, can_start ? 119 : 103, 147,
                 can_start ? "A  DEAL" : "WAIT FOR HOST",
                 can_start ? COLOR_BLACK : COLOR_MUTED, 1U,
                 can_start ? 7U : 13U);
    if (state->human_player_count == 1U && !state->network_mode) {
        p4_draw_text(surface, 100, 170, "P4MP = 2-4 HUMANS",
                     COLOR_MUTED, 1U, 17U);
    } else if (state->network_mode) {
        char linked[24] = {0};
        size_t length = append_text(linked, sizeof(linked), 0U,
                                    "CONNECTED  ");
        length = append_unsigned(linked, sizeof(linked), length,
                                 state->network_player_count);
        (void)append_text(linked, sizeof(linked), length, " HUMANS");
        p4_draw_text(surface, 98, 170, linked, COLOR_MUTED, 1U, 19U);
    }
}

static void draw_table(p4_game_surface_t *surface,
                       const p4_rummy_state_t *state)
{
    draw_table_base(surface);
    draw_title_bar(surface);
    draw_player_strip(surface, state);

    const bool stock_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_STOCK &&
        p4_rummy_local_turn(state);
    const bool discard_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_DISCARD &&
        p4_rummy_local_turn(state);
    fill_rounded_rect(surface, STOCK_X - 6, PILE_Y - 5,
                      CARD_W + 12, CARD_H + 10, 4, COLOR_FELT_DARK);
    fill_rounded_rect(surface, DISCARD_X - 6, PILE_Y - 5,
                      CARD_W + 12, CARD_H + 10, 4, COLOR_FELT_DARK);
    p4_draw_rect(surface, STOCK_X - 5, PILE_Y - 4,
                 CARD_W + 10, CARD_H + 8,
                 stock_selected ? COLOR_GOLD : COLOR_GOLD_DARK);
    p4_draw_rect(surface, DISCARD_X - 5, PILE_Y - 4,
                 CARD_W + 10, CARD_H + 8,
                 discard_selected ? COLOR_GOLD : COLOR_GOLD_DARK);
    draw_card(surface, STOCK_X, PILE_Y, P4_RUMMY_NO_CARD,
              false, stock_selected);
    const uint8_t discard_top = state->discard_count == 0U
        ? P4_RUMMY_NO_CARD : state->discard[state->discard_count - 1U];
    draw_card(surface, DISCARD_X, PILE_Y, discard_top,
              discard_top != P4_RUMMY_NO_CARD, discard_selected);
    p4_draw_text(surface, 108, 113, "STOCK", COLOR_CREAM, 1U, 5U);
    p4_draw_text(surface, 176, 113, "DISCARD", COLOR_CREAM, 1U, 7U);

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
    fill_rounded_rect(surface, 52, 123, 216, 20,
                      3, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, 51, 122, 216, 20,
                      3, COLOR_PANEL_EDGE);
    fill_rounded_rect(surface, 52, 123, 214, 18,
                      2, COLOR_PANEL);
    p4_draw_text(surface, 66, 128, action, action_color, 1U, 28U);
    draw_hand(surface, state);
}

static void draw_round_over(p4_game_surface_t *surface,
                            const p4_rummy_state_t *state)
{
    draw_table(surface, state);
    fill_rounded_rect(surface, 55, 51, 216, 88,
                      5, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, 51, 47, 216, 88,
                      5, COLOR_GOLD);
    fill_rounded_rect(surface, 53, 49, 212, 84,
                      4, COLOR_BACKGROUND);
    draw_suit(surface, 67, 61, SUIT_DIAMOND_GLYPH,
              COLOR_RED, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    draw_suit(surface, 245, 61, SUIT_CLUB_GLYPH,
              COLOR_CREAM, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    char winner[24] = {0};
    size_t length = append_text(winner, sizeof(winner), 0U,
        p4_rummy_player_is_cpu(state, state->winner) ? "CPU" : "PLAYER ");
    length = append_unsigned(winner, sizeof(winner), length,
                             (unsigned)state->winner + 1U);
    (void)append_text(winner, sizeof(winner), length, " WINS!");
    p4_draw_text(surface, 103, 65, winner, COLOR_GOLD, 1U, 16U);
    p4_draw_text(surface, 77, 90, "HAND IS ALL SETS + RUNS",
                 COLOR_TEXT, 1U, 23U);
    const bool can_restart = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    p4_draw_text(surface, can_restart ? 106 : 104, 113,
                 can_restart ? "A  NEW ROUND" : "WAIT FOR HOST",
                 COLOR_MUTED, 1U, can_restart ? 12U : 13U);
}

static bool adjust_setup(p4_rummy_state_t *state, bool increase)
{
    const bool changed = state->network_mode
        ? p4_rummy_adjust_cpu_seats(state, increase)
        : p4_rummy_adjust_offline_players(state, increase);
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
