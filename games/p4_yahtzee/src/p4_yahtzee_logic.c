// SPDX-License-Identifier: MIT

#include "p4_yahtzee_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    ROLL_ANIMATION_MS = 330,
    ROLL_ANIMATION_STEP_MS = 55,
};

static uint32_t random_next(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static uint8_t random_die(uint32_t *state)
{
    return (uint8_t)(random_next(state) % 6U + 1U);
}

int p4_yahtzee_score_dice(
    const uint8_t dice[P4_YAHTZEE_DICE],
    p4_yahtzee_category_t category)
{
    if (dice == NULL || category < P4_YAHTZEE_ONES ||
        category > P4_YAHTZEE_CHANCE) {
        return 0;
    }
    uint8_t counts[7] = {0};
    int sum = 0;
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        if (dice[index] < 1U || dice[index] > 6U) {
            return 0;
        }
        ++counts[dice[index]];
        sum += dice[index];
    }
    if (category <= P4_YAHTZEE_SIXES) {
        const uint8_t face = (uint8_t)category + 1U;
        return (int)face * counts[face];
    }
    uint8_t longest = 0U;
    uint8_t run = 0U;
    uint8_t maximum = 0U;
    bool has_pair = false;
    bool has_three = false;
    for (uint8_t face = 1U; face <= 6U; ++face) {
        if (counts[face] != 0U) {
            ++run;
            if (run > longest) {
                longest = run;
            }
        } else {
            run = 0U;
        }
        if (counts[face] > maximum) {
            maximum = counts[face];
        }
        has_pair = has_pair || counts[face] == 2U;
        has_three = has_three || counts[face] == 3U;
    }
    switch (category) {
        case P4_YAHTZEE_THREE_KIND: return maximum >= 3U ? sum : 0;
        case P4_YAHTZEE_FOUR_KIND: return maximum >= 4U ? sum : 0;
        case P4_YAHTZEE_FULL_HOUSE: return has_pair && has_three ? 25 : 0;
        case P4_YAHTZEE_SMALL_STRAIGHT: return longest >= 4U ? 30 : 0;
        case P4_YAHTZEE_LARGE_STRAIGHT: return longest >= 5U ? 40 : 0;
        case P4_YAHTZEE_FIVE_KIND: return maximum == 5U ? 50 : 0;
        case P4_YAHTZEE_CHANCE: return sum;
        default: return 0;
    }
}

int p4_yahtzee_upper_total(const p4_yahtzee_state_t *state, uint8_t player)
{
    if (state == NULL || player >= P4_YAHTZEE_PLAYERS) {
        return 0;
    }
    int total = 0;
    for (size_t category = 0U; category <= P4_YAHTZEE_SIXES; ++category) {
        if (state->scores[player][category] >= 0) {
            total += state->scores[player][category];
        }
    }
    return total;
}

int p4_yahtzee_total(const p4_yahtzee_state_t *state, uint8_t player)
{
    if (state == NULL || player >= P4_YAHTZEE_PLAYERS) {
        return 0;
    }
    int total = 0;
    for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
         ++category) {
        if (state->scores[player][category] >= 0) {
            total += state->scores[player][category];
        }
    }
    return total + (p4_yahtzee_upper_total(state, player) >= 63 ? 35 : 0);
}

void p4_yahtzee_reset_match(p4_yahtzee_state_t *state, uint32_t seed)
{
    if (state == NULL) {
        return;
    }
    const p4_yahtzee_mode_t mode = state->mode;
    const uint8_t local_slot = state->local_player_slot;
    const p4_game_multiplayer_role_t role = state->network_role;
    const uint64_t network_seed = state->network_seed;
    memset(state, 0, sizeof(*state));
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            state->scores[player][category] = -1;
        }
    }
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        state->dice[index] = (uint8_t)index + 1U;
        state->animation_dice[index] = state->dice[index];
    }
    state->mode = mode;
    state->local_player_slot = local_slot;
    state->network_role = role;
    state->network_seed = network_seed;
    state->phase = P4_YAHTZEE_TURN;
    state->focus = P4_YAHTZEE_FOCUS_DICE;
    state->rng = seed == 0U ? UINT32_C(0x59414854) : seed;
}

bool p4_yahtzee_roll(p4_yahtzee_state_t *state)
{
    if (state == NULL || state->phase != P4_YAHTZEE_TURN ||
        state->roll_count >= P4_YAHTZEE_ROLLS_PER_TURN ||
        state->roll_animation_ms != 0U) {
        return false;
    }
    for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
        if ((state->held_mask & (UINT8_C(1) << index)) == 0U) {
            state->dice[index] = random_die(&state->rng);
            state->animation_dice[index] = random_die(&state->rng);
        }
    }
    ++state->roll_count;
    state->roll_animation_ms = ROLL_ANIMATION_MS;
    state->animation_step_ms = ROLL_ANIMATION_STEP_MS;
    return true;
}

bool p4_yahtzee_score_turn(
    p4_yahtzee_state_t *state, p4_yahtzee_category_t category)
{
    if (state == NULL || category < P4_YAHTZEE_ONES ||
        category > P4_YAHTZEE_CHANCE || state->phase != P4_YAHTZEE_TURN ||
        state->roll_count == 0U || state->roll_animation_ms != 0U ||
        state->scores[state->current_player][category] >= 0) {
        return false;
    }
    state->scores[state->current_player][category] = (int16_t)
        p4_yahtzee_score_dice(state->dice, category);
    ++state->turns_scored[state->current_player];
    state->held_mask = 0U;
    state->roll_count = 0U;
    state->selected_die = 0U;
    state->focus = P4_YAHTZEE_FOCUS_DICE;
    if (state->turns_scored[0] >= P4_YAHTZEE_CATEGORIES &&
        state->turns_scored[1] >= P4_YAHTZEE_CATEGORIES) {
        state->phase = P4_YAHTZEE_GAME_OVER;
    } else {
        state->current_player ^= 1U;
        state->phase = state->mode == P4_YAHTZEE_LOCAL
            ? P4_YAHTZEE_PASS : P4_YAHTZEE_TURN;
    }
    return true;
}

void p4_yahtzee_update_animation(p4_yahtzee_state_t *state,
                                 uint32_t elapsed_ms)
{
    if (state == NULL || state->roll_animation_ms == 0U) {
        return;
    }
    if (elapsed_ms >= state->roll_animation_ms) {
        state->roll_animation_ms = 0U;
        state->animation_step_ms = 0U;
        memcpy(state->animation_dice, state->dice, sizeof(state->dice));
        return;
    }
    state->roll_animation_ms -= elapsed_ms;
    if (elapsed_ms >= state->animation_step_ms) {
        for (size_t index = 0U; index < P4_YAHTZEE_DICE; ++index) {
            if ((state->held_mask & (UINT8_C(1) << index)) == 0U) {
                state->animation_dice[index] = random_die(&state->rng);
            }
        }
        state->animation_step_ms = ROLL_ANIMATION_STEP_MS;
    } else {
        state->animation_step_ms -= elapsed_ms;
    }
}
