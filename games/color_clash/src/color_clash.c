// SPDX-License-Identifier: MIT

#include "color_clash_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"

#include "generated/color_clash_card_frames.inc"

enum {
    COLOR_BG = 0x0823,
    COLOR_TABLE = 0x09c8,
    COLOR_TABLE_DARK = 0x0926,
    COLOR_PANEL = 0x18e8,
    COLOR_PANEL_ALT = 0x296b,
    COLOR_LINE = 0x4a4f,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xad55,
    COLOR_ACCENT = 0x5fea,
    COLOR_GOLD = 0xfe60,
    COLOR_DANGER = 0xf986,
    COLOR_RED = 0xf186,
    COLOR_YELLOW = 0xfe20,
    COLOR_TIFFANY = 0x3679,
    COLOR_VIOLET = 0x981f,
    COLOR_BLACK = 0x0000,
    NO_WINNER = 0xff,
    EXIT_X = 2,
    EXIT_Y = 2,
    EXIT_W = 40,
    EXIT_H = 20,
    HAND_X = 8,
    HAND_Y = 146,
    HAND_STEP = 34,
    DRAW_X = 246,
    DRAW_Y = 103,
    DRAW_W = 70,
    DRAW_H = 28,
};

static const uint16_t s_colors[COLOR_CLASH_COLOR_COUNT] = {
    COLOR_RED, COLOR_YELLOW, COLOR_TIFFANY, COLOR_VIOLET,
};

static const char *const s_color_names[COLOR_CLASH_COLOR_COUNT] = {
    "RED", "GOLD", "TIFF", "PURP",
};

static const uint8_t s_color_name_chars[COLOR_CLASH_COLOR_COUNT] = {
    3U, 4U, 4U, 4U,
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

static bool point_in(uint16_t px, uint16_t py,
                     int x, int y, int width, int height)
{
    return (int)px >= x && (int)px < x + width &&
        (int)py >= y && (int)py < y + height;
}

static uint8_t hand_window_start(const color_clash_state_t *state)
{
    const uint8_t count = state->hand_counts[state->local_player_slot];
    if (count <= COLOR_CLASH_VISIBLE_CARDS ||
        state->selected_card < COLOR_CLASH_VISIBLE_CARDS / 2U) {
        return 0U;
    }
    uint8_t start = (uint8_t)(
        state->selected_card - COLOR_CLASH_VISIBLE_CARDS / 2U);
    const uint8_t maximum = (uint8_t)(count - COLOR_CLASH_VISIBLE_CARDS);
    if (start > maximum) {
        start = maximum;
    }
    return start;
}

static uint8_t next_target(const color_clash_state_t *state,
                           uint8_t target, bool forward)
{
    do {
        target = forward
            ? (uint8_t)((target + 1U) % state->player_count)
            : (uint8_t)((target + state->player_count - 1U) %
                        state->player_count);
    } while (target == state->current_player);
    return target;
}

static void return_to_menu(color_clash_state_t *state)
{
    const uint8_t menu_players = state->menu_players < 2U
        ? 2U : state->menu_players;
    const uint32_t rng = state->rng;
    *state = (color_clash_state_t){
        .menu_players = menu_players,
        .winner = NO_WINNER,
        .phase = COLOR_CLASH_MENU,
        .mode = COLOR_CLASH_PRACTICE,
        .rng = rng,
    };
}

static void start_practice(color_clash_state_t *state)
{
    state->mode = COLOR_CLASH_PRACTICE;
    state->local_player_slot = 0U;
    state->network_role = P4_GAME_MULTIPLAYER_ROLE_NONE;
    color_clash_reset_match(state, state->menu_players, state->rng);
}

static void handle_menu_touch(color_clash_state_t *state,
                              uint16_t x, uint16_t y)
{
    if (point_in(x, y, 48, 116, 58, 28) && state->menu_players > 2U) {
        --state->menu_players;
    } else if (point_in(x, y, 214, 116, 58, 28) &&
               state->menu_players < COLOR_CLASH_MAX_PLAYERS) {
        ++state->menu_players;
    } else if (point_in(x, y, 91, 151, 138, 30)) {
        start_practice(state);
    }
}

static void choose_color_touch(p4_game_context_t *context,
                               color_clash_state_t *state,
                               uint16_t x, uint16_t y)
{
    for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        const int box_x = 42 + (int)color * 61;
        if (point_in(x, y, box_x, 89, 50, 38)) {
            state->selected_color = color;
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_COLOR, color);
            return;
        }
    }
}

static void choose_hand_touch(p4_game_context_t *context,
                              color_clash_state_t *state,
                              uint16_t x, uint16_t y)
{
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        if (player == state->current_player) {
            continue;
        }
        const int box_x = 39 + (int)player * 61;
        if (point_in(x, y, box_x, 86, 50, 44)) {
            state->selected_target = player;
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_HAND, player);
            return;
        }
    }
}

static void handle_turn_touch(p4_game_context_t *context,
                              color_clash_state_t *state,
                              uint16_t x, uint16_t y)
{
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR) {
        choose_color_touch(context, state, x, y);
        return;
    }
    if (state->phase == COLOR_CLASH_CHOOSE_HAND) {
        choose_hand_touch(context, state, x, y);
        return;
    }
    if ((state->phase != COLOR_CLASH_TURN &&
         state->phase != COLOR_CLASH_DRAWN_CARD) ||
        !color_clash_local_turn(state)) {
        return;
    }
    if (point_in(x, y, DRAW_X, DRAW_Y, DRAW_W, DRAW_H)) {
        (void)color_clash_perform_action(
            context, state,
            state->phase == COLOR_CLASH_DRAWN_CARD
                ? COLOR_CLASH_NET_PASS : COLOR_CLASH_NET_DRAW,
            0U);
        return;
    }
    if (y >= HAND_Y - 8 && y < HAND_Y + COLOR_CLASH_FRAME_SMALL_HEIGHT) {
        const uint8_t count = state->hand_counts[state->local_player_slot];
        const uint8_t start = hand_window_start(state);
        for (uint8_t visible = 0U;
             visible < COLOR_CLASH_VISIBLE_CARDS; ++visible) {
            const uint8_t index = (uint8_t)(start + visible);
            if (index >= count) {
                break;
            }
            if (state->phase == COLOR_CLASH_DRAWN_CARD &&
                index != state->selected_card) {
                continue;
            }
            const int card_x = HAND_X + (int)visible * HAND_STEP;
            if (point_in(x, y, card_x, HAND_Y - 8,
                         COLOR_CLASH_FRAME_SMALL_WIDTH, 50)) {
                if (state->selected_card == index) {
                    (void)color_clash_perform_action(
                        context, state, COLOR_CLASH_NET_PLAY, index);
                } else {
                    state->selected_card = index;
                }
                return;
            }
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(color_clash_state_t)) {
        return false;
    }
    color_clash_state_t *const state = context->state;
    *state = (color_clash_state_t){
        .menu_players = 2U,
        .winner = NO_WINNER,
        .phase = COLOR_CLASH_MENU,
        .mode = COLOR_CLASH_PRACTICE,
        .rng = UINT32_C(0x434c4153),
    };
    p4_game_multiplayer_status_t multiplayer;
    if (p4_game_multiplayer_read_status(context, &multiplayer) &&
        multiplayer.state == P4_GAME_MULTIPLAYER_CONNECTED &&
        multiplayer.player_count >= 2U &&
        multiplayer.player_count <= COLOR_CLASH_MAX_PLAYERS &&
        multiplayer.local_player_slot < multiplayer.player_count) {
        state->mode = COLOR_CLASH_NETWORK;
        state->phase = COLOR_CLASH_NETWORK_WAIT;
        state->player_count = multiplayer.player_count;
        state->local_player_slot = multiplayer.local_player_slot;
        state->network_role = multiplayer.role;
        state->network_seed = multiplayer.session_seed;
    }
    (void)p4_game_play_tone(context, 523U, 55U, 3U, P4_WAVE_TRIANGLE);
    (void)p4_game_play_tone(context, 784U, 85U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static void update_menu(color_clash_state_t *state,
                        const p4_game_input_t *input)
{
    if ((input->pressed & (P4_BUTTON_LEFT | P4_BUTTON_UP)) != 0U &&
        state->menu_players > 2U) {
        --state->menu_players;
    }
    if ((input->pressed & (P4_BUTTON_RIGHT | P4_BUTTON_DOWN)) != 0U &&
        state->menu_players < COLOR_CLASH_MAX_PLAYERS) {
        ++state->menu_players;
    }
    if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
        start_practice(state);
    }
}

static void update_choice(p4_game_context_t *context,
                          color_clash_state_t *state,
                          const p4_game_input_t *input)
{
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            state->selected_color = (uint8_t)(
                (state->selected_color + COLOR_CLASH_COLOR_COUNT - 1U) %
                COLOR_CLASH_COLOR_COUNT);
        }
        if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            state->selected_color = (uint8_t)(
                (state->selected_color + 1U) % COLOR_CLASH_COLOR_COUNT);
        }
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_COLOR,
                state->selected_color);
        }
    } else if (state->phase == COLOR_CLASH_CHOOSE_HAND) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            state->selected_target = next_target(
                state, state->selected_target, false);
        }
        if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            state->selected_target = next_target(
                state, state->selected_target, true);
        }
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_HAND,
                state->selected_target);
        }
    }
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    color_clash_state_t *const state = context->state;
    if ((input->pressed & P4_BUTTON_BACK) != 0U) {
        if (state->phase == COLOR_CLASH_MENU ||
            state->mode == COLOR_CLASH_NETWORK) {
            return P4_GAME_EXIT_TO_LAUNCHER;
        }
        return_to_menu(state);
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
    state->notice_ms = elapsed_ms >= state->notice_ms
        ? 0U : state->notice_ms - elapsed_ms;
    if (state->mode == COLOR_CLASH_NETWORK &&
        state->phase != COLOR_CLASH_MENU) {
        color_clash_poll_network(context, state);
    }
    if (input->touch_valid && input->touch_count != 0U &&
        !state->touch_was_down) {
        if (state->phase == COLOR_CLASH_MENU) {
            handle_menu_touch(state, input->touches[0].x,
                              input->touches[0].y);
        } else if (state->phase == COLOR_CLASH_GAME_OVER &&
                   point_in(input->touches[0].x, input->touches[0].y,
                            83, 143, 154, 31)) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_RESTART, 0U);
        } else {
            handle_turn_touch(context, state, input->touches[0].x,
                              input->touches[0].y);
        }
    }
    state->touch_was_down = input->touch_valid && input->touch_count != 0U;

    if (state->phase == COLOR_CLASH_MENU) {
        update_menu(state, input);
        return P4_GAME_CONTINUE;
    }
    if (state->phase == COLOR_CLASH_GAME_OVER) {
        if ((input->pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_RESTART, 0U);
        }
        return P4_GAME_CONTINUE;
    }
    if ((state->mode == COLOR_CLASH_NETWORK && !state->network_started) ||
        state->phase == COLOR_CLASH_NETWORK_WAIT ||
        state->phase == COLOR_CLASH_NETWORK_LOST) {
        return P4_GAME_CONTINUE;
    }
    if (state->mode == COLOR_CLASH_NETWORK && !state->network_started) {
        return P4_GAME_CONTINUE;
    }
    if (state->mode == COLOR_CLASH_PRACTICE &&
        !color_clash_local_turn(state)) {
        color_clash_update_bot(context, state, elapsed_ms);
        return P4_GAME_CONTINUE;
    }
    if (!color_clash_local_turn(state)) {
        return P4_GAME_CONTINUE;
    }
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR ||
        state->phase == COLOR_CLASH_CHOOSE_HAND) {
        update_choice(context, state, input);
        return P4_GAME_CONTINUE;
    }
    if (state->phase == COLOR_CLASH_DRAWN_CARD) {
        if ((input->pressed & P4_BUTTON_A) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PLAY,
                state->selected_card);
        } else if ((input->pressed &
                    (P4_BUTTON_B | P4_BUTTON_START)) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PASS, 0U);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase != COLOR_CLASH_TURN) {
        return P4_GAME_CONTINUE;
    }
    const uint8_t count = state->hand_counts[state->local_player_slot];
    if (count != 0U) {
        if ((input->pressed & P4_BUTTON_LEFT) != 0U) {
            state->selected_card = (uint8_t)(
                (state->selected_card + count - 1U) % count);
        }
        if ((input->pressed & P4_BUTTON_RIGHT) != 0U) {
            state->selected_card = (uint8_t)(
                (state->selected_card + 1U) % count);
        }
        if ((input->pressed & P4_BUTTON_A) != 0U) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PLAY,
                state->selected_card);
        }
    }
    if ((input->pressed & (P4_BUTTON_B | P4_BUTTON_START)) != 0U) {
        (void)color_clash_perform_action(
            context, state, COLOR_CLASH_NET_DRAW, 0U);
    }
    return P4_GAME_CONTINUE;
}

static unsigned frame_for_card(uint8_t card)
{
    const color_clash_rank_t rank = color_clash_card_rank(card);
    if (rank == COLOR_CLASH_WILD ||
        rank == COLOR_CLASH_WILD_DRAW_FOUR) {
        return 4U;
    }
    if (rank == COLOR_CLASH_GAMECHANGER) {
        return 5U;
    }
    return (unsigned)color_clash_card_color(card);
}

static void draw_symbol(p4_game_surface_t *surface, int x, int y,
                        uint8_t card, bool large)
{
    const color_clash_rank_t rank = color_clash_card_rank(card);
    if (rank == COLOR_CLASH_GAMECHANGER) {
        const int logo_w = large ? COLOR_CLASH_LOGO_LARGE_WIDTH
                                 : COLOR_CLASH_LOGO_SMALL_WIDTH;
        const int logo_h = large ? COLOR_CLASH_LOGO_LARGE_HEIGHT
                                 : COLOR_CLASH_LOGO_SMALL_HEIGHT;
        const int card_w = large ? COLOR_CLASH_FRAME_LARGE_WIDTH
                                 : COLOR_CLASH_FRAME_SMALL_WIDTH;
        const int card_h = large ? COLOR_CLASH_FRAME_LARGE_HEIGHT
                                 : COLOR_CLASH_FRAME_SMALL_HEIGHT;
        p4_draw_sprite_rgb565(
            surface, x + (card_w - logo_w) / 2,
            y + (card_h - logo_h) / 2,
            large ? s_color_clash_logo_large : s_color_clash_logo_small,
            (size_t)logo_w, (size_t)logo_h, (size_t)logo_w,
            true, COLOR_CLASH_SPRITE_CHROMA);
        return;
    }
    if (rank == COLOR_CLASH_WILD ||
        rank == COLOR_CLASH_WILD_DRAW_FOUR) {
        const int center_x = x + (large ? 21 : 14);
        const int center_y = y + (large ? 31 : 21);
        const int radius = large ? 3 : 2;
        const int spread_x = large ? 10 : 7;
        const int spread_y = large ? 9 : 7;
        p4_draw_fill_circle(surface, center_x - spread_x,
                            center_y - spread_y,
                            radius, COLOR_RED);
        p4_draw_fill_circle(surface, center_x + spread_x,
                            center_y - spread_y,
                            radius, COLOR_YELLOW);
        p4_draw_fill_circle(surface, center_x - spread_x,
                            center_y + spread_y,
                            radius, COLOR_TIFFANY);
        p4_draw_fill_circle(surface, center_x + spread_x,
                            center_y + spread_y,
                            radius, COLOR_VIOLET);
        const char *const label = rank == COLOR_CLASH_WILD_DRAW_FOUR
            ? "+4" : "W";
        const unsigned label_chars = rank == COLOR_CLASH_WILD_DRAW_FOUR
            ? 2U : 1U;
        const unsigned scale = large ? 2U : 1U;
        const int width = (int)(label_chars * 6U * scale);
        const int text_y = center_y - (large ? 6 : 3);
        p4_draw_text(surface, center_x - width / 2 + 1, text_y + 1,
                     label, COLOR_BLACK, scale, 2U);
        p4_draw_text(surface, center_x - width / 2, text_y,
                     label, COLOR_TEXT, scale, 2U);
        return;
    }
    char label[3] = {'\0', '\0', '\0'};
    if (rank <= COLOR_CLASH_NINE) {
        label[0] = (char)('0' + (uint8_t)rank);
    } else if (rank == COLOR_CLASH_SKIP) {
        label[0] = 'S';
    } else if (rank == COLOR_CLASH_REVERSE) {
        label[0] = 'R';
    } else {
        label[0] = '+';
        label[1] = '2';
    }
    const unsigned label_chars = rank == COLOR_CLASH_DRAW_TWO ? 2U : 1U;
    const unsigned scale = large ? 2U : 1U;
    const int text_width = (int)(label_chars * 6U * scale);
    const int card_width = large ? COLOR_CLASH_FRAME_LARGE_WIDTH
                                 : COLOR_CLASH_FRAME_SMALL_WIDTH;
    const int text_y = y + (large ? 25 : 17);
    p4_draw_text(surface, x + (card_width - text_width) / 2 + 1,
                 text_y + 1, label, COLOR_BLACK, scale, 2U);
    p4_draw_text(surface, x + (card_width - text_width) / 2,
                 text_y, label,
                 color_clash_card_color(card) == COLOR_CLASH_GOLD
                    ? COLOR_BLACK : COLOR_TEXT,
                 scale, 2U);
}

static void draw_card(p4_game_surface_t *surface, int x, int y,
                      uint8_t card, bool large, bool selected)
{
    const unsigned frame = frame_for_card(card);
    const int width = large ? COLOR_CLASH_FRAME_LARGE_WIDTH
                            : COLOR_CLASH_FRAME_SMALL_WIDTH;
    const int height = large ? COLOR_CLASH_FRAME_LARGE_HEIGHT
                             : COLOR_CLASH_FRAME_SMALL_HEIGHT;
    p4_draw_fill_rect(surface, x + 2, y + 3, width, height, COLOR_BLACK);
    p4_draw_sprite_rgb565(
        surface, x, y,
        large ? s_color_clash_frames_large[frame]
              : s_color_clash_frames_small[frame],
        (size_t)width, (size_t)height, (size_t)width,
        true, COLOR_CLASH_SPRITE_CHROMA);
    draw_symbol(surface, x, y, card, large);
    if (selected) {
        p4_draw_rect(surface, x - 2, y - 2, width + 4, height + 4,
                     COLOR_ACCENT);
        p4_draw_rect(surface, x - 1, y - 1, width + 2, height + 2,
                     COLOR_TEXT);
    }
}

static void draw_card_back(p4_game_surface_t *surface, int x, int y)
{
    p4_draw_fill_rect(surface, x + 2, y + 3,
                      COLOR_CLASH_FRAME_LARGE_WIDTH,
                      COLOR_CLASH_FRAME_LARGE_HEIGHT, COLOR_BLACK);
    p4_draw_fill_rect(surface, x, y, COLOR_CLASH_FRAME_LARGE_WIDTH,
                      COLOR_CLASH_FRAME_LARGE_HEIGHT, COLOR_PANEL_ALT);
    p4_draw_rect(surface, x, y, COLOR_CLASH_FRAME_LARGE_WIDTH,
                 COLOR_CLASH_FRAME_LARGE_HEIGHT, COLOR_GOLD);
    p4_draw_rect(surface, x + 3, y + 3,
                 COLOR_CLASH_FRAME_LARGE_WIDTH - 6,
                 COLOR_CLASH_FRAME_LARGE_HEIGHT - 6, COLOR_ACCENT);
    for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        p4_draw_fill_rect(surface, x + 9, y + 14 + (int)color * 8,
                          24, 5, s_colors[color]);
    }
}

static void draw_exit(p4_game_surface_t *surface)
{
    p4_draw_fill_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_DANGER);
    p4_draw_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_GOLD);
    p4_draw_text(surface, 9, 8, "EXIT", COLOR_TEXT, 1U, 4U);
}

static void draw_menu(p4_game_surface_t *surface,
                      const color_clash_state_t *state)
{
    p4_draw_text(surface, 82, 10, "COLOR CLASH", COLOR_TEXT, 2U, 11U);
    p4_draw_text(surface, 100, 33, "ONE PLAYER PER P4",
                 COLOR_MUTED, 1U, 17U);
    for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        draw_card(surface, 56 + (int)color * 52,
                  49 + (int)(color & 1U) * 3,
                  color_clash_make_card(
                      (color_clash_color_t)color,
                      (color_clash_rank_t)(color + 2U)),
                  false, false);
    }
    draw_card(surface, 146, 47,
              color_clash_make_card(
                  COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER),
              false, false);
    p4_draw_text(surface, 89, 99, "SOLO PRACTICE VS BOTS",
                 COLOR_GOLD, 1U, 21U);
    p4_draw_fill_rect(surface, 48, 116, 58, 28, COLOR_PANEL);
    p4_draw_rect(surface, 48, 116, 58, 28, COLOR_LINE);
    p4_draw_text(surface, 70, 126, "-", COLOR_TEXT, 1U, 1U);
    char players[14] = "2 PLAYERS";
    players[0] = (char)('0' + state->menu_players);
    p4_draw_text(surface, 119, 126, players, COLOR_TEXT, 1U, 9U);
    p4_draw_fill_rect(surface, 214, 116, 58, 28, COLOR_PANEL);
    p4_draw_rect(surface, 214, 116, 58, 28, COLOR_LINE);
    p4_draw_text(surface, 237, 126, "+", COLOR_TEXT, 1U, 1U);
    p4_draw_fill_rect(surface, 91, 151, 138, 30, COLOR_ACCENT);
    p4_draw_rect(surface, 91, 151, 138, 30, COLOR_TEXT);
    p4_draw_text(surface, 124, 162, "START PRACTICE",
                 COLOR_BG, 1U, 14U);
    p4_draw_text(surface, 61, 188, "NETWORK PLAY: OPEN P4MP LOBBY",
                 COLOR_MUTED, 1U, 31U);
}

static void draw_player_panels(p4_game_surface_t *surface,
                               const color_clash_state_t *state)
{
    const int available = 273;
    const int width = available / state->player_count;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const int x = 45 + (int)player * width;
        const bool active = player == state->current_player;
        p4_draw_fill_rect(surface, x, 2, width - 2, 20,
                          active ? COLOR_PANEL_ALT : COLOR_PANEL);
        p4_draw_rect(surface, x, 2, width - 2, 20,
                     active ? COLOR_ACCENT : COLOR_LINE);
        char label[10] = "P1  0";
        label[1] = (char)('1' + player);
        if (player == state->local_player_slot) {
            memcpy(label, "YOU 0", 6U);
        } else if (state->mode == COLOR_CLASH_PRACTICE) {
            label[0] = 'B';
        }
        label[4] = '\0';
        (void)append_unsigned(label, sizeof(label), 4U,
                              state->hand_counts[player]);
        p4_draw_text(surface, x + 5, 9, label,
                     active ? COLOR_TEXT : COLOR_MUTED, 1U, 8U);
    }
}

static void draw_status(p4_game_surface_t *surface,
                        const color_clash_state_t *state)
{
    char line[24] = "TURN P1  COLOR";
    line[6] = (char)('1' + state->current_player);
    p4_draw_text(surface, 6, 27, line, COLOR_MUTED, 1U, 14U);
    p4_draw_fill_rect(surface, 123, 25, 18, 10,
                      s_colors[state->active_color]);
    p4_draw_rect(surface, 123, 25, 18, 10, COLOR_TEXT);
    p4_draw_text(surface, 154, 27,
                 state->direction == 0U ? "ORDER >" : "ORDER <",
                 COLOR_MUTED, 1U, 7U);
}

static void draw_hand(p4_game_surface_t *surface,
                      const color_clash_state_t *state)
{
    const uint8_t count = state->hand_counts[state->local_player_slot];
    const uint8_t start = hand_window_start(state);
    for (uint8_t visible = 0U; visible < COLOR_CLASH_VISIBLE_CARDS;
         ++visible) {
        const uint8_t index = (uint8_t)(start + visible);
        if (index >= count) {
            break;
        }
        const bool selected = index == state->selected_card &&
            color_clash_local_turn(state) &&
            (state->phase == COLOR_CLASH_TURN ||
             state->phase == COLOR_CLASH_DRAWN_CARD);
        draw_card(surface, HAND_X + (int)visible * HAND_STEP,
                  HAND_Y - (selected ? 7 : 0),
                  state->hands[state->local_player_slot][index],
                  false, selected);
    }
    if (count > COLOR_CLASH_VISIBLE_CARDS) {
        char range[16] = "CARD 1/1";
        size_t length = append_unsigned(
            range, sizeof(range), 5U, (unsigned)state->selected_card + 1U);
        if (length + 1U < sizeof(range)) {
            range[length++] = '/';
            range[length] = '\0';
        }
        (void)append_unsigned(range, sizeof(range), length, count);
        p4_draw_text(surface, 237, 137, range, COLOR_MUTED, 1U, 15U);
    }
}

static void draw_choice_overlay(p4_game_surface_t *surface,
                                const color_clash_state_t *state)
{
    p4_draw_fill_rect(surface, 27, 49, 266, 91, COLOR_BG);
    p4_draw_rect(surface, 27, 49, 266, 91, COLOR_GOLD);
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR) {
        const color_clash_rank_t top_rank = color_clash_card_rank(
            state->discard[state->discard_count - 1U]);
        const char *const title = top_rank == COLOR_CLASH_WILD_DRAW_FOUR
            ? "DRAW 4: CHOOSE COLOR" : "WILD: CHOOSE COLOR";
        p4_draw_text(surface, top_rank == COLOR_CLASH_WILD_DRAW_FOUR
                         ? 82 : 94,
                     59, title, COLOR_TEXT, 1U,
                     top_rank == COLOR_CLASH_WILD_DRAW_FOUR ? 20U : 18U);
        for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
            const int x = 42 + (int)color * 61;
            p4_draw_fill_rect(surface, x, 89, 50, 38, s_colors[color]);
            p4_draw_rect(surface, x, 89, 50, 38,
                         color == state->selected_color
                            ? COLOR_TEXT : COLOR_LINE);
            const int text_x = x +
                (50 - (int)s_color_name_chars[color] * 6) / 2;
            p4_draw_text(surface, text_x, 104, s_color_names[color],
                         color == COLOR_CLASH_GOLD ||
                         color == COLOR_CLASH_TIFFANY
                            ? COLOR_BLACK : COLOR_TEXT,
                         1U, 4U);
        }
    } else {
        p4_draw_text(surface, 61, 58, "GAMECHANGER: CHOOSE YOUR HAND",
                     COLOR_TEXT, 1U, 29U);
        for (uint8_t player = 0U; player < state->player_count; ++player) {
            if (player == state->current_player) {
                continue;
            }
            const int x = 39 + (int)player * 61;
            p4_draw_fill_rect(surface, x, 86, 50, 44, COLOR_PANEL_ALT);
            p4_draw_rect(surface, x, 86, 50, 44,
                         player == state->selected_target
                            ? COLOR_ACCENT : COLOR_LINE);
            char label[8] = "P1  0";
            label[1] = (char)('1' + player);
            label[4] = '\0';
            (void)append_unsigned(label, sizeof(label), 4U,
                                  state->hand_counts[player]);
            p4_draw_text(surface, x + 8, 102, label, COLOR_TEXT, 1U, 7U);
        }
        p4_draw_text(surface, 53, 132, "YOUR NEW HAND IS TOPPED UP TO 3",
                     COLOR_MUTED, 1U, 31U);
    }
}

static void draw_game_over(p4_game_surface_t *surface,
                           const color_clash_state_t *state)
{
    p4_draw_fill_rect(surface, 35, 42, 250, 139, COLOR_BG);
    p4_draw_rect(surface, 35, 42, 250, 139, COLOR_GOLD);
    draw_card(surface, 55, 67,
              color_clash_make_card(
                  COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER),
              true, false);
    p4_draw_text(surface, 126, 60, "ROUND COMPLETE", COLOR_GOLD, 1U, 14U);
    char winner[18] = "PLAYER 1 WINS";
    winner[7] = (char)('1' + state->winner);
    if (state->winner == state->local_player_slot) {
        memcpy(winner, "YOU WIN!", 9U);
    }
    p4_draw_text(surface, 126, 86, winner, COLOR_TEXT, 2U, 16U);
    p4_draw_fill_rect(surface, 83, 143, 154, 31, COLOR_ACCENT);
    p4_draw_rect(surface, 83, 143, 154, 31, COLOR_TEXT);
    p4_draw_text(surface, 112, 154, "A: PLAY AGAIN", COLOR_BG, 1U, 13U);
}

static bool game_render(p4_game_context_t *context,
                        p4_game_surface_t *surface)
{
    if (!p4_surface_valid(surface)) {
        return false;
    }
    const color_clash_state_t *const state = context->state;
    p4_draw_clear(surface, COLOR_BG);
    if (state->phase == COLOR_CLASH_MENU) {
        draw_menu(surface, state);
        draw_exit(surface);
        return true;
    }
    if (state->phase == COLOR_CLASH_NETWORK_WAIT ||
        state->phase == COLOR_CLASH_NETWORK_LOST) {
        p4_draw_text(surface, 58, 42, "COLOR CLASH P4MP",
                     COLOR_TEXT, 2U, 16U);
        p4_draw_text(surface, 56, 94,
            state->phase == COLOR_CLASH_NETWORK_LOST
                ? "SESSION LOST - BACK TO EXIT"
                : "SYNCING PRIVATE HANDS...",
            state->phase == COLOR_CLASH_NETWORK_LOST
                ? COLOR_DANGER : COLOR_ACCENT,
            1U, 29U);
        p4_draw_text(surface, 51, 132,
                     "HOST-AUTHORITATIVE / OS SESSION",
                     COLOR_MUTED, 1U, 31U);
        draw_exit(surface);
        return true;
    }
    p4_draw_fill_rect(surface, 0, 22, 320, 178, COLOR_TABLE_DARK);
    p4_draw_fill_rect(surface, 3, 38, 314, 101, COLOR_TABLE);
    draw_player_panels(surface, state);
    draw_status(surface, state);
    draw_card(surface, 133, 54,
              state->discard[state->discard_count - 1U], true, false);
    draw_card_back(surface, 192, 54);
    char deck[10] = "DECK 0";
    (void)append_unsigned(deck, sizeof(deck), 5U, state->deck_count);
    p4_draw_text(surface, 194, 119, deck, COLOR_MUTED, 1U, 9U);
    p4_draw_fill_rect(surface, DRAW_X, DRAW_Y, DRAW_W, DRAW_H,
                      color_clash_local_turn(state) &&
                      (state->phase == COLOR_CLASH_TURN ||
                       state->phase == COLOR_CLASH_DRAWN_CARD)
                        ? COLOR_ACCENT : COLOR_PANEL);
    p4_draw_rect(surface, DRAW_X, DRAW_Y, DRAW_W, DRAW_H, COLOR_TEXT);
    p4_draw_text(surface,
                 DRAW_X + (state->phase == COLOR_CLASH_DRAWN_CARD ? 19 : 20),
                 DRAW_Y + 10,
                 state->phase == COLOR_CLASH_DRAWN_CARD ? "PASS" : "DRAW",
                 color_clash_local_turn(state) ? COLOR_BG : COLOR_MUTED,
                 1U, 4U);
    draw_hand(surface, state);
    if (state->notice_ms != 0U) {
        p4_draw_fill_rect(surface, 53, 126, 214, 17, COLOR_DANGER);
        const uint8_t count = state->hand_counts[state->local_player_slot];
        const bool draw_four_notice = count != 0U &&
            state->selected_card < count &&
            color_clash_card_rank(
                state->hands[state->local_player_slot]
                            [state->selected_card]) ==
                COLOR_CLASH_WILD_DRAW_FOUR;
        p4_draw_text(surface, draw_four_notice ? 66 : 70, 132,
                     draw_four_notice
                        ? "DRAW 4 NEEDS NO COLOR MATCH"
                        : "MATCH COLOR OR SYMBOL",
                     COLOR_TEXT, 1U, draw_four_notice ? 27U : 21U);
    } else if (!color_clash_local_turn(state)) {
        p4_draw_text(surface, 25, 132,
            state->mode == COLOR_CLASH_PRACTICE
                ? "BOT IS CHOOSING A CARD..."
                : "WAITING FOR ANOTHER PLAYER...",
            COLOR_GOLD, 1U, 31U);
    } else if (state->phase == COLOR_CLASH_DRAWN_CARD) {
        p4_draw_text(surface, 49, 132,
                     "A PLAY DRAWN CARD  B/START PASS",
                     COLOR_GOLD, 1U, 31U);
    } else {
        p4_draw_text(surface, 12, 132,
                     "LEFT/RIGHT  A PLAY  B/START DRAW",
                     COLOR_MUTED, 1U, 34U);
    }
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR ||
        state->phase == COLOR_CLASH_CHOOSE_HAND) {
        draw_choice_overlay(surface, state);
    } else if (state->phase == COLOR_CLASH_GAME_OVER) {
        draw_game_over(surface, state);
    }
    draw_exit(surface);
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_color_clash_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(114),
    .id = "org.p4console.color-clash",
    .title = "COLOR CLASH",
    .subtitle = "2-4 PLAYER CARD CLASH",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(color_clash_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
