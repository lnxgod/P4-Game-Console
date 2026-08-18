// SPDX-License-Identifier: MIT
/* Original clean-room Klondike Solitaire for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "solitaire_internal.h"

_Static_assert(
    SOLITAIRE_TABLEAU_TOP +
        (SOLITAIRE_TABLEAU_MAX - 1) * 2 + SOLITAIRE_CARD_HEIGHT <= 132,
    "maximum tableau must stay above the standard touch controls");

#define CURSOR_TOP SOLITAIRE_CURSOR_TOP
#define CURSOR_TABLEAU SOLITAIRE_CURSOR_TABLEAU
#define SOURCE_NONE SOLITAIRE_SOURCE_NONE
#define SOURCE_WASTE SOLITAIRE_SOURCE_WASTE
#define SOURCE_FOUNDATION SOLITAIRE_SOURCE_FOUNDATION
#define SOURCE_TABLEAU SOLITAIRE_SOURCE_TABLEAU
#define CARD_WIDTH SOLITAIRE_CARD_WIDTH
#define CARD_HEIGHT SOLITAIRE_CARD_HEIGHT
#define CARD_TOP SOLITAIRE_CARD_TOP
#define TABLEAU_TOP SOLITAIRE_TABLEAU_TOP
#define CARD_GAP_X SOLITAIRE_CARD_GAP_X

static int card_x(uint8_t column);
static int tableau_overlap(uint8_t count);

static uint8_t card_rank(uint8_t card)
{
    return (uint8_t)(card % 13U);
}

static uint8_t card_suit(uint8_t card)
{
    return (uint8_t)(card / 13U);
}

static bool suit_is_red(uint8_t suit)
{
    return suit == 1U || suit == 2U;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void cancel_selection(solitaire_state_t *state)
{
    state->selected_source = SOURCE_NONE;
    state->selected_pile = 0U;
    state->selected_index = 0U;
}

void solitaire_reset(solitaire_state_t *state)
{
    if (state == NULL) {
        return;
    }
    const uint32_t next_deal = state->deal_number + 1U;
    *state = (solitaire_state_t){
        .deal_number = next_deal,
        .cursor_area = CURSOR_TOP,
    };
    uint8_t deck[SOLITAIRE_DECK];
    for (uint8_t card = 0U; card < SOLITAIRE_DECK; ++card) {
        deck[card] = card;
    }
    uint32_t random = UINT32_C(0x51A17A1E) ^
        (next_deal * UINT32_C(0x9E3779B9));
    for (size_t remaining = SOLITAIRE_DECK;
         remaining > 1U; --remaining) {
        const size_t other = (size_t)(next_random(&random) % remaining);
        const uint8_t temporary = deck[remaining - 1U];
        deck[remaining - 1U] = deck[other];
        deck[other] = temporary;
    }
    size_t deck_index = 0U;
    for (uint8_t pile = 0U; pile < SOLITAIRE_PILES; ++pile) {
        const uint8_t count = (uint8_t)(pile + 1U);
        state->tableau_count[pile] = count;
        state->face_up_from[pile] = (uint8_t)(count - 1U);
        for (uint8_t index = 0U; index < count; ++index) {
            state->tableau[pile][index] = deck[deck_index++];
        }
    }
    while (deck_index < SOLITAIRE_DECK) {
        state->stock[state->stock_count++] = deck[deck_index++];
    }
}

static void tone(p4_game_context_t *context,
                 uint16_t frequency, uint16_t duration, uint8_t volume)
{
    (void)p4_game_play_tone(
        context, frequency, duration, volume, P4_WAVE_TRIANGLE);
}

static void draw_from_stock(p4_game_context_t *context,
                            solitaire_state_t *state)
{
    cancel_selection(state);
    if (state->stock_count > 0U &&
        state->waste_count < SOLITAIRE_STOCK_MAX) {
        state->waste[state->waste_count++] =
            state->stock[--state->stock_count];
        ++state->moves;
        tone(context, 420U, 35U, 2U);
        return;
    }
    if (state->waste_count == 0U) {
        tone(context, 120U, 70U, 2U);
        return;
    }
    while (state->waste_count > 0U) {
        state->stock[state->stock_count++] =
            state->waste[--state->waste_count];
    }
    ++state->moves;
    tone(context, 260U, 75U, 2U);
}

static bool selected_card(const solitaire_state_t *state,
                          uint8_t *card_out, uint8_t *count_out)
{
    if (card_out == NULL || count_out == NULL) {
        return false;
    }
    if (state->selected_source == SOURCE_WASTE &&
        state->waste_count > 0U) {
        *card_out = state->waste[state->waste_count - 1U];
        *count_out = 1U;
        return true;
    }
    if (state->selected_source == SOURCE_FOUNDATION &&
        state->selected_pile < SOLITAIRE_SUITS &&
        state->foundations[state->selected_pile] > 0U) {
        *card_out = (uint8_t)(state->selected_pile * 13U +
            state->foundations[state->selected_pile] - 1U);
        *count_out = 1U;
        return true;
    }
    if (state->selected_source == SOURCE_TABLEAU &&
        state->selected_pile < SOLITAIRE_PILES &&
        state->selected_index <
            state->tableau_count[state->selected_pile]) {
        *card_out = state->tableau[state->selected_pile]
            [state->selected_index];
        *count_out = (uint8_t)(
            state->tableau_count[state->selected_pile] -
            state->selected_index);
        return true;
    }
    return false;
}

static void remove_selected(solitaire_state_t *state, uint8_t count)
{
    if (state->selected_source == SOURCE_WASTE) {
        --state->waste_count;
    } else if (state->selected_source == SOURCE_FOUNDATION) {
        --state->foundations[state->selected_pile];
    } else if (state->selected_source == SOURCE_TABLEAU) {
        const uint8_t pile = state->selected_pile;
        state->tableau_count[pile] =
            (uint8_t)(state->tableau_count[pile] - count);
        if (state->tableau_count[pile] > 0U &&
            state->face_up_from[pile] >= state->tableau_count[pile]) {
            state->face_up_from[pile] =
                (uint8_t)(state->tableau_count[pile] - 1U);
        }
    }
    cancel_selection(state);
}

static bool can_place_tableau(const solitaire_state_t *state,
                              uint8_t pile, uint8_t card)
{
    const uint8_t count = state->tableau_count[pile];
    if (count == 0U) {
        return card_rank(card) == 12U;
    }
    const uint8_t top = state->tableau[pile][count - 1U];
    return card_rank(top) == (uint8_t)(card_rank(card) + 1U) &&
        suit_is_red(card_suit(top)) != suit_is_red(card_suit(card));
}

static bool move_to_tableau(solitaire_state_t *state, uint8_t pile)
{
    uint8_t card = 0U;
    uint8_t count = 0U;
    if (pile >= SOLITAIRE_PILES ||
        !selected_card(state, &card, &count) ||
        state->tableau_count[pile] + count > SOLITAIRE_TABLEAU_MAX ||
        !can_place_tableau(state, pile, card) ||
        (state->selected_source == SOURCE_TABLEAU &&
         state->selected_pile == pile)) {
        return false;
    }
    uint8_t moving[SOLITAIRE_TABLEAU_MAX];
    if (state->selected_source == SOURCE_TABLEAU) {
        memcpy(moving,
               &state->tableau[state->selected_pile][state->selected_index],
               count);
    } else {
        moving[0] = card;
    }
    remove_selected(state, count);
    memcpy(&state->tableau[pile][state->tableau_count[pile]], moving, count);
    if (state->tableau_count[pile] == 0U) {
        state->face_up_from[pile] = 0U;
    }
    state->tableau_count[pile] =
        (uint8_t)(state->tableau_count[pile] + count);
    ++state->moves;
    return true;
}

static bool move_to_foundation(solitaire_state_t *state, uint8_t suit)
{
    uint8_t card = 0U;
    uint8_t count = 0U;
    if (suit >= SOLITAIRE_SUITS ||
        !selected_card(state, &card, &count) || count != 1U ||
        card_suit(card) != suit ||
        card_rank(card) != state->foundations[suit]) {
        return false;
    }
    remove_selected(state, 1U);
    ++state->foundations[suit];
    ++state->moves;
    state->won = true;
    for (uint8_t index = 0U; index < SOLITAIRE_SUITS; ++index) {
        if (state->foundations[index] != 13U) {
            state->won = false;
        }
    }
    return true;
}

static void begin_selection(solitaire_state_t *state)
{
    if (state->cursor_area == CURSOR_TOP) {
        if (state->cursor_column == 1U && state->waste_count > 0U) {
            state->selected_source = SOURCE_WASTE;
        } else if (state->cursor_column >= 3U &&
                   state->foundations[state->cursor_column - 3U] > 0U) {
            state->selected_source = SOURCE_FOUNDATION;
            state->selected_pile = (uint8_t)(state->cursor_column - 3U);
        }
        return;
    }
    const uint8_t pile = state->cursor_column;
    const uint8_t count = state->tableau_count[pile];
    if (count == 0U) {
        return;
    }
    uint8_t depth = state->cursor_depth;
    const uint8_t face_up_count =
        (uint8_t)(count - state->face_up_from[pile]);
    if (depth >= face_up_count) {
        depth = (uint8_t)(face_up_count - 1U);
    }
    state->selected_source = SOURCE_TABLEAU;
    state->selected_pile = pile;
    state->selected_index = (uint8_t)(count - 1U - depth);
}

static void activate_cursor(p4_game_context_t *context,
                            solitaire_state_t *state)
{
    if (state->selected_source == SOURCE_NONE) {
        if (state->cursor_area == CURSOR_TOP &&
            state->cursor_column == 0U) {
            draw_from_stock(context, state);
            return;
        }
        begin_selection(state);
        tone(context, state->selected_source == SOURCE_NONE ? 140U : 560U,
             50U, 2U);
        return;
    }
    bool moved = false;
    if (state->cursor_area == CURSOR_TABLEAU) {
        moved = move_to_tableau(state, state->cursor_column);
    } else if (state->cursor_column >= 3U) {
        moved = move_to_foundation(
            state, (uint8_t)(state->cursor_column - 3U));
    }
    if (!moved) {
        tone(context, 130U, 90U, 3U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
    } else {
        tone(context, state->won ? 880U : 660U,
             state->won ? 240U : 70U, 3U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, state->won ?
            P4_GAME_AUDIO_EFFECT_REWARD : P4_GAME_AUDIO_EFFECT_ACTION);
    }
}

static void quick_foundation(p4_game_context_t *context,
                             solitaire_state_t *state)
{
    if (state->selected_source == SOURCE_NONE) {
        begin_selection(state);
    }
    uint8_t card = 0U;
    uint8_t count = 0U;
    if (!selected_card(state, &card, &count) || count != 1U ||
        !move_to_foundation(state, card_suit(card))) {
        cancel_selection(state);
        tone(context, 130U, 80U, 2U);
        (void)p4_game_audio_effect_play(
            context, &state->audio, P4_GAME_AUDIO_EFFECT_IMPACT);
        return;
    }
    tone(context, state->won ? 880U : 720U,
         state->won ? 240U : 70U, 3U);
    (void)p4_game_audio_effect_play(
        context, &state->audio, state->won ?
        P4_GAME_AUDIO_EFFECT_REWARD : P4_GAME_AUDIO_EFFECT_ACTION);
}

static bool touch_column(uint16_t x, uint8_t *column_out)
{
    if (column_out == NULL) {
        return false;
    }
    for (uint8_t column = 0U; column < SOLITAIRE_PILES; ++column) {
        const int left = card_x(column);
        if ((int)x >= left && (int)x < left + CARD_WIDTH) {
            *column_out = column;
            return true;
        }
    }
    return false;
}

static bool cursor_from_touch(solitaire_state_t *state,
                              const p4_game_point_t *point)
{
    uint8_t column = 0U;
    if (state == NULL || point == NULL ||
        !touch_column(point->x, &column)) {
        return false;
    }
    if (point->y >= CARD_TOP &&
        point->y < CARD_TOP + CARD_HEIGHT) {
        if (column == 2U) {
            return false;
        }
        state->cursor_area = CURSOR_TOP;
        state->cursor_column = column;
        state->cursor_depth = 0U;
        return true;
    }
    if (point->y < TABLEAU_TOP ||
        point->y >= SOLITAIRE_TABLEAU_BOTTOM) {
        return false;
    }
    state->cursor_area = CURSOR_TABLEAU;
    state->cursor_column = column;
    state->cursor_depth = 0U;
    const uint8_t count = state->tableau_count[column];
    if (count == 0U) {
        return true;
    }
    const int overlap = tableau_overlap(count);
    const int last_y = TABLEAU_TOP + overlap * ((int)count - 1);
    if ((int)point->y >= last_y + CARD_HEIGHT) {
        return false;
    }
    uint8_t index = 0U;
    if (count > 1U) {
        index = (uint8_t)(((int)point->y - TABLEAU_TOP) / overlap);
        if (index >= count) {
            index = (uint8_t)(count - 1U);
        }
    }
    if (index < state->face_up_from[column]) {
        return false;
    }
    state->cursor_depth = (uint8_t)(count - 1U - index);
    return true;
}

static bool cursor_is_selected_source(const solitaire_state_t *state)
{
    if (state->selected_source == SOURCE_WASTE) {
        return state->cursor_area == CURSOR_TOP &&
            state->cursor_column == 1U;
    }
    if (state->selected_source == SOURCE_FOUNDATION) {
        return state->cursor_area == CURSOR_TOP &&
            state->cursor_column == state->selected_pile + 3U;
    }
    if (state->selected_source == SOURCE_TABLEAU &&
        state->cursor_area == CURSOR_TABLEAU &&
        state->cursor_column == state->selected_pile) {
        const uint8_t count = state->tableau_count[state->selected_pile];
        return count > 0U &&
            state->selected_index == count - 1U - state->cursor_depth;
    }
    return false;
}

static void activate_touch(p4_game_context_t *context,
                           solitaire_state_t *state,
                           const p4_game_point_t *point)
{
    if (!cursor_from_touch(state, point)) {
        tone(context, 140U, 45U, 1U);
        return;
    }
    if (state->cursor_area == CURSOR_TOP &&
        state->cursor_column == 0U) {
        draw_from_stock(context, state);
        return;
    }
    if (cursor_is_selected_source(state)) {
        cancel_selection(state);
        tone(context, 240U, 35U, 1U);
        return;
    }
    activate_cursor(context, state);
}

static void move_cursor(solitaire_state_t *state, uint32_t pressed)
{
    if ((pressed & P4_BUTTON_LEFT) != 0U && state->cursor_column > 0U) {
        --state->cursor_column;
    }
    if ((pressed & P4_BUTTON_RIGHT) != 0U &&
        state->cursor_column + 1U < SOLITAIRE_PILES) {
        ++state->cursor_column;
    }
    if (state->cursor_area == CURSOR_TOP && state->cursor_column == 2U) {
        state->cursor_column = (pressed & P4_BUTTON_LEFT) != 0U ? 1U : 3U;
    }
    if ((pressed & P4_BUTTON_DOWN) != 0U) {
        if (state->cursor_area == CURSOR_TOP) {
            state->cursor_area = CURSOR_TABLEAU;
            state->cursor_depth = 0U;
        } else if (state->cursor_depth > 0U) {
            --state->cursor_depth;
        }
    }
    if ((pressed & P4_BUTTON_UP) != 0U &&
        state->cursor_area == CURSOR_TABLEAU) {
        const uint8_t count = state->tableau_count[state->cursor_column];
        const uint8_t face_count = count == 0U ? 0U :
            (uint8_t)(count - state->face_up_from[state->cursor_column]);
        if (state->cursor_depth + 1U < face_count) {
            ++state->cursor_depth;
        } else {
            state->cursor_area = CURSOR_TOP;
            state->cursor_depth = 0U;
            if (state->cursor_column == 2U) {
                state->cursor_column = 1U;
            }
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(solitaire_state_t)) {
        return false;
    }
    solitaire_state_t *const state = context->state;
    *state = (solitaire_state_t){0};
    solitaire_reset(state);
    tone(context, 523U, 80U, 2U);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    solitaire_state_t *const state = context->state;
    (void)p4_game_audio_effect_service(context, &state->audio);
    const bool touch_down = input->touch_valid && input->touch_count > 0U;
    const bool touch_pressed = touch_down && !state->touch_was_down;
    state->touch_was_down = touch_down;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        solitaire_reset(state);
        tone(context, 523U, 100U, 2U);
        return P4_GAME_CONTINUE;
    }
    move_cursor(state, input->pressed);
    if (touch_pressed) {
        activate_touch(context, state, &input->touches[0]);
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        activate_cursor(context, state);
    }
    if ((input->pressed & P4_BUTTON_B) != 0U) {
        if (state->selected_source != SOURCE_NONE) {
            cancel_selection(state);
            tone(context, 240U, 35U, 1U);
        } else {
            quick_foundation(context, state);
        }
    }
    return P4_GAME_CONTINUE;
}

static int card_x(uint8_t column)
{
    return 8 + (int)column * (CARD_WIDTH + CARD_GAP_X);
}

static int tableau_overlap(uint8_t count)
{
    if (count <= 1U) {
        return 0;
    }
    int overlap =
        (SOLITAIRE_TABLEAU_BOTTOM - TABLEAU_TOP - CARD_HEIGHT) /
        ((int)count - 1);
    if (overlap > 8) {
        overlap = 8;
    }
    if (overlap < 2) {
        overlap = 2;
    }
    return overlap;
}

static void rank_text(uint8_t rank, char text[3])
{
    static const char names[13][3] = {
        "A", "2", "3", "4", "5", "6", "7",
        "8", "9", "10", "J", "Q", "K",
    };
    memcpy(text, names[rank], 3U);
}

static void draw_card_back(p4_game_surface_t *surface, int x, int y)
{
    p4_draw_fill_rect(surface, x, y, CARD_WIDTH, CARD_HEIGHT,
                      UINT16_C(0xFFFF));
    p4_draw_fill_rect(surface, x + 2, y + 2,
                      CARD_WIDTH - 4, CARD_HEIGHT - 4, UINT16_C(0x0018));
    for (int line = 0; line < 5; ++line) {
        p4_draw_rect(surface, x + 5 + line, y + 5 + line,
                     CARD_WIDTH - 10 - line * 2,
                     CARD_HEIGHT - 10 - line * 2, UINT16_C(0x07FF));
    }
}

static void draw_card(p4_game_surface_t *surface,
                      int x, int y, uint8_t card)
{
    const uint8_t suit = card_suit(card);
    const uint16_t ink = suit_is_red(suit)
        ? UINT16_C(0xF800) : UINT16_C(0x0000);
    p4_draw_fill_rect(surface, x, y, CARD_WIDTH, CARD_HEIGHT,
                      UINT16_C(0xFFFF));
    p4_draw_rect(surface, x, y, CARD_WIDTH, CARD_HEIGHT, UINT16_C(0x4208));
    char rank[3];
    rank_text(card_rank(card), rank);
    p4_draw_text(surface, x + 3, y + 3, rank, ink, 1U, 2U);
    static const char suits[4][2] = {"S", "H", "D", "C"};
    p4_draw_text(surface, x + 3, y + 13, suits[suit], ink, 1U, 1U);
    p4_draw_fill_circle(surface, x + CARD_WIDTH / 2,
                        y + CARD_HEIGHT / 2 + 5, 4, ink);
}

static void draw_empty_slot(p4_game_surface_t *surface, int x, int y)
{
    p4_draw_rect(surface, x, y, CARD_WIDTH, CARD_HEIGHT, UINT16_C(0x87F0));
    p4_draw_rect(surface, x + 2, y + 2,
                 CARD_WIDTH - 4, CARD_HEIGHT - 4, UINT16_C(0x0400));
}

static int selected_y(const solitaire_state_t *state)
{
    if (state->selected_source != SOURCE_TABLEAU) {
        return CARD_TOP;
    }
    return TABLEAU_TOP + tableau_overlap(
        state->tableau_count[state->selected_pile]) *
        (int)state->selected_index;
}

static void draw_selection(const solitaire_state_t *state,
                           p4_game_surface_t *surface)
{
    if (state->selected_source == SOURCE_NONE) {
        return;
    }
    uint8_t column = state->selected_pile;
    if (state->selected_source == SOURCE_WASTE) {
        column = 1U;
    } else if (state->selected_source == SOURCE_FOUNDATION) {
        column = (uint8_t)(state->selected_pile + 3U);
    }
    p4_draw_rect(surface, card_x(column) - 2, selected_y(state) - 2,
                 CARD_WIDTH + 4, CARD_HEIGHT + 4, UINT16_C(0xF81F));
}

static void draw_cursor(const solitaire_state_t *state,
                        p4_game_surface_t *surface)
{
    int y = CARD_TOP;
    if (state->cursor_area == CURSOR_TABLEAU) {
        const uint8_t count = state->tableau_count[state->cursor_column];
        uint8_t index = 0U;
        if (count > 0U) {
            const uint8_t max_depth =
                (uint8_t)(count - state->face_up_from[state->cursor_column] - 1U);
            const uint8_t depth = state->cursor_depth < max_depth
                ? state->cursor_depth : max_depth;
            index = (uint8_t)(count - 1U - depth);
        }
        y = TABLEAU_TOP + tableau_overlap(count) * (int)index;
    }
    p4_draw_rect(surface, card_x(state->cursor_column) - 1, y - 1,
                 CARD_WIDTH + 2, CARD_HEIGHT + 2, UINT16_C(0xFFE0));
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const solitaire_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0400));
    p4_draw_text(surface, 72, 8, "SOLITAIRE  TAP CARDS",
                 UINT16_C(0xFFFF), 1U, 20U);

    if (state->stock_count > 0U) {
        draw_card_back(surface, card_x(0U), CARD_TOP);
    } else {
        draw_empty_slot(surface, card_x(0U), CARD_TOP);
        p4_draw_text(surface, card_x(0U) + 9, CARD_TOP + 17,
                     "R", UINT16_C(0xFFFF), 1U, 1U);
    }
    if (state->waste_count > 0U) {
        draw_card(surface, card_x(1U), CARD_TOP,
                  state->waste[state->waste_count - 1U]);
    } else {
        draw_empty_slot(surface, card_x(1U), CARD_TOP);
    }
    for (uint8_t suit = 0U; suit < SOLITAIRE_SUITS; ++suit) {
        const int x = card_x((uint8_t)(suit + 3U));
        if (state->foundations[suit] == 0U) {
            draw_empty_slot(surface, x, CARD_TOP);
        } else {
            const uint8_t card = (uint8_t)(suit * 13U +
                state->foundations[suit] - 1U);
            draw_card(surface, x, CARD_TOP, card);
        }
    }

    for (uint8_t pile = 0U; pile < SOLITAIRE_PILES; ++pile) {
        const uint8_t count = state->tableau_count[pile];
        if (count == 0U) {
            draw_empty_slot(surface, card_x(pile), TABLEAU_TOP);
            continue;
        }
        const int overlap = tableau_overlap(count);
        for (uint8_t index = 0U; index < count; ++index) {
            const int y = TABLEAU_TOP + overlap * (int)index;
            if (index < state->face_up_from[pile]) {
                draw_card_back(surface, card_x(pile), y);
            } else {
                draw_card(surface, card_x(pile), y,
                          state->tableau[pile][index]);
            }
        }
    }
    draw_selection(state, surface);
    draw_cursor(state, surface);
    p4_game_feedback_draw_audio_effect(
        surface, &state->audio,
        state->won ? 160 : card_x(state->cursor_column) + CARD_WIDTH / 2,
        state->won ? 100 :
        (state->cursor_area == CURSOR_TOP ? CARD_TOP + CARD_HEIGHT / 2 : 100));
    if (state->won) {
        p4_draw_fill_rect(surface, 70, 76, 180, 48, UINT16_C(0x0010));
        p4_draw_rect(surface, 70, 76, 180, 48, UINT16_C(0xFFE0));
        p4_draw_text(surface, 106, 88, "YOU WON!", UINT16_C(0xFFE0), 2U, 8U);
        p4_draw_text(surface, 98, 111, "START = NEW DEAL",
                     UINT16_C(0xFFFF), 1U, 16U);
    }
    p4_game_draw_standard_controls(
        surface, UINT16_C(0xBDF7), UINT16_C(0xFFE0),
        state->held_buttons);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_solitaire_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(107),
    .id = "org.p4console.solitaire",
    .title = "SOLITAIRE",
    .subtitle = "ORIGINAL KLONDIKE",
    .accent_rgb565 = UINT16_C(0x07E0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM,
    .state_bytes = sizeof(solitaire_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
