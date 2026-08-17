// SPDX-License-Identifier: MIT

#include "p4_games/calculator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calculator_internal.h"

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static void enter_number(p4_calculator_state_t *state, const char *digits)
{
    while (*digits != '\0') {
        CHECK(p4_calculator_digit(state, (uint8_t)(*digits - '0')));
        ++digits;
    }
}

static void test_arithmetic(void)
{
    p4_calculator_state_t state;
    p4_calculator_reset(&state);
    enter_number(&state, "12");
    CHECK(p4_calculator_choose_operation(&state, P4_CALC_ADD));
    enter_number(&state, "30");
    CHECK(p4_calculator_equals(&state));
    CHECK(state.entry == 42);

    CHECK(p4_calculator_choose_operation(&state, P4_CALC_MULTIPLY));
    enter_number(&state, "10");
    CHECK(p4_calculator_equals(&state));
    CHECK(state.entry == 420);

    CHECK(p4_calculator_choose_operation(&state, P4_CALC_SUBTRACT));
    enter_number(&state, "500");
    CHECK(p4_calculator_equals(&state));
    CHECK(state.entry == -80);

    CHECK(p4_calculator_choose_operation(&state, P4_CALC_DIVIDE));
    enter_number(&state, "4");
    CHECK(p4_calculator_equals(&state));
    CHECK(state.entry == -20);
}

static void test_bounds_and_format(void)
{
    p4_calculator_state_t state;
    p4_calculator_reset(&state);
    enter_number(&state, "999999999");
    CHECK(!p4_calculator_digit(&state, 9U));
    CHECK(state.error);
    char text[16];
    CHECK(p4_calculator_format(&state, text, sizeof(text)) == 5U);
    CHECK(strcmp(text, "ERROR") == 0);

    p4_calculator_reset(&state);
    enter_number(&state, "8");
    CHECK(p4_calculator_choose_operation(&state, P4_CALC_DIVIDE));
    enter_number(&state, "0");
    CHECK(!p4_calculator_equals(&state));
    CHECK(state.error);
    p4_calculator_backspace(&state);
    CHECK(!state.error && state.entry == 0);
}

static void test_lifecycle_and_render_bounds(void)
{
    CHECK(p4_game_descriptor_valid(&p4_calculator_game));
    p4_calculator_state_t state;
    p4_game_instance_t instance = {0};
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
                                  P4_GAME_CAP_CONTROLS,
    };
    CHECK(p4_game_instance_start(
        &instance, &p4_calculator_game, &services, &state, sizeof(state)));
    enum {
        GUARD = 19,
        STRIDE = P4_GAME_SURFACE_WIDTH + 3,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const pixels = calloc(TOTAL, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        for (size_t index = 0U; index < TOTAL; ++index) {
            pixels[index] = UINT16_C(0xA55A);
        }
        p4_game_surface_t surface = {
            .pixels = pixels + GUARD,
            .stride_pixels = STRIDE,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        for (size_t index = 0U; index < GUARD; ++index) {
            CHECK(pixels[index] == UINT16_C(0xA55A));
            CHECK(pixels[GUARD + WORDS + index] == UINT16_C(0xA55A));
        }
        free(pixels);
    }
    const p4_game_input_t back = {
        .pressed = P4_BUTTON_BACK,
        .held = P4_BUTTON_BACK,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

int main(void)
{
    test_arithmetic();
    test_bounds_and_format();
    test_lifecycle_and_render_bounds();
    if (s_failures != 0) {
        fprintf(stderr, "%d calculator test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("calculator tests passed");
    return EXIT_SUCCESS;
}
