// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* Exercise the internal deterministic board physics, including clear routes. */
#include "../src/orble.c"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static unsigned targets_left(uint16_t targets)
{
    unsigned count = 0U;
    while (targets != 0U) {
        count += targets & UINT16_C(1);
        targets >>= 1U;
    }
    return count;
}

static bool run_shot(p4_game_context_t *context, orble_state_t *state,
                     uint8_t level, int8_t aim)
{
    state->aim = aim;
    launch_ball(context, state);
    for (unsigned frame = 0U; frame < 800U; ++frame) {
        simulate_step(context, state);
        if (state->won || state->level != level) {
            return true;
        }
        if (!state->ball_flying || state->game_over) {
            return false;
        }
    }
    return false;
}

static void test_each_board_has_a_six_ball_clear_route(void)
{
    for (uint8_t level = 0U; level < ORBLE_LEVEL_COUNT; ++level) {
        orble_state_t state;
        reset_campaign(&state);
        begin_level(&state, level);
        p4_game_context_t context = {
            .state = &state,
            .state_bytes = sizeof(state),
        };
        bool completed = false;
        for (unsigned shot = 0U; shot < ORBLE_MAX_BALLS && !completed;
             ++shot) {
            unsigned best_remaining = ORBLE_TARGET_COUNT + 1U;
            orble_state_t best = state;
            for (int8_t aim = -5; aim <= 5; ++aim) {
                orble_state_t candidate = state;
                context.state = &candidate;
                if (run_shot(&context, &candidate, level, aim)) {
                    completed = true;
                    break;
                }
                const unsigned remaining = targets_left(candidate.targets);
                if (!candidate.game_over &&
                    (remaining < best_remaining ||
                     (remaining == best_remaining &&
                      candidate.balls > best.balls))) {
                    best_remaining = remaining;
                    best = candidate;
                }
            }
            state = best;
        }
        CHECK(completed);
    }
}

static void test_descriptor(void)
{
    CHECK(p4_game_descriptor_valid(&p4_orble_game));
    CHECK(p4_orble_game.launcher_id == 109U);
}

int main(void)
{
    test_descriptor();
    test_each_board_has_a_six_ball_clear_route();
    if (s_failures != 0) {
        fprintf(stderr, "%d Orble test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("orble tests passed");
    return EXIT_SUCCESS;
}
