// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

/* Test wave progression and cannon behavior against the real game physics. */
#include "../src/planet_smash.c"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void test_paced_waves_and_clear_progression(void)
{
    planet_smash_state_t state;
    reset_game(&state);
    p4_game_context_t context = {
        .state = &state,
        .state_bytes = sizeof(state),
    };
    simulate_step(&context, &state);
    CHECK(state.pending_meteors == meteor_count_for_wave(1U) - 1U);
    CHECK(any_meteors(&state));

    for (uint8_t wave = 1U; wave <= SMASH_WAVE_COUNT; ++wave) {
        start_wave(&state, wave);
        state.pending_meteors = 0U;
        simulate_step(&context, &state);
        if (wave < SMASH_WAVE_COUNT) {
            CHECK(state.wave == wave + 1U);
            CHECK(state.pending_meteors == meteor_count_for_wave(wave + 1U));
        }
    }
    CHECK(state.won);
}

static void test_cannon_fires_and_descriptor_is_valid(void)
{
    planet_smash_state_t state;
    reset_game(&state);
    p4_game_context_t context = {
        .state = &state,
        .state_bytes = sizeof(state),
    };
    fire_bullet(&context, &state);
    CHECK(state.bullets[0].active);
    CHECK(state.fire_cooldown_ms != 0U);
    CHECK(p4_game_descriptor_valid(&p4_planet_smash_game));
    CHECK(p4_planet_smash_game.launcher_id == 110U);
}

int main(void)
{
    test_paced_waves_and_clear_progression();
    test_cannon_fires_and_descriptor_is_valid();
    if (s_failures != 0) {
        fprintf(stderr, "%d Planet Smash test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("planet_smash tests passed");
    return EXIT_SUCCESS;
}
