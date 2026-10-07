// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/card_art.h"
#include "generated/table_materials.inc"
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
    STOCK_X = 96,
    DISCARD_FAN_X = 138,
    DISCARD_FAN_STEP = 14,
    DISCARD_VISIBLE_CARDS = 5,
    DISCARD_PAGE_LEFT_X = 138,
    DISCARD_PAGE_RIGHT_X = 204,
    DISCARD_PAGE_Y = 48,
    DISCARD_PAGE_W = 18,
    DISCARD_PAGE_H = 14,
    PILE_Y = 66,
    CARD_W = 30,
    CARD_H = 42,
    HAND_VISIBLE_CARDS = 8,
    HAND_PAGE_LEFT_X = 3,
    HAND_PAGE_RIGHT_X = 299,
    HAND_PAGE_Y = 153,
    HAND_PAGE_W = 18,
    HAND_PAGE_H = 32,
    MELD_BUTTON_X = 51,
    MELD_BUTTON_Y = 122,
    MELD_BUTTON_W = 104,
    MELD_BUTTON_H = 20,
    DISCARD_BUTTON_X = 163,
    DISCARD_BUTTON_Y = 122,
    DISCARD_BUTTON_W = 104,
    DISCARD_BUTTON_H = 20,
    SETUP_X = 52,
    SETUP_Y = 91,
    SETUP_W = 216,
    SETUP_H = 28,
    START_X = 91,
    START_Y = 137,
    START_W = 138,
    START_H = 27,
    DRAG_THRESHOLD = 6,
    DROP_NONE = 0, DROP_DISCARD = 1, DROP_MELD = 2,
};


/* Layout stays in canonical touch coordinates. Each primitive and glyph draws
 * directly into the negotiated surface; there is no scaled low-res frame. */
#define p4_draw_fill_rect p4_card_fill
#define p4_draw_rect p4_card_outline
#define p4_draw_fill_circle p4_card_circle
#define p4_draw_text p4_card_text
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

static size_t append_signed(char *text, size_t capacity, size_t length,
                            int value)
{
    if (value < 0) {
        length = append_text(text, capacity, length, "-");
        value = -value;
    }
    return append_unsigned(text, capacity, length, (unsigned)value);
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

static uint8_t selection_count(uint64_t selection_mask)
{
    uint8_t count = 0U;
    while (selection_mask != 0U) {
        count = (uint8_t)(count + (selection_mask & 1U));
        selection_mask >>= 1U;
    }
    return count;
}

static void fill_rounded_rect(p4_game_surface_t *surface,
                              int x, int y, int width, int height,
                              int radius, uint16_t color)
{
    p4_ui_round_rect(surface,p4_ui_x(surface,x),p4_ui_y(surface,y),
                      p4_ui_x(surface,width),p4_ui_y(surface,height),
                      p4_ui_x(surface,radius),color);
}

static void draw_suit(p4_game_surface_t *surface,int x,int y,
                      uint8_t glyph,uint16_t color,uint16_t background,unsigned height)
{
    (void)background;
    const unsigned suit=glyph==SUIT_CLUB_GLYPH?0U:glyph==SUIT_DIAMOND_GLYPH?1U:
        glyph==SUIT_HEART_GLYPH?2U:3U;
    p4_card_suit(surface,p4_ui_x(surface,x),p4_ui_y(surface,y),
                 p4_ui_y(surface,(int)height),suit,color);
}

static void draw_card(p4_game_surface_t *surface,int x,int y,
                      uint8_t card,bool visible,bool selected)
{
    if(!visible || card==P4_RUMMY_NO_CARD){
        p4_card_back(surface,x,y,CARD_W,CARD_H,card_table_back,selected);
        return;
    }
    const unsigned rank=(unsigned)(card%13U)+2U;
    p4_card_face(surface,x,y,CARD_W,CARD_H,rank==14U?1U:rank,card/13U,selected);
}

static void draw_mini_card(p4_game_surface_t *surface, int x, int y,
                           uint8_t card)
{
    p4_draw_fill_rect(surface, x + 1, y + 1, 16, 14, COLOR_CARD_SHADOW);
    p4_draw_fill_rect(surface, x, y, 16, 14, COLOR_CARD);
    p4_draw_rect(surface, x, y, 16, 14, COLOR_PANEL_EDGE);
    char rank[3] = {0};
    rank_text(card, rank);
    const uint16_t color = suit_color(card);
    if (rank[1] == '\0') {
        p4_draw_text(surface, x + 1, y + 4, rank, color, 1U, 1U);
    } else {
        p4_draw_fill_rect(surface, x + 2, y + 4, 1, 7, color);
        p4_card_fill(surface, x + 1, y + 5, 1, 1, color);
        p4_draw_fill_rect(surface, x + 1, y + 10, 3, 1, color);
        p4_draw_rect(surface, x + 5, y + 4, 3, 7, color);
    }
    draw_suit(surface, x + 8, y + 3, suit_glyph(card),
              color, COLOR_CARD,
              P4_DRAW_CP437_COMPACT_HEIGHT);
}

static void meld_bounds(uint8_t meld, int *x, int *y)
{
    if (x != NULL) {
        *x = (meld & 1U) == 0U ? 7 : 226;
    }
    if (y != NULL) {
        *y = 43 + (int)(meld / 2U) * 19;
    }
}

static void draw_table_melds(p4_game_surface_t *surface,
                             const p4_rummy_state_t *state)
{
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        int x = 0;
        int y = 0;
        meld_bounds(meld, &x, &y);
        const bool target = p4_rummy_local_turn(state) &&
            state->phase == P4_RUMMY_PHASE_DISCARD &&
            state->selected_meld == meld;
        fill_rounded_rect(surface, x, y, 87, 17, 2,
                          target ? COLOR_GOLD : COLOR_PANEL_EDGE);
        fill_rounded_rect(surface, x + 1, y + 1, 85, 15, 1, COLOR_PANEL);
        char owner[3] = {'P',
            (char)('1' + state->meld_owners[meld]), '\0'};
        p4_draw_text(surface, x + 3, y + 5, owner,
                     target ? COLOR_TEXT : COLOR_GOLD, 1U, 2U);
        const uint8_t visible = state->meld_counts[meld] <= 4U
            ? state->meld_counts[meld] : 3U;
        for (uint8_t index = 0U; index < visible; ++index) {
            draw_mini_card(surface, x + 18 + (int)index * 17,
                           y + 1, state->melds[meld][index]);
        }
        if (state->meld_counts[meld] > visible) {
            const char more[2] = {'+', '\0'};
            p4_draw_text(surface, x + 72, y + 5, more,
                         COLOR_GOLD, 1U, 1U);
        }
    }
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
    p4_card_felt(surface,6,24,308,170,card_table_felt);
    p4_draw_rect(surface, 5, 23, 310, 172, COLOR_GOLD_DARK);
}

static void draw_title_bar(p4_game_surface_t *surface)
{
    draw_suit(surface, 110, 1, SUIT_HEART_GLYPH,
              COLOR_RED, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    p4_draw_text(surface, 130, 6, "RUMMY 500", COLOR_GOLD, 1U, 9U);
    draw_suit(surface, 191, 1, SUIT_SPADE_GLYPH,
              COLOR_CREAM, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    p4_draw_text(surface, 247, 6, "HAND/SCORE",
                 COLOR_MUTED, 1U, 10U);
    draw_exit(surface);
}

static void player_label(char text[16], const p4_rummy_state_t *state,
                         uint8_t player)
{
    size_t length = append_text(text, 16U, 0U,
                                p4_rummy_player_is_cpu(state, player)
                                    ? "C" : "P");
    length = append_unsigned(text, 16U, length, (unsigned)player + 1U);
    length = append_text(text, 16U, length, " ");
    length = append_unsigned(text, 16U, length,
                             state->hand_counts[player]);
    length = append_text(text, 16U, length, "/");
    (void)append_signed(text, 16U, length, state->scores[player]);
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
        char label[16] = {0};
        player_label(label, state, player);
        p4_draw_text(surface, x + 5, 27, label,
                     current ? COLOR_GOLD : COLOR_TEXT, 1U, 13U);
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

static uint8_t hand_window_start(const p4_rummy_state_t *state,
                                 uint8_t player)
{
    if (state == NULL || player >= state->player_count ||
        state->hand_counts[player] <= HAND_VISIBLE_CARDS) {
        return 0U;
    }
    const uint8_t focused = state->selected_card <
            state->hand_counts[player]
        ? state->selected_card : 0U;
    return (uint8_t)((focused / HAND_VISIBLE_CARDS) *
                     HAND_VISIBLE_CARDS);
}

static uint8_t hand_window_count(const p4_rummy_state_t *state,
                                 uint8_t player, uint8_t start)
{
    const uint8_t remaining =
        (uint8_t)(state->hand_counts[player] - start);
    return remaining < HAND_VISIBLE_CARDS
        ? remaining : HAND_VISIBLE_CARDS;
}

static void draw_hand_page_arrow(p4_game_surface_t *surface,
                                 int x, const char *label)
{
    fill_rounded_rect(surface, x, HAND_PAGE_Y,
                      HAND_PAGE_W, HAND_PAGE_H, 3, COLOR_PANEL_EDGE);
    fill_rounded_rect(surface, x + 1, HAND_PAGE_Y + 1,
                      HAND_PAGE_W - 2, HAND_PAGE_H - 2,
                      2, COLOR_PANEL);
    p4_draw_text(surface, x + 6, HAND_PAGE_Y + 12,
                 label, COLOR_GOLD, 1U, 1U);
}

static void draw_hand(p4_game_surface_t *surface,
                      const p4_rummy_state_t *state)
{
    const uint8_t player = visible_hand_player(state);
    if (player >= state->player_count || state->hand_counts[player] == 0U) {
        return;
    }
    const uint8_t window_start = hand_window_start(state, player);
    const uint8_t visible = hand_window_count(
        state, player, window_start);
    const int start_x = hand_start_x(visible);
    for (uint8_t shown = 0U; shown < visible; ++shown) {
        const uint8_t index = (uint8_t)(window_start + shown);
        if (state->hand_dragging && index==state->drag_card && player==state->drag_player) continue;
        const bool local = player == state->current_player &&
            p4_rummy_local_turn(state) &&
            state->phase == P4_RUMMY_PHASE_DISCARD;
        const bool marked = local &&
            (state->selected_mask & (UINT64_C(1) << index)) != 0U;
        const bool focused = local && index == state->selected_card;
        draw_card(surface, start_x + (int)shown * 33,
                  marked ? 142 : 147, state->hands[player][index],
                  true, marked);
        if (focused) {
            p4_draw_fill_rect(surface,
                start_x + (int)shown * 33 + 7, 191,
                16, 2, COLOR_GOLD);
        }
    }
    if (window_start > 0U) {
        draw_hand_page_arrow(surface, HAND_PAGE_LEFT_X, "<");
    }
    if ((uint8_t)(window_start + visible) < state->hand_counts[player]) {
        draw_hand_page_arrow(surface, HAND_PAGE_RIGHT_X, ">");
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
    p4_draw_text(surface, 106, 38, "RUMMY 500", COLOR_GOLD, 2U, 9U);
    draw_suit(surface, 105, 66, SUIT_CLUB_GLYPH,
              COLOR_CREAM, COLOR_FELT_DARK, P4_DRAW_CP437_COMPACT_HEIGHT);
    draw_suit(surface, 207, 66, SUIT_SPADE_GLYPH,
              COLOR_CREAM, COLOR_FELT_DARK, P4_DRAW_CP437_COMPACT_HEIGHT);
    p4_draw_text(surface, 116, 67, "SETS  +  RUNS",
                 COLOR_MUTED, 1U, 13U);
    p4_draw_text(surface, 124, 79, "FIRST TO 500",
                 COLOR_GOLD, 1U, 12U);

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
    p4_draw_text(surface, can_start ? 148 : 103, 147,
                 can_start ? "DEAL" : "WAIT FOR HOST",
                 can_start ? COLOR_BLACK : COLOR_MUTED, 1U,
                 can_start ? 4U : 13U);
    if (state->human_player_count == 1U && !state->network_mode) {
        p4_draw_text(surface, 100, 170, "PLAY WITH FRIENDS",
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

static uint8_t discard_window_start(const p4_rummy_state_t *state)
{
    if (state->discard_count <= DISCARD_VISIBLE_CARDS) {
        return 0U;
    }
    const uint8_t maximum = (uint8_t)(
        state->discard_count - DISCARD_VISIBLE_CARDS);
    if (state->selected_discard < maximum) {
        return state->selected_discard;
    }
    return maximum;
}

static uint8_t discard_window_count(const p4_rummy_state_t *state,
                                    uint8_t start)
{
    const uint8_t remaining = (uint8_t)(state->discard_count - start);
    return remaining < DISCARD_VISIBLE_CARDS
        ? remaining : DISCARD_VISIBLE_CARDS;
}

static void draw_discard_pile(p4_game_surface_t *surface,
                              const p4_rummy_state_t *state,
                              bool pile_selected)
{
    const uint8_t start = discard_window_start(state);
    const uint8_t visible = discard_window_count(state, start);
    const int width = visible == 0U ? CARD_W
        : CARD_W + ((int)visible - 1) * DISCARD_FAN_STEP;
    fill_rounded_rect(surface, DISCARD_FAN_X - 5, PILE_Y - 5,
                      width + 10, CARD_H + 10, 4, COLOR_FELT_DARK);
    p4_draw_rect(surface, DISCARD_FAN_X - 4, PILE_Y - 4,
                 width + 8, CARD_H + 8,
                 pile_selected ? COLOR_GOLD : COLOR_GOLD_DARK);
    for (uint8_t shown = 0U; shown < visible; ++shown) {
        const uint8_t index = (uint8_t)(start + shown);
        const bool selected = pile_selected &&
            index == state->selected_discard;
        draw_card(surface,
                  DISCARD_FAN_X + (int)shown * DISCARD_FAN_STEP,
                  selected ? PILE_Y - 3 : PILE_Y,
                  state->discard[index], true, selected);
    }
    char label[24] = {0};
    size_t length = append_text(label, sizeof(label), 0U, "PILE ");
    length = append_unsigned(label, sizeof(label), length,
                             state->discard_count);
    if (pile_selected &&
        state->selected_discard < state->discard_count) {
        length = append_text(label, sizeof(label), length, "  TAKE ");
        (void)append_unsigned(
            label, sizeof(label), length,
            (unsigned)(state->discard_count - state->selected_discard));
    }
    p4_draw_text(surface, 137, 113, label,
                 pile_selected ? COLOR_GOLD : COLOR_CREAM,
                 1U, 14U);
    if (start > 0U) {
        fill_rounded_rect(surface, DISCARD_PAGE_LEFT_X, DISCARD_PAGE_Y,
                          DISCARD_PAGE_W, DISCARD_PAGE_H, 2,
                          COLOR_PANEL_EDGE);
        p4_draw_text(surface, DISCARD_PAGE_LEFT_X + 6,
                     DISCARD_PAGE_Y + 3, "<", COLOR_GOLD, 1U, 1U);
    }
    if ((uint8_t)(start + visible) < state->discard_count) {
        fill_rounded_rect(surface, DISCARD_PAGE_RIGHT_X, DISCARD_PAGE_Y,
                          DISCARD_PAGE_W, DISCARD_PAGE_H, 2,
                          COLOR_PANEL_EDGE);
        p4_draw_text(surface, DISCARD_PAGE_RIGHT_X + 6,
                     DISCARD_PAGE_Y + 3, ">", COLOR_GOLD, 1U, 1U);
    }
}

static void draw_table(p4_game_surface_t *surface,
                       const p4_rummy_state_t *state)
{
    draw_table_base(surface);
    draw_title_bar(surface);
    draw_player_strip(surface, state);
    draw_table_melds(surface, state);

    const bool stock_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_STOCK &&
        p4_rummy_local_turn(state);
    const bool discard_selected = state->phase == P4_RUMMY_PHASE_DRAW &&
        state->draw_source == P4_RUMMY_DRAW_DISCARD &&
        p4_rummy_local_turn(state);
    fill_rounded_rect(surface, STOCK_X - 6, PILE_Y - 5,
                      CARD_W + 12, CARD_H + 10, 4, COLOR_FELT_DARK);
    p4_draw_rect(surface, STOCK_X - 5, PILE_Y - 4,
                 CARD_W + 10, CARD_H + 8,
                 stock_selected ? COLOR_GOLD : COLOR_GOLD_DARK);
    draw_card(surface, STOCK_X, PILE_Y, P4_RUMMY_NO_CARD,
              false, stock_selected);
    draw_discard_pile(surface, state, discard_selected);
    p4_draw_text(surface, 95, 113, "STOCK", COLOR_CREAM, 1U, 5U);

    if (state->phase == P4_RUMMY_PHASE_DISCARD &&
        p4_rummy_local_turn(state) && !state->network_request_pending) {
        const uint8_t marked = selection_count(state->selected_mask);
        fill_rounded_rect(surface, MELD_BUTTON_X + 1,
                          MELD_BUTTON_Y + 1,
                          MELD_BUTTON_W, MELD_BUTTON_H,
                          3, COLOR_CARD_SHADOW);
        fill_rounded_rect(surface, MELD_BUTTON_X, MELD_BUTTON_Y,
                          MELD_BUTTON_W, MELD_BUTTON_H,
                          3, marked == 0U ? COLOR_PANEL_EDGE : COLOR_GOLD);
        fill_rounded_rect(surface, MELD_BUTTON_X + 1,
                          MELD_BUTTON_Y + 1,
                          MELD_BUTTON_W - 2, MELD_BUTTON_H - 2,
                          2, COLOR_PANEL);
        char play_label[14] = {0};
        size_t play_length = 0U;
        if (state->selected_meld < state->meld_count) {
            play_length = append_text(
                play_label, sizeof(play_label), 0U, "ADD TO P");
            (void)append_unsigned(
                play_label, sizeof(play_label), play_length,
                (unsigned)state->meld_owners[state->selected_meld] + 1U);
        } else {
            (void)append_text(play_label, sizeof(play_label), 0U,
                              state->required_meld_card != P4_RUMMY_NO_CARD
                                  ? "MELD PICK" : "PLAY CARDS");
        }
        p4_draw_text(surface,
                     state->selected_meld < state->meld_count ? 70 : 69,
                     128, play_label,
                     marked == 0U ? COLOR_MUTED : COLOR_GOLD,
                     1U, 10U);

        fill_rounded_rect(surface, DISCARD_BUTTON_X + 1,
                          DISCARD_BUTTON_Y + 1,
                          DISCARD_BUTTON_W, DISCARD_BUTTON_H,
                          3, COLOR_CARD_SHADOW);
        fill_rounded_rect(surface, DISCARD_BUTTON_X, DISCARD_BUTTON_Y,
                          DISCARD_BUTTON_W, DISCARD_BUTTON_H,
                          3, COLOR_PANEL_EDGE);
        fill_rounded_rect(surface, DISCARD_BUTTON_X + 1,
                          DISCARD_BUTTON_Y + 1,
                          DISCARD_BUTTON_W - 2, DISCARD_BUTTON_H - 2,
                          2, COLOR_PANEL);
        p4_draw_text(surface,
                     state->required_meld_card != P4_RUMMY_NO_CARD
                         ? 184 : 194,
                     128,
                     state->required_meld_card != P4_RUMMY_NO_CARD
                         ? "MELD FIRST" : "DISCARD",
                     state->selected_card == state->drawn_card_index ||
                             state->required_meld_card != P4_RUMMY_NO_CARD
                         ? COLOR_MUTED : COLOR_TEXT,
                     1U, state->required_meld_card != P4_RUMMY_NO_CARD
                         ? 10U : 7U);
    } else {
        char action[30] = {0};
        uint16_t action_color = COLOR_TEXT;
        if (state->network_request_pending) {
            (void)append_text(action, sizeof(action), 0U,
                              "SENDING MOVE...");
            action_color = COLOR_GOLD;
        } else if (p4_rummy_player_is_cpu(state, state->current_player)) {
            (void)append_text(action, sizeof(action), 0U,
                              "CPU IS THINKING...");
            action_color = COLOR_MUTED;
        } else {
            (void)append_text(action, sizeof(action), 0U,
                              "TAP STOCK OR A PILE CARD");
        }
        fill_rounded_rect(surface, 52, 123, 216, 20,
                          3, COLOR_CARD_SHADOW);
        fill_rounded_rect(surface, 51, 122, 216, 20,
                          3, COLOR_PANEL_EDGE);
        fill_rounded_rect(surface, 52, 123, 214, 18,
                          2, COLOR_PANEL);
        p4_draw_text(surface, 70, 128, action, action_color, 1U, 26U);
    }
    draw_hand(surface, state);
}

static void draw_round_over(p4_game_surface_t *surface,
                            const p4_rummy_state_t *state)
{
    draw_table(surface, state);
    fill_rounded_rect(surface, 49, 43, 226, 124,
                      5, COLOR_CARD_SHADOW);
    fill_rounded_rect(surface, 45, 39, 226, 124,
                      5, COLOR_GOLD);
    fill_rounded_rect(surface, 47, 41, 222, 120,
                      4, COLOR_BACKGROUND);
    draw_suit(surface, 56, 49, SUIT_DIAMOND_GLYPH,
              COLOR_RED, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    draw_suit(surface, 252, 49, SUIT_CLUB_GLYPH,
              COLOR_CREAM, COLOR_BACKGROUND, P4_DRAW_CP437_FULL_HEIGHT);
    const uint8_t match_winner = p4_rummy_match_winner(state);
    const uint8_t shown_winner = match_winner == P4_RUMMY_NO_PLAYER
        ? state->winner : match_winner;
    char winner[28] = {0};
    size_t length = append_text(winner, sizeof(winner), 0U,
        p4_rummy_player_is_cpu(state, shown_winner) ? "CPU" : "PLAYER ");
    length = append_unsigned(winner, sizeof(winner), length,
                             (unsigned)shown_winner + 1U);
    (void)append_text(winner, sizeof(winner), length,
        match_winner == P4_RUMMY_NO_PLAYER ? " WINS ROUND" : " WINS MATCH");
    p4_draw_text(surface, 96, 51, winner, COLOR_GOLD, 1U, 20U);
    p4_draw_text(surface, 112, 65, "MATCH TOTALS",
                 COLOR_MUTED, 1U, 12U);
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        char score[20] = {0};
        size_t score_length = append_text(
            score, sizeof(score), 0U,
            p4_rummy_player_is_cpu(state, player) ? "CPU" : "P");
        score_length = append_unsigned(
            score, sizeof(score), score_length, (unsigned)player + 1U);
        score_length = append_text(
            score, sizeof(score), score_length, "  TOTAL ");
        (void)append_signed(score, sizeof(score), score_length,
                            state->scores[player]);
        p4_draw_text(surface, 105, 79 + (int)player * 12, score,
                     player == shown_winner ? COLOR_GOLD : COLOR_TEXT,
                     1U, 19U);
    }
    const bool can_restart = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    const char *const action = match_winner == P4_RUMMY_NO_PLAYER
        ? "NEW ROUND" : "NEW MATCH";
    p4_draw_text(surface, can_restart ? 130 : 119, 143,
                 can_restart ? action : "WAIT FOR HOST",
                 COLOR_MUTED, 1U, can_restart ? 9U : 13U);
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
    state->selected_card = right
        ? (uint8_t)((state->selected_card + 1U) % count)
        : (uint8_t)((state->selected_card + count - 1U) % count);
}

static void toggle_selected_card(p4_rummy_state_t *state)
{
    const uint8_t count = state->hand_counts[state->current_player];
    if (state->selected_card >= count) {
        return;
    }
    state->selected_mask ^=
        UINT64_C(1) << state->selected_card;
}

static void move_hand_page(p4_rummy_state_t *state, bool right)
{
    const uint8_t count = state->hand_counts[state->current_player];
    if (count <= HAND_VISIBLE_CARDS) {
        return;
    }
    if (right) {
        const uint8_t next = (uint8_t)(state->selected_card +
                                       HAND_VISIBLE_CARDS);
        state->selected_card = next < count
            ? next : (uint8_t)(count - 1U);
    } else {
        state->selected_card = state->selected_card >= HAND_VISIBLE_CARDS
            ? (uint8_t)(state->selected_card - HAND_VISIBLE_CARDS) : 0U;
    }
}

static void move_discard_selection(p4_rummy_state_t *state, bool right)
{
    if (state->discard_count == 0U) {
        state->selected_discard = P4_RUMMY_NO_CARD;
        return;
    }
    state->draw_source = P4_RUMMY_DRAW_DISCARD;
    if (state->selected_discard >= state->discard_count) {
        state->selected_discard = (uint8_t)(state->discard_count - 1U);
    } else if (right &&
               state->selected_discard + 1U < state->discard_count) {
        ++state->selected_discard;
    } else if (!right && state->selected_discard > 0U) {
        --state->selected_discard;
    }
}

static void move_discard_page(p4_rummy_state_t *state, bool newer)
{
    if (state->discard_count <= DISCARD_VISIBLE_CARDS) {
        return;
    }
    state->draw_source = P4_RUMMY_DRAW_DISCARD;
    if (state->selected_discard >= state->discard_count) {
        state->selected_discard = (uint8_t)(state->discard_count - 1U);
    }
    if (newer) {
        const uint8_t next = (uint8_t)(state->selected_discard +
                                       DISCARD_VISIBLE_CARDS);
        state->selected_discard = next < state->discard_count
            ? next : (uint8_t)(state->discard_count - 1U);
    } else {
        state->selected_discard =
            state->selected_discard >= DISCARD_VISIBLE_CARDS
                ? (uint8_t)(state->selected_discard -
                            DISCARD_VISIBLE_CARDS)
                : 0U;
    }
}

static void move_meld_target(p4_rummy_state_t *state, bool forward)
{
    if (state->meld_count == 0U) {
        state->selected_meld = P4_RUMMY_NO_CARD;
        return;
    }
    if (state->selected_meld >= state->meld_count) {
        state->selected_meld = forward
            ? 0U : (uint8_t)(state->meld_count - 1U);
    } else if (forward) {
        state->selected_meld =
            state->selected_meld + 1U < state->meld_count
                ? (uint8_t)(state->selected_meld + 1U)
                : P4_RUMMY_NO_CARD;
    } else {
        state->selected_meld = state->selected_meld == 0U
            ? P4_RUMMY_NO_CARD
            : (uint8_t)(state->selected_meld - 1U);
    }
}

static uint8_t touched_discard(const p4_rummy_state_t *state,
                               uint16_t x, uint16_t y)
{
    const uint8_t start = discard_window_start(state);
    const uint8_t visible = discard_window_count(state, start);
    if (visible == 0U ||
        !point_in(x, y, DISCARD_FAN_X - 4, PILE_Y - 8,
                  CARD_W + ((int)visible - 1) * DISCARD_FAN_STEP + 8,
                  CARD_H + 16)) {
        return P4_RUMMY_NO_CARD;
    }
    int shown = ((int)x - DISCARD_FAN_X) / DISCARD_FAN_STEP;
    if ((int)x < DISCARD_FAN_X) {
        shown = 0;
    }
    if (shown >= visible) {
        shown = visible - 1;
    }
    return (uint8_t)(start + shown);
}

/* Gestures are local presentation state. Drop checks call the existing pure
 * rules on a bounded copy, and only a legal release sends the normal action. */
static bool hand_gesture_current(const p4_rummy_state_t *state)
{
    return p4_rummy_local_turn(state) && !state->network_request_pending &&
        state->phase==P4_RUMMY_PHASE_DISCARD && state->revision==state->drag_revision &&
        state->current_player==state->drag_player &&
        state->hand_counts[state->drag_player]==state->drag_hand_count &&
        state->drag_card<state->drag_hand_count &&
        state->hands[state->drag_player][state->drag_card]==state->drag_value;
}

static bool begin_hand_touch(p4_rummy_state_t *state,uint16_t x,uint16_t y)
{
    if (!p4_rummy_local_turn(state) || state->network_request_pending ||
        state->phase!=P4_RUMMY_PHASE_DISCARD) return false;
    const uint8_t player=state->current_player;
    const uint8_t start=hand_window_start(state,player);
    const uint8_t visible=hand_window_count(state,player,start);
    const int left=hand_start_x(visible);
    for(uint8_t shown=0U;shown<visible;++shown) {
        const uint8_t card=(uint8_t)(start+shown);
        const int top=(state->selected_mask&(UINT64_C(1)<<card))!=0U?142:147;
        if (!point_in(x,y,left+(int)shown*33,top,CARD_W,CARD_H)) continue;
        state->hand_touch_active=true; state->hand_dragging=false;
        state->drag_card=card; state->drag_player=player;
        state->drag_value=state->hands[player][card];
        state->drag_hand_count=state->hand_counts[player];
        state->drag_revision=state->revision;
        state->drag_mask=state->selected_mask|(UINT64_C(1)<<card);
        state->drag_start_x=x; state->drag_start_y=y;
        state->drag_x=x; state->drag_y=y;
        state->drag_offset_x=(int16_t)((int)x-left-(int)shown*33);
        state->drag_offset_y=(int16_t)((int)y-top);
        state->drag_drop_kind=DROP_NONE; state->drag_drop_legal=false;
        return true;
    }
    return false;
}

static void locate_drop(p4_rummy_state_t *state)
{
    state->drag_drop_kind=DROP_NONE;
    state->drag_drop_meld=P4_RUMMY_NO_CARD;
    state->drag_drop_legal=false;
    if (!hand_gesture_current(state)) return;
    const uint16_t x=state->drag_x,y=state->drag_y;
    for(uint8_t meld=0U;meld<state->meld_count;++meld) {
        int left=0,top=0;meld_bounds(meld,&left,&top);
        if(point_in(x,y,left,top,87,17)) {
            state->drag_drop_kind=DROP_MELD; state->drag_drop_meld=meld;
            break;
        }
    }
    if (state->drag_drop_kind==DROP_NONE) {
        if (point_in(x,y,DISCARD_BUTTON_X,DISCARD_BUTTON_Y,DISCARD_BUTTON_W,DISCARD_BUTTON_H) ||
            point_in(x,y,DISCARD_FAN_X-4,PILE_Y-8,88,CARD_H+16)) {
            state->drag_drop_kind=DROP_DISCARD;
        } else if (point_in(x,y,MELD_BUTTON_X,MELD_BUTTON_Y,MELD_BUTTON_W,MELD_BUTTON_H)) {
            state->drag_drop_kind=DROP_MELD;
            state->drag_drop_meld=state->selected_meld;
        }
    }
    if (state->drag_drop_kind!=DROP_NONE) {
        p4_rummy_state_t probe=*state;
        state->drag_drop_legal=state->drag_drop_kind==DROP_DISCARD
            ?p4_rummy_discard_card(&probe,state->drag_player,state->drag_card)
            :p4_rummy_play_meld_to(&probe,state->drag_player,state->drag_mask,state->drag_drop_meld);
    }
}

static void update_hand_touch(p4_rummy_state_t *state,uint16_t x,uint16_t y)
{
    if (!state->hand_touch_active) return;
    state->drag_x=x;state->drag_y=y;
    const int dx=(int)x-(int)state->drag_start_x,dy=(int)y-(int)state->drag_start_y;
    if (dx>=DRAG_THRESHOLD || dx<=-DRAG_THRESHOLD || dy>=DRAG_THRESHOLD || dy<=-DRAG_THRESHOLD)
        state->hand_dragging=true;
    if(state->hand_dragging)locate_drop(state);
}

static void finish_hand_touch(p4_game_context_t *context,p4_rummy_state_t *state)
{
    if(!state->hand_touch_active)return;
    const bool current=hand_gesture_current(state),dragging=state->hand_dragging;
    state->hand_touch_active=false;state->hand_dragging=false;
    if(!current)return;
    if(!dragging) {
        state->selected_card=state->drag_card;
        toggle_selected_card(state);
        return;
    }
    locate_drop(state);
    if(!state->drag_drop_legal)return;
    if(state->drag_drop_kind==DROP_DISCARD) {
        (void)p4_rummy_perform_discard(context,state,state->drag_card);
    } else {
        (void)p4_rummy_perform_meld_to(context,state,state->drag_mask,state->drag_drop_meld);
    }
}

static void draw_drag(p4_game_surface_t *surface,const p4_rummy_state_t *state)
{
    if(!state->hand_dragging || !hand_gesture_current(state))return;
    const uint16_t ink=state->drag_drop_kind==DROP_NONE?COLOR_GOLD:
        (state->drag_drop_legal?COLOR_ACCENT:COLOR_RED);
    int x=MELD_BUTTON_X,y=MELD_BUTTON_Y,w=MELD_BUTTON_W,h=MELD_BUTTON_H;
    if(state->drag_drop_kind==DROP_DISCARD) {
        x=DISCARD_FAN_X-4;y=PILE_Y-8;w=88;h=CARD_H+16;
    } else if(state->drag_drop_meld<state->meld_count) {
        meld_bounds(state->drag_drop_meld,&x,&y);w=87;h=17;
    }
    p4_draw_rect(surface,x-1,y-1,w+2,h+2,ink);
    p4_draw_rect(surface,x-2,y-2,w+4,h+4,ink);
    const int left=(int)state->drag_x-state->drag_offset_x;
    const int top=(int)state->drag_y-state->drag_offset_y-3;
    draw_card(surface,left,top,state->drag_value,true,true);
    const uint8_t count=selection_count(state->drag_mask);
    if(count>1U && state->drag_drop_kind!=DROP_DISCARD) {
        char label[4]={0};(void)append_unsigned(label,sizeof(label),0U,count);
        fill_rounded_rect(surface,left+CARD_W-8,top-6,14,12,3,COLOR_PANEL);
        p4_draw_text(surface,left+CARD_W-5,top-3,label,COLOR_GOLD,1U,2U);
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
        if (point_in(x, y, 45, 39, 226, 124)) {
            (void)p4_rummy_request_new_round(context, state);
        }
        return;
    }
    if (!p4_rummy_local_turn(state) || state->network_request_pending) {
        return;
    }
    if (state->phase == P4_RUMMY_PHASE_DRAW) {
        if (point_in(x, y, DISCARD_PAGE_LEFT_X, DISCARD_PAGE_Y,
                     DISCARD_PAGE_W, DISCARD_PAGE_H)) {
            move_discard_page(state, false);
        } else if (point_in(x, y, DISCARD_PAGE_RIGHT_X, DISCARD_PAGE_Y,
                            DISCARD_PAGE_W, DISCARD_PAGE_H)) {
            move_discard_page(state, true);
        } else if (point_in(x, y, STOCK_X - 8, PILE_Y - 5,
                     CARD_W + 16, CARD_H + 24)) {
            state->draw_source = P4_RUMMY_DRAW_STOCK;
            (void)p4_rummy_perform_draw(
                context, state, P4_RUMMY_DRAW_STOCK,
                P4_RUMMY_NO_CARD);
        } else {
            const uint8_t discard_index = touched_discard(state, x, y);
            if (discard_index == P4_RUMMY_NO_CARD) {
                return;
            }
            state->draw_source = P4_RUMMY_DRAW_DISCARD;
            state->selected_discard = discard_index;
            (void)p4_rummy_perform_draw(
                context, state, P4_RUMMY_DRAW_DISCARD,
                discard_index);
        }
        return;
    }
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        int meld_x = 0;
        int meld_y = 0;
        meld_bounds(meld, &meld_x, &meld_y);
        if (point_in(x, y, meld_x, meld_y, 87, 17)) {
            state->selected_meld = meld;
            if (state->selected_mask != 0U) {
                (void)p4_rummy_perform_meld_to(
                    context, state, state->selected_mask, meld);
            }
            return;
        }
    }
    if (point_in(x, y, MELD_BUTTON_X, MELD_BUTTON_Y,
                 MELD_BUTTON_W, MELD_BUTTON_H)) {
        (void)p4_rummy_perform_meld_to(
            context, state, state->selected_mask,
            state->selected_meld);
        return;
    }
    if (point_in(x, y, DISCARD_BUTTON_X, DISCARD_BUTTON_Y,
                 DISCARD_BUTTON_W, DISCARD_BUTTON_H)) {
        (void)p4_rummy_perform_discard(
            context, state, state->selected_card);
        return;
    }
    if (point_in(x, y, HAND_PAGE_LEFT_X, HAND_PAGE_Y,
                 HAND_PAGE_W, HAND_PAGE_H)) {
        move_hand_page(state, false);
        return;
    }
    if (point_in(x, y, HAND_PAGE_RIGHT_X, HAND_PAGE_Y,
                 HAND_PAGE_W, HAND_PAGE_H)) {
        move_hand_page(state, true);
        return;
    }
    const uint8_t window_start = hand_window_start(
        state, state->current_player);
    const uint8_t visible = hand_window_count(
        state, state->current_player, window_start);
    const int start_x = hand_start_x(visible);
    for (uint8_t shown = 0U; shown < visible; ++shown) {
        const uint8_t index = (uint8_t)(window_start + shown);
        if (point_in(x, y, start_x + (int)shown * 33, 137,
                     CARD_W, 58)) {
            state->selected_card = index;
            toggle_selected_card(state);
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
    p4_game_input_t direct_input=*input;
    if((input->touch_valid && input->touch_count!=0U) || state->touch_was_down) {
        direct_input.held=0U;direct_input.pressed=0U;direct_input.released=0U;
        input=&direct_input;
    }
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    const bool touch_down = input->touch_valid && input->touch_count != 0U;
    const bool touch_pressed = touch_down && !state->touch_was_down;
    const bool touch_released = !touch_down && state->touch_was_down;
    if (touch_pressed && point_in(input->touches[0].x, input->touches[0].y,
                                  EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
        state->touch_was_down = touch_down;
        return P4_GAME_EXIT_TO_LAUNCHER;
    }

    p4_rummy_network_poll(context, state, elapsed_ms);
    if(state->hand_touch_active && (!hand_gesture_current(state) || input->touch_count>1U)) {
        state->hand_touch_active=false;state->hand_dragging=false;
    }
    if (touch_pressed && input->touch_count==1U) {
        if(!begin_hand_touch(state,input->touches[0].x,input->touches[0].y))
            handle_touch(context,state,input->touches[0].x,input->touches[0].y);
    } else if(touch_down && state->hand_touch_active) {
        update_hand_touch(state,input->touches[0].x,input->touches[0].y);
    }
    if(touch_released)finish_hand_touch(context,state);

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
            if ((pressed & (P4_BUTTON_UP | P4_BUTTON_DOWN |
                            P4_BUTTON_B)) != 0U) {
                state->draw_source = state->draw_source == P4_RUMMY_DRAW_STOCK
                    ? P4_RUMMY_DRAW_DISCARD : P4_RUMMY_DRAW_STOCK;
                if (state->draw_source == P4_RUMMY_DRAW_DISCARD &&
                    state->discard_count != 0U) {
                    state->selected_discard =
                        (uint8_t)(state->discard_count - 1U);
                }
            }
            if ((pressed & P4_BUTTON_LEFT) != 0U) {
                if (state->draw_source == P4_RUMMY_DRAW_STOCK) {
                    state->draw_source = P4_RUMMY_DRAW_DISCARD;
                    state->selected_discard = state->discard_count == 0U
                        ? P4_RUMMY_NO_CARD
                        : (uint8_t)(state->discard_count - 1U);
                } else {
                    move_discard_selection(state, false);
                }
            }
            if ((pressed & P4_BUTTON_RIGHT) != 0U) {
                if (state->draw_source == P4_RUMMY_DRAW_STOCK) {
                    state->draw_source = P4_RUMMY_DRAW_DISCARD;
                    state->selected_discard = state->discard_count == 0U
                        ? P4_RUMMY_NO_CARD
                        : (uint8_t)(state->discard_count - 1U);
                } else {
                    move_discard_selection(state, true);
                }
            }
            if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
                (void)p4_rummy_perform_draw(
                    context, state, state->draw_source,
                    state->draw_source == P4_RUMMY_DRAW_DISCARD
                        ? state->selected_discard : P4_RUMMY_NO_CARD);
            }
        } else if (state->phase == P4_RUMMY_PHASE_DISCARD) {
            if ((pressed & P4_BUTTON_LEFT) != 0U) {
                move_selection(state, false);
            }
            if ((pressed & P4_BUTTON_RIGHT) != 0U) {
                move_selection(state, true);
            }
            if ((pressed & P4_BUTTON_UP) != 0U) {
                move_meld_target(state, false);
            }
            if ((pressed & P4_BUTTON_DOWN) != 0U) {
                move_meld_target(state, true);
            }
            if ((pressed & P4_BUTTON_B) != 0U) {
                toggle_selected_card(state);
            }
            if ((pressed & P4_BUTTON_START) != 0U) {
                (void)p4_rummy_perform_meld_to(
                    context, state, state->selected_mask,
                    state->selected_meld);
            }
            if ((pressed & P4_BUTTON_A) != 0U) {
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
    draw_drag(surface,state);
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
    .title = "Rummy 500",
    .subtitle = "Build melds and reach 500",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(p4_rummy_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
