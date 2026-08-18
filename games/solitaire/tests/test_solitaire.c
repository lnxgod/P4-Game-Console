// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "solitaire_internal.h"

extern const p4_game_descriptor_t p4_solitaire_game;

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static bool start_game(p4_game_instance_t *instance,
                       solitaire_state_t *state)
{
    static const p4_game_services_t services = {
        .available_capabilities =
            P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    *instance = (p4_game_instance_t){0};
    return p4_game_instance_start(
        instance, &p4_solitaire_game, &services, state, sizeof(*state));
}

static void release_touch(p4_game_instance_t *instance)
{
    const p4_game_input_t input = {.touch_valid = true};
    CHECK(p4_game_instance_update(instance, &input, 16U) ==
          P4_GAME_CONTINUE);
}

static void tap(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    CHECK(p4_game_instance_update(instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    release_touch(instance);
}

static uint16_t card_center_x(uint8_t column)
{
    return (uint16_t)(8U + (uint16_t)column *
        (SOLITAIRE_CARD_WIDTH + SOLITAIRE_CARD_GAP_X) +
        SOLITAIRE_CARD_WIDTH / 2U);
}

static void test_deal_and_stock_touch(void)
{
    solitaire_state_t state = {0};
    solitaire_reset(&state);
    CHECK(state.stock_count == SOLITAIRE_STOCK_MAX);
    bool seen[SOLITAIRE_DECK] = {false};
    for (uint8_t pile = 0U; pile < SOLITAIRE_PILES; ++pile) {
        CHECK(state.tableau_count[pile] == pile + 1U);
        CHECK(state.face_up_from[pile] == pile);
        for (uint8_t index = 0U; index < state.tableau_count[pile]; ++index) {
            const uint8_t card = state.tableau[pile][index];
            CHECK(card < SOLITAIRE_DECK);
            CHECK(!seen[card]);
            seen[card] = true;
        }
    }
    for (uint8_t index = 0U; index < state.stock_count; ++index) {
        const uint8_t card = state.stock[index];
        CHECK(card < SOLITAIRE_DECK);
        CHECK(!seen[card]);
        seen[card] = true;
    }
    for (uint8_t card = 0U; card < SOLITAIRE_DECK; ++card) {
        CHECK(seen[card]);
    }

    p4_game_instance_t instance;
    CHECK(start_game(&instance, &state));
    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_CARD_TOP + 5));
    CHECK(state.stock_count == SOLITAIRE_STOCK_MAX - 1U);
    CHECK(state.waste_count == 1U);
    p4_game_instance_stop(&instance);
}

static void test_touch_moves_and_cancel(void)
{
    p4_game_instance_t instance;
    solitaire_state_t state;
    CHECK(start_game(&instance, &state));
    memset(&state, 0, sizeof(state));
    state.tableau[0][0] = 0U; /* ace of spades */
    state.tableau_count[0] = 1U;
    state.face_up_from[0] = 0U;

    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 5));
    CHECK(state.selected_source == SOLITAIRE_SOURCE_TABLEAU);
    CHECK(state.selected_pile == 0U);
    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 5));
    CHECK(state.selected_source == SOLITAIRE_SOURCE_NONE);

    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 5));
    tap(&instance, card_center_x(3U),
        (uint16_t)(SOLITAIRE_CARD_TOP + 5));
    CHECK(state.tableau_count[0] == 0U);
    CHECK(state.foundations[0] == 1U);
    CHECK(state.selected_source == SOLITAIRE_SOURCE_NONE);

    memset(&state, 0, sizeof(state));
    state.tableau[0][0] = 24U; /* queen of hearts */
    state.tableau_count[0] = 1U;
    state.face_up_from[0] = 0U;
    state.tableau[1][0] = 12U; /* king of spades */
    state.tableau_count[1] = 1U;
    state.face_up_from[1] = 0U;
    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 5));
    tap(&instance, card_center_x(1U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 5));
    CHECK(state.tableau_count[0] == 0U);
    CHECK(state.tableau_count[1] == 2U);
    CHECK(state.tableau[1][1] == 24U);
    p4_game_instance_stop(&instance);
}

static void test_face_down_touch_and_render_bounds(void)
{
    enum {
        GUARD = 17,
        STRIDE = P4_GAME_SURFACE_WIDTH + 7,
        WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
        TOTAL = GUARD + WORDS + GUARD,
    };
    uint16_t *const allocation = calloc(TOTAL, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation == NULL) {
        return;
    }
    for (size_t index = 0U; index < TOTAL; ++index) {
        allocation[index] = UINT16_C(0x5aa5);
    }
    p4_game_surface_t surface = {
        .pixels = allocation + GUARD,
        .stride_pixels = STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    p4_game_instance_t instance;
    solitaire_state_t state;
    CHECK(start_game(&instance, &state));
    memset(&state, 0, sizeof(state));
    state.tableau[0][0] = 12U;
    state.tableau[0][1] = 24U;
    state.tableau_count[0] = 2U;
    state.face_up_from[0] = 1U;
    tap(&instance, card_center_x(0U),
        (uint16_t)(SOLITAIRE_TABLEAU_TOP + 1));
    CHECK(state.selected_source == SOLITAIRE_SOURCE_NONE);

    solitaire_reset(&state);
    CHECK(p4_game_instance_render(&instance, &surface));
    for (size_t index = 0U; index < GUARD; ++index) {
        CHECK(allocation[index] == UINT16_C(0x5aa5));
        CHECK(allocation[GUARD + WORDS + index] == UINT16_C(0x5aa5));
    }
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH;
             column < STRIDE; ++column) {
            CHECK(surface.pixels[row * STRIDE + column] ==
                  UINT16_C(0x5aa5));
        }
    }
    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    free(allocation);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_solitaire_game));
    CHECK(p4_solitaire_game.launcher_id == 107U);
    test_deal_and_stock_touch();
    test_touch_moves_and_cancel();
    test_face_down_touch_and_render_bounds();
    if (s_failures != 0) {
        fprintf(stderr, "%d solitaire test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("solitaire tests passed");
    return EXIT_SUCCESS;
}
