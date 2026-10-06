// SPDX-License-Identifier: MIT
/* Original clean-room Klondike Solitaire for P4 Game API v1. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/audio_pack.h"
#include "p4/draw.h"
#include "p4/card_art.h"
#include "generated/table_materials.inc"
#include "p4/feedback.h"
#include "p4/game.h"
#include "p4/input.h"
#include "solitaire_internal.h"

_Static_assert(SOLITAIRE_TABLEAU_BOTTOM <= P4_GAME_SURFACE_HEIGHT,
               "tableau must fit the canonical touch surface");

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


/* Layout stays in canonical touch coordinates. Each primitive and glyph draws
 * directly into the negotiated surface; there is no scaled low-res frame. */
#define p4_draw_fill_rect p4_card_fill
#define p4_draw_rect p4_card_outline
#define p4_draw_fill_circle p4_card_circle
#define p4_draw_text p4_card_text

static int card_x(uint8_t column);
static int tableau_overlap(const solitaire_state_t *state, uint8_t pile);
static int tableau_y(const solitaire_state_t *state, uint8_t pile, uint8_t index);
static int selected_y(const solitaire_state_t *state);

enum { TOUCH_NONE, TOUCH_CARD, TOUCH_STOCK, TOUCH_EXIT, TOUCH_NEW_DEAL };

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
    const int last_y = tableau_y(state, column, (uint8_t)(count - 1U));
    if ((int)point->y >= last_y + CARD_HEIGHT) {
        return false;
    }
    uint8_t index = (uint8_t)(count - 1U);
    while (index > 0U && (int)point->y < tableau_y(state, column, index)) {
        --index;
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

static bool drop_cursor(solitaire_state_t *state, int x, int y)
{
    uint8_t column = 0U;
    if (x < 0 || x >= P4_GAME_SURFACE_WIDTH || y < 0 ||
        !touch_column((uint16_t)x, &column)) return false;
    if (y >= CARD_TOP && y < CARD_TOP + CARD_HEIGHT && column >= 3U) {
        state->cursor_area = CURSOR_TOP;
    } else if (y >= TABLEAU_TOP && y < SOLITAIRE_TABLEAU_BOTTOM) {
        state->cursor_area = CURSOR_TABLEAU;
    } else return false;
    state->cursor_column = column;
    state->cursor_depth = 0U;
    return true;
}

static bool legal_destination(const solitaire_state_t *state,
                              bool foundation, uint8_t pile)
{
    uint8_t card = 0U, count = 0U;
    if (!selected_card(state, &card, &count)) return false;
    if (foundation) return pile < SOLITAIRE_SUITS && count == 1U &&
        card_suit(card) == pile && card_rank(card) == state->foundations[pile];
    return pile < SOLITAIRE_PILES &&
        state->tableau_count[pile] + count <= SOLITAIRE_TABLEAU_MAX &&
        !(state->selected_source == SOURCE_TABLEAU && state->selected_pile == pile) &&
        can_place_tableau(state, pile, card);
}

static void cancel_gesture(solitaire_state_t *state)
{
    if (state->dragging) cancel_selection(state);
    state->gesture_active = false;
    state->gesture_moved = false;
    state->dragging = false;
    state->press_action = TOUCH_NONE;
}

static void press_touch(solitaire_state_t *state, const p4_game_point_t *point)
{
    cancel_gesture(state);
    state->keyboard_focus = false;
    state->press_x = state->touch_x = (int16_t)point->x;
    state->press_y = state->touch_y = (int16_t)point->y;
    state->gesture_active = true;
    if (point->y < 24U) {
        if (point->x < 52U) state->press_action = TOUCH_EXIT;
        else if (point->x >= 268U) state->press_action = TOUCH_NEW_DEAL;
        return;
    }
    if (!cursor_from_touch(state, point)) return;
    if (state->cursor_area == CURSOR_TOP && state->cursor_column == 0U) {
        state->press_action = TOUCH_STOCK;
        return;
    }
    state->press_action = TOUCH_CARD;
    const solitaire_source_kind_t previous_source = state->selected_source;
    const uint8_t previous_pile = state->selected_pile;
    const uint8_t previous_index = state->selected_index;
    cancel_selection(state);
    begin_selection(state);
    state->press_source = state->selected_source;
    state->press_pile = state->selected_pile;
    state->press_index = state->selected_index;
    state->drag_offset_x = (int16_t)((int)point->x - card_x(state->cursor_column));
    state->drag_offset_y = (int16_t)((int)point->y - selected_y(state));
    state->selected_source = previous_source;
    state->selected_pile = previous_pile;
    state->selected_index = previous_index;
}

static void hold_touch(solitaire_state_t *state, const p4_game_point_t *point)
{
    if (!state->gesture_active) return;
    state->touch_x = (int16_t)point->x;
    state->touch_y = (int16_t)point->y;
    const int dx = state->touch_x - state->press_x;
    const int dy = state->touch_y - state->press_y;
    if (dx * dx + dy * dy >= 16) state->gesture_moved = true;
    if (!state->dragging && state->gesture_moved &&
        state->press_action == TOUCH_CARD && state->press_source != SOURCE_NONE) {
        state->selected_source = state->press_source;
        state->selected_pile = state->press_pile;
        state->selected_index = state->press_index;
        state->dragging = true;
    }
}

static p4_game_result_t release_gesture(p4_game_context_t *context,
                                       solitaire_state_t *state)
{
    if (!state->gesture_active) return P4_GAME_CONTINUE;
    if (state->dragging) {
        if (drop_cursor(state, state->touch_x, state->touch_y) &&
            legal_destination(state, state->cursor_area == CURSOR_TOP,
                state->cursor_area == CURSOR_TOP ?
                (uint8_t)(state->cursor_column - 3U) : state->cursor_column)) {
            activate_cursor(context, state);
        } else {
            tone(context, 180U, 40U, 1U);
        }
        cancel_selection(state);
    } else if (!state->gesture_moved) {
        if (state->press_action == TOUCH_EXIT) return P4_GAME_EXIT_TO_LAUNCHER;
        if (state->press_action == TOUCH_NEW_DEAL) {
            solitaire_reset(state);
            tone(context, 523U, 100U, 2U);
        } else if (state->press_action == TOUCH_CARD ||
                   state->press_action == TOUCH_STOCK) {
            const p4_game_point_t point = {
                .x = (uint16_t)state->press_x, .y = (uint16_t)state->press_y};
            activate_touch(context, state, &point);
        }
    }
    cancel_gesture(state);
    return P4_GAME_CONTINUE;
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
    const bool had_touch = state->touch_was_down;
    const bool touch_down = input->touch_valid && input->touch_count > 0U;
    state->touch_was_down = touch_down;
    /* The shared mapper also synthesizes virtual buttons from touch. Consume
     * the complete gesture here so a card crossing their old regions cannot
     * activate A/B, move the cursor, restart, or exit the game. */
    if (touch_down) {
        if (input->touch_count != 1U || input->touches[0].x >= 320U ||
            input->touches[0].y >= 200U) {
            cancel_gesture(state);
        } else if (!had_touch) {
            press_touch(state, &input->touches[0]);
        } else {
            hold_touch(state, &input->touches[0]);
        }
        return P4_GAME_CONTINUE;
    }
    if (had_touch) {
        if (!input->touch_valid) {
            cancel_gesture(state);
            return P4_GAME_CONTINUE;
        }
        return release_gesture(context, state);
    }
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    if ((input->pressed & P4_BUTTON_START) != 0U) {
        solitaire_reset(state);
        tone(context, 523U, 100U, 2U);
        return P4_GAME_CONTINUE;
    }
    if (input->pressed != 0U) state->keyboard_focus = true;
    move_cursor(state, input->pressed);
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

static int tableau_overlap(const solitaire_state_t *state, uint8_t pile)
{
    const int count = state->tableau_count[pile];
    const int hidden = state->face_up_from[pile];
    const int exposed_steps = count - hidden - 1;
    if (exposed_steps <= 0) return 14;
    int overlap = (SOLITAIRE_TABLEAU_BOTTOM - TABLEAU_TOP - CARD_HEIGHT -
                   hidden * 3) / exposed_steps;
    if (overlap > 14) overlap = 14;
    if (overlap < 2) overlap = 2;
    return overlap;
}

static int tableau_y(const solitaire_state_t *state, uint8_t pile, uint8_t index)
{
    const int hidden = state->face_up_from[pile];
    return TABLEAU_TOP + (index < hidden ? (int)index * 3 :
        hidden * 3 + ((int)index - hidden) * tableau_overlap(state, pile));
}

static void draw_card_back(p4_game_surface_t *surface, int x, int y)
{
    p4_card_back(surface,x,y,CARD_WIDTH,CARD_HEIGHT,card_table_back,false);
}

static void draw_card(p4_game_surface_t *surface,int x,int y,uint8_t card)
{
    static const unsigned suits[4]={3U,2U,1U,0U};
    p4_card_face(surface,x,y,CARD_WIDTH,CARD_HEIGHT,
                 (unsigned)card_rank(card)+1U,suits[card_suit(card)],false);
}

/* Covered cards get a rank strip that fits the available fan exposure.
 * Very deep piles retain their color stripe; the open top-row focus slot
 * shows the complete card selected by Up/Down. */
static void draw_fan_rank(p4_game_surface_t *surface,int x,int y,uint8_t card,int overlap)
{
    if(overlap<5)return;
    static const unsigned suits[4]={3U,2U,1U,0U};
    static const char ranks[13][3]={"A","2","3","4","5","6","7","8","9","10","J","Q","K"};
    const int left=p4_ui_x(surface,x),top=p4_ui_y(surface,y);
    const int width=p4_ui_x(surface,CARD_WIDTH),height=p4_ui_y(surface,overlap);
    p4_ui_round_rect(surface,left+1,top+1,width-2,height,2,UINT16_C(0xffde));
    const unsigned font=(unsigned)(height>18?18:height);
    const uint16_t ink=suit_is_red(card_suit(card))?UINT16_C(0xb904):UINT16_C(0x18e5);
    p4_ui_text(surface,left+3,top,ranks[card_rank(card)],ink,font,2U);
    const int size=height>12?height-4:height-2;
    p4_card_suit(surface,left+width-size-3,top+1,size,suits[card_suit(card)],ink);
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
    return tableau_y(state, state->selected_pile, state->selected_index);
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
        y = tableau_y(state, state->cursor_column, index);
    }
    p4_draw_rect(surface, card_x(state->cursor_column) - 1, y - 1,
                 CARD_WIDTH + 2, CARD_HEIGHT + 2, UINT16_C(0xFFE0));
}

static void draw_centered_hint(p4_game_surface_t *surface, int y,
                               const char *text, uint16_t color, size_t length)
{
    if (surface->width == P4_GAME_SURFACE_WIDTH) {
        p4_draw_text(surface, (320 - (int)length * 6) / 2, y,
                     text, color, 1U, length);
    } else {
        const unsigned font = (unsigned)p4_ui_y(surface, 9);
        p4_ui_text(surface, ((int)surface->width -
            p4_ui_text_width(text, font, length)) / 2, p4_ui_y(surface, y),
            text, color, font, length);
    }
}

static void draw_actions(p4_game_surface_t *surface, const solitaire_state_t *state)
{
    static const int left[2] = {0, 268};
    static const char *const labels[2] = {"Exit", "New deal"};
    for (unsigned i = 0U; i < 2U; ++i) {
        const int x = p4_ui_x(surface, left[i]);
        const int w = p4_ui_x(surface, 52), h = p4_ui_y(surface, 24);
        const bool active = state->gesture_active && !state->gesture_moved &&
            state->press_action == (i == 0U ? TOUCH_EXIT : TOUCH_NEW_DEAL);
        p4_ui_round_rect(surface, x + 2, 2, w - 4, h - 4, 6,
                         active ? UINT16_C(0xbdcf) : UINT16_C(0x19e9));
        const unsigned font = (unsigned)p4_ui_y(surface, 8);
        const int tw = p4_ui_text_width(labels[i], font, 8U);
        p4_ui_text(surface, x + (w - tw) / 2, (h - (int)font) / 2,
                   labels[i], active ? UINT16_C(0x0844) : UINT16_C(0xf7ba), font, 8U);
    }
}

static void draw_drop_targets(const solitaire_state_t *state, p4_game_surface_t *surface)
{
    if (!state->dragging) return;
    uint8_t hover = 0U;
    const bool column_hit = touch_column((uint16_t)state->touch_x, &hover);
    for (uint8_t pile = 0U; pile < SOLITAIRE_PILES; ++pile) {
        if (!legal_destination(state, false, pile)) continue;
        const bool active = column_hit && hover == pile && state->touch_y >= TABLEAU_TOP &&
            state->touch_y < SOLITAIRE_TABLEAU_BOTTOM;
        const uint8_t count = state->tableau_count[pile];
        const int top = count == 0U ? TABLEAU_TOP : tableau_y(state, pile, (uint8_t)(count - 1U));
        p4_draw_rect(surface, card_x(pile) - 2, top - 2, CARD_WIDTH + 4,
                     CARD_HEIGHT + 4, active ? UINT16_C(0xffe0) : UINT16_C(0x07f4));
        if (active) p4_draw_rect(surface, card_x(pile) - 3, top - 3,
                                CARD_WIDTH + 6, CARD_HEIGHT + 6, UINT16_C(0xffe0));
    }
    for (uint8_t suit = 0U; suit < SOLITAIRE_SUITS; ++suit) {
        if (!legal_destination(state, true, suit)) continue;
        const bool active = column_hit && hover == suit + 3U &&
            state->touch_y >= CARD_TOP && state->touch_y < CARD_TOP + CARD_HEIGHT;
        p4_draw_rect(surface, card_x((uint8_t)(suit + 3U)) - 2, CARD_TOP - 2,
                     CARD_WIDTH + 4, CARD_HEIGHT + 4,
                     active ? UINT16_C(0xffe0) : UINT16_C(0x07f4));
    }
}

static void draw_drag(const solitaire_state_t *state, p4_game_surface_t *surface)
{
    uint8_t card = 0U, count = 0U;
    if (!state->dragging || !selected_card(state, &card, &count)) return;
    int overlap = count > 1U ? (160 - CARD_HEIGHT) / ((int)count - 1) : 0;
    if (overlap > 14) overlap = 14;
    const int height = CARD_HEIGHT + ((int)count - 1) * overlap;
    int x = state->touch_x - state->drag_offset_x;
    int y = state->touch_y - state->drag_offset_y;
    if (x < 2) x = 2;
    if (x > 318 - CARD_WIDTH) x = 318 - CARD_WIDTH;
    if (y < 25) y = 25;
    if (y > 197 - height) y = 197 - height;
    for (uint8_t index = 0U; index < count; ++index) {
        const uint8_t moving = state->selected_source == SOURCE_TABLEAU ?
            state->tableau[state->selected_pile][state->selected_index + index] : card;
        draw_card(surface, x, y + (int)index * overlap, moving);
        if (index + 1U < count) draw_fan_rank(surface, x, y + (int)index * overlap, moving, overlap);
    }
    p4_draw_rect(surface, x - 1, y - 1, CARD_WIDTH + 2, height + 2, UINT16_C(0xffe0));
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const solitaire_state_t *const state = context->state;
    p4_draw_clear(surface, UINT16_C(0x0844));
    p4_card_felt(surface,0,24,320,176,card_table_felt);
    p4_draw_fill_rect(surface,0,18,320,1,UINT16_C(0x8c08));
    draw_centered_hint(surface, 7, "SOLITAIRE", UINT16_C(0xffff), 9U);

    if (state->stock_count > 0U) {
        draw_card_back(surface, card_x(0U), CARD_TOP);
    } else {
        draw_empty_slot(surface, card_x(0U), CARD_TOP);
        p4_draw_text(surface, card_x(0U) + 9, CARD_TOP + 17,
                     "R", UINT16_C(0xFFFF), 1U, 1U);
    }
    const uint8_t visible_waste = (uint8_t)(state->waste_count -
        (state->dragging && state->selected_source == SOURCE_WASTE ? 1U : 0U));
    if (visible_waste > 0U) {
        draw_card(surface, card_x(1U), CARD_TOP,
                  state->waste[visible_waste - 1U]);
    } else {
        draw_empty_slot(surface, card_x(1U), CARD_TOP);
    }
    for (uint8_t suit = 0U; suit < SOLITAIRE_SUITS; ++suit) {
        const int x = card_x((uint8_t)(suit + 3U));
        const uint8_t visible_count = (uint8_t)(state->foundations[suit] -
            (state->dragging && state->selected_source == SOURCE_FOUNDATION &&
             state->selected_pile == suit ? 1U : 0U));
        if (visible_count == 0U) {
            draw_empty_slot(surface, x, CARD_TOP);
            static const unsigned suits[4]={3U,2U,1U,0U};
            p4_card_suit(surface,p4_ui_x(surface,x+11),p4_ui_y(surface,CARD_TOP+8),
                         p4_ui_y(surface,14),suits[suit],UINT16_C(0x6510));
        } else {
            const uint8_t card = (uint8_t)(suit * 13U +
                visible_count - 1U);
            draw_card(surface, x, CARD_TOP, card);
        }
    }

    for (uint8_t pile = 0U; pile < SOLITAIRE_PILES; ++pile) {
        const uint8_t count = state->dragging && state->selected_source == SOURCE_TABLEAU &&
            state->selected_pile == pile ? state->selected_index : state->tableau_count[pile];
        if (count == 0U) {
            draw_empty_slot(surface, card_x(pile), TABLEAU_TOP);
            continue;
        }
        const int overlap = tableau_overlap(state, pile);
        for (uint8_t index = 0U; index < count; ++index) {
            const int y = tableau_y(state, pile, index);
            if (index < state->face_up_from[pile]) {
                draw_card_back(surface, card_x(pile), y);
                /* A navy/gold edge distinguishes the compressed covered cards
                 * from the cream rank strips of exposed cards. */
                p4_draw_fill_rect(surface, card_x(pile) + 2, y + 1,
                                  CARD_WIDTH - 4, 2, UINT16_C(0x1929));
                p4_draw_fill_rect(surface, card_x(pile) + 4, y + 1,
                                  CARD_WIDTH - 8, 1, UINT16_C(0xa446));
            } else {
                draw_card(surface, card_x(pile), y,
                          state->tableau[pile][index]);
                if(index+1U<count)draw_fan_rank(surface,card_x(pile),y,
                    state->tableau[pile][index],overlap);
            }
        }
    }
    if (!state->dragging) draw_selection(state, surface);
    if (state->keyboard_focus && !state->dragging) draw_cursor(state, surface);
    draw_drop_targets(state, surface);
    p4_game_feedback_draw_audio_effect(
        surface, &state->audio,
        p4_ui_x(surface,state->won ? 160 : card_x(state->cursor_column) + CARD_WIDTH / 2),
        p4_ui_y(surface,state->won ? 100 :
        (state->cursor_area == CURSOR_TOP ? CARD_TOP + CARD_HEIGHT / 2 : 100)));
    if (state->won) {
        p4_draw_fill_rect(surface, 70, 76, 180, 48, UINT16_C(0x0010));
        p4_draw_rect(surface, 70, 76, 180, 48, UINT16_C(0xFFE0));
        p4_draw_text(surface, 106, 88, "YOU WON!", UINT16_C(0xFFE0), 2U, 8U);
        p4_draw_text(surface, 98, 111, "TAP NEW DEAL",
                     UINT16_C(0xFFFF), 1U, 16U);
    }
    draw_actions(surface, state);
    if (!state->dragging) {
        uint8_t preview = 0U, preview_count = 0U;
        bool show_preview = selected_card(state, &preview, &preview_count);
        if (!show_preview && state->cursor_area == CURSOR_TABLEAU &&
            (state->keyboard_focus || (state->gesture_active && state->press_action == TOUCH_CARD))) {
            const uint8_t count = state->tableau_count[state->cursor_column];
            if (count > state->cursor_depth) {
                const uint8_t index = (uint8_t)(count - 1U - state->cursor_depth);
                if (index >= state->face_up_from[state->cursor_column]) {
                    preview = state->tableau[state->cursor_column][index];
                    show_preview = true;
                }
            }
        }
        if (show_preview) draw_card(surface, card_x(2U), CARD_TOP, preview);
    }
    if (!state->dragging) draw_centered_hint(surface, 191,
        "DRAG CARDS OR TAP TO MOVE", UINT16_C(0xbdf7), 25U);
    draw_drag(state, surface);
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
    .title = "Solitaire",
    .subtitle = "Classic Klondike patience",
    .accent_rgb565 = UINT16_C(0x07E0),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
                             P4_GAME_CAP_AUDIO_STREAM | P4_GAME_CAP_VIDEO_HIGH_RES,
    .state_bytes = sizeof(solitaire_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
