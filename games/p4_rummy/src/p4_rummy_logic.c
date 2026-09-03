// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    if (value == 0U) {
        value = UINT32_C(0x52554d4d);
    }
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static uint8_t card_rank(uint8_t card)
{
    return (uint8_t)(card % 13U);
}

static uint8_t card_suit(uint8_t card)
{
    return (uint8_t)(card / 13U);
}

static uint8_t card_points(uint8_t card)
{
    const uint8_t rank = card_rank(card);
    if (rank == 12U) {
        return 1U;
    }
    const uint8_t value = (uint8_t)(rank + 2U);
    return value > 10U ? 10U : value;
}

static uint8_t bit_count(uint16_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static bool subset_is_meld(const uint8_t *cards, uint8_t count,
                           uint16_t mask)
{
    const uint8_t subset_count = bit_count(mask);
    if (cards == NULL || count > P4_RUMMY_DRAWN_CARDS ||
        subset_count < 3U) {
        return false;
    }

    uint8_t first = P4_RUMMY_NO_CARD;
    bool same_rank = true;
    bool same_suit = true;
    uint16_t ranks = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if ((mask & (UINT16_C(1) << index)) == 0U) {
            continue;
        }
        const uint8_t card = cards[index];
        if (card >= P4_RUMMY_DECK_CARDS) {
            return false;
        }
        if (first == P4_RUMMY_NO_CARD) {
            first = card;
        } else {
            same_rank = same_rank && card_rank(card) == card_rank(first);
            same_suit = same_suit && card_suit(card) == card_suit(first);
        }
        const uint16_t rank_bit = (uint16_t)(UINT16_C(1) << card_rank(card));
        if ((ranks & rank_bit) != 0U) {
            same_suit = false;
        }
        ranks |= rank_bit;
    }
    if (same_rank && subset_count <= 4U) {
        return true;
    }
    if (!same_suit) {
        return false;
    }

    uint8_t minimum = 13U;
    uint8_t maximum = 0U;
    for (uint8_t rank = 0U; rank < 13U; ++rank) {
        if ((ranks & (UINT16_C(1) << rank)) != 0U) {
            if (rank < minimum) {
                minimum = rank;
            }
            maximum = rank;
        }
    }
    if ((uint8_t)(maximum - minimum + 1U) == subset_count) {
        return true;
    }

    if ((ranks & (UINT16_C(1) << 12U)) == 0U) {
        return false;
    }
    uint16_t expected = UINT16_C(1) << 12U;
    for (uint8_t rank = 0U; rank + 1U < subset_count; ++rank) {
        expected |= (uint16_t)(UINT16_C(1) << rank);
    }
    return ranks == expected;
}

uint16_t p4_rummy_hand_deadwood(const uint8_t *cards, uint8_t count)
{
    if (cards == NULL || count == 0U || count > P4_RUMMY_DRAWN_CARDS) {
        return count == 0U ? 0U : UINT16_MAX;
    }
    const uint16_t limit = (uint16_t)(UINT16_C(1) << count);
    bool reachable[UINT16_C(1) << P4_RUMMY_DRAWN_CARDS] = {false};
    reachable[0] = true;
    for (uint16_t used = 0U; used < limit; ++used) {
        if (!reachable[used]) {
            continue;
        }
        for (uint16_t meld = 1U; meld < limit; ++meld) {
            if ((used & meld) == 0U && subset_is_meld(cards, count, meld)) {
                reachable[used | meld] = true;
            }
        }
    }

    uint16_t best = UINT16_MAX;
    for (uint16_t used = 0U; used < limit; ++used) {
        if (!reachable[used]) {
            continue;
        }
        uint16_t deadwood = 0U;
        for (uint8_t index = 0U; index < count; ++index) {
            if ((used & (UINT16_C(1) << index)) == 0U) {
                deadwood = (uint16_t)(deadwood + card_points(cards[index]));
            }
        }
        if (deadwood < best) {
            best = deadwood;
        }
    }
    return best;
}

bool p4_rummy_hand_is_complete(const uint8_t *cards, uint8_t count)
{
    return count == P4_RUMMY_HAND_CARDS &&
        p4_rummy_hand_deadwood(cards, count) == 0U;
}

static void clear_cards(p4_rummy_state_t *state)
{
    memset(state->hands, P4_RUMMY_NO_CARD, sizeof(state->hands));
    memset(state->hand_counts, 0, sizeof(state->hand_counts));
    memset(state->deck, P4_RUMMY_NO_CARD, sizeof(state->deck));
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    state->deck_count = 0U;
    state->deck_index = 0U;
    state->discard_count = 0U;
}

static void sort_hand(uint8_t *cards, uint8_t count)
{
    for (uint8_t index = 1U; index < count; ++index) {
        const uint8_t card = cards[index];
        uint8_t position = index;
        while (position > 0U && cards[position - 1U] > card) {
            cards[position] = cards[position - 1U];
            --position;
        }
        cards[position] = card;
    }
}

static void shuffle_cards(p4_rummy_state_t *state,
                          uint8_t *cards, uint8_t count)
{
    for (uint8_t remaining = count; remaining > 1U; --remaining) {
        const uint8_t pick = (uint8_t)(next_random(&state->rng) % remaining);
        const uint8_t last = (uint8_t)(remaining - 1U);
        const uint8_t temporary = cards[pick];
        cards[pick] = cards[last];
        cards[last] = temporary;
    }
}

void p4_rummy_reset_lobby(p4_rummy_state_t *state,
                          uint8_t human_players, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    if (human_players < 1U || human_players > P4_RUMMY_MAX_PLAYERS) {
        human_players = 1U;
    }
    memset(state, 0, sizeof(*state));
    clear_cards(state);
    state->human_player_count = human_players;
    state->player_count = human_players == 1U ? 2U : human_players;
    state->cpu_mask = human_players == 1U ? UINT8_C(0x02) : 0U;
    state->winner = P4_RUMMY_NO_PLAYER;
    state->drawn_card_index = P4_RUMMY_NO_CARD;
    state->phase = P4_RUMMY_PHASE_SETUP;
    state->rng = seed == 0U ? UINT32_C(0x5034524d) : seed;
    state->revision = 1U;
}

bool p4_rummy_adjust_human_players(p4_rummy_state_t *state,
                                   bool increase)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_SETUP ||
        state->network_mode) {
        return false;
    }
    uint8_t humans = state->human_player_count;
    humans = increase
        ? (humans == P4_RUMMY_MAX_PLAYERS ? 1U : (uint8_t)(humans + 1U))
        : (humans == 1U ? P4_RUMMY_MAX_PLAYERS : (uint8_t)(humans - 1U));
    state->human_player_count = humans;
    state->player_count = humans == 1U ? 2U : humans;
    state->cpu_mask = humans == 1U ? UINT8_C(0x02) : 0U;
    ++state->revision;
    return true;
}

bool p4_rummy_adjust_cpu_seats(p4_rummy_state_t *state, bool increase)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_SETUP ||
        !state->network_mode ||
        state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST ||
        state->network_player_count < P4_RUMMY_MIN_PLAYERS ||
        state->network_player_count > P4_RUMMY_MAX_PLAYERS) {
        return false;
    }
    const uint8_t maximum = (uint8_t)(
        P4_RUMMY_MAX_PLAYERS - state->network_player_count);
    if (maximum == 0U) {
        return false;
    }
    uint8_t cpus = (uint8_t)(state->player_count -
                             state->network_player_count);
    cpus = increase
        ? (cpus == maximum ? 0U : (uint8_t)(cpus + 1U))
        : (cpus == 0U ? maximum : (uint8_t)(cpus - 1U));
    state->player_count = (uint8_t)(state->network_player_count + cpus);
    state->cpu_mask = 0U;
    for (uint8_t player = state->network_player_count;
         player < state->player_count; ++player) {
        state->cpu_mask |= (uint8_t)(UINT8_C(1) << player);
    }
    ++state->revision;
    return true;
}

bool p4_rummy_player_is_cpu(const p4_rummy_state_t *state,
                            uint8_t player)
{
    return state != NULL && player < state->player_count &&
        (state->cpu_mask & (UINT8_C(1) << player)) != 0U;
}

static bool recycle_discard(p4_rummy_state_t *state)
{
    if (state->deck_index < state->deck_count) {
        return true;
    }
    if (state->discard_count <= 1U) {
        return false;
    }
    const uint8_t top = state->discard[state->discard_count - 1U];
    const uint8_t recycled = (uint8_t)(state->discard_count - 1U);
    memcpy(state->deck, state->discard, recycled);
    shuffle_cards(state, state->deck, recycled);
    state->deck_count = recycled;
    state->deck_index = 0U;
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    state->discard[0] = top;
    state->discard_count = 1U;
    return true;
}

bool p4_rummy_begin_round(p4_rummy_state_t *state)
{
    if (state == NULL || state->player_count < P4_RUMMY_MIN_PLAYERS ||
        state->player_count > P4_RUMMY_MAX_PLAYERS ||
        (state->network_mode &&
         state->network_role != P4_GAME_MULTIPLAYER_ROLE_HOST)) {
        return false;
    }
    clear_cards(state);
    for (uint8_t card = 0U; card < P4_RUMMY_DECK_CARDS; ++card) {
        state->deck[card] = card;
    }
    state->deck_count = P4_RUMMY_DECK_CARDS;
    shuffle_cards(state, state->deck, state->deck_count);
    for (uint8_t card_index = 0U;
         card_index < P4_RUMMY_HAND_CARDS; ++card_index) {
        for (uint8_t player = 0U; player < state->player_count; ++player) {
            state->hands[player][card_index] =
                state->deck[state->deck_index++];
            ++state->hand_counts[player];
        }
    }
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        sort_hand(state->hands[player], state->hand_counts[player]);
    }
    state->discard[state->discard_count++] =
        state->deck[state->deck_index++];
    state->current_player = (uint8_t)(
        state->round_number % state->player_count);
    state->winner = P4_RUMMY_NO_PLAYER;
    state->phase = P4_RUMMY_PHASE_DRAW;
    state->draw_source = P4_RUMMY_DRAW_STOCK;
    state->selected_card = 0U;
    state->drawn_card_index = P4_RUMMY_NO_CARD;
    state->turn_count = 0U;
    state->cpu_think_ms = 0U;
    state->network_request_pending = false;
    state->pass_required = !state->network_mode &&
        !p4_rummy_player_is_cpu(state, state->current_player);
    ++state->round_number;
    ++state->revision;
    return true;
}

bool p4_rummy_draw(p4_rummy_state_t *state, uint8_t player,
                   p4_rummy_draw_source_t source)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_DRAW ||
        player != state->current_player || player >= state->player_count ||
        state->hand_counts[player] != P4_RUMMY_HAND_CARDS ||
        source > P4_RUMMY_DRAW_DISCARD) {
        return false;
    }
    uint8_t drawn = P4_RUMMY_NO_CARD;
    if (source == P4_RUMMY_DRAW_DISCARD) {
        if (state->discard_count == 0U) {
            return false;
        }
        drawn = state->discard[--state->discard_count];
        state->discard[state->discard_count] = P4_RUMMY_NO_CARD;
    } else {
        if (!recycle_discard(state)) {
            return false;
        }
        drawn = state->deck[state->deck_index++];
    }
    const uint8_t index = state->hand_counts[player];
    state->hands[player][index] = drawn;
    state->hand_counts[player] = (uint8_t)(index + 1U);
    state->selected_card = index;
    state->drawn_card_index = source == P4_RUMMY_DRAW_DISCARD
        ? index : P4_RUMMY_NO_CARD;
    state->draw_source = source;
    state->phase = P4_RUMMY_PHASE_DISCARD;
    state->network_request_pending = false;
    ++state->revision;
    return true;
}

static uint8_t lowest_deadwood_player(const p4_rummy_state_t *state)
{
    uint8_t winner = 0U;
    uint16_t best = UINT16_MAX;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const uint16_t score = p4_rummy_hand_deadwood(
            state->hands[player], state->hand_counts[player]);
        if (score < best) {
            best = score;
            winner = player;
        }
    }
    return winner;
}

bool p4_rummy_discard_card(p4_rummy_state_t *state, uint8_t player,
                           uint8_t hand_index)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_DISCARD ||
        player != state->current_player || player >= state->player_count ||
        state->hand_counts[player] != P4_RUMMY_DRAWN_CARDS ||
        hand_index >= P4_RUMMY_DRAWN_CARDS ||
        hand_index == state->drawn_card_index ||
        state->discard_count >= P4_RUMMY_DECK_CARDS) {
        return false;
    }
    const uint8_t card = state->hands[player][hand_index];
    for (uint8_t index = hand_index;
         index + 1U < state->hand_counts[player]; ++index) {
        state->hands[player][index] = state->hands[player][index + 1U];
    }
    --state->hand_counts[player];
    state->hands[player][state->hand_counts[player]] = P4_RUMMY_NO_CARD;
    sort_hand(state->hands[player], state->hand_counts[player]);
    state->discard[state->discard_count++] = card;
    ++state->turn_count;
    state->drawn_card_index = P4_RUMMY_NO_CARD;
    state->network_request_pending = false;
    if (p4_rummy_hand_is_complete(
            state->hands[player], state->hand_counts[player])) {
        state->winner = player;
        state->phase = P4_RUMMY_PHASE_ROUND_OVER;
        state->pass_required = false;
    } else if (state->turn_count >= P4_RUMMY_TURN_LIMIT) {
        state->winner = lowest_deadwood_player(state);
        state->phase = P4_RUMMY_PHASE_ROUND_OVER;
        state->pass_required = false;
    } else {
        state->current_player = (uint8_t)(
            (state->current_player + 1U) % state->player_count);
        state->phase = P4_RUMMY_PHASE_DRAW;
        state->draw_source = P4_RUMMY_DRAW_STOCK;
        state->selected_card = 0U;
        state->cpu_think_ms = 0U;
        state->pass_required = !state->network_mode &&
            !p4_rummy_player_is_cpu(state, state->current_player);
    }
    ++state->revision;
    return true;
}

bool p4_rummy_local_turn(const p4_rummy_state_t *state)
{
    if (state == NULL || state->phase == P4_RUMMY_PHASE_SETUP ||
        state->phase == P4_RUMMY_PHASE_ROUND_OVER ||
        p4_rummy_player_is_cpu(state, state->current_player)) {
        return false;
    }
    if (!state->network_mode) {
        return !state->pass_required;
    }
    return state->network_started &&
        state->current_player == state->local_player_slot;
}

static void hand_without_card(const uint8_t source[P4_RUMMY_DRAWN_CARDS],
                              uint8_t removed,
                              uint8_t result[P4_RUMMY_HAND_CARDS])
{
    uint8_t output = 0U;
    for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
        if (index != removed) {
            result[output++] = source[index];
        }
    }
}

static uint8_t choose_cpu_discard(const p4_rummy_state_t *state,
                                  uint8_t player)
{
    uint8_t best_index = 0U;
    uint16_t best_deadwood = UINT16_MAX;
    uint8_t best_points = 0U;
    for (uint8_t index = 0U; index < P4_RUMMY_DRAWN_CARDS; ++index) {
        if (index == state->drawn_card_index) {
            continue;
        }
        uint8_t candidate[P4_RUMMY_HAND_CARDS];
        hand_without_card(state->hands[player], index, candidate);
        const uint16_t deadwood = p4_rummy_hand_deadwood(
            candidate, P4_RUMMY_HAND_CARDS);
        const uint8_t points = card_points(state->hands[player][index]);
        if (deadwood < best_deadwood ||
            (deadwood == best_deadwood && points > best_points)) {
            best_deadwood = deadwood;
            best_points = points;
            best_index = index;
        }
    }
    return best_index;
}

static bool discard_improves_cpu_hand(const p4_rummy_state_t *state,
                                      uint8_t player)
{
    if (state->discard_count == 0U) {
        return false;
    }
    uint8_t candidate[P4_RUMMY_DRAWN_CARDS];
    memcpy(candidate, state->hands[player], P4_RUMMY_HAND_CARDS);
    candidate[P4_RUMMY_HAND_CARDS] =
        state->discard[state->discard_count - 1U];
    uint16_t best = UINT16_MAX;
    for (uint8_t removed = 0U; removed < P4_RUMMY_HAND_CARDS; ++removed) {
        uint8_t reduced[P4_RUMMY_HAND_CARDS];
        hand_without_card(candidate, removed, reduced);
        const uint16_t deadwood = p4_rummy_hand_deadwood(
            reduced, P4_RUMMY_HAND_CARDS);
        if (deadwood < best) {
            best = deadwood;
        }
    }
    return best < p4_rummy_hand_deadwood(
        state->hands[player], P4_RUMMY_HAND_CARDS);
}

bool p4_rummy_cpu_step(p4_rummy_state_t *state)
{
    if (state == NULL || !p4_rummy_player_is_cpu(
            state, state->current_player)) {
        return false;
    }
    const uint8_t player = state->current_player;
    if (state->phase == P4_RUMMY_PHASE_DRAW) {
        const p4_rummy_draw_source_t source =
            discard_improves_cpu_hand(state, player)
            ? P4_RUMMY_DRAW_DISCARD : P4_RUMMY_DRAW_STOCK;
        return p4_rummy_draw(state, player, source);
    }
    if (state->phase == P4_RUMMY_PHASE_DISCARD) {
        return p4_rummy_discard_card(
            state, player, choose_cpu_discard(state, player));
    }
    return false;
}
