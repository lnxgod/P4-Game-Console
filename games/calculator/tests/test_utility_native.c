// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calculator_internal.h"
#include "p4/game.h"
#include "p4/input.h"
#include "p4_games/calculator.h"

extern const p4_game_descriptor_t p4_av_test_game;
extern const p4_game_descriptor_t p4_input_test_game;

static unsigned s_failures;
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static void capture(const char *slug, const p4_game_surface_t *surface)
{
    const char *const directory = getenv("P4_UTILITY_CAPTURE_DIR");
    if (directory == NULL) return;
    char path[1024];
    const int count = snprintf(path, sizeof(path), "%s/%s.ppm", directory, slug);
    CHECK(count > 0 && (size_t)count < sizeof(path));
    if (count <= 0 || (size_t)count >= sizeof(path)) return;
    FILE *const file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file == NULL) return;
    CHECK(fprintf(file, "P6\n%u %u\n255\n", (unsigned)surface->width,
                  (unsigned)surface->height) > 0);
    for (size_t y = 0U; y < surface->height; ++y) {
        for (size_t x = 0U; x < surface->width; ++x) {
            const uint16_t color = surface->pixels[y * surface->stride_pixels + x];
            const unsigned char rgb[3] = {
                (unsigned char)(((color >> 11) & 31U) * 255U / 31U),
                (unsigned char)(((color >> 5) & 63U) * 255U / 63U),
                (unsigned char)((color & 31U) * 255U / 31U),
            };
            CHECK(fwrite(rgb, 1U, sizeof(rgb), file) == sizeof(rgb));
        }
    }
    CHECK(fclose(file) == 0);
}

static void test_view(const p4_game_descriptor_t *descriptor, const char *slug,
                      unsigned sample_x, unsigned sample_y, uint16_t color)
{
    enum { GUARD = 19, WIDTH = 768, HEIGHT = 480, STRIDE = WIDTH + 7,
           WORDS = STRIDE * HEIGHT, TOTAL = GUARD + WORDS + GUARD };
    const uint16_t sentinel = UINT16_C(0xA55A);
    void *const state = calloc(1U, descriptor->state_bytes);
    uint16_t *const allocation = malloc(TOTAL * sizeof(*allocation));
    CHECK(state != NULL && allocation != NULL);
    if (state == NULL || allocation == NULL) {
        free(state); free(allocation); return;
    }
    CHECK((descriptor->required_capabilities & P4_GAME_CAP_VIDEO_HIGH_RES) != 0U);
    p4_game_instance_t instance = {0};
    const p4_game_services_t legacy = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    CHECK(!p4_game_instance_start(&instance, descriptor, &legacy, state,
                                  descriptor->state_bytes));
    const p4_game_services_t native = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_VIDEO_HIGH_RES,
    };
    CHECK(p4_game_instance_start(&instance, descriptor, &native, state,
                                 descriptor->state_bytes));
    for (size_t index = 0U; index < TOTAL; ++index) allocation[index] = sentinel;
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD, .stride_pixels = STRIDE,
        .width = WIDTH, .height = HEIGHT,
    };
    p4_game_surface_t wrong_geometry = surface;
    wrong_geometry.width = P4_GAME_SURFACE_WIDTH;
    wrong_geometry.height = P4_GAME_SURFACE_HEIGHT;
    CHECK(!p4_game_instance_render(&instance, &wrong_geometry));
    CHECK(p4_game_instance_render(&instance, &surface));
    CHECK(surface.pixels[sample_y * STRIDE + sample_x] == color);
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == sentinel);
        CHECK(allocation[GUARD + WORDS + index] == sentinel);
    }
    for (size_t y = 0U; y < HEIGHT; ++y) {
        for (size_t x = 0U; x < WIDTH; ++x) {
            CHECK(surface.pixels[y * STRIDE + x] != sentinel);
        }
        for (size_t x = WIDTH; x < STRIDE; ++x) {
            CHECK(surface.pixels[y * STRIDE + x] == sentinel);
        }
    }
    if (descriptor == &p4_input_test_game) {
        const p4_game_input_t touch = {
            .touch_valid = true, .touch_count = 1U, .touches = {{230U, 120U}},
        };
        CHECK(p4_game_instance_update(&instance, &touch, 16U) == P4_GAME_CONTINUE);
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(surface.pixels[288U * STRIDE + 552U] == UINT16_C(0xFFFF));
    }
    capture(slug, &surface);
    if (descriptor == &p4_av_test_game) {
        const p4_game_input_t next_pattern = {.pressed = P4_BUTTON_RIGHT};
        for (unsigned pattern = 1U; pattern < 4U; ++pattern) {
            CHECK(p4_game_instance_update(&instance, &next_pattern, 16U) ==
                  P4_GAME_CONTINUE);
            CHECK(p4_game_instance_render(&instance, &surface));
            for (size_t y = 0U; y < HEIGHT; ++y) {
                for (size_t x = WIDTH; x < STRIDE; ++x) {
                    CHECK(surface.pixels[y * STRIDE + x] == sentinel);
                }
            }
            for (size_t index = 0U; index < GUARD; ++index) {
                CHECK(allocation[index] == sentinel);
                CHECK(allocation[GUARD + WORDS + index] == sentinel);
            }
        }
    }
    p4_game_instance_stop(&instance);

    /* Legacy diagnostics use an explicit local descriptor copy; the released
     * descriptor is never allowed to negotiate a low-resolution surface. */
    p4_game_descriptor_t legacy_descriptor = *descriptor;
    legacy_descriptor.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    CHECK(p4_game_instance_start(&instance, &legacy_descriptor, &legacy, state,
                                 descriptor->state_bytes));
    CHECK(p4_game_instance_render(&instance, &wrong_geometry));
    p4_game_instance_stop(&instance);
    free(state); free(allocation);
}

static void test_calculator_native_touches(void)
{
    p4_calculator_state_t state = {0};
    p4_game_instance_t instance = {0};
    const p4_game_services_t native = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_VIDEO_HIGH_RES,
    };
    CHECK(p4_game_instance_start(&instance, &p4_calculator_game, &native,
                                 &state, sizeof(state)));
    p4_game_input_mapper_t mapper;
    p4_game_input_mapper_init(&mapper);
    uint16_t *const pixels = calloc(768U * 480U, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels == NULL) { p4_game_instance_stop(&instance); return; }
    p4_game_surface_t surface = {
        .pixels = pixels, .stride_pixels = 768U, .width = 768U, .height = 480U,
    };
    for (unsigned key = 0U; key < 16U; ++key) {
        p4_calculator_reset(&state);
        if (key == 12U) { state.entry = 9; state.entering = true; }
        const unsigned x = 34U + (key % 4U) * 48U;
        const unsigned y = 63U + (key / 4U) * 20U;
        const p4_physical_touch_t touch = {
            .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
                x * P4_INPUT_VIEWPORT_WIDTH / P4_GAME_SURFACE_WIDTH),
            .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
                y * P4_INPUT_VIEWPORT_HEIGHT / P4_GAME_SURFACE_HEIGHT),
        };
        p4_game_input_t input;
        p4_game_input_mapper_update(&mapper, true, &touch, 1U, 0U, &input);
        CHECK(input.held == 0U); /* In particular, C must not press D-pad UP. */
        CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
        if (key == 12U) CHECK(state.entry == 0);
        else CHECK(state.cursor == key);
        CHECK(p4_game_instance_render(&instance, &surface));
        const unsigned left = (12U + (key % 4U) * 48U) * 768U / 320U;
        const unsigned top = (55U + (key / 4U) * 20U) * 480U / 200U;
        CHECK(pixels[top * 768U + left] ==
              (key == 12U ? UINT16_C(0xFFFF) : UINT16_C(0xFFE0)));
        p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &input);
        CHECK(p4_game_instance_update(&instance, &input, 16U) == P4_GAME_CONTINUE);
    }
    p4_game_instance_stop(&instance);
    free(pixels);
}

int main(void)
{
    test_view(&p4_av_test_game, "av_test", 600U, 200U, UINT16_C(0x001F));
    test_view(&p4_input_test_game, "input_test", 300U, 90U, UINT16_C(0x2104));
    test_view(&p4_calculator_game, "calculator", 700U, 70U, UINT16_C(0x0000));
    test_calculator_native_touches();
    if (s_failures != 0U) {
        fprintf(stderr, "%u utility native test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("utility native guards, geometry, footprint and touch tests passed");
    return EXIT_SUCCESS;
}
