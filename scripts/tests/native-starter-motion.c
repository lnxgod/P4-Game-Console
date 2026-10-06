// SPDX-License-Identifier: MIT
// Exercise generated source through its public callbacks, not source matching.
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "p4/game.h"

extern const p4_game_descriptor_t p4_star_hop_game;

static void run_trace(uint16_t *pixels, uint16_t width, uint16_t height,
                      const uint32_t *steps, size_t count)
{
    const p4_game_descriptor_t *game = &p4_star_hop_game;
    void *state = calloc(1U, game->state_bytes);
    assert(state != NULL);
    p4_game_context_t context = {.state = state, .state_bytes = game->state_bytes};
    assert(game->start(&context));
    const p4_game_input_t input = {.held = P4_BUTTON_RIGHT};
    for (size_t i = 0U; i < count; ++i) {
        assert(game->update(&context, &input, steps[i]) == P4_GAME_CONTINUE);
    }
    p4_game_surface_t surface = {
        .pixels = pixels, .stride_pixels = width, .width = width, .height = height,
    };
    assert(game->render(&context, &surface));
    const p4_game_input_t back = {.pressed = P4_BUTTON_BACK};
    assert(game->update(&context, &back, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    game->stop(&context);
    free(state);
}

static void test_resolution(uint16_t width, uint16_t height)
{
    size_t bytes = (size_t)width * height * sizeof(uint16_t);
    uint16_t *a = malloc(bytes);
    uint16_t *b = malloc(bytes);
    assert(a != NULL && b != NULL);
    const uint32_t fast[] = {8U, 8U, 8U, 8U, 8U, 8U};
    const uint32_t slow[] = {16U, 16U, 16U};
    const uint32_t uneven[] = {7U, 13U, 5U, 23U};
    run_trace(a, width, height, fast, 6U);
    run_trace(b, width, height, slow, 3U);
    assert(memcmp(a, b, bytes) == 0);  // Speed is independent of callback cadence.
    run_trace(b, width, height, uneven, 4U);
    assert(memcmp(a, b, bytes) == 0);  // Fractional elapsed time is retained.
    const uint32_t stall[] = {UINT32_MAX};
    const uint32_t cap[] = {50U};
    run_trace(a, width, height, stall, 1U);
    run_trace(b, width, height, cap, 1U);
    assert(memcmp(a, b, bytes) == 0);  // No catch-up spiral or long-stall teleport.
    if (width == P4_GAME_SURFACE_HIGH_RES_WIDTH) {
        const uint32_t zero[] = {0U};
        const uint32_t short_step[] = {8U};
        const uint32_t full_step[] = {16U};
        run_trace(a, width, height, zero, 1U);
        run_trace(b, width, height, short_step, 1U);
        assert(memcmp(a, b, bytes) != 0); // No whole-tick dead frame.
        run_trace(a, width, height, short_step, 1U);
        run_trace(b, width, height, full_step, 1U);
        assert(memcmp(a, b, bytes) != 0); // Sub-logical-pixel motion reaches raster.
    }
    free(b);
    free(a);
}

int main(void)
{
    test_resolution(768U, 480U);
    test_resolution(320U, 200U);
    return 0;
}
