// SPDX-License-Identifier: MIT
/* Inject real SDL batches into the actual runner, not a stand-in game loop. */
#define P4_HOST_GAME_DESCRIPTOR p4_mouse_test_game
#define main p4_runner_main
#include "play_game.c"
#undef main

static unsigned scenario, observed, failures;
typedef struct { bool valid, down; uint16_t x, y; } expected_t;
static expected_t expected[4];
static unsigned expected_count;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "MOUSE FAIL %u:%d: %s\n", scenario, __LINE__, #c); \
    ++failures; } } while (0)
static void event_at(Uint32 type, uint16_t x, uint16_t y)
{
    SDL_Event event = {0};
    event.type = type;
    /* Center the canonical pixel so both integer conversions are unambiguous. */
    const float px = ((float)x + 0.5F) * 2.4F;
    const float py = ((float)y + 0.5F) * 2.4F;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.state = SDL_BUTTON_LMASK;
        event.motion.x = px; event.motion.y = py;
    } else {
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.x = px; event.button.y = py;
    }
    CHECK(SDL_PushEvent(&event));
}
static bool fixture_start(p4_game_context_t *context)
{
    (void)context;
    observed = 0U;
    if (scenario == 1U) {
        expected_count = 2U;
        expected[0] = (expected_t){true, true, 28U, 40U};
        expected[1] = (expected_t){true, false, 0U, 0U};
        event_at(SDL_EVENT_MOUSE_BUTTON_DOWN, 28U, 40U);
        event_at(SDL_EVENT_MOUSE_BUTTON_UP, 0U, 0U);
    } else if (scenario == 3U) {
        expected_count = 1U;
        expected[0] = (expected_t){false, false, 0U, 0U};
        event_at(SDL_EVENT_MOUSE_BUTTON_DOWN, 28U, 84U);
        event_at(SDL_EVENT_MOUSE_MOTION, 116U, 110U);
        const SDL_Event lost = {.type = SDL_EVENT_WINDOW_FOCUS_LOST};
        SDL_Event event = lost;
        CHECK(SDL_PushEvent(&event));
    } else {
        expected_count = scenario == 2U ? 2U : 3U;
        expected[0] = (expected_t){true, true, 28U, 84U};
        expected[1] = scenario == 2U ? (expected_t){true, false, 0U, 0U} :
            (expected_t){true, true, 116U, 110U};
        expected[2] = (expected_t){true, false, 0U, 0U};
        event_at(SDL_EVENT_MOUSE_BUTTON_DOWN, 28U, 84U);
        if (scenario == 0U) {
            event_at(SDL_EVENT_MOUSE_MOTION, 70U, 95U);
            event_at(SDL_EVENT_MOUSE_MOTION, 116U, 110U);
        }
        /* Up carries a different position: only real held motion moves touch. */
        event_at(SDL_EVENT_MOUSE_BUTTON_UP, scenario == 2U ? 116U : 0U,
                 scenario == 2U ? 110U : 0U);
    }
    return true;
}
static p4_game_result_t fixture_update(p4_game_context_t *context,
    const p4_game_input_t *input, uint32_t elapsed_ms)
{
    (void)context;
    CHECK(elapsed_ms == HOST_SERVICE_INTERVAL_MS);
    if (observed == expected_count) return P4_GAME_EXIT_TO_LAUNCHER;
    const expected_t sample = expected[observed++];
    CHECK(input->touch_valid == sample.valid);
    CHECK(input->touch_count == (sample.down ? 1U : 0U));
    if (sample.down) {
        CHECK(input->touches[0].x == sample.x);
        CHECK(input->touches[0].y == sample.y);
    }
    return P4_GAME_CONTINUE;
}
static bool fixture_render(p4_game_context_t *context, p4_game_surface_t *surface)
{ (void)context; (void)surface; return true; }
static void fixture_stop(p4_game_context_t *context) { (void)context; }
const p4_game_descriptor_t p4_mouse_test_game = {
    .api_version = P4_GAME_API_VERSION, .launcher_id = 901U,
    .id = "org.p4console.runner-mouse-test", .title = "Mouse Test",
    .subtitle = "Ordered gesture regression", .state_bytes = sizeof(uint32_t),
    .required_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    .optional_capabilities = P4_GAME_CAP_VIDEO_HIGH_RES,
    .start = fixture_start, .update = fixture_update,
    .render = fixture_render, .stop = fixture_stop,
};
static void test_bounded_queue(void)
{
    p4_host_mouse_t mouse = {.current = {.valid = true}};
    p4_host_mouse_press(&mouse, (p4_physical_touch_t){100U, 200U});
    for (unsigned i = 0U; i < 10000U; ++i)
        p4_host_mouse_move(&mouse, (p4_physical_touch_t){(uint16_t)(101U + i % 500U), 200U});
    CHECK(mouse.count == 2U);
    p4_host_mouse_release(&mouse);
    CHECK(mouse.count == 3U);
    CHECK(p4_host_mouse_next(&mouse).point.x == 100U);
    CHECK(p4_host_mouse_next(&mouse).point.x == 600U);
    CHECK(!p4_host_mouse_next(&mouse).down);
    CHECK(!p4_host_mouse_next(&mouse).down);
    mouse = (p4_host_mouse_t){.current = {.valid = true}};
    for (unsigned i = 0U; i < P4_HOST_MOUSE_CAPACITY / 2U; ++i) {
        p4_host_mouse_press(&mouse, (p4_physical_touch_t){100U, 200U});
        p4_host_mouse_release(&mouse);
    }
    CHECK(mouse.count == P4_HOST_MOUSE_CAPACITY);
    p4_host_mouse_press(&mouse, (p4_physical_touch_t){100U, 200U});
    CHECK(mouse.count == 1U && !mouse.collecting_down);
    const p4_host_mouse_sample_t cancelled = p4_host_mouse_next(&mouse);
    CHECK(!cancelled.valid && !cancelled.down);
}
int main(void)
{
    test_bounded_queue();
    char *arguments[] = {"runner-mouse-test", "--frames", "8", NULL};
    for (scenario = 0U; scenario < 4U; ++scenario) {
        CHECK(p4_runner_main(3, arguments) == EXIT_SUCCESS);
        CHECK(observed == expected_count);
    }
    return failures == 0U ? EXIT_SUCCESS : EXIT_FAILURE;
}
