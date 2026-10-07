// SPDX-License-Identifier: MIT

#include "texas_holdem_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "p4/draw.h"
#include "p4/card_art.h"
#include "generated/table_materials.inc"
#include "p4/game.h"
#include "p4/input.h"

enum {
    COLOR_BACKGROUND = 0x0844,
    COLOR_FELT = 0x04e8,
    COLOR_FELT_DARK = 0x02c5,
    COLOR_PANEL = 0x10c7,
    COLOR_PANEL_EDGE = 0x3a4d,
    COLOR_TEXT = 0xffff,
    COLOR_MUTED = 0xad55,
    COLOR_ACCENT = 0x07ec,
    COLOR_GOLD = 0xfe60,
    COLOR_RED = 0xf986,
    COLOR_BLACK = 0x0000,
    COLOR_CARD = 0xffdf,
    COLOR_CARD_BACK = 0x319f,
    EXIT_X = 2,
    EXIT_Y = 2,
    EXIT_W = 42,
    EXIT_H = 14,
    ACTION_X = 27,
    ACTION_Y = 166,
    ACTION_W = 86,
    ACTION_H = 20,
    ACTION_GAP = 3,
    SETUP_ROW_X = 52,
    SETUP_ROW_W = 216,
    SETUP_ROW_H = 24,
    SETUP_STACK_Y = 98,
    SETUP_CPU_Y = 127,
    SETUP_DEAL_Y = 158,
};


/* Layout stays in canonical touch coordinates. Each primitive and glyph draws
 * directly into the negotiated surface; there is no scaled low-res frame. */
#define p4_draw_fill_rect p4_card_fill
#define p4_draw_rect p4_card_outline
#define p4_draw_fill_circle p4_card_circle
#define p4_draw_text p4_card_text

static const char *const s_phase_names[] = {
    "SETUP", "PREFLOP", "FLOP", "TURN", "RIVER", "SHOWDOWN", "CHAMPION",
};

static const char *const s_action_names[TEXAS_HOLDEM_ACTION_COUNT] = {
    "FOLD", "CHECK/CALL", "RAISE +20",
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

static void player_stack_text(char text[16], uint8_t player,
                              uint16_t stack, bool cpu)
{
    size_t length = append_text(text, 16U, 0U, cpu ? "C" : "P");
    length = append_unsigned(text, 16U, length, (unsigned)player + 1U);
    length = append_text(text, 16U, length, "  ");
    (void)append_unsigned(text, 16U, length, stack);
}

static uint8_t cpu_player_count(const texas_holdem_state_t *state)
{
    uint8_t count = 0U;
    uint8_t mask = state->cpu_mask;
    while (mask != 0U) {
        count = (uint8_t)(count + (mask & 1U));
        mask >>= 1U;
    }
    return count;
}

static void amount_text(char text[20], const char *label, uint16_t amount)
{
    size_t length = append_text(text, 20U, 0U, label);
    (void)append_unsigned(text, 20U, length, amount);
}

static char rank_character(uint8_t card)
{
    const uint8_t rank = (uint8_t)(card % 13U + 2U);
    if (rank <= 9U) {
        return (char)('0' + rank);
    }
    static const char high[] = {'T', 'J', 'Q', 'K', 'A'};
    return high[rank - 10U];
}

static char suit_character(uint8_t card)
{
    static const char suits[] = {'C', 'D', 'H', 'S'};
    return suits[card / 13U];
}

static void draw_card(p4_game_surface_t *surface,int x,int y,uint8_t card,bool visible)
{
    if (!visible || card == TEXAS_HOLDEM_NO_CARD) {
        p4_card_back(surface,x,y,27,34,card_table_back,false);
        return;
    }
    const unsigned rank=(unsigned)(card%13U)+2U;
    p4_card_face(surface,x,y,27,34,rank==14U?1U:rank,card/13U,false);
}

static void hole_abbreviation(char text[6], const uint8_t hole[2])
{
    text[0] = rank_character(hole[0]);
    text[1] = suit_character(hole[0]);
    text[2] = ' ';
    text[3] = rank_character(hole[1]);
    text[4] = suit_character(hole[1]);
    text[5] = '\0';
}

static bool cards_visible_for_player(const texas_holdem_state_t *state,
                                     uint8_t player)
{
    if ((state->active_mask & (UINT8_C(1) << player)) == 0U) {
        return false;
    }
    if (state->phase == TEXAS_HOLDEM_PHASE_SHOWDOWN ||
        state->phase == TEXAS_HOLDEM_PHASE_MATCH_OVER) {
        return (state->folded_mask & (UINT8_C(1) << player)) == 0U;
    }
    if (texas_holdem_player_is_cpu(state, player)) {
        return false;
    }
    if (state->network_mode) {
        return state->network_started && state->local_player_slot == player;
    }
    return !state->pass_required && state->current_player == player;
}

static void draw_exit(p4_game_surface_t *surface)
{
    p4_draw_fill_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_PANEL);
    p4_draw_rect(surface, EXIT_X, EXIT_Y, EXIT_W, EXIT_H, COLOR_MUTED);
    p4_draw_text(surface, 7, 5, "EXIT", COLOR_TEXT, 1U, 4U);
}

static void draw_setup(p4_game_surface_t *surface,
                       const texas_holdem_state_t *state)
{
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_card_felt(surface,20,22,280,160,card_table_felt);
    p4_draw_rect(surface, 20, 22, 280, 160, COLOR_ACCENT);
    p4_draw_text(surface, 84, 34, "TEXAS HOLD'EM",
                 COLOR_GOLD, 2U, 13U);
    p4_draw_text(surface, 91, 62, "TABLE LOBBY",
                 COLOR_TEXT, 1U, 11U);
    char players[20];
    size_t length = append_text(players, sizeof(players), 0U, "PLAYERS  ");
    (void)append_unsigned(players, sizeof(players), length,
                          state->player_count);
    p4_draw_text(surface, 111, 77, players, COLOR_MUTED, 1U, 12U);
    p4_draw_fill_rect(surface, SETUP_ROW_X, SETUP_STACK_Y,
                      SETUP_ROW_W, SETUP_ROW_H, COLOR_PANEL);
    p4_draw_rect(surface, SETUP_ROW_X, SETUP_STACK_Y,
                 SETUP_ROW_W, SETUP_ROW_H,
                 state->setup_focus == 0U ? COLOR_GOLD : COLOR_PANEL_EDGE);
    p4_draw_text(surface, 58, 106, "<", COLOR_ACCENT, 1U, 1U);
    p4_draw_text(surface, 76, 106, "START CHIPS", COLOR_TEXT, 1U, 11U);
    char stack[10] = {0};
    (void)append_unsigned(stack, sizeof(stack), 0U, state->starting_stack);
    p4_draw_text(surface, state->starting_stack < 1000U ? 213 : 207,
                 106, stack, COLOR_GOLD, 1U, 6U);
    p4_draw_text(surface, 254, 106, ">", COLOR_ACCENT, 1U, 1U);

    p4_draw_fill_rect(surface, SETUP_ROW_X, SETUP_CPU_Y,
                      SETUP_ROW_W, SETUP_ROW_H, COLOR_PANEL);
    p4_draw_rect(surface, SETUP_ROW_X, SETUP_CPU_Y,
                 SETUP_ROW_W, SETUP_ROW_H,
                 state->setup_focus == 1U ? COLOR_GOLD : COLOR_PANEL_EDGE);
    p4_draw_text(surface, 58, 135, "<", COLOR_ACCENT, 1U, 1U);
    p4_draw_text(surface, 76, 135, "CPU PLAYERS", COLOR_TEXT, 1U, 11U);
    char cpus[4] = {0};
    (void)append_unsigned(cpus, sizeof(cpus), 0U,
                          cpu_player_count(state));
    p4_draw_text(surface, 224, 135, cpus, COLOR_GOLD, 1U, 1U);
    p4_draw_text(surface, 254, 135, ">", COLOR_ACCENT, 1U, 1U);

    const bool owner = !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
    if (owner) {
        p4_draw_fill_rect(surface, 104, SETUP_DEAL_Y, 112, 20, COLOR_ACCENT);
        p4_draw_text(surface, 132, SETUP_DEAL_Y + 6, "DEAL",
                     COLOR_BLACK, 1U, 4U);
        p4_draw_text(surface, 68, 186, "TAP ARROWS THEN DEAL",
                     COLOR_MUTED, 1U, 20U);
    } else {
        p4_draw_text(surface, 70, 158, "HOST SETS CHIPS + CPUS",
                     COLOR_GOLD, 1U, 22U);
        p4_draw_text(surface, 116, 174, "WAITING...",
                     COLOR_MUTED, 1U, 10U);
    }
    draw_exit(surface);
}

static void draw_players(p4_game_surface_t *surface,
                         const texas_holdem_state_t *state)
{
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const int x = 2 + (int)player * 79;
        uint16_t fill = COLOR_PANEL;
        const uint8_t bit = (uint8_t)(UINT8_C(1) << player);
        if ((state->folded_mask & bit) != 0U) {
            fill = COLOR_FELT_DARK;
        } else if ((state->winner_mask & bit) != 0U) {
            fill = COLOR_GOLD;
        }
        p4_draw_fill_rect(surface, x, 19, 76, 31, fill);
        p4_draw_rect(surface, x, 19, 76, 31,
                     player == state->current_player &&
                             state->phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
                             state->phase <= TEXAS_HOLDEM_PHASE_RIVER
                         ? COLOR_ACCENT : COLOR_PANEL_EDGE);
        char label[16];
        player_stack_text(label, player, state->stacks[player],
                          texas_holdem_player_is_cpu(state, player));
        p4_draw_text(surface, x + 3, 23, label,
                     fill == COLOR_GOLD ? COLOR_BLACK : COLOR_TEXT,
                     1U, 12U);
        if (cards_visible_for_player(state, player) &&
            state->hole[player][0] != TEXAS_HOLDEM_NO_CARD) {
            char cards[6];
            hole_abbreviation(cards, state->hole[player]);
            p4_draw_text(surface, x + 3, 36, cards,
                         fill == COLOR_GOLD ? COLOR_BLACK : COLOR_MUTED,
                         1U, 5U);
        } else if ((state->all_in_mask & bit) != 0U) {
            p4_draw_text(surface, x + 3, 36, "ALL IN",
                         COLOR_GOLD, 1U, 6U);
        } else if ((state->folded_mask & bit) != 0U) {
            p4_draw_text(surface, x + 3, 36, "FOLDED",
                         COLOR_RED, 1U, 6U);
        } else if (texas_holdem_player_is_cpu(state, player)) {
            p4_draw_text(surface, x + 3, 36, "CPU",
                         COLOR_GOLD, 1U, 3U);
        } else if (player == state->dealer) {
            p4_draw_text(surface, x + 3, 36, "DEALER",
                         COLOR_MUTED, 1U, 6U);
        }
    }
}

static void draw_actions(p4_game_surface_t *surface,
                         const texas_holdem_state_t *state)
{
    for (uint8_t action = 0U; action < TEXAS_HOLDEM_ACTION_COUNT; ++action) {
        const int x = ACTION_X + (int)action * (ACTION_W + ACTION_GAP);
        const bool legal = texas_holdem_action_legal(
            state, state->current_player, (texas_holdem_action_t)action);
        const bool selected = action == state->action_selection;
        p4_draw_fill_rect(surface, x, ACTION_Y, ACTION_W, ACTION_H,
                          selected ? COLOR_ACCENT : COLOR_PANEL);
        p4_draw_rect(surface, x, ACTION_Y, ACTION_W, ACTION_H,
                     legal ? COLOR_TEXT : COLOR_PANEL_EDGE);
        p4_draw_text(surface, x + (action == TEXAS_HOLDEM_ACTION_CALL
                                      ? 5 : 13), ACTION_Y + 6,
                     s_action_names[action],
                     legal ? (selected ? COLOR_BLACK : COLOR_TEXT)
                           : COLOR_MUTED,
                     1U, 10U);
    }
}

static void draw_table(p4_game_surface_t *surface,
                       const texas_holdem_state_t *state)
{
    p4_draw_clear(surface, COLOR_BACKGROUND);
    p4_card_felt(surface,4,53,312,107,card_table_felt);
    p4_draw_rect(surface, 4, 53, 312, 107, COLOR_GOLD);
    p4_draw_text(surface, 51, 5, "TEXAS HOLD'EM",
                 COLOR_GOLD, 1U, 13U);
    p4_draw_text(surface, 183, 5, s_phase_names[state->phase],
                 COLOR_MUTED, 1U, 8U);
    draw_players(surface, state);

    for (uint8_t index = 0U; index < TEXAS_HOLDEM_COMMUNITY_CARDS; ++index) {
        draw_card(surface, 83 + (int)index * 31, 61,
                  state->community[index], index < state->community_count);
    }
    char pot[20];
    amount_text(pot, "POT  ", state->pot);
    p4_draw_text(surface, 126, 100, pot, COLOR_GOLD, 1U, 13U);

    uint8_t view_player = state->network_mode
        ? state->local_player_slot : state->current_player;
    if (view_player >= state->player_count) {
        view_player = 0U;
    }
    const bool visible = cards_visible_for_player(state, view_player);
    draw_card(surface, 130, 117, state->hole[view_player][0], visible);
    draw_card(surface, 161, 117, state->hole[view_player][1], visible);

    if (state->phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) {
        p4_draw_text(surface, 7, 139, "WINNER", COLOR_GOLD, 1U, 6U);
        const bool owner = !state->network_mode ||
            state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
        p4_draw_fill_rect(surface, 104, 166, 112, 20,
                          owner ? COLOR_ACCENT : COLOR_PANEL);
        p4_draw_text(surface, owner ? 121 : 112, 172,
                     owner ? "NEXT HAND" : "WAIT FOR HOST",
                     owner ? COLOR_BLACK : COLOR_MUTED,
                     1U, owner ? 9U : 13U);
    } else if (state->phase == TEXAS_HOLDEM_PHASE_MATCH_OVER) {
        p4_draw_text(surface, 7, 136, "CHAMPION!",
                     COLOR_GOLD, 1U, 15U);
        const bool owner = !state->network_mode ||
            state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
        p4_draw_fill_rect(surface, 104, 166, 112, 20,
                          owner ? COLOR_ACCENT : COLOR_PANEL);
        p4_draw_text(surface, owner ? 120 : 112, 172,
                     owner ? "NEW MATCH" : "WAIT FOR HOST",
                     owner ? COLOR_BLACK : COLOR_MUTED,
                     1U, owner ? 9U : 13U);
    } else if (state->network_mode && !state->network_started) {
        p4_draw_text(surface, 100, 173, "SYNCING TABLE...",
                     COLOR_GOLD, 1U, 16U);
    } else if (texas_holdem_player_is_cpu(
                   state, state->current_player)) {
        char turn[20];
        size_t length = append_text(turn, sizeof(turn), 0U, "CPU P");
        length = append_unsigned(turn, sizeof(turn), length,
                                 (unsigned)state->current_player + 1U);
        (void)append_text(turn, sizeof(turn), length, " THINKING");
        p4_draw_text(surface, 105, 173, turn, COLOR_GOLD, 1U, 15U);
    } else if (!texas_holdem_local_turn(state)) {
        char turn[20];
        size_t length = append_text(turn, sizeof(turn), 0U, "P");
        length = append_unsigned(turn, sizeof(turn), length,
                                 (unsigned)state->current_player + 1U);
        (void)append_text(turn, sizeof(turn), length, " TO ACT");
        p4_draw_text(surface, 115, 173, turn, COLOR_MUTED, 1U, 12U);
    } else {
        draw_actions(surface, state);
    }
    if (state->network_request_pending) {
        p4_draw_text(surface, 122, 154, "SENDING...",
                     COLOR_MUTED, 1U, 10U);
    } else if (state->peer_lost_fallback) {
        p4_draw_text(surface, 77, 188, "LINK LOST - PASS & PLAY",
                     COLOR_RED, 1U, 23U);
    }
    draw_exit(surface);
}

static void draw_pass_overlay(p4_game_surface_t *surface,
                              const texas_holdem_state_t *state)
{
    p4_draw_fill_rect(surface, 55, 61, 210, 91, COLOR_PANEL);
    p4_draw_rect(surface, 55, 61, 210, 91, COLOR_GOLD);
    char player[20];
    size_t length = append_text(player, sizeof(player), 0U, "PASS TO PLAYER ");
    (void)append_unsigned(player, sizeof(player), length,
                          (unsigned)state->current_player + 1U);
    p4_draw_text(surface, 86, 80, player, COLOR_GOLD, 1U, 16U);
    p4_draw_text(surface, 79, 102, "KEEP CARDS PRIVATE",
                 COLOR_TEXT, 1U, 18U);
    p4_draw_fill_rect(surface, 105, 124, 110, 19, COLOR_ACCENT);
    p4_draw_text(surface, 129, 130, "REVEAL",
                 COLOR_BLACK, 1U, 6U);
}

static void cycle_action(texas_holdem_state_t *state, bool increase)
{
    for (uint8_t attempt = 0U; attempt < TEXAS_HOLDEM_ACTION_COUNT;
         ++attempt) {
        state->action_selection = increase
            ? (uint8_t)((state->action_selection + 1U) %
                        TEXAS_HOLDEM_ACTION_COUNT)
            : (state->action_selection == 0U
                   ? TEXAS_HOLDEM_ACTION_COUNT - 1U
                   : (uint8_t)(state->action_selection - 1U));
        if (texas_holdem_action_legal(
                state, state->current_player,
                (texas_holdem_action_t)state->action_selection)) {
            return;
        }
    }
    state->action_selection = TEXAS_HOLDEM_ACTION_CALL;
}

static bool setup_owner(const texas_holdem_state_t *state)
{
    return !state->network_mode ||
        state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST;
}

static void handle_setup_input(p4_game_context_t *context,
                               texas_holdem_state_t *state,
                               uint32_t pressed,
                               bool touch_pressed,
                               uint16_t touch_x, uint16_t touch_y)
{
    if (!setup_owner(state)) {
        return;
    }
    bool changed = false;
    if ((pressed & (P4_BUTTON_UP | P4_BUTTON_DOWN)) != 0U) {
        state->setup_focus = state->setup_focus == 0U ? 1U : 0U;
    }
    if (touch_pressed && point_in(
            touch_x, touch_y, SETUP_ROW_X, SETUP_STACK_Y,
            SETUP_ROW_W, SETUP_ROW_H)) {
        state->setup_focus = 0U;
    } else if (touch_pressed && point_in(
                   touch_x, touch_y, SETUP_ROW_X, SETUP_CPU_Y,
                   SETUP_ROW_W, SETUP_ROW_H)) {
        state->setup_focus = 1U;
    }
    const bool decrease = (pressed & P4_BUTTON_LEFT) != 0U ||
        (touch_pressed && touch_x < 74U &&
         ((touch_y >= SETUP_STACK_Y && touch_y < SETUP_STACK_Y + SETUP_ROW_H) ||
          (touch_y >= SETUP_CPU_Y && touch_y < SETUP_CPU_Y + SETUP_ROW_H)));
    const bool increase = (pressed & P4_BUTTON_RIGHT) != 0U ||
        (touch_pressed && touch_x >= 246U && touch_x < 268U &&
         ((touch_y >= SETUP_STACK_Y && touch_y < SETUP_STACK_Y + SETUP_ROW_H) ||
          (touch_y >= SETUP_CPU_Y && touch_y < SETUP_CPU_Y + SETUP_ROW_H)));
    if (decrease || increase) {
        changed = state->setup_focus == 0U
            ? texas_holdem_adjust_stack(state, increase)
            : texas_holdem_adjust_cpu_players(state, increase);
    }
    if (changed) {
        texas_holdem_mark_snapshot_dirty(state);
        (void)p4_game_play_tone(context, 523U, 35U, 2U, P4_WAVE_TRIANGLE);
    }
    if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
        (touch_pressed && point_in(
            touch_x, touch_y, 104, SETUP_DEAL_Y, 112, 20))) {
        if (texas_holdem_begin_match(state)) {
            texas_holdem_mark_snapshot_dirty(state);
            (void)p4_game_play_tone(
                context, 784U, 85U, 4U, P4_WAVE_TRIANGLE);
        }
    }
}

static bool game_start(p4_game_context_t *context)
{
    if (context == NULL || context->state == NULL ||
        context->state_bytes != sizeof(texas_holdem_state_t)) {
        return false;
    }
    texas_holdem_state_t *const state = context->state;
    texas_holdem_reset_lobby(
        state, TEXAS_HOLDEM_PLAYERS, UINT32_C(0x484f4c44));
    (void)texas_holdem_network_begin(context, state);
    (void)p4_game_play_tone(context, 523U, 60U, 3U, P4_WAVE_TRIANGLE);
    return true;
}

static p4_game_result_t game_update(
    p4_game_context_t *context,
    const p4_game_input_t *input,
    uint32_t elapsed_ms)
{
    if (context == NULL || input == NULL || context->state == NULL) {
        return P4_GAME_ERROR;
    }
    texas_holdem_state_t *const state = context->state;
    state->held_buttons = input->held;
    texas_holdem_network_poll(context, state, elapsed_ms);

    const bool touch_down = input->touch_valid && input->touch_count != 0U;
    const bool touch_pressed = touch_down && !state->touch_was_down;
    /* The shared mapper also produces virtual-gamepad bits from a touch.
     * The table owns these pixels, so only its semantic hit targets act. */
    const uint32_t pressed = touch_down || state->touch_was_down
        ? 0U : input->pressed;
    const uint16_t touch_x = touch_down ? input->touches[0].x : 0U;
    const uint16_t touch_y = touch_down ? input->touches[0].y : 0U;
    state->touch_was_down = touch_down;
    if ((pressed & P4_BUTTON_BACK) != 0U ||
        (touch_pressed && point_in(
            touch_x, touch_y, EXIT_X, EXIT_Y, EXIT_W, EXIT_H))) {
        return P4_GAME_EXIT_TO_LAUNCHER;
    }

    if (state->phase == TEXAS_HOLDEM_PHASE_SETUP) {
        handle_setup_input(context, state, pressed,
                           touch_pressed, touch_x, touch_y);
        return P4_GAME_CONTINUE;
    }
    if (state->pass_required && !state->network_mode) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
            (touch_pressed && point_in(
                touch_x, touch_y, 105, 124, 110, 19))) {
            state->pass_required = false;
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
        state->phase <= TEXAS_HOLDEM_PHASE_RIVER &&
        texas_holdem_player_is_cpu(state, state->current_player)) {
        const bool authority = !state->network_mode ||
            (state->network_started &&
             state->network_role == P4_GAME_MULTIPLAYER_ROLE_HOST);
        if (authority) {
            if (UINT32_MAX - state->cpu_think_ms < elapsed_ms) {
                state->cpu_think_ms = TEXAS_HOLDEM_CPU_THINK_MS;
            } else {
                state->cpu_think_ms += elapsed_ms;
            }
            if (state->cpu_think_ms >= TEXAS_HOLDEM_CPU_THINK_MS) {
                (void)texas_holdem_perform_cpu_action(context, state);
            }
        }
        return P4_GAME_CONTINUE;
    }
    state->cpu_think_ms = 0U;
    if (state->phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
            (touch_pressed &&
             point_in(touch_x, touch_y, 104, 166, 112, 20))) {
            (void)texas_holdem_request_next_hand(context, state);
        }
        return P4_GAME_CONTINUE;
    }
    if (state->phase == TEXAS_HOLDEM_PHASE_MATCH_OVER) {
        if ((pressed & (P4_BUTTON_A | P4_BUTTON_START)) != 0U ||
            (touch_pressed &&
             point_in(touch_x, touch_y, 104, 166, 112, 20))) {
            (void)texas_holdem_request_new_match(context, state);
        }
        return P4_GAME_CONTINUE;
    }
    if (!texas_holdem_local_turn(state)) {
        return P4_GAME_CONTINUE;
    }

    if ((pressed & P4_BUTTON_LEFT) != 0U) {
        cycle_action(state, false);
    } else if ((pressed & P4_BUTTON_RIGHT) != 0U) {
        cycle_action(state, true);
    }
    if ((pressed & P4_BUTTON_B) != 0U) {
        (void)texas_holdem_perform_action(
            context, state, TEXAS_HOLDEM_ACTION_FOLD);
    } else if ((pressed & P4_BUTTON_START) != 0U) {
        (void)texas_holdem_perform_action(
            context, state, TEXAS_HOLDEM_ACTION_CALL);
    } else if ((pressed & P4_BUTTON_A) != 0U) {
        (void)texas_holdem_perform_action(
            context, state,
            (texas_holdem_action_t)state->action_selection);
    } else if (touch_pressed && touch_y >= ACTION_Y &&
               touch_y < ACTION_Y + ACTION_H) {
        for (uint8_t action = 0U; action < TEXAS_HOLDEM_ACTION_COUNT;
             ++action) {
            const int x = ACTION_X + (int)action * (ACTION_W + ACTION_GAP);
            if (point_in(touch_x, touch_y, x, ACTION_Y,
                         ACTION_W, ACTION_H)) {
                state->action_selection = action;
                (void)texas_holdem_perform_action(
                    context, state, (texas_holdem_action_t)action);
                break;
            }
        }
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
    const texas_holdem_state_t *const state = context->state;
    if (state->phase == TEXAS_HOLDEM_PHASE_SETUP) {
        draw_setup(surface, state);
    } else {
        draw_table(surface, state);
        if (state->pass_required && !state->network_mode) {
            draw_pass_overlay(surface, state);
        }
    }
    return true;
}

static void game_stop(p4_game_context_t *context)
{
    p4_game_stop_audio(context);
}

const p4_game_descriptor_t p4_texas_holdem_game = {
    .api_version = P4_GAME_API_VERSION,
    .launcher_id = UINT32_C(117),
    .id = "org.p4console.texas-holdem",
    .title = "Texas Hold'em",
    .subtitle = "Poker with friends or the CPU",
    .accent_rgb565 = UINT16_C(COLOR_ACCENT),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_VIDEO_HIGH_RES,
    .optional_capabilities = P4_GAME_CAP_AUDIO_TONE |
        P4_GAME_CAP_MULTIPLAYER_SESSION,
    .state_bytes = sizeof(texas_holdem_state_t),
    .start = game_start,
    .update = game_update,
    .render = game_render,
    .stop = game_stop,
};
