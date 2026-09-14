// SPDX-License-Identifier: MIT

#include "p4_rummy_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    P4_RUMMY_CPU_ANALYSIS_CARDS = P4_RUMMY_HAND_CARDS + 1,
};

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
        return 15U;
    }
    const uint8_t value = (uint8_t)(rank + 2U);
    return value > 10U ? 10U : value;
}

static uint8_t bit_count(uint64_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static bool subset_is_meld(const uint8_t *cards, uint8_t count,
                           uint64_t mask)
{
    const uint8_t subset_count = bit_count(mask);
    if (cards == NULL || count > P4_RUMMY_MAX_MELD_CARDS ||
        subset_count < 3U) {
        return false;
    }

    uint8_t first = P4_RUMMY_NO_CARD;
    bool same_rank = true;
    bool same_suit = true;
    uint16_t ranks = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if ((mask & (UINT64_C(1) << index)) == 0U) {
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

bool p4_rummy_meld_is_valid(const uint8_t *cards, uint8_t count)
{
    if (cards == NULL || count < 3U ||
        count > P4_RUMMY_MAX_MELD_CARDS) {
        return false;
    }
    const uint64_t mask =
        (UINT64_C(1) << count) - UINT64_C(1);
    return subset_is_meld(cards, count, mask);
}

static bool ace_is_low_in_meld(const uint8_t *cards, uint8_t count)
{
    if (cards == NULL || count < 3U ||
        !p4_rummy_meld_is_valid(cards, count)) {
        return false;
    }
    const uint8_t suit = card_suit(cards[0]);
    uint16_t ranks = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (card_suit(cards[index]) != suit) {
            return false;
        }
        ranks |= (uint16_t)(UINT16_C(1) << card_rank(cards[index]));
    }
    uint16_t expected = UINT16_C(1) << 12U;
    for (uint8_t rank = 0U; rank + 1U < count; ++rank) {
        expected |= (uint16_t)(UINT16_C(1) << rank);
    }
    return ranks == expected;
}

uint16_t p4_rummy_hand_points(const uint8_t *cards, uint8_t count)
{
    if (cards == NULL || count > P4_RUMMY_MAX_HAND_CARDS) {
        return UINT16_MAX;
    }
    uint16_t points = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (cards[index] >= P4_RUMMY_DECK_CARDS) {
            return UINT16_MAX;
        }
        points = (uint16_t)(points + card_points(cards[index]));
    }
    return points;
}

static int16_t saturated_score(int32_t score)
{
    if (score > INT16_MAX) {
        return INT16_MAX;
    }
    if (score < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)score;
}

static void add_score(p4_rummy_state_t *state, uint8_t player,
                      int16_t points)
{
    state->scores[player] = saturated_score(
        (int32_t)state->scores[player] + points);
    state->round_scores[player] = saturated_score(
        (int32_t)state->round_scores[player] + points);
}

static void finish_round(p4_rummy_state_t *state, uint8_t winner)
{
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const uint16_t hand_points = p4_rummy_hand_points(
            state->hands[player], state->hand_counts[player]);
        add_score(state, player, (int16_t)-(int16_t)hand_points);
    }
    state->winner = winner;
    state->phase = P4_RUMMY_PHASE_ROUND_OVER;
}

uint16_t p4_rummy_hand_deadwood(const uint8_t *cards, uint8_t count)
{
    if (cards == NULL || count == 0U ||
        count > P4_RUMMY_MAX_HAND_CARDS) {
        return count == 0U ? 0U : UINT16_MAX;
    }
    if (count > P4_RUMMY_CPU_ANALYSIS_CARDS) {
        return p4_rummy_hand_points(cards, count);
    }
    const uint16_t limit = (uint16_t)(UINT16_C(1) << count);
    bool reachable[UINT16_C(1) << P4_RUMMY_CPU_ANALYSIS_CARDS] = {false};
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
    memset(state->melds, P4_RUMMY_NO_CARD, sizeof(state->melds));
    memset(state->meld_counts, 0, sizeof(state->meld_counts));
    memset(state->meld_owners, P4_RUMMY_NO_PLAYER,
           sizeof(state->meld_owners));
    memset(state->deck, P4_RUMMY_NO_CARD, sizeof(state->deck));
    memset(state->discard, P4_RUMMY_NO_CARD, sizeof(state->discard));
    state->deck_count = 0U;
    state->deck_index = 0U;
    state->discard_count = 0U;
    state->meld_count = 0U;
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
    state->required_meld_card = P4_RUMMY_NO_CARD;
    state->selected_discard = P4_RUMMY_NO_CARD;
    state->selected_meld = P4_RUMMY_NO_CARD;
    state->phase = P4_RUMMY_PHASE_SETUP;
    state->rng = seed == 0U ? UINT32_C(0x5034524d) : seed;
    state->revision = 1U;
}

bool p4_rummy_adjust_offline_players(p4_rummy_state_t *state,
                                     bool increase)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_SETUP ||
        state->network_mode) {
        return false;
    }
    uint8_t players = state->player_count;
    if (players < P4_RUMMY_MIN_PLAYERS ||
        players > P4_RUMMY_MAX_PLAYERS) {
        players = P4_RUMMY_MIN_PLAYERS;
    }
    players = increase
        ? (players == P4_RUMMY_MAX_PLAYERS
               ? P4_RUMMY_MIN_PLAYERS : (uint8_t)(players + 1U))
        : (players == P4_RUMMY_MIN_PLAYERS
               ? P4_RUMMY_MAX_PLAYERS : (uint8_t)(players - 1U));
    state->human_player_count = 1U;
    state->player_count = players;
    state->cpu_mask = 0U;
    for (uint8_t player = 1U; player < players; ++player) {
        state->cpu_mask |= (uint8_t)(UINT8_C(1) << player);
    }
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
    state->selected_mask = 0U;
    state->drawn_card_index = P4_RUMMY_NO_CARD;
    state->required_meld_card = P4_RUMMY_NO_CARD;
    state->selected_discard = (uint8_t)(state->discard_count - 1U);
    state->selected_meld = P4_RUMMY_NO_CARD;
    state->turn_count = 0U;
    state->cpu_think_ms = 0U;
    state->network_request_pending = false;
    memset(state->round_scores, 0, sizeof(state->round_scores));
    ++state->round_number;
    ++state->revision;
    return true;
}

bool p4_rummy_draw(p4_rummy_state_t *state, uint8_t player,
                   p4_rummy_draw_source_t source,
                   uint8_t discard_index)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_DRAW ||
        player != state->current_player || player >= state->player_count ||
        state->hand_counts[player] == 0U ||
        state->hand_counts[player] >= P4_RUMMY_MAX_HAND_CARDS ||
        source > P4_RUMMY_DRAW_DISCARD) {
        return false;
    }
    uint8_t drawn = P4_RUMMY_NO_CARD;
    uint8_t take_count = 1U;
    if (source == P4_RUMMY_DRAW_DISCARD) {
        if (discard_index >= state->discard_count) {
            return false;
        }
        drawn = state->discard[discard_index];
        take_count = (uint8_t)(state->discard_count - discard_index);
        if (take_count > (uint8_t)(P4_RUMMY_MAX_HAND_CARDS -
                                   state->hand_counts[player])) {
            return false;
        }
        memcpy(state->hands[player] + state->hand_counts[player],
               state->discard + discard_index, take_count);
        memset(state->discard + discard_index, P4_RUMMY_NO_CARD,
               take_count);
        state->discard_count = discard_index;
    } else {
        if (!recycle_discard(state)) {
            return false;
        }
        drawn = state->deck[state->deck_index++];
        state->hands[player][state->hand_counts[player]] = drawn;
    }
    state->hand_counts[player] = (uint8_t)(
        state->hand_counts[player] + take_count);
    sort_hand(state->hands[player], state->hand_counts[player]);
    uint8_t drawn_index = P4_RUMMY_NO_CARD;
    for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
        if (state->hands[player][index] == drawn) {
            drawn_index = index;
            break;
        }
    }
    if (drawn_index == P4_RUMMY_NO_CARD) {
        return false;
    }
    state->selected_card = drawn_index;
    state->selected_mask = 0U;
    state->drawn_card_index = source == P4_RUMMY_DRAW_DISCARD
        ? drawn_index : P4_RUMMY_NO_CARD;
    state->required_meld_card = source == P4_RUMMY_DRAW_DISCARD &&
            take_count > 1U
        ? drawn : P4_RUMMY_NO_CARD;
    if (state->required_meld_card != P4_RUMMY_NO_CARD) {
        state->selected_mask = UINT64_C(1) << drawn_index;
    }
    state->selected_discard = P4_RUMMY_NO_CARD;
    state->selected_meld = P4_RUMMY_NO_CARD;
    state->draw_source = source;
    state->phase = P4_RUMMY_PHASE_DISCARD;
    state->network_request_pending = false;
    ++state->revision;
    return true;
}

static void sort_meld(uint8_t *cards, uint8_t count)
{
    sort_hand(cards, count);
    if (count < 3U || card_suit(cards[0]) != card_suit(cards[count - 1U]) ||
        card_rank(cards[0]) != 0U || card_rank(cards[count - 1U]) != 12U) {
        return;
    }
    for (uint8_t index = 0U; index + 1U < count; ++index) {
        if (card_rank(cards[index]) != index) {
            return;
        }
    }
    for (uint8_t index = (uint8_t)(count - 1U); index > 0U; --index) {
        const uint8_t previous = cards[index - 1U];
        cards[index - 1U] = cards[index];
        cards[index] = previous;
    }
}

static uint8_t selected_cards(
    const p4_rummy_state_t *state, uint8_t player,
    uint64_t selection_mask,
    uint8_t cards[P4_RUMMY_MAX_HAND_CARDS])
{
    if (state == NULL || player >= state->player_count ||
        state->hand_counts[player] > P4_RUMMY_MAX_HAND_CARDS) {
        return 0U;
    }
    const uint64_t valid_mask = state->hand_counts[player] == 64U
        ? UINT64_MAX
        : (UINT64_C(1) << state->hand_counts[player]) - UINT64_C(1);
    if (selection_mask == 0U || (selection_mask & ~valid_mask) != 0U) {
        return 0U;
    }
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
        if ((selection_mask & (UINT64_C(1) << index)) != 0U) {
            cards[count++] = state->hands[player][index];
        }
    }
    return count;
}

static bool meld_accepts_cards(const p4_rummy_state_t *state,
                               uint8_t meld_index,
                               const uint8_t *cards, uint8_t count)
{
    if (state == NULL || cards == NULL ||
        meld_index >= state->meld_count ||
        state->meld_counts[meld_index] < 3U ||
        count == 0U ||
        state->meld_counts[meld_index] + count >
            P4_RUMMY_MAX_MELD_CARDS) {
        return false;
    }
    uint8_t combined[P4_RUMMY_MAX_MELD_CARDS];
    const uint8_t old_count = state->meld_counts[meld_index];
    memcpy(combined, state->melds[meld_index], old_count);
    memcpy(combined + old_count, cards, count);
    return p4_rummy_meld_is_valid(combined, (uint8_t)(old_count + count));
}

static bool selection_can_play(const p4_rummy_state_t *state,
                               uint8_t player, uint64_t selection_mask)
{
    uint8_t cards[P4_RUMMY_MAX_HAND_CARDS];
    const uint8_t count = selected_cards(
        state, player, selection_mask, cards);
    if (count == 0U) {
        return false;
    }
    for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
        if (meld_accepts_cards(state, meld, cards, count)) {
            return true;
        }
    }
    return count >= 3U && state->meld_count < P4_RUMMY_MAX_MELDS &&
        p4_rummy_meld_is_valid(cards, count);
}

bool p4_rummy_play_meld(p4_rummy_state_t *state, uint8_t player,
                        uint64_t selection_mask)
{
    return p4_rummy_play_meld_to(
        state, player, selection_mask, P4_RUMMY_NO_CARD);
}

bool p4_rummy_play_meld_to(p4_rummy_state_t *state, uint8_t player,
                           uint64_t selection_mask,
                           uint8_t meld_index)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_DISCARD ||
        player != state->current_player || player >= state->player_count ||
        !selection_can_play(state, player, selection_mask)) {
        return false;
    }
    uint8_t cards[P4_RUMMY_MAX_HAND_CARDS];
    const uint8_t count = selected_cards(
        state, player, selection_mask, cards);
    uint8_t target = P4_RUMMY_NO_CARD;
    if (meld_index != P4_RUMMY_NO_CARD) {
        if (!meld_accepts_cards(state, meld_index, cards, count)) {
            return false;
        }
        target = meld_index;
    } else {
        for (uint8_t meld = 0U; meld < state->meld_count; ++meld) {
            if (meld_accepts_cards(state, meld, cards, count)) {
                target = meld;
                break;
            }
        }
    }
    uint8_t scoring_meld[P4_RUMMY_MAX_MELD_CARDS];
    uint8_t scoring_count = count;
    if (target == P4_RUMMY_NO_CARD) {
        memcpy(scoring_meld, cards, count);
    } else {
        scoring_count = (uint8_t)(state->meld_counts[target] + count);
        memcpy(scoring_meld, state->melds[target],
               state->meld_counts[target]);
        memcpy(scoring_meld + state->meld_counts[target], cards, count);
    }
    const bool low_ace = ace_is_low_in_meld(scoring_meld, scoring_count);
    uint16_t points = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        points = (uint16_t)(points +
            (card_rank(cards[index]) == 12U && low_ace
                ? 1U : card_points(cards[index])));
    }
    if (target == P4_RUMMY_NO_CARD) {
        target = state->meld_count++;
        state->meld_counts[target] = 0U;
        state->meld_owners[target] = player;
    }
    const uint8_t old_meld_count = state->meld_counts[target];
    memcpy(state->melds[target] + old_meld_count, cards, count);
    state->meld_counts[target] = (uint8_t)(old_meld_count + count);
    sort_meld(state->melds[target], state->meld_counts[target]);

    const uint8_t old_hand_count = state->hand_counts[player];
    const uint8_t drawn_card = state->drawn_card_index < old_hand_count
        ? state->hands[player][state->drawn_card_index]
        : P4_RUMMY_NO_CARD;
    bool played_required = false;
    for (uint8_t index = 0U; index < count; ++index) {
        played_required = played_required ||
            cards[index] == state->required_meld_card;
    }
    uint8_t output = 0U;
    for (uint8_t index = 0U; index < old_hand_count; ++index) {
        if ((selection_mask & (UINT64_C(1) << index)) == 0U) {
            state->hands[player][output++] = state->hands[player][index];
        }
    }
    state->hand_counts[player] = output;
    while (output < P4_RUMMY_MAX_HAND_CARDS) {
        state->hands[player][output++] = P4_RUMMY_NO_CARD;
    }
    sort_hand(state->hands[player], state->hand_counts[player]);
    state->drawn_card_index = P4_RUMMY_NO_CARD;
    if (drawn_card != P4_RUMMY_NO_CARD) {
        for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
            if (state->hands[player][index] == drawn_card) {
                state->drawn_card_index = index;
                break;
            }
        }
    }
    state->selected_card = 0U;
    state->selected_mask = 0U;
    state->selected_meld = P4_RUMMY_NO_CARD;
    if (played_required) {
        state->required_meld_card = P4_RUMMY_NO_CARD;
    }
    state->network_request_pending = false;
    add_score(state, player, (int16_t)points);
    if (state->hand_counts[player] == 0U) {
        finish_round(state, player);
    }
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

uint8_t p4_rummy_match_winner(const p4_rummy_state_t *state)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_ROUND_OVER) {
        return P4_RUMMY_NO_PLAYER;
    }
    int16_t highest = INT16_MIN;
    uint8_t winner = P4_RUMMY_NO_PLAYER;
    bool tied = false;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        if (state->scores[player] > highest) {
            highest = state->scores[player];
            winner = player;
            tied = false;
        } else if (state->scores[player] == highest) {
            tied = true;
        }
    }
    return highest >= P4_RUMMY_MATCH_TARGET && !tied
        ? winner : P4_RUMMY_NO_PLAYER;
}

bool p4_rummy_discard_card(p4_rummy_state_t *state, uint8_t player,
                           uint8_t hand_index)
{
    if (state == NULL || state->phase != P4_RUMMY_PHASE_DISCARD ||
        player != state->current_player || player >= state->player_count ||
        state->hand_counts[player] == 0U ||
        state->hand_counts[player] > P4_RUMMY_MAX_HAND_CARDS ||
        hand_index >= state->hand_counts[player] ||
        hand_index == state->drawn_card_index ||
        state->required_meld_card != P4_RUMMY_NO_CARD ||
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
    state->required_meld_card = P4_RUMMY_NO_CARD;
    state->selected_mask = 0U;
    state->selected_meld = P4_RUMMY_NO_CARD;
    state->network_request_pending = false;
    if (state->hand_counts[player] == 0U) {
        finish_round(state, player);
    } else if (state->turn_count >= P4_RUMMY_TURN_LIMIT) {
        finish_round(state, lowest_deadwood_player(state));
    } else {
        state->current_player = (uint8_t)(
            (state->current_player + 1U) % state->player_count);
        state->phase = P4_RUMMY_PHASE_DRAW;
        state->draw_source = P4_RUMMY_DRAW_STOCK;
        state->selected_discard = (uint8_t)(state->discard_count - 1U);
        state->selected_card = 0U;
        state->selected_mask = 0U;
        state->cpu_think_ms = 0U;
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
        return true;
    }
    return state->network_started &&
        state->current_player == state->local_player_slot;
}

static uint8_t hand_without_card(
    const uint8_t source[P4_RUMMY_MAX_HAND_CARDS], uint8_t count,
    uint8_t removed, uint8_t result[P4_RUMMY_MAX_HAND_CARDS])
{
    uint8_t output = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (index != removed) {
            result[output++] = source[index];
        }
    }
    return output;
}

static uint8_t choose_cpu_discard(const p4_rummy_state_t *state,
                                  uint8_t player)
{
    uint8_t best_index = 0U;
    uint16_t best_deadwood = UINT16_MAX;
    uint8_t best_points = 0U;
    const uint8_t count = state->hand_counts[player];
    for (uint8_t index = 0U; index < count; ++index) {
        if (index == state->drawn_card_index) {
            continue;
        }
        uint8_t candidate[P4_RUMMY_MAX_HAND_CARDS];
        const uint8_t candidate_count = hand_without_card(
            state->hands[player], count, index, candidate);
        const uint16_t deadwood = p4_rummy_hand_deadwood(
            candidate, candidate_count);
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
    const uint8_t hand_count = state->hand_counts[player];
    if (hand_count == 0U || hand_count >= P4_RUMMY_MAX_HAND_CARDS) {
        return false;
    }
    uint8_t candidate[P4_RUMMY_MAX_HAND_CARDS];
    memcpy(candidate, state->hands[player], hand_count);
    candidate[hand_count] =
        state->discard[state->discard_count - 1U];
    uint16_t best = UINT16_MAX;
    const uint8_t candidate_count = (uint8_t)(hand_count + 1U);
    for (uint8_t removed = 0U; removed < hand_count; ++removed) {
        uint8_t reduced[P4_RUMMY_MAX_HAND_CARDS];
        const uint8_t reduced_count = hand_without_card(
            candidate, candidate_count, removed, reduced);
        const uint16_t deadwood = p4_rummy_hand_deadwood(
            reduced, reduced_count);
        if (deadwood < best) {
            best = deadwood;
        }
    }
    return best < p4_rummy_hand_deadwood(
        state->hands[player], hand_count);
}

static uint8_t choose_cpu_meld(const p4_rummy_state_t *state,
                               uint8_t player)
{
    const uint8_t count = state->hand_counts[player];
    if (count > P4_RUMMY_CPU_ANALYSIS_CARDS) {
        return 0U;
    }
    const uint16_t limit = (uint16_t)(UINT16_C(1) << count);
    uint64_t best_mask = 0U;
    uint8_t best_count = 0U;
    uint16_t best_points = 0U;
    for (uint16_t candidate = 1U; candidate < limit; ++candidate) {
        const uint64_t mask = candidate;
        if (!selection_can_play(state, player, mask)) {
            continue;
        }
        const uint8_t candidate_count = bit_count(candidate);
        uint16_t points = 0U;
        for (uint8_t index = 0U; index < count; ++index) {
            if ((mask & (UINT64_C(1) << index)) != 0U) {
                points = (uint16_t)(points +
                    card_points(state->hands[player][index]));
            }
        }
        if (candidate_count > best_count ||
            (candidate_count == best_count && points > best_points)) {
            best_mask = mask;
            best_count = candidate_count;
            best_points = points;
        }
    }
    return (uint8_t)best_mask;
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
        const uint8_t discard_index = state->discard_count == 0U
            ? P4_RUMMY_NO_CARD
            : (uint8_t)(state->discard_count - 1U);
        return p4_rummy_draw(state, player, source, discard_index);
    }
    if (state->phase == P4_RUMMY_PHASE_DISCARD) {
        for (uint8_t played = 0U; played < P4_RUMMY_MAX_MELDS; ++played) {
            const uint8_t mask = choose_cpu_meld(state, player);
            if (mask == 0U || !p4_rummy_play_meld(state, player, mask)) {
                break;
            }
            if (state->phase == P4_RUMMY_PHASE_ROUND_OVER) {
                return true;
            }
        }
        return p4_rummy_discard_card(
            state, player, choose_cpu_discard(state, player));
    }
    return false;
}
