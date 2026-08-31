// SPDX-License-Identifier: MIT

#include "color_clash_internal.h"

#include <string.h>

enum {
    CARD_RANK_MASK = 0x0f,
    CARD_COLOR_SHIFT = 4,
    NO_WINNER = 0xff,
    BOT_THINK_MS = 520,
};

static uint32_t random_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void shuffle(uint8_t *cards, uint8_t count, uint32_t *rng)
{
    if (cards == NULL || rng == NULL || count < 2U) {
        return;
    }
    for (uint8_t remaining = count; remaining > 1U; --remaining) {
        const uint8_t index = (uint8_t)(random_next(rng) % remaining);
        const uint8_t tail = (uint8_t)(remaining - 1U);
        const uint8_t card = cards[index];
        cards[index] = cards[tail];
        cards[tail] = card;
    }
}

uint8_t color_clash_make_card(color_clash_color_t color,
                              color_clash_rank_t rank)
{
    return (uint8_t)(((uint8_t)color << CARD_COLOR_SHIFT) | (uint8_t)rank);
}

color_clash_color_t color_clash_card_color(uint8_t card)
{
    return (color_clash_color_t)(card >> CARD_COLOR_SHIFT);
}

color_clash_rank_t color_clash_card_rank(uint8_t card)
{
    return (color_clash_rank_t)(card & CARD_RANK_MASK);
}

bool color_clash_card_valid(uint8_t card)
{
    const color_clash_rank_t rank = color_clash_card_rank(card);
    const color_clash_color_t color = color_clash_card_color(card);
    if (rank == COLOR_CLASH_WILD ||
        rank == COLOR_CLASH_WILD_DRAW_FOUR ||
        rank == COLOR_CLASH_GAMECHANGER) {
        return color == COLOR_CLASH_RED;
    }
    return rank <= COLOR_CLASH_DRAW_TWO && color < COLOR_CLASH_COLOR_COUNT;
}

bool color_clash_card_playable(const color_clash_state_t *state,
                               uint8_t card)
{
    if (state == NULL || !color_clash_card_valid(card) ||
        state->discard_count == 0U ||
        state->active_color >= COLOR_CLASH_COLOR_COUNT) {
        return false;
    }
    const uint8_t top = state->discard[state->discard_count - 1U];
    return color_clash_card_rank(card) == COLOR_CLASH_WILD ||
        color_clash_card_rank(card) == COLOR_CLASH_WILD_DRAW_FOUR ||
        color_clash_card_rank(card) == COLOR_CLASH_GAMECHANGER ||
        color_clash_card_color(card) ==
            (color_clash_color_t)state->active_color ||
        color_clash_card_rank(card) == color_clash_card_rank(top);
}

bool color_clash_card_playable_for_player(
    const color_clash_state_t *state, uint8_t player, uint8_t hand_index)
{
    if (state == NULL || player >= state->player_count ||
        hand_index >= state->hand_counts[player] ||
        !color_clash_card_playable(state, state->hands[player][hand_index])) {
        return false;
    }
    if (color_clash_card_rank(state->hands[player][hand_index]) !=
        COLOR_CLASH_WILD_DRAW_FOUR) {
        return true;
    }
    for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
        if (index == hand_index) {
            continue;
        }
        const uint8_t other = state->hands[player][index];
        if (color_clash_card_rank(other) <= COLOR_CLASH_DRAW_TWO &&
            color_clash_card_color(other) ==
                (color_clash_color_t)state->active_color) {
            return false;
        }
    }
    return true;
}

static void build_deck(color_clash_state_t *state)
{
    uint8_t count = 0U;
    for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        state->deck[count++] = color_clash_make_card(
            (color_clash_color_t)color, COLOR_CLASH_ZERO);
        for (uint8_t rank = COLOR_CLASH_ONE;
             rank <= COLOR_CLASH_NINE; ++rank) {
            for (uint8_t copy = 0U; copy < 2U; ++copy) {
                state->deck[count++] = color_clash_make_card(
                    (color_clash_color_t)color,
                    (color_clash_rank_t)rank);
            }
        }
        for (uint8_t rank = COLOR_CLASH_SKIP;
             rank <= COLOR_CLASH_DRAW_TWO; ++rank) {
            for (uint8_t copy = 0U; copy < 2U; ++copy) {
                state->deck[count++] = color_clash_make_card(
                    (color_clash_color_t)color,
                    (color_clash_rank_t)rank);
            }
        }
    }
    for (uint8_t wild = 0U; wild < 4U; ++wild) {
        state->deck[count++] = color_clash_make_card(
            COLOR_CLASH_RED, COLOR_CLASH_WILD);
    }
    for (uint8_t wild = 0U; wild < 4U; ++wild) {
        state->deck[count++] = color_clash_make_card(
            COLOR_CLASH_RED, COLOR_CLASH_WILD_DRAW_FOUR);
    }
    state->deck[count++] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);
    state->deck_count = count;
    shuffle(state->deck, state->deck_count, &state->rng);
}

static bool replenish_deck(color_clash_state_t *state)
{
    if (state->deck_count != 0U) {
        return true;
    }
    if (state->discard_count <= 1U) {
        return false;
    }
    const uint8_t top = state->discard[state->discard_count - 1U];
    const uint8_t refill = (uint8_t)(state->discard_count - 1U);
    memcpy(state->deck, state->discard, refill);
    state->deck_count = refill;
    state->discard[0] = top;
    state->discard_count = 1U;
    shuffle(state->deck, state->deck_count, &state->rng);
    return true;
}

static bool draw_one(color_clash_state_t *state, uint8_t player)
{
    if (state == NULL || player >= state->player_count ||
        state->hand_counts[player] >= COLOR_CLASH_HAND_CAPACITY ||
        !replenish_deck(state)) {
        return false;
    }
    const uint8_t card = state->deck[--state->deck_count];
    state->hands[player][state->hand_counts[player]++] = card;
    return true;
}

static bool draw_up_to(color_clash_state_t *state, uint8_t player,
                       uint8_t count)
{
    bool drew_any = false;
    for (uint8_t draw = 0U; draw < count; ++draw) {
        if (!draw_one(state, player)) {
            break;
        }
        drew_any = true;
    }
    return drew_any;
}

static void advance_player(color_clash_state_t *state, uint8_t steps)
{
    for (uint8_t step = 0U; step < steps; ++step) {
        if (state->direction == 0U) {
            state->current_player = (uint8_t)(
                (state->current_player + 1U) % state->player_count);
        } else {
            state->current_player = (uint8_t)(
                (state->current_player + state->player_count - 1U) %
                state->player_count);
        }
    }
    state->selected_card = 0U;
    state->bot_wait_ms = 0U;
}

void color_clash_reset_match(color_clash_state_t *state,
                             uint8_t player_count, uint32_t seed)
{
    if (state == NULL || player_count < 2U ||
        player_count > COLOR_CLASH_MAX_PLAYERS) {
        return;
    }
    const color_clash_mode_t mode = state->mode;
    const uint8_t local_slot = state->local_player_slot;
    const p4_game_multiplayer_role_t role = state->network_role;
    const uint64_t network_seed = state->network_seed;
    const uint32_t revision = state->network_revision;
    uint32_t sequences[COLOR_CLASH_MAX_PLAYERS];
    memcpy(sequences, state->last_network_sequence, sizeof(sequences));
    const bool network_started = state->network_started;
    const bool touch_was_down = state->touch_was_down;
    *state = (color_clash_state_t){
        .player_count = player_count,
        .local_player_slot = local_slot,
        .menu_players = player_count,
        .winner = NO_WINNER,
        .phase = COLOR_CLASH_TURN,
        .mode = mode,
        .network_role = role,
        .rng = seed == 0U ? UINT32_C(0x434c4153) : seed,
        .network_revision = revision,
        .network_seed = network_seed,
        .touch_was_down = touch_was_down,
        .network_started = network_started,
    };
    memcpy(state->last_network_sequence, sequences, sizeof(sequences));
    build_deck(state);
    for (uint8_t round = 0U; round < COLOR_CLASH_STARTING_HAND; ++round) {
        for (uint8_t player = 0U; player < player_count; ++player) {
            (void)draw_one(state, player);
        }
    }
    for (uint8_t index = 0U; index < state->deck_count; ++index) {
        if (color_clash_card_rank(state->deck[index]) <= COLOR_CLASH_NINE) {
            const uint8_t tail = (uint8_t)(state->deck_count - 1U);
            const uint8_t card = state->deck[index];
            state->deck[index] = state->deck[tail];
            state->deck[tail] = card;
            break;
        }
    }
    const uint8_t first = state->deck[--state->deck_count];
    state->discard[state->discard_count++] = first;
    state->active_color = (uint8_t)color_clash_card_color(first);
}

bool color_clash_play_card(color_clash_state_t *state,
                           uint8_t player, uint8_t hand_index)
{
    if (state == NULL ||
        (state->phase != COLOR_CLASH_TURN &&
         state->phase != COLOR_CLASH_DRAWN_CARD) ||
        player != state->current_player || player >= state->player_count ||
        hand_index >= state->hand_counts[player] ||
        (state->phase == COLOR_CLASH_DRAWN_CARD &&
         hand_index != (uint8_t)(state->hand_counts[player] - 1U))) {
        return false;
    }
    const uint8_t card = state->hands[player][hand_index];
    if (!color_clash_card_playable_for_player(state, player, hand_index) ||
        state->discard_count >= COLOR_CLASH_DECK_CARDS ||
        (color_clash_card_rank(card) == COLOR_CLASH_GAMECHANGER &&
         state->hand_counts[player] == 1U)) {
        state->notice_ms = 900U;
        return false;
    }
    const uint8_t hand_count = state->hand_counts[player];
    if ((uint8_t)(hand_index + 1U) < hand_count) {
        /*
         * Keep this byte shift explicit. P4G cartridges intentionally have a
         * tiny runtime import allowlist, and memmove is not part of it.
         * Volatile prevents the RISC-V optimizer from replacing the bounded
         * forward copy with a memmove import.
         */
        volatile uint8_t *const hand = state->hands[player];
        for (uint8_t index = hand_index;
             (uint8_t)(index + 1U) < hand_count; ++index) {
            hand[index] = hand[index + 1U];
        }
    }
    --state->hand_counts[player];
    state->discard[state->discard_count++] = card;
    state->phase = COLOR_CLASH_TURN;
    const color_clash_rank_t rank = color_clash_card_rank(card);
    const bool played_last = state->hand_counts[player] == 0U;
    if (rank == COLOR_CLASH_WILD ||
        rank == COLOR_CLASH_WILD_DRAW_FOUR) {
        state->selected_color = state->active_color;
        if (played_last) {
            state->winner = player;
        }
        state->phase = COLOR_CLASH_CHOOSE_COLOR;
        return true;
    }
    if (rank == COLOR_CLASH_GAMECHANGER) {
        state->selected_target = state->direction == 0U
            ? (uint8_t)((player + 1U) % state->player_count)
            : (uint8_t)((player + state->player_count - 1U) %
                        state->player_count);
        state->phase = COLOR_CLASH_CHOOSE_HAND;
        return true;
    }
    state->active_color = (uint8_t)color_clash_card_color(card);
    if (rank == COLOR_CLASH_SKIP) {
        advance_player(state, 2U);
    } else if (rank == COLOR_CLASH_REVERSE) {
        state->direction ^= 1U;
        advance_player(state, state->player_count == 2U ? 2U : 1U);
    } else if (rank == COLOR_CLASH_DRAW_TWO) {
        advance_player(state, 1U);
        (void)draw_up_to(state, state->current_player, 2U);
        advance_player(state, 1U);
    } else {
        advance_player(state, 1U);
    }
    if (played_last) {
        state->winner = player;
        state->phase = COLOR_CLASH_GAME_OVER;
    }
    return true;
}

bool color_clash_draw_card(color_clash_state_t *state, uint8_t player)
{
    if (state == NULL || state->phase != COLOR_CLASH_TURN ||
        player != state->current_player || player >= state->player_count) {
        return false;
    }
    const bool drew = draw_one(state, player);
    if (!drew && state->deck_count == 0U && state->discard_count <= 1U) {
        state->winner = player;
        for (uint8_t candidate = 0U; candidate < state->player_count;
             ++candidate) {
            if (state->hand_counts[candidate] <
                state->hand_counts[state->winner]) {
                state->winner = candidate;
            }
        }
        state->phase = COLOR_CLASH_GAME_OVER;
        return true;
    }
    if (!drew) {
        state->notice_ms = 900U;
        return false;
    }
    state->selected_card = (uint8_t)(state->hand_counts[player] - 1U);
    if (color_clash_card_playable_for_player(
            state, player, state->selected_card)) {
        state->phase = COLOR_CLASH_DRAWN_CARD;
    } else {
        advance_player(state, 1U);
    }
    return true;
}

bool color_clash_pass_drawn_card(color_clash_state_t *state, uint8_t player)
{
    if (state == NULL || state->phase != COLOR_CLASH_DRAWN_CARD ||
        player != state->current_player || player >= state->player_count) {
        return false;
    }
    state->phase = COLOR_CLASH_TURN;
    advance_player(state, 1U);
    return true;
}

bool color_clash_choose_color(color_clash_state_t *state,
                              uint8_t player, uint8_t color)
{
    if (state == NULL || state->phase != COLOR_CLASH_CHOOSE_COLOR ||
        player != state->current_player || player >= state->player_count ||
        color >= COLOR_CLASH_COLOR_COUNT) {
        return false;
    }
    state->active_color = color;
    state->selected_color = color;
    const uint8_t top = state->discard[state->discard_count - 1U];
    if (color_clash_card_rank(top) == COLOR_CLASH_WILD_DRAW_FOUR) {
        advance_player(state, 1U);
        (void)draw_up_to(state, state->current_player, 4U);
        if (state->winner == NO_WINNER) {
            advance_player(state, 1U);
        }
    }
    if (state->winner != NO_WINNER) {
        state->phase = COLOR_CLASH_GAME_OVER;
    } else if (color_clash_card_rank(top) !=
               COLOR_CLASH_WILD_DRAW_FOUR) {
        state->phase = COLOR_CLASH_TURN;
        advance_player(state, 1U);
    } else {
        state->phase = COLOR_CLASH_TURN;
    }
    return true;
}

bool color_clash_choose_hand(color_clash_state_t *state,
                             uint8_t player, uint8_t target)
{
    if (state == NULL || state->phase != COLOR_CLASH_CHOOSE_HAND ||
        player != state->current_player || player >= state->player_count ||
        target >= state->player_count || target == player) {
        return false;
    }
    uint8_t rotated_hands[COLOR_CLASH_MAX_PLAYERS]
                         [COLOR_CLASH_HAND_CAPACITY] = {{0}};
    uint8_t rotated_counts[COLOR_CLASH_MAX_PLAYERS] = {0};
    const uint8_t offset = (uint8_t)(
        (target + state->player_count - player) % state->player_count);
    for (uint8_t destination = 0U; destination < state->player_count;
         ++destination) {
        const uint8_t source = (uint8_t)(
            (destination + offset) % state->player_count);
        rotated_counts[destination] = state->hand_counts[source];
        memcpy(rotated_hands[destination], state->hands[source],
               rotated_counts[destination]);
    }
    memcpy(state->hands, rotated_hands, sizeof(rotated_hands));
    memcpy(state->hand_counts, rotated_counts, sizeof(rotated_counts));
    state->selected_target = target;
    while (state->hand_counts[player] < 3U && draw_one(state, player)) {
        /* A chosen one- or two-card hand is topped up to exactly three. */
    }
    state->phase = COLOR_CLASH_TURN;
    advance_player(state, 1U);
    return true;
}

static uint8_t bot_color(const color_clash_state_t *state, uint8_t player)
{
    uint8_t counts[COLOR_CLASH_COLOR_COUNT] = {0};
    for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
        const uint8_t card = state->hands[player][index];
        if (color_clash_card_rank(card) <= COLOR_CLASH_DRAW_TWO) {
            ++counts[color_clash_card_color(card)];
        }
    }
    uint8_t best = 0U;
    for (uint8_t color = 1U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        if (counts[color] > counts[best]) {
            best = color;
        }
    }
    return best;
}

void color_clash_update_bot(p4_game_context_t *context,
                            color_clash_state_t *state,
                            uint32_t elapsed_ms)
{
    if (state == NULL || state->mode != COLOR_CLASH_PRACTICE ||
        state->current_player == state->local_player_slot ||
        (state->phase != COLOR_CLASH_TURN &&
         state->phase != COLOR_CLASH_DRAWN_CARD &&
         state->phase != COLOR_CLASH_CHOOSE_COLOR &&
         state->phase != COLOR_CLASH_CHOOSE_HAND)) {
        if (state != NULL && state->current_player == state->local_player_slot) {
            state->bot_wait_ms = 0U;
        }
        return;
    }
    if (state->bot_wait_ms + elapsed_ms < BOT_THINK_MS) {
        state->bot_wait_ms += elapsed_ms;
        return;
    }
    state->bot_wait_ms = 0U;
    const uint8_t player = state->current_player;
    if (state->phase == COLOR_CLASH_CHOOSE_COLOR) {
        (void)color_clash_perform_action(
            context, state, COLOR_CLASH_NET_COLOR, bot_color(state, player));
        return;
    }
    if (state->phase == COLOR_CLASH_CHOOSE_HAND) {
        const uint8_t target = state->direction == 0U
            ? (uint8_t)((player + 1U) % state->player_count)
            : (uint8_t)((player + state->player_count - 1U) %
                        state->player_count);
        (void)color_clash_perform_action(
            context, state, COLOR_CLASH_NET_HAND, target);
        return;
    }
    if (state->phase == COLOR_CLASH_DRAWN_CARD) {
        const uint8_t drawn = (uint8_t)(state->hand_counts[player] - 1U);
        if (!color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PLAY, drawn)) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PASS, 0U);
        }
        return;
    }
    for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
        if (color_clash_card_playable_for_player(state, player, index)) {
            (void)color_clash_perform_action(
                context, state, COLOR_CLASH_NET_PLAY, index);
            return;
        }
    }
    (void)color_clash_perform_action(
        context, state, COLOR_CLASH_NET_DRAW, 0U);
}
