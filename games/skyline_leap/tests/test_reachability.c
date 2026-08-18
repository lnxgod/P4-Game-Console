// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

/* Keep every collectible vertically reachable with exactly one grounded jump. */
#include "../src/skyline_leap.c"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void test_all_shards_clear_one_jump(void)
{
    for (uint8_t stage = 0U; stage < STAGE_COUNT; ++stage) {
        for (uint8_t shard = 0U; shard < SHARD_COUNT; ++shard) {
            skyline_leap_state_t state = {
                .level = stage,
                .lives = 3U,
            };
            reset_stage(&state);
            state.player_x = (int16_t)(s_shards[stage][shard].x -
                                       PLAYER_WIDTH / 2);
            for (uint8_t bot = 0U; bot < MAX_BOTS; ++bot) {
                state.bot_active[bot] = false;
            }
            state.velocity_y = JUMP_SPEED;
            p4_game_context_t context = {
                .state = &state,
                .state_bytes = sizeof(state),
            };
            for (unsigned tick = 0U; tick < 16U; ++tick) {
                CHECK(move_player(&context, &state));
                collect_shards(&context, &state);
            }
            CHECK(state.shard_collected[shard]);
        }
    }
}

int main(void)
{
    test_all_shards_clear_one_jump();
    if (s_failures != 0) {
        fprintf(stderr, "%d Skyline Leap reachability test failure(s)\n",
                s_failures);
        return EXIT_FAILURE;
    }
    puts("skyline_leap reachability tests passed");
    return EXIT_SUCCESS;
}
