// SPDX-License-Identifier: MIT

#ifndef P4_SOLITAIRE_INTERNAL_H
#define P4_SOLITAIRE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "p4/audio_pack.h"

enum {
    SOLITAIRE_PILES = 7,
    SOLITAIRE_SUITS = 4,
    SOLITAIRE_DECK = 52,
    SOLITAIRE_STOCK_MAX = 24,
    SOLITAIRE_TABLEAU_MAX = 19,
    SOLITAIRE_CARD_WIDTH = 40,
    SOLITAIRE_CARD_HEIGHT = 46,
    SOLITAIRE_CARD_TOP = 27,
    SOLITAIRE_TABLEAU_TOP = 79,
    SOLITAIRE_TABLEAU_BOTTOM = 190,
    SOLITAIRE_CARD_GAP_X = 4,
};

typedef enum {
    SOLITAIRE_CURSOR_TOP = 0,
    SOLITAIRE_CURSOR_TABLEAU,
} solitaire_cursor_area_t;

typedef enum {
    SOLITAIRE_SOURCE_NONE = 0,
    SOLITAIRE_SOURCE_WASTE,
    SOLITAIRE_SOURCE_FOUNDATION,
    SOLITAIRE_SOURCE_TABLEAU,
} solitaire_source_kind_t;

typedef struct {
    uint8_t tableau[SOLITAIRE_PILES][SOLITAIRE_TABLEAU_MAX];
    uint8_t tableau_count[SOLITAIRE_PILES];
    uint8_t face_up_from[SOLITAIRE_PILES];
    uint8_t stock[SOLITAIRE_STOCK_MAX];
    uint8_t waste[SOLITAIRE_STOCK_MAX];
    uint8_t foundations[SOLITAIRE_SUITS];
    uint8_t stock_count;
    uint8_t waste_count;
    uint8_t cursor_column;
    uint8_t cursor_depth;
    uint8_t selected_pile;
    uint8_t selected_index;
    solitaire_cursor_area_t cursor_area;
    solitaire_source_kind_t selected_source;
    uint32_t held_buttons;
    uint32_t deal_number;
    uint32_t moves;
    p4_game_audio_effect_player_t audio;
    /* Transient UI only; the rules and card identities remain unchanged. */
    int16_t press_x, press_y, touch_x, touch_y;
    int16_t drag_offset_x, drag_offset_y;
    uint8_t press_action;
    solitaire_source_kind_t press_source;
    uint8_t press_pile, press_index;
    bool gesture_active;
    bool gesture_moved;
    bool dragging;
    bool keyboard_focus;
    bool touch_was_down;
    bool won;
} solitaire_state_t;

void solitaire_reset(solitaire_state_t *state);

#endif
