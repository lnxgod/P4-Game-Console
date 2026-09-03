// SPDX-License-Identifier: MIT

#include "texas_holdem_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

const uint16_t texas_holdem_starting_stacks[TEXAS_HOLDEM_STACK_CHOICES] = {
    500U, 1000U, 2000U, 5000U,
};

static uint8_t bit_count(uint8_t value)
{
    uint8_t count = 0U;
    while (value != 0U) {
        count = (uint8_t)(count + (value & 1U));
        value >>= 1U;
    }
    return count;
}

static uint8_t next_in_mask(uint8_t player_count, uint8_t after,
                            uint8_t mask)
{
    for (uint8_t step = 1U; step <= player_count; ++step) {
        const uint8_t player = (uint8_t)((after + step) % player_count);
        if ((mask & (UINT8_C(1) << player)) != 0U) {
            return player;
        }
    }
    return after;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    if (value == 0U) {
        value = UINT32_C(0x54455841);
    }
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void clear_cards(texas_holdem_state_t *state)
{
    memset(state->hole, TEXAS_HOLDEM_NO_CARD, sizeof(state->hole));
    memset(state->community, TEXAS_HOLDEM_NO_CARD,
           sizeof(state->community));
}

static void shuffle_deck(texas_holdem_state_t *state)
{
    for (uint8_t card = 0U; card < TEXAS_HOLDEM_DECK_CARDS; ++card) {
        state->deck[card] = card;
    }
    for (uint8_t remaining = TEXAS_HOLDEM_DECK_CARDS;
         remaining > 1U; --remaining) {
        const uint8_t pick = (uint8_t)(
            next_random(&state->rng) % remaining);
        const uint8_t last = (uint8_t)(remaining - 1U);
        const uint8_t temporary = state->deck[pick];
        state->deck[pick] = state->deck[last];
        state->deck[last] = temporary;
    }
    state->deck_index = 0U;
}

static uint16_t contribution_sum(const texas_holdem_state_t *state)
{
    uint32_t total = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        total += state->contribution[player];
    }
    return (uint16_t)total;
}

static uint8_t contender_mask(const texas_holdem_state_t *state)
{
    return (uint8_t)(state->active_mask & (uint8_t)~state->folded_mask);
}

static uint8_t actor_mask(const texas_holdem_state_t *state)
{
    return (uint8_t)(contender_mask(state) &
                     (uint8_t)~state->all_in_mask);
}

static void mark_all_in_if_empty(texas_holdem_state_t *state,
                                 uint8_t player)
{
    if (state->stacks[player] == 0U) {
        state->all_in_mask |= (uint8_t)(UINT8_C(1) << player);
    }
}

static void post_blind(texas_holdem_state_t *state, uint8_t player,
                       uint16_t blind)
{
    const uint16_t paid = state->stacks[player] < blind
        ? state->stacks[player] : blind;
    state->stacks[player] = (uint16_t)(state->stacks[player] - paid);
    state->round_bet[player] = paid;
    state->contribution[player] = paid;
    mark_all_in_if_empty(state, player);
}

static uint8_t first_actor_after(const texas_holdem_state_t *state,
                                 uint8_t after)
{
    const uint8_t actors = actor_mask(state);
    return actors == 0U ? after
                        : next_in_mask(state->player_count, after, actors);
}

static bool betting_phase(texas_holdem_phase_t phase)
{
    return phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
        phase <= TEXAS_HOLDEM_PHASE_RIVER;
}

bool texas_holdem_player_is_cpu(const texas_holdem_state_t *state,
                                uint8_t player)
{
    return state != NULL && player < state->player_count &&
        (state->cpu_mask & (UINT8_C(1) << player)) != 0U;
}

static bool pass_needed_for_current_player(
    const texas_holdem_state_t *state)
{
    return !state->network_mode &&
        !texas_holdem_player_is_cpu(state, state->current_player);
}

static bool round_complete(const texas_holdem_state_t *state)
{
    const uint8_t actors = actor_mask(state);
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        const uint8_t bit = (uint8_t)(UINT8_C(1) << player);
        if ((actors & bit) != 0U &&
            (((state->acted_mask & bit) == 0U) ||
             state->round_bet[player] != state->current_bet)) {
            return false;
        }
    }
    return true;
}

static void award_uncontested(texas_holdem_state_t *state, uint8_t winner)
{
    const uint16_t pot = contribution_sum(state);
    state->stacks[winner] = (uint16_t)(state->stacks[winner] + pot);
    state->pot = pot;
    memset(state->round_bet, 0, sizeof(state->round_bet));
    memset(state->contribution, 0, sizeof(state->contribution));
    state->current_bet = 0U;
    state->winner_mask = (uint8_t)(UINT8_C(1) << winner);
    state->current_player = winner;
    state->phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
    state->pass_required = false;
}

static void deal_to_community(texas_holdem_state_t *state,
                              uint8_t target_count)
{
    while (state->community_count < target_count &&
           state->deck_index < TEXAS_HOLDEM_DECK_CARDS) {
        state->community[state->community_count++] =
            state->deck[state->deck_index++];
    }
}

static void advance_betting_round(texas_holdem_state_t *state)
{
    for (uint8_t street = 0U; street < 4U; ++street) {
        memset(state->round_bet, 0, sizeof(state->round_bet));
        state->current_bet = 0U;
        state->min_raise = TEXAS_HOLDEM_BIG_BLIND;
        state->acted_mask = 0U;
        if (state->phase == TEXAS_HOLDEM_PHASE_PREFLOP) {
            state->phase = TEXAS_HOLDEM_PHASE_FLOP;
            deal_to_community(state, 3U);
        } else if (state->phase == TEXAS_HOLDEM_PHASE_FLOP) {
            state->phase = TEXAS_HOLDEM_PHASE_TURN;
            deal_to_community(state, 4U);
        } else if (state->phase == TEXAS_HOLDEM_PHASE_TURN) {
            state->phase = TEXAS_HOLDEM_PHASE_RIVER;
            deal_to_community(state, 5U);
        } else {
            texas_holdem_resolve_showdown(state);
            return;
        }
        if (actor_mask(state) != 0U) {
            state->current_player = first_actor_after(state, state->dealer);
            state->action_selection = TEXAS_HOLDEM_ACTION_CALL;
            state->pass_required = pass_needed_for_current_player(state);
            return;
        }
    }
}

static void start_hand(texas_holdem_state_t *state)
{
    uint8_t live_mask = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        if (state->stacks[player] != 0U) {
            live_mask |= (uint8_t)(UINT8_C(1) << player);
        }
    }
    if (bit_count(live_mask) < 2U) {
        state->active_mask = live_mask;
        state->folded_mask = 0U;
        state->all_in_mask = 0U;
        state->acted_mask = 0U;
        state->winner_mask = live_mask;
        state->pot = 0U;
        state->current_bet = 0U;
        state->min_raise = TEXAS_HOLDEM_BIG_BLIND;
        state->community_count = 0U;
        memset(state->round_bet, 0, sizeof(state->round_bet));
        memset(state->contribution, 0, sizeof(state->contribution));
        clear_cards(state);
        state->current_player = live_mask == 0U ? 0U : next_in_mask(
            state->player_count, state->dealer, live_mask);
        state->phase = TEXAS_HOLDEM_PHASE_MATCH_OVER;
        state->pass_required = false;
        state->cpu_think_ms = 0U;
        return;
    }

    state->dealer = next_in_mask(state->player_count, state->dealer,
                                 live_mask);
    state->active_mask = live_mask;
    state->folded_mask = 0U;
    state->all_in_mask = 0U;
    state->acted_mask = 0U;
    state->winner_mask = 0U;
    state->pot = 0U;
    state->current_bet = 0U;
    state->min_raise = TEXAS_HOLDEM_BIG_BLIND;
    state->community_count = 0U;
    memset(state->round_bet, 0, sizeof(state->round_bet));
    memset(state->contribution, 0, sizeof(state->contribution));
    clear_cards(state);
    shuffle_deck(state);

    for (uint8_t deal = 0U; deal < TEXAS_HOLDEM_HOLE_CARDS; ++deal) {
        uint8_t player = state->dealer;
        for (uint8_t seat = 0U; seat < state->player_count; ++seat) {
            player = next_in_mask(state->player_count, player, live_mask);
            state->hole[player][deal] = state->deck[state->deck_index++];
        }
    }

    const uint8_t small_blind = bit_count(live_mask) == 2U
        ? state->dealer
        : next_in_mask(state->player_count, state->dealer, live_mask);
    const uint8_t big_blind = next_in_mask(
        state->player_count, small_blind, live_mask);
    post_blind(state, small_blind, TEXAS_HOLDEM_SMALL_BLIND);
    post_blind(state, big_blind, TEXAS_HOLDEM_BIG_BLIND);
    state->current_bet = TEXAS_HOLDEM_BIG_BLIND;
    state->pot = contribution_sum(state);
    state->phase = TEXAS_HOLDEM_PHASE_PREFLOP;
    state->current_player = first_actor_after(state, big_blind);
    state->action_selection = TEXAS_HOLDEM_ACTION_CALL;
    state->pass_required = pass_needed_for_current_player(state);
    state->cpu_think_ms = 0U;
    ++state->hand_number;

    if (actor_mask(state) == 0U) {
        advance_betting_round(state);
    }
}

void texas_holdem_reset_lobby(texas_holdem_state_t *state,
                              uint8_t player_count, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    if (player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        player_count > TEXAS_HOLDEM_PLAYERS) {
        player_count = TEXAS_HOLDEM_PLAYERS;
    }
    *state = (texas_holdem_state_t){
        .starting_stack = texas_holdem_starting_stacks[1],
        .player_count = player_count,
        .setup_index = 1U,
        .dealer = (uint8_t)(player_count - 1U),
        .phase = TEXAS_HOLDEM_PHASE_SETUP,
        .rng = seed == 0U ? UINT32_C(0x54455841) : seed,
        .revision = 1U,
        .action_selection = TEXAS_HOLDEM_ACTION_CALL,
    };
    clear_cards(state);
    for (uint8_t player = 0U; player < player_count; ++player) {
        state->stacks[player] = state->starting_stack;
    }
}

bool texas_holdem_adjust_stack(texas_holdem_state_t *state, bool increase)
{
    if (state == NULL || state->phase != TEXAS_HOLDEM_PHASE_SETUP) {
        return false;
    }
    if (increase) {
        state->setup_index = (uint8_t)(
            (state->setup_index + 1U) % TEXAS_HOLDEM_STACK_CHOICES);
    } else {
        state->setup_index = state->setup_index == 0U
            ? TEXAS_HOLDEM_STACK_CHOICES - 1U
            : (uint8_t)(state->setup_index - 1U);
    }
    state->starting_stack =
        texas_holdem_starting_stacks[state->setup_index];
    memset(state->stacks, 0, sizeof(state->stacks));
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        state->stacks[player] = state->starting_stack;
    }
    ++state->revision;
    return true;
}

bool texas_holdem_adjust_cpu_players(texas_holdem_state_t *state,
                                     bool increase)
{
    if (state == NULL || state->phase != TEXAS_HOLDEM_PHASE_SETUP) {
        return false;
    }
    const uint8_t human_count = state->network_mode
        ? state->network_player_count : 1U;
    if (human_count == 0U || human_count > TEXAS_HOLDEM_PLAYERS) {
        return false;
    }
    const uint8_t maximum = (uint8_t)(
        TEXAS_HOLDEM_PLAYERS - human_count);
    if (maximum == 0U) {
        return false;
    }
    uint8_t count = bit_count(state->cpu_mask);
    if (count > maximum) {
        count = maximum;
    }
    if (increase) {
        count = count == maximum ? 0U : (uint8_t)(count + 1U);
    } else {
        count = count == 0U ? maximum : (uint8_t)(count - 1U);
    }

    state->cpu_mask = 0U;
    const uint8_t first_cpu = state->network_mode
        ? human_count : (uint8_t)(TEXAS_HOLDEM_PLAYERS - count);
    for (uint8_t cpu = 0U; cpu < count; ++cpu) {
        state->cpu_mask |= (uint8_t)(
            UINT8_C(1) << (first_cpu + cpu));
    }
    state->player_count = state->network_mode
        ? (uint8_t)(human_count + count) : TEXAS_HOLDEM_PLAYERS;
    state->dealer = (uint8_t)(state->player_count - 1U);
    state->current_player = 0U;
    memset(state->stacks, 0, sizeof(state->stacks));
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        state->stacks[player] = state->starting_stack;
    }
    ++state->revision;
    return true;
}

bool texas_holdem_begin_match(texas_holdem_state_t *state)
{
    if (state == NULL ||
        (state->phase != TEXAS_HOLDEM_PHASE_SETUP &&
         state->phase != TEXAS_HOLDEM_PHASE_MATCH_OVER) ||
        state->player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        state->player_count > TEXAS_HOLDEM_PLAYERS) {
        return false;
    }
    memset(state->stacks, 0, sizeof(state->stacks));
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        state->stacks[player] = state->starting_stack;
    }
    state->dealer = (uint8_t)(state->player_count - 1U);
    state->hand_number = 0U;
    start_hand(state);
    ++state->revision;
    return true;
}

bool texas_holdem_next_hand(texas_holdem_state_t *state)
{
    if (state == NULL || state->phase != TEXAS_HOLDEM_PHASE_SHOWDOWN) {
        return false;
    }
    start_hand(state);
    ++state->revision;
    return true;
}

bool texas_holdem_action_legal(const texas_holdem_state_t *state,
                               uint8_t player, texas_holdem_action_t action)
{
    if (state == NULL || !betting_phase(state->phase) ||
        player >= state->player_count || player != state->current_player ||
        (actor_mask(state) & (UINT8_C(1) << player)) == 0U ||
        action >= TEXAS_HOLDEM_ACTION_COUNT) {
        return false;
    }
    if (action != TEXAS_HOLDEM_ACTION_RAISE) {
        return true;
    }
    const uint16_t call = state->current_bet > state->round_bet[player]
        ? (uint16_t)(state->current_bet - state->round_bet[player]) : 0U;
    return state->stacks[player] >= (uint16_t)(call + state->min_raise) &&
        bit_count(actor_mask(state)) >= 2U;
}

bool texas_holdem_apply_action(texas_holdem_state_t *state,
                               uint8_t player, texas_holdem_action_t action)
{
    if (!texas_holdem_action_legal(state, player, action)) {
        return false;
    }
    const uint8_t player_bit = (uint8_t)(UINT8_C(1) << player);
    if (action == TEXAS_HOLDEM_ACTION_FOLD) {
        state->folded_mask |= player_bit;
        state->acted_mask |= player_bit;
    } else {
        uint16_t target = state->current_bet;
        if (action == TEXAS_HOLDEM_ACTION_RAISE) {
            target = (uint16_t)(state->current_bet + state->min_raise);
        }
        const uint16_t needed = target > state->round_bet[player]
            ? (uint16_t)(target - state->round_bet[player]) : 0U;
        const uint16_t paid = state->stacks[player] < needed
            ? state->stacks[player] : needed;
        state->stacks[player] = (uint16_t)(state->stacks[player] - paid);
        state->round_bet[player] =
            (uint16_t)(state->round_bet[player] + paid);
        state->contribution[player] =
            (uint16_t)(state->contribution[player] + paid);
        mark_all_in_if_empty(state, player);
        if (action == TEXAS_HOLDEM_ACTION_RAISE) {
            state->current_bet = state->round_bet[player];
            state->acted_mask = player_bit;
        } else {
            state->acted_mask |= player_bit;
        }
    }
    state->pot = contribution_sum(state);

    const uint8_t contenders = contender_mask(state);
    if (bit_count(contenders) == 1U) {
        award_uncontested(state, next_in_mask(
            state->player_count, state->current_player, contenders));
    } else if (round_complete(state)) {
        advance_betting_round(state);
    } else {
        state->current_player = first_actor_after(state, player);
        state->action_selection = TEXAS_HOLDEM_ACTION_CALL;
        state->pass_required = pass_needed_for_current_player(state);
        state->cpu_think_ms = 0U;
    }
    ++state->revision;
    return true;
}

static uint32_t pack_rank(uint8_t category, const uint8_t values[5])
{
    return (uint32_t)category << 20U |
        (uint32_t)values[0] << 16U |
        (uint32_t)values[1] << 12U |
        (uint32_t)values[2] << 8U |
        (uint32_t)values[3] << 4U |
        values[4];
}

uint32_t texas_holdem_five_card_rank(const uint8_t cards[5])
{
    uint8_t rank_count[15] = {0};
    uint8_t suit_count[4] = {0};
    for (uint8_t index = 0U; index < 5U; ++index) {
        const uint8_t rank = (uint8_t)(cards[index] % 13U + 2U);
        const uint8_t suit = (uint8_t)(cards[index] / 13U);
        ++rank_count[rank];
        ++suit_count[suit];
    }
    bool flush = false;
    for (uint8_t suit = 0U; suit < 4U; ++suit) {
        flush = flush || suit_count[suit] == 5U;
    }
    uint8_t straight_high = 0U;
    for (uint8_t high = 14U; high >= 5U; --high) {
        bool straight = true;
        for (uint8_t offset = 0U; offset < 5U; ++offset) {
            uint8_t rank = (uint8_t)(high - offset);
            if (high == 5U && rank == 1U) {
                rank = 14U;
            }
            if (rank_count[rank] == 0U) {
                straight = false;
            }
        }
        if (straight) {
            straight_high = high;
            break;
        }
        if (high == 5U) {
            break;
        }
    }

    uint8_t four = 0U;
    uint8_t three = 0U;
    uint8_t pairs[2] = {0U, 0U};
    uint8_t pair_count = 0U;
    uint8_t singles[5] = {0U, 0U, 0U, 0U, 0U};
    uint8_t single_count = 0U;
    for (uint8_t rank = 14U; rank >= 2U; --rank) {
        if (rank_count[rank] == 4U) {
            four = rank;
        } else if (rank_count[rank] == 3U && three == 0U) {
            three = rank;
        } else if (rank_count[rank] == 2U && pair_count < 2U) {
            pairs[pair_count++] = rank;
        } else if (rank_count[rank] == 1U && single_count < 5U) {
            singles[single_count++] = rank;
        }
        if (rank == 2U) {
            break;
        }
    }
    uint8_t values[5] = {0U, 0U, 0U, 0U, 0U};
    if (flush && straight_high != 0U) {
        values[0] = straight_high;
        return pack_rank(8U, values);
    }
    if (four != 0U) {
        values[0] = four;
        values[1] = singles[0];
        return pack_rank(7U, values);
    }
    if (three != 0U && pair_count != 0U) {
        values[0] = three;
        values[1] = pairs[0];
        return pack_rank(6U, values);
    }
    if (flush) {
        uint8_t value_index = 0U;
        for (uint8_t rank = 14U; rank >= 2U && value_index < 5U; --rank) {
            for (uint8_t copy = 0U; copy < rank_count[rank]; ++copy) {
                values[value_index++] = rank;
            }
            if (rank == 2U) {
                break;
            }
        }
        return pack_rank(5U, values);
    }
    if (straight_high != 0U) {
        values[0] = straight_high;
        return pack_rank(4U, values);
    }
    if (three != 0U) {
        values[0] = three;
        values[1] = singles[0];
        values[2] = singles[1];
        return pack_rank(3U, values);
    }
    if (pair_count >= 2U) {
        values[0] = pairs[0];
        values[1] = pairs[1];
        values[2] = singles[0];
        return pack_rank(2U, values);
    }
    if (pair_count == 1U) {
        values[0] = pairs[0];
        values[1] = singles[0];
        values[2] = singles[1];
        values[3] = singles[2];
        return pack_rank(1U, values);
    }
    for (uint8_t index = 0U; index < single_count; ++index) {
        values[index] = singles[index];
    }
    return pack_rank(0U, values);
}

uint32_t texas_holdem_best_hand_rank(
    const uint8_t hole[TEXAS_HOLDEM_HOLE_CARDS],
    const uint8_t community[TEXAS_HOLDEM_COMMUNITY_CARDS])
{
    uint8_t cards[7] = {
        hole[0], hole[1], community[0], community[1],
        community[2], community[3], community[4],
    };
    uint32_t best = 0U;
    for (uint8_t a = 0U; a < 3U; ++a) {
        for (uint8_t b = (uint8_t)(a + 1U); b < 4U; ++b) {
            for (uint8_t c = (uint8_t)(b + 1U); c < 5U; ++c) {
                for (uint8_t d = (uint8_t)(c + 1U); d < 6U; ++d) {
                    for (uint8_t e = (uint8_t)(d + 1U); e < 7U; ++e) {
                        const uint8_t five[5] = {
                            cards[a], cards[b], cards[c], cards[d], cards[e],
                        };
                        const uint32_t rank = texas_holdem_five_card_rank(five);
                        if (rank > best) {
                            best = rank;
                        }
                    }
                }
            }
        }
    }
    return best;
}

static uint32_t best_known_rank(const texas_holdem_state_t *state,
                                uint8_t player)
{
    uint8_t cards[7] = {
        state->hole[player][0], state->hole[player][1],
        TEXAS_HOLDEM_NO_CARD, TEXAS_HOLDEM_NO_CARD,
        TEXAS_HOLDEM_NO_CARD, TEXAS_HOLDEM_NO_CARD,
        TEXAS_HOLDEM_NO_CARD,
    };
    const uint8_t card_count = (uint8_t)(
        TEXAS_HOLDEM_HOLE_CARDS + state->community_count);
    for (uint8_t index = 0U; index < state->community_count; ++index) {
        cards[TEXAS_HOLDEM_HOLE_CARDS + index] = state->community[index];
    }
    if (card_count < 5U) {
        return 0U;
    }
    uint32_t best = 0U;
    for (uint8_t a = 0U; a + 4U < card_count; ++a) {
        for (uint8_t b = (uint8_t)(a + 1U); b + 3U < card_count; ++b) {
            for (uint8_t c = (uint8_t)(b + 1U); c + 2U < card_count; ++c) {
                for (uint8_t d = (uint8_t)(c + 1U); d + 1U < card_count;
                     ++d) {
                    for (uint8_t e = (uint8_t)(d + 1U); e < card_count;
                         ++e) {
                        const uint8_t five[5] = {
                            cards[a], cards[b], cards[c], cards[d], cards[e],
                        };
                        const uint32_t rank = texas_holdem_five_card_rank(five);
                        if (rank > best) {
                            best = rank;
                        }
                    }
                }
            }
        }
    }
    return best;
}

texas_holdem_action_t texas_holdem_choose_cpu_action(
    texas_holdem_state_t *state, uint8_t player)
{
    if (state == NULL || !texas_holdem_player_is_cpu(state, player) ||
        player != state->current_player) {
        return TEXAS_HOLDEM_ACTION_FOLD;
    }
    const uint16_t call = state->current_bet > state->round_bet[player]
        ? (uint16_t)(state->current_bet - state->round_bet[player]) : 0U;
    const uint8_t first_rank = (uint8_t)(state->hole[player][0] % 13U + 2U);
    const uint8_t second_rank = (uint8_t)(state->hole[player][1] % 13U + 2U);
    const uint8_t high = first_rank > second_rank ? first_rank : second_rank;
    const uint8_t low = first_rank > second_rank ? second_rank : first_rank;
    const uint8_t gap = (uint8_t)(high - low);
    uint8_t strength = (uint8_t)(high + low / 2U);
    if (first_rank == second_rank) {
        strength = (uint8_t)(strength + 10U);
    }
    if (state->hole[player][0] / 13U == state->hole[player][1] / 13U) {
        strength = (uint8_t)(strength + 2U);
    }
    if (gap <= 2U) {
        strength = (uint8_t)(strength + 2U);
    }

    const uint8_t category = (uint8_t)(
        best_known_rank(state, player) >> 20U);
    const uint32_t roll = next_random(&state->rng) % 100U;
    const bool strong = state->community_count == 0U
        ? strength >= 24U : category >= 2U;
    const bool playable = state->community_count == 0U
        ? strength >= 17U : category >= 1U || high >= 13U;
    const bool cheap = call <= state->min_raise ||
        (state->stacks[player] != 0U &&
         call <= (uint16_t)(state->stacks[player] / 8U));

    if (strong && roll < 55U && texas_holdem_action_legal(
            state, player, TEXAS_HOLDEM_ACTION_RAISE)) {
        return TEXAS_HOLDEM_ACTION_RAISE;
    }
    if (call == 0U || strong || (playable && cheap) || roll < 12U) {
        return TEXAS_HOLDEM_ACTION_CALL;
    }
    return TEXAS_HOLDEM_ACTION_FOLD;
}

void texas_holdem_resolve_showdown(texas_holdem_state_t *state)
{
    if (state == NULL) {
        return;
    }
    const uint8_t contenders = contender_mask(state);
    uint32_t ranks[TEXAS_HOLDEM_PLAYERS] = {0U, 0U, 0U, 0U};
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        if ((contenders & (UINT8_C(1) << player)) != 0U) {
            ranks[player] = texas_holdem_best_hand_rank(
                state->hole[player], state->community);
        }
    }
    state->pot = contribution_sum(state);
    state->winner_mask = 0U;
    uint16_t previous = 0U;
    for (uint8_t layer_index = 0U;
         layer_index < state->player_count; ++layer_index) {
        uint16_t level = UINT16_MAX;
        for (uint8_t player = 0U; player < state->player_count; ++player) {
            const uint16_t amount = state->contribution[player];
            if (amount > previous && amount < level) {
                level = amount;
            }
        }
        if (level == UINT16_MAX) {
            break;
        }
        uint8_t contributors = 0U;
        uint8_t eligible = 0U;
        for (uint8_t player = 0U; player < state->player_count; ++player) {
            if (state->contribution[player] >= level) {
                ++contributors;
                if ((contenders & (UINT8_C(1) << player)) != 0U) {
                    eligible |= (uint8_t)(UINT8_C(1) << player);
                }
            }
        }
        uint32_t best = 0U;
        uint8_t winners = 0U;
        for (uint8_t player = 0U; player < state->player_count; ++player) {
            const uint8_t bit = (uint8_t)(UINT8_C(1) << player);
            if ((eligible & bit) == 0U) {
                continue;
            }
            if (winners == 0U || ranks[player] > best) {
                best = ranks[player];
                winners = bit;
            } else if (ranks[player] == best) {
                winners |= bit;
            }
        }
        const uint16_t layer = (uint16_t)(
            (uint16_t)(level - previous) * contributors);
        const uint8_t winner_count = bit_count(winners);
        if (winner_count != 0U) {
            const uint16_t share = (uint16_t)(layer / winner_count);
            uint8_t remainder = (uint8_t)(layer % winner_count);
            for (uint8_t step = 1U; step <= state->player_count; ++step) {
                const uint8_t player = (uint8_t)(
                    (state->dealer + step) % state->player_count);
                if ((winners & (UINT8_C(1) << player)) != 0U) {
                    state->stacks[player] = (uint16_t)(
                        state->stacks[player] + share +
                        (remainder != 0U ? 1U : 0U));
                    if (remainder != 0U) {
                        --remainder;
                    }
                }
            }
            state->winner_mask |= winners;
        }
        previous = level;
    }
    memset(state->round_bet, 0, sizeof(state->round_bet));
    memset(state->contribution, 0, sizeof(state->contribution));
    state->current_bet = 0U;
    state->acted_mask = 0U;
    state->phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
    state->pass_required = false;
    if (state->winner_mask != 0U) {
        state->current_player = next_in_mask(
            state->player_count, state->dealer, state->winner_mask);
    }
}

bool texas_holdem_local_turn(const texas_holdem_state_t *state)
{
    if (state == NULL || !betting_phase(state->phase)) {
        return false;
    }
    if (texas_holdem_player_is_cpu(state, state->current_player)) {
        return false;
    }
    if (!state->network_mode) {
        return !state->pass_required;
    }
    return state->network_started && !state->network_request_pending &&
        state->local_player_slot == state->current_player;
}
