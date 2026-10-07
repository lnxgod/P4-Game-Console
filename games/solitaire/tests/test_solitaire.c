// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "p4/input.h"
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
    /* This suite preserves canonical touch and the explicit 320x200 legacy renderer.
     * The presentation suite exercises required native admission and the actual descriptor. */
    static p4_game_descriptor_t legacy;
    legacy = p4_solitaire_game;
    legacy.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    legacy.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    return p4_game_instance_start(
        instance, &legacy, &services, state, sizeof(*state));
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

/* Exercise the actual mapper too: synthetic A/B/D-pad regions remain present
 * in API v1 but must never fire while a direct card gesture crosses them. */
static void pointer(p4_game_instance_t *instance, p4_game_input_mapper_t *mapper,
                    int x, int y)
{
    const p4_physical_touch_t point = {
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
            (unsigned)(x < 0 ? 0 : x) * P4_INPUT_VIEWPORT_WIDTH / 320U + 1U),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
            (unsigned)(y < 0 ? 0 : y) * P4_INPUT_VIEWPORT_HEIGHT / 200U + 1U)};
    p4_game_input_t input;
    p4_game_input_mapper_update(mapper, true, x < 0 ? NULL : &point,
                               x < 0 ? 0U : 1U, 0U, &input);
    CHECK(p4_game_instance_update(instance, &input, 16U) == P4_GAME_CONTINUE);
}

static void check_cards_unchanged(const solitaire_state_t *a, const solitaire_state_t *b)
{
    CHECK(memcmp(a->tableau, b->tableau, sizeof(a->tableau)) == 0);
    CHECK(memcmp(a->tableau_count, b->tableau_count, sizeof(a->tableau_count)) == 0);
    CHECK(memcmp(a->face_up_from, b->face_up_from, sizeof(a->face_up_from)) == 0);
    CHECK(memcmp(a->stock, b->stock, sizeof(a->stock)) == 0);
    CHECK(memcmp(a->waste, b->waste, sizeof(a->waste)) == 0);
    CHECK(memcmp(a->foundations, b->foundations, sizeof(a->foundations)) == 0);
    CHECK(a->stock_count == b->stock_count && a->waste_count == b->waste_count);
    CHECK(a->moves == b->moves && a->deal_number == b->deal_number);
}

static void stack_fixture(solitaire_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->tableau_count[0] = 4U;
    state->face_up_from[0] = 1U;
    state->tableau[0][0] = 6U;  /* covered card must flip only after valid drop */
    state->tableau[0][1] = 24U; /* red queen, black jack, red ten */
    state->tableau[0][2] = 10U;
    state->tableau[0][3] = 22U;
    state->tableau_count[1] = 1U;
    state->tableau[1][0] = 12U; /* black king */
}

static void test_drag_stack_and_mapper_regions(void)
{
    p4_game_instance_t instance;
    solitaire_state_t state;
    CHECK(start_game(&instance, &state));
    stack_fixture(&state);
    const solitaire_state_t before = state;
    p4_game_input_mapper_t mapper = {0};
    pointer(&instance, &mapper, 28, SOLITAIRE_TABLEAU_TOP + 5);
    pointer(&instance, &mapper, 29, SOLITAIRE_TABLEAU_TOP + 6);
    CHECK(!state.dragging); /* hand jitter remains a tap */
    check_cards_unchanged(&before, &state);
    pointer(&instance, &mapper, 286, 158); /* former virtual A */
    CHECK(state.dragging && state.selected_index == 1U);
    check_cards_unchanged(&before, &state);
    pointer(&instance, &mapper, 240, 176); /* former virtual B */
    CHECK(state.dragging);
    check_cards_unchanged(&before, &state);
    pointer(&instance, &mapper, 72, 160); /* former virtual Right; valid pile */
    check_cards_unchanged(&before, &state);
    pointer(&instance, &mapper, -1, -1);
    CHECK(!state.dragging && state.selected_source == SOLITAIRE_SOURCE_NONE);
    CHECK(state.moves == 1U && state.tableau_count[0] == 1U);
    CHECK(state.face_up_from[0] == 0U && state.tableau_count[1] == 4U);
    CHECK(state.tableau[1][1] == 24U && state.tableau[1][2] == 10U && state.tableau[1][3] == 22U);
    pointer(&instance, &mapper, -1, -1);
    CHECK(state.moves == 1U); /* repeated release never repeats a move */
    p4_game_instance_stop(&instance);
}

static void test_rejected_and_cancelled_drags(void)
{
    p4_game_instance_t instance;
    solitaire_state_t state;
    CHECK(start_game(&instance, &state));
    for (unsigned scenario = 0U; scenario < 6U; ++scenario) {
        stack_fixture(&state);
        state.tableau[1][0] = 25U; /* same-color king: not a legal destination */
        const solitaire_state_t before = state;
        p4_game_input_mapper_t mapper = {0};
        pointer(&instance, &mapper, 28, SOLITAIRE_TABLEAU_TOP + 5);
        pointer(&instance, &mapper, 72, 155);
        if (scenario == 0U) pointer(&instance, &mapper, 72, 160);
        else if (scenario == 1U) pointer(&instance, &mapper, 160, 40); /* multi-card foundation */
        else if (scenario == 2U) pointer(&instance, &mapper, 160, 195); /* outside piles */
        else if (scenario == 3U) pointer(&instance, &mapper, 300, 10); /* crosses New deal */
        else if (scenario == 4U) {
            const p4_game_input_t lost = {0};
            CHECK(p4_game_instance_update(&instance, &lost, 16U) == P4_GAME_CONTINUE);
        } else {
            const p4_game_input_t multiple = {.touch_valid = true, .touch_count = 2U,
                .touches = {{.x = 72, .y = 155}, {.x = 120, .y = 155}}};
            CHECK(p4_game_instance_update(&instance, &multiple, 16U) == P4_GAME_CONTINUE);
            pointer(&instance, &mapper, 72, 155); /* no restart until all fingers lift */
            CHECK(!state.dragging);
        }
        pointer(&instance, &mapper, -1, -1);
        check_cards_unchanged(&before, &state);
        CHECK(!state.dragging && state.selected_source == SOLITAIRE_SOURCE_NONE);
    }
    p4_game_instance_stop(&instance);
}

static void test_waste_foundation_empty_pile_and_actions(void)
{
    p4_game_instance_t instance;
    solitaire_state_t state;
    CHECK(start_game(&instance, &state));
    memset(&state, 0, sizeof(state));
    state.waste_count = 1U; state.waste[0] = 13U; /* red ace */
    p4_game_input_mapper_t mapper = {0};
    pointer(&instance, &mapper, 72, 40);
    pointer(&instance, &mapper, 204, 40);
    CHECK(state.foundations[1] == 0U && state.waste_count == 1U);
    pointer(&instance, &mapper, -1, -1);
    CHECK(state.foundations[1] == 1U && state.waste_count == 0U);
    state.tableau_count[0] = 1U; state.tableau[0][0] = 1U; /* black two */
    pointer(&instance, &mapper, 204, 40);
    pointer(&instance, &mapper, 28, 160);
    pointer(&instance, &mapper, -1, -1);
    CHECK(state.foundations[1] == 0U && state.tableau_count[0] == 2U);
    CHECK(state.tableau[0][1] == 13U);
    state.waste_count = 1U; state.waste[0] = 12U;
    pointer(&instance, &mapper, 72, 40);
    pointer(&instance, &mapper, 292, 160);
    pointer(&instance, &mapper, -1, -1);
    CHECK(state.tableau_count[6] == 1U && state.tableau[6][0] == 12U);
    const uint32_t deal = state.deal_number;
    tap(&instance, 290U, 12U);
    CHECK(state.deal_number == deal + 1U && state.stock_count == 24U);
    const solitaire_state_t before = state;
    pointer(&instance, &mapper, 28, 40); /* dragging stock never draws */
    pointer(&instance, &mapper, 72, 40);
    pointer(&instance, &mapper, -1, -1);
    check_cards_unchanged(&before, &state);
    const p4_game_input_t key = {.pressed = P4_BUTTON_A};
    state.cursor_area = SOLITAIRE_CURSOR_TOP; state.cursor_column = 0U;
    CHECK(p4_game_instance_update(&instance, &key, 16U) == P4_GAME_CONTINUE);
    CHECK(state.stock_count == 23U && state.keyboard_focus);
    const p4_game_input_t exit_press = {.touch_valid = true, .touch_count = 1U,
        .touches = {{.x = 20U, .y = 12U}}, .pressed = P4_BUTTON_BACK};
    CHECK(p4_game_instance_update(&instance, &exit_press, 16U) == P4_GAME_CONTINUE);
    const p4_game_input_t exit_release = {.touch_valid = true};
    CHECK(p4_game_instance_update(&instance, &exit_release, 16U) == P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_solitaire_game));
    CHECK(p4_solitaire_game.launcher_id == 107U);
    test_drag_stack_and_mapper_regions();
    test_rejected_and_cancelled_drags();
    test_waste_foundation_empty_pile_and_actions();
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
