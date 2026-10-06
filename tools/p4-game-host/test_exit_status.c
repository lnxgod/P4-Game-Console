// SPDX-License-Identifier: MIT
// Exercise the real SDL runner entry point without a visible window or hardware.
#define P4_HOST_GAME_DESCRIPTOR p4_exit_test_game
#define main p4_runner_main
#include "play_game.c"
#undef main

static unsigned failure_mode;
static bool fixture_start(p4_game_context_t *context)
{
    (void)context;
    return true;
}
static p4_game_result_t fixture_update(p4_game_context_t *context,
    const p4_game_input_t *input, uint32_t elapsed_ms)
{
    (void)context; (void)input; (void)elapsed_ms;
    return failure_mode == 0U ? P4_GAME_ERROR :
        failure_mode == 2U ? P4_GAME_EXIT_TO_LAUNCHER : P4_GAME_CONTINUE;
}
static bool fixture_render(p4_game_context_t *context, p4_game_surface_t *surface)
{
    (void)context; (void)surface;
    return failure_mode != 1U;
}
static void fixture_stop(p4_game_context_t *context) { (void)context; }
const p4_game_descriptor_t p4_exit_test_game = {
    .api_version = P4_GAME_API_VERSION, .launcher_id = 900U,
    .id = "org.p4console.runner-exit-test", .title = "Exit Test",
    .subtitle = "Runner failure regression", .state_bytes = sizeof(uint32_t),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .start = fixture_start, .update = fixture_update,
    .render = fixture_render, .stop = fixture_stop,
};
int main(void)
{
    char *arguments[] = {"runner-exit-test", "--frames", "1", NULL};
    for (failure_mode = 0U; failure_mode < 3U; ++failure_mode) {
        const int expected = failure_mode == 2U ? EXIT_SUCCESS : EXIT_FAILURE;
        const int actual = p4_runner_main(3, arguments);
        if (actual != expected) {
            fprintf(stderr, "runner exit mode %u: got %d, expected %d\n",
                    failure_mode, actual, expected);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
