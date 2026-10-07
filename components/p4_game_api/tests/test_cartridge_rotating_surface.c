/* SPDX-License-Identifier: MIT */
#include "p4/cartridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern int app_main(int argc, char *argv[]);
typedef struct { unsigned updates; } game_state_t;
typedef struct {
    p4_cartridge_host_v1_t host;
    uint16_t *pixels[2];
    unsigned renders, presents, fail_present;
    bool finished;
    p4_game_result_t result;
} fixture_t;
static fixture_t *fixture;

static bool start(p4_game_context_t *context)
{
    return context && context->state && context->state_bytes == sizeof(game_state_t);
}
static p4_game_result_t update(p4_game_context_t *context,
    const p4_game_input_t *input, uint32_t elapsed_ms)
{
    (void)input;
    assert(elapsed_ms == 16U);
    game_state_t *state = context->state;
    return ++state->updates == 32U ? P4_GAME_EXIT_TO_LAUNCHER : P4_GAME_CONTINUE;
}
static bool render(p4_game_context_t *context, p4_game_surface_t *surface)
{
    (void)context;
    assert(surface == &fixture->host.surface);
    assert(surface->pixels == fixture->pixels[fixture->renders % 2U]);
    assert(surface->width == 768U && surface->height == 480U);
    const uint16_t value = (uint16_t)(++fixture->renders);
    for (size_t i = 0U; i < 768U * 480U; ++i) surface->pixels[i] = value;
    return true;
}
const p4_game_descriptor_t p4_test_rotating_game = {
    .api_version = P4_GAME_API_VERSION, .launcher_id = 999U,
    .id = "org.p4.rotating", .title = "ROTATING", .subtitle = "SURFACE",
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
        P4_GAME_CAP_VIDEO_HIGH_RES,
    .state_bytes = sizeof(game_state_t), .start = start, .update = update,
    .render = render,
};
static bool poll(void *opaque, p4_game_input_t *input, uint32_t *elapsed_ms)
{
    assert(opaque == fixture && fixture->host.surface.pixels);
    *input = (p4_game_input_t){0}; *elapsed_ms = 16U;
    return true;
}
static bool present(void *opaque)
{
    assert(opaque == fixture);
    uint16_t *rendered = fixture->host.surface.pixels;
    for (size_t i = 0U; i < 768U * 480U; ++i)
        assert(rendered[i] == fixture->renders);
    ++fixture->presents;
    /* Model Console OS invalidating the committed source before acquiring
     * the next lease. The runtime must read the current host surface again. */
    fixture->host.surface.pixels = NULL;
    if (fixture->presents == fixture->fail_present) return false;
    fixture->host.surface.pixels = fixture->pixels[fixture->presents % 2U];
    return true;
}
static void finished(void *opaque, p4_game_result_t result)
{
    assert(opaque == fixture && !fixture->finished);
    fixture->finished = true; fixture->result = result;
}
static void run(unsigned fail_present)
{
    fixture_t f = {.fail_present = fail_present}; fixture = &f;
    for (unsigned i = 0U; i < 2U; ++i) {
        f.pixels[i] = calloc(768U * 480U, sizeof(uint16_t)); assert(f.pixels[i]);
    }
    f.host = (p4_cartridge_host_v1_t){
        .magic = P4_CARTRIDGE_HOST_MAGIC, .api_version = P4_CARTRIDGE_HOST_API_VERSION,
        .struct_bytes = sizeof(f.host), .available_capabilities =
            P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS | P4_GAME_CAP_VIDEO_HIGH_RES,
        .expected_game_id = p4_test_rotating_game.id,
        .surface = {.pixels = f.pixels[0], .stride_pixels = 768U,
            .width = 768U, .height = 480U},
        .context = &f, .poll_frame = poll, .present = present, .finished = finished,
    };
    char *args[] = {(char *)(void *)&f.host};
    const int result = app_main(1, args);
    assert(f.finished && f.renders == f.presents);
    if (fail_present) {
        assert(result == P4_CARTRIDGE_EXIT_RENDER_FAILED);
        assert(f.result == P4_GAME_ERROR && f.renders == fail_present);
        assert(f.host.surface.pixels == NULL);
    } else {
        assert(result == P4_CARTRIDGE_EXIT_OK);
        assert(f.result == P4_GAME_EXIT_TO_LAUNCHER && f.renders == 32U);
    }
    for (unsigned i = 0U; i < 2U; ++i) free(f.pixels[i]);
    fixture = NULL;
}
int main(void)
{
    run(0U); run(1U); run(3U);
    puts("PASS actual cartridge runtime reads rotating native surface and stops on lost lease");
    return 0;
}
