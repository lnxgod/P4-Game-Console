// SPDX-License-Identifier: MIT

#include "checkers_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/game.h"

enum {
    COLOR_BACKGROUND = 0x0844,
    COLOR_PANEL = 0x10c7,
    COLOR_PANEL_EDGE = 0x3a4d,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xad55,
    COLOR_ACCENT = 0x5fea,
    COLOR_LIGHT_SQUARE = 0xde94,
    COLOR_DARK_SQUARE = 0x7224,
    COLOR_DARK_HIGHLIGHT = 0x9b27,
    COLOR_RED = 0xe1e7,
    COLOR_RED_EDGE = 0x7800,
    COLOR_WHITE = 0xffdf,
    COLOR_WHITE_EDGE = 0x8410,
    COLOR_GOLD = 0xfe60,
    COLOR_CURSOR = 0x07ff,
    COLOR_LEGAL = 0x87f0,
    COLOR_DANGER = 0xf986,
    BOARD_X = 6,
    BOARD_Y = 28,
    BOARD_CELL = 20,
    BOARD_PIXELS = CHECKERS_BOARD_SIDE * BOARD_CELL,
    PANEL_X = 174,
    EXIT_X = 272,
    EXIT_Y = 3,
    EXIT_W = 44,
    EXIT_H = 19,
    RESTART_X = 181,
    RESTART_Y = 145,
    RESTART_W = 130,
    RESTART_H = 24,
};

static bool point_in(uint16_t point_x, uint16_t point_y,
                     int x, int y, int width, int height)
{
    return (int)point_x >= x && (int)point_x < x + width &&
        (int)point_y >= y && (int)point_y < y + height;
}

static bool board_flipped(const checkers_state_t *state)
{
    return state->network_mode &&
        state->local_player_slot == CHECKERS_PLAYER_WHITE;
}

static uint8_t screen_position_to_square(const checkers_state_t *state,
                                         int screen_row, int screen_column)
{
    int row = screen_row;
    int column = screen_column;
    if (board_flipped(state)) {
        row = 7 - row;
        column = 7 - column;
    }
    return (uint8_t)(row * CHECKERS_BOARD_SIDE + column);
}

static void square_to_screen_position(const checkers_state_t *state,
                                      uint8_t square,
                                      int *screen_row, int *screen_column)
{
    int row = (int)square / CHECKERS_BOARD_SIDE;
    int column = (int)square % CHECKERS_BOARD_SIDE;
    if (board_flipped(state)) {
        row = 7 - row;
        column = 7 - column;
    }
    *screen_row = row;
    *screen_column = column;
}

static bool touch_to_square(const checkers_state_t *state,
                            uint16_t x, uint16_t y, uint8_t *square_out)
{
    if (!point_in(x, y, BOARD_X, BOARD_Y, BOARD_PIXELS, BOARD_PIXELS)) {
        return false;
    }
    const int screen_column = ((int)x - BOARD_X) / BOARD_CELL;
    const int screen_row = ((int)y - BOARD_Y) / BOARD_CELL;
    *square_out = screen_position_to_square(
        state, screen_row, screen_column);
    return true;
}

static void move_cursor(checkers_state_t *state, uint32_t pressed)
{
    int row = 0;
    int column = 0;
    square_to_screen_position(state, state->cursor, &row, &column);
    if ((pressed & P4_BUTTON_LEFT) != 0U && column > 0) {
        --column;
    }
    if ((pressed & P4_BUTTON_RIGHT) != 0U && column < 7) {
        ++column;
    }
    if ((pressed & P4_BUTTON_UP) != 0U && row > 0) {
        --row;
    }
    if ((pressed & P4_BUTTON_DOWN) != 0U && row < 7) {
        ++row;
    }
    state->cursor = screen_position_to_square(state, row, column);
}

static void play_move_event(p4_game_context_t *context, uint8_t flags)
{
    if ((flags & CHECKERS_MOVE_GAME_OVER) != 0U) {
        (void)p4_game_play_tone(context, 523U, 90U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 784U, 150U, 4U, P4_WAVE_TRIANGLE);
    } else if ((flags & CHECKERS_MOVE_CROWNED) != 0U) {
        (void)p4_game_play_tone(context, 659U, 75U, 4U, P4_WAVE_TRIANGLE);
        (void)p4_game_play_tone(context, 988U, 100U, 4U, P4_WAVE_TRIANGLE);
    } else if ((flags & CHECKERS_MOVE_CAPTURED) != 0U) {
        (void)p4_game_play_tone(context, 294U, 55U, 4U, P4_WAVE_SQUARE);
        (void)p4_game_play_tone(context, 440U, 75U, 3U, P4_WAVE_TRIANGLE);
    } else {
        (void)p4_game_play_tone(context, 392U, 38U, 2U, P4_WAVE_TRIANGLE);
    }
}

static bool piece_is_selectable(const checkers_state_t *state,
                                uint8_t square)
{
    if (!checkers_local_turn(state) || square >= CHECKERS_BOARD_SQUARES ||
        !checkers_piece_belongs_to(state->board[square],
                                   state->current_player)) {
        return false;
    }
    if (state->forced_piece != CHECKERS_NO_SQUARE) {
        return state->forced_piece == square;
    }
    return !checkers_player_has_capture(state, state->current_player) ||
        checkers_piece_can_capture(state, square);
}

static void handle_square_action(p4_game_context_t *context,
                                 checkers_state_t *state, uint8_t square)
{
    if (!checkers_local_turn(state)) {
        return;
    }
    state->cursor = square;
    if (state->selected == CHECKERS_NO_SQUARE) {
        if (piece_is_selectable(state, square)) {
            state->selected = square;
            (void)p4_game_play_tone(
                context, 587U, 35U, 2U, P4_WAVE_TRIANGLE);
        }
        return;
    }
    if (square == state->selected) {
        if (state->forced_piece == CHECKERS_NO_SQUARE) {
            state->selected = CHECKERS_NO_SQUARE;
        }
        return;
    }
    if (piece_is_selectable(state, square) &&
        state->forced_piece == CHECKERS_NO_SQUARE) {
        state->selected = square;
        return;
    }
    uint8_t flags = 0U;
    if (checkers_perform_move(
            context, state, state->selected, square, &flags) &&
        (!state->network_mode ||
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_CLIENT)) {
        play_move_event(context, flags);
    }
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

static void count_text(char text[12], const char *label, unsigned count)
{
    size_t length = 0U;
    while (label[length] != '\0' && length + 1U < 12U) {
        text[length] = label[length];
        ++length;
    }
    text[length] = '\0';
    (void)append_unsigned(text, 12U, length, count);
}

static void draw_piece(p4_game_surface_t *surface,
                       int center_x, int center_y, uint8_t piece)
{
    const bool red = checkers_piece_belongs_to(
        piece, CHECKERS_PLAYER_RED);
    const uint16_t edge = red ? COLOR_RED_EDGE : COLOR_WHITE_EDGE;
    const uint16_t fill = red ? COLOR_RED : COLOR_WHITE;
    p4_draw_fill_circle(surface, center_x, center_y, 8, edge);
    p4_draw_fill_circle(surface, center_x, center_y, 6, fill);
    p4_draw_fill_circle(surface, center_x - 2, center_y - 2, 2,
                        red ? UINT16_C(0xf3ae) : UINT16_C(0xffff));
    if (piece == CHECKERS_RED_KING || piece == CHECKERS_WHITE_KING) {
        p4_draw_fill_circle(surface, center_x, center_y + 1, 3, COLOR_GOLD);
        p4_draw_fill_circle(surface, center_x - 4, center_y - 2, 2,
                            COLOR_GOLD);
        p4_draw_fill_circle(surface, center_x + 4, center_y - 2, 2,
                            COLOR_GOLD);
    }
}

static void draw_board(p4_game_surface_t *surface,
                       const checkers_state_t *state)
{
    p4_draw_rect(surface, BOARD_X - 2, BOARD_Y - 2,
                 BOARD_PIXELS + 4, BOARD_PIXELS + 4, COLOR_PANEL_EDGE);
    for (int screen_row = 0; screen_row < CHECKERS_BOARD_SIDE; ++screen_row) {
        for (int screen_column = 0; screen_column < CHECKERS_BOARD_SIDE;
             ++screen_column) {
            const uint8_t square = screen_position_to_square(
                state, screen_row, screen_column);
            const int x = BOARD_X + screen_column * BOARD_CELL;
            const int y = BOARD_Y + screen_row * BOARD_CELL;
            uint16_t color = ((screen_row + screen_column) & 1) == 0
                ? COLOR_LIGHT_SQUARE : COLOR_DARK_SQUARE;
            if (square == state->selected) {
                color = COLOR_DARK_HIGHLIGHT;
            }
            p4_draw_fill_rect(surface, x, y, BOARD_CELL, BOARD_CELL, color);
            if (state->selected != CHECKERS_NO_SQUARE &&
                checkers_move_is_legal(state, state->selected, square)) {
                p4_draw_fill_circle(surface, x + BOARD_CELL / 2,
                                    y + BOARD_CELL / 2, 3, COLOR_LEGAL);
            }
            const uint8_t piece = state->board[square];
            if (piece != CHECKERS_EMPTY) {
                draw_piece(surface, x + BOARD_CELL / 2,
                           y + BOARD_CELL / 2, piece);
            }
            if (square == state->selected) {
                p4_draw_rect(surface, x + 1, y + 1,
                             BOARD_CELL - 2, BOARD_CELL - 2, COLOR_GOLD);
            }
            if (square == state->cursor) {
                p4_draw_rect(surface, x, y, BOARD_CELL, BOARD_CELL,
                             COLOR_CURSOR);
                p4_draw_rect(surface, x + 1, y + 1,
                             BOARD_CELL - 2, BOARD_CELL - 2, COLOR_CURSOR);
            }
        }
    }
}

static void draw_status(p4_game_surface_t *surface,
                        const checkers_state_t *state)
{
    char count[12];
    p4_draw_fill_rect(surface, PANEL_X, 28, 142, 160, COLOR_PANEL);
    p4_draw_rect(surface, PANEL_X, 28, 142, 160, COLOR_PANEL_EDGE);

    count_text(count, "RED ", state->red_count);
    p4_draw_text(surface, 181, 35, count, COLOR_RED, 1U, 11U);
    count_text(count, "WHITE ", state->white_count);
    p4_draw_text(surface, 245, 35, count, COLOR_WHITE, 1U, 11U);

    if (state->phase == CHECKERS_PHASE_GAME_OVER) {
        if (state->winner == CHECKERS_WINNER_DRAW) {
            p4_draw_text(surface, 198, 58, "DRAW GAME",
                         COLOR_GOLD, 1U, 12U);
        } else if (state->winner == CHECKERS_PLAYER_RED) {
            p4_draw_text(surface, 200, 58, "RED WINS!",
                         COLOR_RED, 1U, 12U);
        } else {
            p4_draw_text(surface, 194, 58, "WHITE WINS!",
                         COLOR_WHITE, 1U, 12U);
        }
        p4_draw_text(surface, 190, 78, "START OR TAP",
                     COLOR_TEXT, 1U, 14U);
        p4_draw_fill_rect(surface, RESTART_X, RESTART_Y,
                          RESTART_W, RESTART_H, COLOR_ACCENT);
        p4_draw_rect(surface, RESTART_X, RESTART_Y,
                     RESTART_W, RESTART_H, COLOR_TEXT);
        p4_draw_text(surface, 210, 153, "NEW MATCH",
                     COLOR_BACKGROUND, 1U, 10U);
        return;
    }

    if (state->network_mode && !state->network_started) {
        p4_draw_text(surface, 205, 57, "SYNCING...",
                     COLOR_GOLD, 1U, 12U);
    } else if (state->network_mode &&
               state->current_player != state->local_player_slot) {
        p4_draw_text(surface, 204, 57, "THEIR TURN",
                     COLOR_MUTED, 1U, 12U);
    } else if (state->network_mode) {
        p4_draw_text(surface, 207, 57, "YOUR TURN",
                     COLOR_ACCENT, 1U, 12U);
    } else if (state->current_player == CHECKERS_PLAYER_RED) {
        p4_draw_text(surface, 202, 57, "RED'S TURN",
                     COLOR_RED, 1U, 12U);
    } else {
        p4_draw_text(surface, 194, 57, "WHITE'S TURN",
                     COLOR_WHITE, 1U, 13U);
    }

    if (state->forced_piece != CHECKERS_NO_SQUARE) {
        p4_draw_text(surface, 200, 75, "JUMP AGAIN!",
                     COLOR_GOLD, 1U, 12U);
    } else if (checkers_player_has_capture(state, state->current_player)) {
        p4_draw_text(surface, 204, 75, "CAPTURE!",
                     COLOR_GOLD, 1U, 10U);
    }
    if (state->network_request_pending) {
        p4_draw_text(surface, 206, 91, "SENDING...",
                     COLOR_MUTED, 1U, 11U);
    } else if (state->peer_lost_fallback) {
        p4_draw_text(surface, 190, 91, "LINK LOST: LOCAL",
                     COLOR_DANGER, 1U, 16U);
    } else if (state->network_mode) {
        const char *const side = state->local_player_slot ==
                CHECKERS_PLAYER_RED
            ? "YOU ARE RED" : "YOU ARE WHITE";
        p4_draw_text(surface, 194, 91, side,
                     COLOR_MUTED, 1U, 13U);
    } else {
        p4_draw_text(surface, 198, 91, "PASS & PLAY",
                     COLOR_MUTED, 1U, 13U);
    }

    p4_draw_text(surface, 183, 116, "ARROWS  MOVE",
                 COLOR_TEXT, 1U, 14U);
    p4_draw_text(surface, 183, 130, "A  SELECT/MOVE",
                 COLOR_TEXT, 1U, 16U);
    p4_draw_text(surface, 183, 144, "B  CANCEL",
                 COLOR_TEXT, 1U, 11U);
    p4_draw_text(surface, 183, 166, "TAP BOARD",
                 COLOR_MUTED, 1U, 11U);
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(checkers_state_t)) {
        return false;
    }
    checkers_state_t *const state = context->state;
    *state = (checkers_state_t){0};
    checkers_reset_board(state, 1U);
    (void)checkers_network_begin(context, state);
    (void)p4_game_play_tone(context, 523U, 65U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(p4_game_context_t *context,
                                    const p4_game_input_t *input,
                                    uint32_t elapsed_ms)
{
    if (context == NULL || context->state == NULL || input == NULL) {
        return P4_GAME_ERROR;
    }
    checkers_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }
    state->held_buttons = input->held;
    checkers_network_poll(context, state, elapsed_ms);

    const bool touch_down = input->touch_valid && input->touch_count != 0U;
    if (touch_down && !state->touch_was_down) {
        const uint16_t x = input->touches[0].x;
        const uint16_t y = input->touches[0].y;
        if (point_in(x, y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H)) {
            return P4_GAME_EXIT_TO_LAUNCHER;
        }
        if (state->phase == CHECKERS_PHASE_GAME_OVER &&
            point_in(x, y, RESTART_X, RESTART_Y,
                     RESTART_W, RESTART_H)) {
            (void)checkers_restart(context, state);
        } else {
            uint8_t square = 0U;
            if (touch_to_square(state, x, y, &square)) {
                handle_square_action(context, state, square);
            }
        }
    }
    state->touch_was_down = touch_down;

    if (state->phase == CHECKERS_PHASE_GAME_OVER) {
        if ((input->pressed & P4_BUTTON_START) != 0U) {
            (void)checkers_restart(context, state);
        }
        return P4_GAME_CONTINUE;
    }
    move_cursor(state, input->pressed);
    if ((input->pressed & P4_BUTTON_B) != 0U &&
        state->forced_piece == CHECKERS_NO_SQUARE &&
        !state->network_request_pending) {
        state->selected = CHECKERS_NO_SQUARE;
    }
    if ((input->pressed & P4_BUTTON_A) != 0U) {
        handle_square_action(context, state, state->cursor);
    }
    return P4_GAME_CONTINUE;
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (context == NULL || context->state == NULL ||
        !p4_surface_valid(surface)) {
        return false;
    }
    const checkers_state_t *const state = context->state;
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_draw_text(surface, 8, 7, "CHECKERS", COLOR_TEXT, 2U, 8U);
    p4_draw_text(surface, 178, 8,
                 state->network_mode ? "2-CONSOLE" : "2-PLAYER LOCAL",
                 COLOR_ACCENT, 1U, 15U);
    p4_draw_fill_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_DANGER);
    p4_draw_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_TEXT);
    p4_draw_text(surface, EXIT_X + 6, EXIT_Y + 6, "EXIT",
                 COLOR_TEXT, 1U, 4U);
    draw_board(surface, state);
    draw_status(surface, state);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_checkers_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(116),
    .id = "org.p4console.checkers",
    .title = "Checkers",
    .subtitle = "Jump, crown and capture",
    .accent_rgb565 = COLOR_ACCENT,
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(checkers_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
