// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color_clash_internal.h"
#include "p4/audio.h"
#include "p4/game.h"

extern const p4_game_descriptor_t p4_color_clash_game;

enum {
    GUARD_WORDS = 23,
    STRIDE = P4_GAME_SURFACE_WIDTH + 9,
    FRAME_WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
    TOTAL_WORDS = GUARD_WORDS + FRAME_WORDS + GUARD_WORDS,
    HIGH_RES_STRIDE = P4_GAME_SURFACE_HIGH_RES_WIDTH + 11,
    HIGH_RES_FRAME_WORDS =
        HIGH_RES_STRIDE * P4_GAME_SURFACE_HIGH_RES_HEIGHT,
    HIGH_RES_TOTAL_WORDS =
        GUARD_WORDS + HIGH_RES_FRAME_WORDS + GUARD_WORDS,
    LINK_QUEUE = 64,
};

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

typedef struct test_link test_link_t;

typedef struct {
    test_link_t *link;
    uint8_t slot;
    uint32_t next_sequence;
    p4_game_multiplayer_message_t queue[LINK_QUEUE];
    size_t queue_head;
    size_t queue_count;
} test_endpoint_t;

struct test_link {
    test_endpoint_t endpoints[COLOR_CLASH_MAX_PLAYERS];
    uint8_t player_count;
    p4_game_multiplayer_state_t state;
};

static bool link_status(
    void *context, p4_game_multiplayer_status_t *status_out)
{
    test_endpoint_t *const endpoint = context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x123456789abcdef0),
        .state = endpoint->link->state,
        .role = endpoint->slot == 0U ? P4_GAME_MULTIPLAYER_ROLE_HOST
                                    : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = endpoint->slot,
        .player_count = endpoint->link->player_count,
    };
    return true;
}

static bool link_send(void *context, const uint8_t *data, size_t data_bytes)
{
    test_endpoint_t *const source = context;
    if (data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES) {
        return false;
    }
    for (uint8_t slot = 0U; slot < source->link->player_count; ++slot) {
        if (slot != source->slot &&
            source->link->endpoints[slot].queue_count >= LINK_QUEUE) {
            return false;
        }
    }
    const uint32_t sequence = ++source->next_sequence;
    for (uint8_t slot = 0U; slot < source->link->player_count; ++slot) {
        if (slot == source->slot) {
            continue;
        }
        test_endpoint_t *const destination = &source->link->endpoints[slot];
        const size_t tail = (destination->queue_head +
                             destination->queue_count) % LINK_QUEUE;
        p4_game_multiplayer_message_t *const message =
            &destination->queue[tail];
        *message = (p4_game_multiplayer_message_t){
            .sequence = sequence,
            .player_slot = source->slot,
            .bytes = (uint8_t)data_bytes,
        };
        memcpy(message->data, data, data_bytes);
        ++destination->queue_count;
    }
    return true;
}

static bool link_receive(
    void *context, p4_game_multiplayer_message_t *message_out)
{
    test_endpoint_t *const endpoint = context;
    if (endpoint->queue_count == 0U) {
        return false;
    }
    *message_out = endpoint->queue[endpoint->queue_head];
    endpoint->queue_head = (endpoint->queue_head + 1U) % LINK_QUEUE;
    --endpoint->queue_count;
    return true;
}

static void init_link(test_link_t *link, uint8_t player_count)
{
    memset(link, 0, sizeof(*link));
    link->player_count = player_count;
    link->state = P4_GAME_MULTIPLAYER_CONNECTED;
    for (uint8_t slot = 0U; slot < player_count; ++slot) {
        link->endpoints[slot].link = link;
        link->endpoints[slot].slot = slot;
    }
}

static p4_game_services_t network_services(test_endpoint_t *endpoint)
{
    return (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .multiplayer_context = endpoint,
        .multiplayer_read_status = link_status,
        .multiplayer_send = link_send,
        .multiplayer_receive = link_receive,
    };
}

static bool update_button(p4_game_instance_t *instance, uint32_t button)
{
    const p4_game_input_t input = {
        .held = button,
        .pressed = button,
        .touch_valid = true,
    };
    return p4_game_instance_update(instance, &input, 16U) ==
        P4_GAME_CONTINUE;
}

static bool update_empty(p4_game_instance_t *instance, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {.touch_valid = true};
    return p4_game_instance_update(instance, &input, elapsed_ms) ==
        P4_GAME_CONTINUE;
}

static void tap(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    const p4_game_input_t pressed = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    CHECK(p4_game_instance_update(instance, &pressed, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(update_empty(instance, 16U));
}

static void hold_touch(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    CHECK(p4_game_instance_update(instance, &input, 16U) ==
          P4_GAME_CONTINUE);
}

static color_clash_state_t simple_state(uint8_t players)
{
    color_clash_state_t state = {
        .player_count = players,
        .local_player_slot = 0U,
        .winner = UINT8_C(0xff),
        .uno_pending_player = UINT8_C(0xff),
        .notice_player = UINT8_C(0xff),
        .phase = COLOR_CLASH_TURN,
        .mode = COLOR_CLASH_PRACTICE,
        .rng = 1U,
        .active_color = COLOR_CLASH_RED,
        .discard_count = 1U,
    };
    state.discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_THREE);
    return state;
}

static void force_nonplayable_draw(color_clash_state_t *state)
{
    CHECK(state != NULL);
    CHECK(state != NULL && state->deck_count != 0U);
    CHECK(state != NULL && state->discard_count != 0U);
    if (state == NULL || state->deck_count == 0U ||
        state->discard_count == 0U) {
        return;
    }
    const color_clash_color_t color = (color_clash_color_t)(
        (state->active_color + 1U) % COLOR_CLASH_COLOR_COUNT);
    const color_clash_rank_t top_rank = color_clash_card_rank(
        state->discard[state->discard_count - 1U]);
    const color_clash_rank_t rank = top_rank == COLOR_CLASH_ONE
        ? COLOR_CLASH_TWO : COLOR_CLASH_ONE;
    state->deck[state->deck_count - 1U] = color_clash_make_card(color, rank);
}

static void set_mixed_order_hand(color_clash_state_t *state)
{
    state->hand_counts[0] = 6U;
    state->hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state->hands[0][1] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    state->hands[0][2] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_EIGHT);
    state->hands[0][3] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_THREE);
    state->hands[0][4] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD);
    state->hands[0][5] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
}

static bool region_differs_from(const uint16_t *pixels,
                                unsigned left, unsigned top,
                                unsigned width, unsigned height,
                                uint16_t color)
{
    for (unsigned y = top; y < top + height; ++y) {
        for (unsigned x = left; x < left + width; ++x) {
            if (pixels[y * P4_GAME_SURFACE_WIDTH + x] != color) {
                return true;
            }
        }
    }
    return false;
}

static size_t region_distinct_colors(const uint16_t *pixels, size_t stride,
                                     unsigned left, unsigned top,
                                     unsigned width, unsigned height,
                                     size_t limit)
{
    uint16_t colors[64];
    size_t count = 0U;
    if (limit > sizeof(colors) / sizeof(colors[0])) {
        limit = sizeof(colors) / sizeof(colors[0]);
    }
    for (unsigned y = top; y < top + height && count < limit; ++y) {
        for (unsigned x = left; x < left + width && count < limit; ++x) {
            const uint16_t candidate = pixels[(size_t)y * stride + x];
            size_t existing = 0U;
            while (existing < count && colors[existing] != candidate) {
                ++existing;
            }
            if (existing == count) {
                colors[count++] = candidate;
            }
        }
    }
    return count;
}

static void test_render_resolution_negotiation(void)
{
    const p4_game_services_t low_res_services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    color_clash_state_t state;
    CHECK(p4_game_instance_start(
        &instance, &p4_color_clash_game, &low_res_services,
        &state, sizeof(state)));
    state = simple_state(2U);
    state.color_aid = COLOR_CLASH_COLOR_AID_HIGH_CONTRAST;
    uint16_t *const low_res_pixels = calloc(
        P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*low_res_pixels));
    CHECK(low_res_pixels != NULL);
    if (low_res_pixels != NULL) {
        p4_game_surface_t low_res_surface = {
            .pixels = low_res_pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &low_res_surface));
        p4_game_surface_t wrong_surface = {
            .pixels = low_res_pixels,
            .stride_pixels = P4_GAME_SURFACE_HIGH_RES_WIDTH,
            .width = P4_GAME_SURFACE_HIGH_RES_WIDTH,
            .height = P4_GAME_SURFACE_HIGH_RES_HEIGHT,
        };
        CHECK(!p4_game_instance_render(&instance, &wrong_surface));
        free(low_res_pixels);
    }
    p4_game_instance_stop(&instance);

    const p4_game_services_t high_res_services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_VIDEO_HIGH_RES,
    };
    CHECK(p4_game_instance_start(
        &instance, &p4_color_clash_game, &high_res_services,
        &state, sizeof(state)));
    CHECK(state.phase == COLOR_CLASH_MENU);
    tap(&instance, 150U, 165U);
    CHECK(state.phase == COLOR_CLASH_TURN);
    state = simple_state(2U);
    state.color_aid = COLOR_CLASH_COLOR_AID_HIGH_CONTRAST;
    state.discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);

    uint16_t *const allocation = calloc(
        HIGH_RES_TOTAL_WORDS, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        for (size_t index = 0U; index < HIGH_RES_TOTAL_WORDS; ++index) {
            allocation[index] = UINT16_C(0x5aa5);
        }
        p4_game_surface_t high_res_surface = {
            .pixels = allocation + GUARD_WORDS,
            .stride_pixels = HIGH_RES_STRIDE,
            .width = P4_GAME_SURFACE_HIGH_RES_WIDTH,
            .height = P4_GAME_SURFACE_HIGH_RES_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &high_res_surface));
        for (size_t index = 0U; index < GUARD_WORDS; ++index) {
            CHECK(allocation[index] == UINT16_C(0x5aa5));
            CHECK(allocation[GUARD_WORDS + HIGH_RES_FRAME_WORDS + index] ==
                  UINT16_C(0x5aa5));
        }
        for (size_t row = 0U;
             row < P4_GAME_SURFACE_HIGH_RES_HEIGHT; ++row) {
            for (size_t column = P4_GAME_SURFACE_HIGH_RES_WIDTH;
                 column < HIGH_RES_STRIDE; ++column) {
                CHECK(high_res_surface.pixels[
                          row * HIGH_RES_STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        CHECK(region_distinct_colors(
                  high_res_surface.pixels, HIGH_RES_STRIDE,
                  319U, 130U, 101U, 149U, 48U) == 48U);
        free(allocation);
    }
    p4_game_instance_stop(&instance);
}

static void test_playable_hand_order_and_lift(void)
{
    color_clash_state_t state = simple_state(2U);
    set_mixed_order_hand(&state);
    static const uint8_t expected[] = {1U, 3U, 4U, 0U, 2U, 5U};
    for (uint8_t visual = 0U; visual < sizeof(expected); ++visual) {
        CHECK(color_clash_hand_index_at_visual(&state, 0U, visual) ==
              expected[visual]);
        CHECK(color_clash_hand_visual_index(
                  &state, 0U, expected[visual]) == visual);
    }
    CHECK(color_clash_hand_index_at_visual(&state, 0U, 6U) == UINT8_MAX);
    CHECK(color_clash_hand_visual_index(&state, 0U, 6U) == UINT8_MAX);

    state.phase = COLOR_CLASH_DRAWN_CARD;
    state.hands[0][5] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_TWO);
    CHECK(color_clash_hand_card_playable_now(&state, 0U, 5U));
    CHECK(!color_clash_hand_card_playable_now(&state, 0U, 1U));
    CHECK(color_clash_hand_index_at_visual(&state, 0U, 0U) == 5U);

    state = simple_state(2U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_ONE);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD_DRAW_FOUR);
    CHECK(color_clash_hand_card_playable_now(&state, 0U, 0U));
    CHECK(!color_clash_hand_card_playable_now(&state, 0U, 1U));
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);
    CHECK(!color_clash_hand_card_playable_now(&state, 0U, 0U));

    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    CHECK(p4_game_instance_start(&instance, &p4_color_clash_game, &services,
                                 &state, sizeof(state)));
    state = simple_state(2U);
    set_mixed_order_hand(&state);
    state.selected_card = 1U;
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.selected_card == 3U);
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.selected_card == 4U);
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.selected_card == 0U);
    CHECK(update_button(&instance, P4_BUTTON_LEFT));
    CHECK(state.selected_card == 4U);

    state.selected_card = 5U;
    uint16_t *const pixels = calloc(
        P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(region_differs_from(
            pixels, 8U, 140U, 30U, 6U, UINT16_C(0x0926)));
        CHECK(!region_differs_from(
            pixels, 110U, 140U, 30U, 6U, UINT16_C(0x0926)));
        free(pixels);
    }

    const uint8_t hand_before = state.hand_counts[0];
    tap(&instance, 12U, 160U);
    CHECK(state.selected_card == 1U);
    CHECK(state.hand_counts[0] == hand_before);
    tap(&instance, 12U, 160U);
    CHECK(state.hand_counts[0] == (uint8_t)(hand_before - 1U));
    CHECK(color_clash_card_rank(
              state.discard[state.discard_count - 1U]) ==
          COLOR_CLASH_FIVE);
    p4_game_instance_stop(&instance);
}

static void test_deck_and_matching(void)
{
    color_clash_state_t state = {
        .mode = COLOR_CLASH_PRACTICE,
        .local_player_slot = 0U,
        .menu_players = 4U,
    };
    color_clash_reset_match(&state, 4U, UINT32_C(0x1234));
    CHECK(state.player_count == 4U);
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.deck_count == 80U);
    CHECK(state.discard_count == 1U);
    unsigned counts[256] = {0};
    for (uint8_t player = 0U; player < state.player_count; ++player) {
        CHECK(state.hand_counts[player] == COLOR_CLASH_STARTING_HAND);
        for (uint8_t index = 0U; index < state.hand_counts[player]; ++index) {
            CHECK(color_clash_card_valid(state.hands[player][index]));
            ++counts[state.hands[player][index]];
        }
    }
    for (uint8_t index = 0U; index < state.deck_count; ++index) {
        CHECK(color_clash_card_valid(state.deck[index]));
        ++counts[state.deck[index]];
    }
    ++counts[state.discard[0]];
    unsigned total = 0U;
    unsigned wilds = 0U;
    unsigned draw_fours = 0U;
    unsigned gamechangers = 0U;
    for (size_t card = 0U; card < 256U; ++card) {
        total += counts[card];
        if (counts[card] != 0U) {
            if (color_clash_card_rank((uint8_t)card) == COLOR_CLASH_WILD) {
                wilds += counts[card];
            }
            if (color_clash_card_rank((uint8_t)card) ==
                COLOR_CLASH_WILD_DRAW_FOUR) {
                draw_fours += counts[card];
            }
            if (color_clash_card_rank((uint8_t)card) ==
                COLOR_CLASH_GAMECHANGER) {
                gamechangers += counts[card];
            }
        }
    }
    CHECK(total == COLOR_CLASH_DECK_CARDS);
    CHECK(wilds == 4U);
    CHECK(draw_fours == 4U);
    CHECK(gamechangers == 1U);
    for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
        CHECK(counts[color_clash_make_card(
                  (color_clash_color_t)color, COLOR_CLASH_ZERO)] == 1U);
        for (uint8_t rank = COLOR_CLASH_ONE;
             rank <= COLOR_CLASH_DRAW_TWO; ++rank) {
            CHECK(counts[color_clash_make_card(
                      (color_clash_color_t)color,
                      (color_clash_rank_t)rank)] == 2U);
        }
    }

    state = simple_state(4U);
    CHECK(color_clash_card_playable(
        &state, color_clash_make_card(COLOR_CLASH_RED, COLOR_CLASH_FIVE)));
    CHECK(color_clash_card_playable(
        &state, color_clash_make_card(COLOR_CLASH_GOLD, COLOR_CLASH_THREE)));
    CHECK(!color_clash_card_playable(
        &state, color_clash_make_card(COLOR_CLASH_GOLD, COLOR_CLASH_FIVE)));
    CHECK(color_clash_card_playable(
        &state, color_clash_make_card(COLOR_CLASH_RED, COLOR_CLASH_WILD)));
    CHECK(color_clash_card_playable(&state, color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD_DRAW_FOUR)));
    CHECK(color_clash_card_playable(&state, color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER)));
}

static void test_action_rules(void)
{
    color_clash_state_t state = simple_state(4U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_SKIP);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.current_player == 2U);

    state = simple_state(4U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_REVERSE);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.direction == 1U);
    CHECK(state.current_player == 3U);

    state = simple_state(2U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_REVERSE);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.current_player == 0U);

    state = simple_state(4U);
    state.hand_counts[0] = 2U;
    state.hand_counts[1] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_DRAW_TWO);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.hands[1][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_TWO);
    state.deck_count = 2U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_FOUR);
    state.deck[1] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_FIVE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.hand_counts[1] == 3U);
    CHECK(state.current_player == 2U);

    state = simple_state(2U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.phase == COLOR_CLASH_CHOOSE_COLOR);
    CHECK(color_clash_choose_color(&state, 0U, COLOR_CLASH_VIOLET));
    CHECK(state.active_color == COLOR_CLASH_VIOLET);
    CHECK(state.current_player == 1U);

    state = simple_state(4U);
    state.hand_counts[0] = 3U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD_DRAW_FOUR);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_SEVEN);
    state.hands[0][2] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(!color_clash_card_playable_for_player(&state, 0U, 0U));
    CHECK(!color_clash_play_card(&state, 0U, 0U));
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_SEVEN);
    CHECK(color_clash_card_playable_for_player(&state, 0U, 0U));
    state.hand_counts[1] = 1U;
    state.hands[1][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_TWO);
    state.deck_count = 4U;
    for (uint8_t index = 0U; index < 4U; ++index) {
        state.deck[index] = color_clash_make_card(
            COLOR_CLASH_VIOLET, (color_clash_rank_t)index);
    }
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.phase == COLOR_CLASH_CHOOSE_COLOR);
    CHECK(color_clash_choose_color(&state, 0U, COLOR_CLASH_VIOLET));
    CHECK(state.active_color == COLOR_CLASH_VIOLET);
    CHECK(state.hand_counts[1] == 5U);
    CHECK(state.current_player == 2U);
}

static void test_drawn_card_choice(void)
{
    color_clash_state_t state = simple_state(2U);
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.deck_count = 1U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    CHECK(color_clash_draw_card(&state, 0U));
    CHECK(state.phase == COLOR_CLASH_DRAWN_CARD);
    CHECK(state.current_player == 0U);
    CHECK(state.selected_card == 1U);
    CHECK(color_clash_play_card(&state, 0U, 1U));
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.current_player == 1U);

    state = simple_state(2U);
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.deck_count = 1U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    CHECK(color_clash_draw_card(&state, 0U));
    CHECK(color_clash_pass_drawn_card(&state, 0U));
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 2U);

    state = simple_state(2U);
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.deck_count = 1U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_FIVE);
    CHECK(color_clash_draw_card(&state, 0U));
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.current_player == 1U);
}

static void set_uno_hand(color_clash_state_t *state)
{
    state->hand_counts[0] = 2U;
    state->hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    state->hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state->deck_count = 6U;
    for (uint8_t index = 0U; index < state->deck_count; ++index) {
        state->deck[index] = color_clash_make_card(
            COLOR_CLASH_VIOLET, (color_clash_rank_t)index);
    }
}

static void test_uno_calls(void)
{
    color_clash_state_t state = simple_state(3U);
    set_uno_hand(&state);
    CHECK(color_clash_play_card_and_call_uno(&state, 0U, 0U));
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);
    CHECK(state.notice_player == 0U);

    state = simple_state(3U);
    set_uno_hand(&state);
    state.hand_counts[0] = 3U;
    state.hands[0][2] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
    const uint8_t previous_top = state.discard[state.discard_count - 1U];
    CHECK(!color_clash_play_card_and_call_uno(&state, 0U, 0U));
    CHECK(state.current_player == 0U);
    CHECK(state.hand_counts[0] == 3U);
    CHECK(state.discard[state.discard_count - 1U] == previous_top);
    CHECK(state.uno_pending_player == UINT8_C(0xff));

    state = simple_state(3U);
    set_uno_hand(&state);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.uno_pending_player == 0U);
    CHECK(color_clash_call_uno(&state, 0U));
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);
    CHECK(state.notice_player == 0U);
    CHECK(!color_clash_call_uno(&state, 1U));

    state = simple_state(3U);
    set_uno_hand(&state);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(color_clash_call_uno(&state, 1U));
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.hand_counts[0] == 3U);
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CAUGHT);
    CHECK(state.notice_player == 0U);

    state = simple_state(3U);
    set_uno_hand(&state);
    state.hand_counts[1] = 1U;
    state.hands[1][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    force_nonplayable_draw(&state);
    CHECK(color_clash_draw_card(&state, 1U));
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(!color_clash_call_uno(&state, 2U));

    state = simple_state(2U);
    state.hand_counts[0] = 2U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.deck_count = 2U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_TWO);
    state.deck[1] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_FOUR);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.phase == COLOR_CLASH_CHOOSE_COLOR);
    CHECK(state.uno_pending_player == 0U);
    CHECK(color_clash_call_uno(&state, 1U));
    CHECK(state.hand_counts[0] == 3U);
    CHECK(color_clash_choose_color(&state, 0U, COLOR_CLASH_VIOLET));
    CHECK(state.current_player == 1U);
}

static void test_gamechanger_rotation(void)
{
    color_clash_state_t state = simple_state(4U);
    state.hand_counts[0] = 3U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_ONE);
    state.hands[0][2] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.hand_counts[1] = 1U;
    state.hands[1][0] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_ONE);
    state.hand_counts[2] = 2U;
    state.hands[2][0] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_TWO);
    state.hands[2][1] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_THREE);
    state.hand_counts[3] = 4U;
    for (uint8_t index = 0U; index < 4U; ++index) {
        state.hands[3][index] = color_clash_make_card(
            COLOR_CLASH_GOLD, (color_clash_rank_t)index);
    }
    state.deck_count = 1U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_SIX);
    CHECK(color_clash_play_card(&state, 0U, 0U));
    CHECK(state.phase == COLOR_CLASH_CHOOSE_HAND);
    CHECK(state.selected_target == 1U);
    CHECK(color_clash_choose_hand(&state, 0U, 2U));
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 3U);
    CHECK(state.hands[0][0] == color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_TWO));
    CHECK(state.hands[0][1] == color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_THREE));
    CHECK(state.hand_counts[1] == 4U);
    CHECK(state.hand_counts[2] == 2U);
    CHECK(state.hands[2][0] == color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_ONE));
    CHECK(state.hand_counts[3] == 1U);

    state = simple_state(2U);
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);
    CHECK(!color_clash_play_card(&state, 0U, 0U));
    CHECK(state.hand_counts[0] == 1U);
}

static void test_uno_controls(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    color_clash_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_color_clash_game, &services,
                                 &state, sizeof(state)));

    state = simple_state(2U);
    state.current_player = 1U;
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.uno_pending_player = 0U;
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);

    state = simple_state(2U);
    set_uno_hand(&state);
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);

    state = simple_state(2U);
    set_uno_hand(&state);
    tap(&instance, 280U, 34U);
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);

    state = simple_state(2U);
    set_uno_hand(&state);
    state.hands[0][1] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_SIX);
    state.selected_card = 1U;
    state.phase = COLOR_CLASH_DRAWN_CARD;
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.current_player == 1U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CALLED);

    state = simple_state(2U);
    state.current_player = 1U;
    state.hand_counts[1] = 1U;
    state.hands[1][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.deck_count = 2U;
    state.deck[0] = color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
    state.deck[1] = color_clash_make_card(
        COLOR_CLASH_VIOLET, COLOR_CLASH_FOUR);
    state.uno_pending_player = 1U;
    tap(&instance, 280U, 34U);
    CHECK(state.uno_pending_player == UINT8_C(0xff));
    CHECK(state.hand_counts[1] == 3U);
    CHECK(state.notice == COLOR_CLASH_NOTICE_UNO_CAUGHT);

    p4_game_instance_stop(&instance);
}

static void test_lifecycle_touch_and_framebuffer(void)
{
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_AUDIO_TONE,
        .audio_context = &mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    p4_game_instance_t instance = {0};
    color_clash_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_color_clash_game, &services,
                                 &state, sizeof(state)));
    CHECK(state.phase == COLOR_CLASH_MENU);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
    CHECK(update_button(&instance, P4_BUTTON_UP));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_STANDARD);
    CHECK(state.menu_players == 2U);
    CHECK(update_button(&instance, P4_BUTTON_DOWN));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
    CHECK(update_button(&instance, P4_BUTTON_DOWN));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    CHECK(state.menu_players == 2U);
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.menu_players == 4U);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == COLOR_CLASH_TURN);
    CHECK(state.player_count == 4U);
    CHECK(state.local_player_slot == 0U);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    CHECK(update_button(&instance, P4_BUTTON_UP));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
    CHECK(update_button(&instance, P4_BUTTON_DOWN));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    tap(&instance, 132U, 30U);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_STANDARD);
    tap(&instance, 132U, 30U);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
    CHECK(update_button(&instance, P4_BUTTON_DOWN));
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);

    const uint8_t before = state.hand_counts[0];
    force_nonplayable_draw(&state);
    tap(&instance, 279U, 116U);
    CHECK(state.hand_counts[0] == (uint8_t)(before + 1U));
    CHECK(state.current_player == 1U);
    const uint8_t bot_hand_before = state.hand_counts[1];
    const uint8_t deck_before = state.deck_count;
    const uint8_t discard_before = state.discard_count;
    for (uint8_t frame = 0U; frame < 5U; ++frame) {
        CHECK(update_empty(&instance, 100U));
    }
    CHECK(state.current_player != 1U ||
          state.hand_counts[1] != bot_hand_before ||
          state.deck_count != deck_before ||
          state.discard_count != discard_before ||
          state.phase != COLOR_CLASH_TURN);

    uint16_t *const allocation = calloc(TOTAL_WORDS, sizeof(*allocation));
    CHECK(allocation != NULL);
    if (allocation != NULL) {
        for (size_t index = 0U; index < TOTAL_WORDS; ++index) {
            allocation[index] = UINT16_C(0x5aa5);
        }
        p4_game_surface_t surface = {
            .pixels = allocation + GUARD_WORDS,
            .stride_pixels = STRIDE,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        for (size_t index = 0U; index < GUARD_WORDS; ++index) {
            CHECK(allocation[index] == UINT16_C(0x5aa5));
            CHECK(allocation[GUARD_WORDS + FRAME_WORDS + index] ==
                  UINT16_C(0x5aa5));
        }
        for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
            for (size_t column = P4_GAME_SURFACE_WIDTH;
                 column < STRIDE; ++column) {
                CHECK(surface.pixels[row * STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        free(allocation);
    }
    CHECK(update_button(&instance, P4_BUTTON_BACK));
    CHECK(state.phase == COLOR_CLASH_MENU);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    tap(&instance, 60U, 150U);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
    tap(&instance, 250U, 150U);
    CHECK(state.color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_hand_touch_scrolling(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    color_clash_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_color_clash_game, &services,
                                 &state, sizeof(state)));
    state = simple_state(2U);
    state.hand_counts[0] = 12U;
    for (uint8_t index = 0U; index < state.hand_counts[0]; ++index) {
        state.hands[0][index] = color_clash_make_card(
            COLOR_CLASH_RED,
            (color_clash_rank_t)(index % (COLOR_CLASH_NINE + 1U)));
    }
    const uint8_t hand_before = state.hand_counts[0];
    const uint8_t discard_before = state.discard_count;

    hold_touch(&instance, 280U, 160U);
    hold_touch(&instance, 190U, 160U);
    hold_touch(&instance, 100U, 160U);
    CHECK(update_empty(&instance, 16U));
    CHECK(state.hand_window_start == 3U);
    CHECK(state.selected_card >= state.hand_window_start);
    CHECK(state.selected_card <
          (uint8_t)(state.hand_window_start + COLOR_CLASH_VISIBLE_CARDS));
    CHECK(state.hand_counts[0] == hand_before);
    CHECK(state.discard_count == discard_before);

    hold_touch(&instance, 40U, 160U);
    hold_touch(&instance, 140U, 160U);
    hold_touch(&instance, 240U, 160U);
    CHECK(update_empty(&instance, 16U));
    CHECK(state.hand_window_start == 0U);
    CHECK(state.hand_counts[0] == hand_before);
    CHECK(state.discard_count == discard_before);

    tap(&instance, 81U, 160U);
    CHECK(state.selected_card == 2U);
    CHECK(state.hand_counts[0] == hand_before);
    tap(&instance, 81U, 160U);
    CHECK(state.hand_counts[0] == (uint8_t)(hand_before - 1U));
    CHECK(state.discard_count == (uint8_t)(discard_before + 1U));

    p4_game_instance_stop(&instance);
}

static void test_color_chooser_render(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    color_clash_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_color_clash_game, &services,
                                 &state, sizeof(state)));
    state = simple_state(2U);
    state.color_aid = COLOR_CLASH_COLOR_AID_SYMBOLS;
    state.phase = COLOR_CLASH_CHOOSE_COLOR;
    state.hand_counts[0] = 1U;
    state.hands[0][0] = color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    state.discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_WILD_DRAW_FOUR);
    uint16_t *const pixels = calloc(
        P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT, sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        static const uint16_t expected[COLOR_CLASH_COLOR_COUNT] = {
            UINT16_C(0xf186), UINT16_C(0xfe20),
            UINT16_C(0x3679), UINT16_C(0x981f),
        };
        for (uint8_t color = 0U; color < COLOR_CLASH_COLOR_COUNT; ++color) {
            const size_t x = (size_t)(46U + color * 61U);
            CHECK(pixels[94U * P4_GAME_SURFACE_WIDTH + x] ==
                  expected[color]);
        }
        free(pixels);
    }
    p4_game_instance_stop(&instance);
}

static void poll_clients(p4_game_instance_t instances[COLOR_CLASH_MAX_PLAYERS],
                         uint8_t player_count)
{
    for (uint8_t slot = 1U; slot < player_count; ++slot) {
        CHECK(update_empty(&instances[slot], 16U));
    }
}

static void flush_network(
    p4_game_instance_t instances[COLOR_CLASH_MAX_PLAYERS],
    uint8_t player_count)
{
    CHECK(update_empty(&instances[0], 16U));
    poll_clients(instances, player_count);
}

static void check_private_views(
    const color_clash_state_t states[COLOR_CLASH_MAX_PLAYERS],
    uint8_t player_count)
{
    static const uint8_t hidden_hand[COLOR_CLASH_HAND_CAPACITY] = {0};
    for (uint8_t slot = 1U; slot < player_count; ++slot) {
        CHECK(states[slot].network_started);
        CHECK(states[slot].network_revision == states[0].network_revision);
        CHECK(states[slot].hand_counts[slot] ==
              states[0].hand_counts[slot]);
        CHECK(memcmp(states[slot].hands[slot], states[0].hands[slot],
                     states[0].hand_counts[slot]) == 0);
        for (uint8_t hidden = 0U;
             hidden < states[0].player_count; ++hidden) {
            if (hidden != slot) {
                CHECK(memcmp(states[slot].hands[hidden], hidden_hand,
                             sizeof(hidden_hand)) == 0);
            }
        }
    }
}

static void test_network_player_counts(void)
{
    for (uint8_t player_count = 2U;
         player_count <= COLOR_CLASH_MAX_PLAYERS; ++player_count) {
        test_link_t link;
        init_link(&link, player_count);
        p4_game_services_t services[COLOR_CLASH_MAX_PLAYERS];
        p4_game_instance_t instances[COLOR_CLASH_MAX_PLAYERS] = {0};
        color_clash_state_t states[COLOR_CLASH_MAX_PLAYERS];
        for (uint8_t slot = 0U; slot < player_count; ++slot) {
            services[slot] = network_services(&link.endpoints[slot]);
            CHECK(p4_game_instance_start(
                &instances[slot], &p4_color_clash_game, &services[slot],
                &states[slot], sizeof(states[slot])));
            CHECK(states[slot].phase == COLOR_CLASH_NETWORK_WAIT);
        }
        flush_network(instances, player_count);
        CHECK(states[0].network_started);
        CHECK(states[0].player_count == COLOR_CLASH_MAX_PLAYERS);
        CHECK(states[0].human_player_count == player_count);
        for (uint8_t player = 0U;
             player < COLOR_CLASH_MAX_PLAYERS; ++player) {
            CHECK(states[0].hand_counts[player] ==
                  COLOR_CLASH_STARTING_HAND);
        }
        check_private_views(states, player_count);
        for (uint8_t slot = 0U; slot < player_count; ++slot) {
            p4_game_instance_stop(&instances[slot]);
        }
    }
}

static void set_card(color_clash_state_t *state, uint8_t player,
                     uint8_t index, color_clash_color_t color,
                     color_clash_rank_t rank);

static void set_network_bot_round(color_clash_state_t *host)
{
    memset(host->hands, 0, sizeof(host->hands));
    memset(host->deck, 0, sizeof(host->deck));
    memset(host->discard, 0, sizeof(host->discard));
    for (uint8_t player = 0U; player < COLOR_CLASH_MAX_PLAYERS; ++player) {
        host->hand_counts[player] = 3U;
        set_card(host, player, 0U, COLOR_CLASH_RED,
                 (color_clash_rank_t)(COLOR_CLASH_FIVE + (player & 1U)));
        set_card(host, player, 1U, COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
        set_card(host, player, 2U, COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
    }
    host->deck_count = 8U;
    for (uint8_t index = 0U; index < host->deck_count; ++index) {
        host->deck[index] = color_clash_make_card(
            COLOR_CLASH_VIOLET, (color_clash_rank_t)index);
    }
    host->discard_count = 1U;
    host->discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_THREE);
    host->current_player = 2U;
    host->active_color = COLOR_CLASH_RED;
    host->direction = 0U;
    host->selected_card = 0U;
    host->winner = UINT8_C(0xff);
    host->uno_pending_player = UINT8_C(0xff);
    host->notice_player = UINT8_C(0xff);
    host->notice = COLOR_CLASH_NOTICE_NONE;
    host->notice_ms = 0U;
    host->phase = COLOR_CLASH_TURN;
    ++host->network_revision;
    host->network_sync_pending = true;
    host->network_sync_cursor = 0U;
    host->network_sync_stage = 0U;
    host->network_sync_offset = 0U;
}

static void test_network_bots(void)
{
    test_link_t link;
    init_link(&link, 2U);
    p4_game_services_t services[2];
    p4_game_instance_t instances[2] = {0};
    color_clash_state_t states[2];
    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        services[slot] = network_services(&link.endpoints[slot]);
        CHECK(p4_game_instance_start(
            &instances[slot], &p4_color_clash_game, &services[slot],
            &states[slot], sizeof(states[slot])));
    }
    flush_network(instances, 2U);
    CHECK(states[0].player_count == 4U);
    CHECK(states[0].human_player_count == 2U);
    CHECK(states[1].human_player_count == 2U);

    set_network_bot_round(&states[0]);
    flush_network(instances, 2U);
    CHECK(states[1].current_player == 2U);
    for (uint8_t frame = 0U; frame < 6U; ++frame) {
        CHECK(update_empty(&instances[1], 100U));
    }
    CHECK(states[1].current_player == 2U);

    const uint32_t before = states[0].network_revision;
    for (uint8_t frame = 0U; frame < 6U; ++frame) {
        CHECK(update_empty(&instances[0], 100U));
    }
    CHECK(states[0].network_revision == before + 1U);
    CHECK(states[0].current_player == 3U);
    CHECK(states[0].hand_counts[2] == 2U);
    flush_network(instances, 2U);
    CHECK(states[1].current_player == 3U);
    CHECK(states[1].hand_counts[2] == 2U);

    for (uint8_t frame = 0U; frame < 6U; ++frame) {
        CHECK(update_empty(&instances[0], 100U));
    }
    CHECK(states[0].current_player == 0U);
    CHECK(states[0].hand_counts[3] == 2U);
    flush_network(instances, 2U);
    CHECK(states[1].current_player == 0U);
    CHECK(states[1].hand_counts[3] == 2U);
    CHECK(states[1].network_revision == states[0].network_revision);

    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        p4_game_instance_stop(&instances[slot]);
    }
}

static void set_card(color_clash_state_t *state, uint8_t player,
                     uint8_t index, color_clash_color_t color,
                     color_clash_rank_t rank)
{
    state->hands[player][index] = color_clash_make_card(color, rank);
}

static void set_scripted_network_round(color_clash_state_t *host)
{
    memset(host->hands, 0, sizeof(host->hands));
    memset(host->deck, 0, sizeof(host->deck));
    memset(host->discard, 0, sizeof(host->discard));
    host->hand_counts[0] = 5U;
    set_card(host, 0U, 0U, COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    set_card(host, 0U, 1U, COLOR_CLASH_RED, COLOR_CLASH_GAMECHANGER);
    set_card(host, 0U, 2U, COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    set_card(host, 0U, 3U, COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
    set_card(host, 0U, 4U, COLOR_CLASH_VIOLET, COLOR_CLASH_THREE);
    host->hand_counts[1] = 4U;
    set_card(host, 1U, 0U, COLOR_CLASH_RED, COLOR_CLASH_WILD);
    set_card(host, 1U, 1U, COLOR_CLASH_GOLD, COLOR_CLASH_TWO);
    set_card(host, 1U, 2U, COLOR_CLASH_VIOLET, COLOR_CLASH_FOUR);
    set_card(host, 1U, 3U, COLOR_CLASH_TIFFANY, COLOR_CLASH_SEVEN);
    host->hand_counts[2] = 3U;
    set_card(host, 2U, 0U, COLOR_CLASH_RED,
             COLOR_CLASH_WILD_DRAW_FOUR);
    set_card(host, 2U, 1U, COLOR_CLASH_TIFFANY, COLOR_CLASH_EIGHT);
    set_card(host, 2U, 2U, COLOR_CLASH_GOLD, COLOR_CLASH_SIX);
    host->hand_counts[3] = 4U;
    set_card(host, 3U, 0U, COLOR_CLASH_RED, COLOR_CLASH_ZERO);
    set_card(host, 3U, 1U, COLOR_CLASH_GOLD, COLOR_CLASH_THREE);
    set_card(host, 3U, 2U, COLOR_CLASH_TIFFANY, COLOR_CLASH_FOUR);
    set_card(host, 3U, 3U, COLOR_CLASH_VIOLET, COLOR_CLASH_FIVE);
    host->deck_count = 24U;
    for (uint8_t index = 0U; index < host->deck_count; ++index) {
        host->deck[index] = color_clash_make_card(
            (color_clash_color_t)(index % COLOR_CLASH_COLOR_COUNT),
            (color_clash_rank_t)(index % (COLOR_CLASH_NINE + 1U)));
    }
    host->discard_count = 1U;
    host->discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_THREE);
    host->current_player = 0U;
    host->active_color = COLOR_CLASH_RED;
    host->direction = 0U;
    host->selected_card = 0U;
    host->winner = UINT8_C(0xff);
    host->phase = COLOR_CLASH_TURN;
    ++host->network_revision;
    host->network_sync_pending = true;
    host->network_sync_cursor = 0U;
    host->network_sync_stage = 0U;
    host->network_sync_offset = 0U;
}

static void set_network_uno_round(color_clash_state_t *host,
                                  uint8_t current_player)
{
    memset(host->hands, 0, sizeof(host->hands));
    memset(host->deck, 0, sizeof(host->deck));
    memset(host->discard, 0, sizeof(host->discard));
    host->hand_counts[0] = 2U;
    host->hand_counts[1] = 2U;
    set_card(host, 0U, 0U, COLOR_CLASH_RED, COLOR_CLASH_FIVE);
    set_card(host, 0U, 1U, COLOR_CLASH_GOLD, COLOR_CLASH_ONE);
    set_card(host, 1U, 0U, COLOR_CLASH_RED, COLOR_CLASH_SIX);
    set_card(host, 1U, 1U, COLOR_CLASH_TIFFANY, COLOR_CLASH_TWO);
    host->deck_count = 8U;
    for (uint8_t index = 0U; index < host->deck_count; ++index) {
        host->deck[index] = color_clash_make_card(
            COLOR_CLASH_VIOLET, (color_clash_rank_t)index);
    }
    host->discard_count = 1U;
    host->discard[0] = color_clash_make_card(
        COLOR_CLASH_RED, COLOR_CLASH_THREE);
    host->current_player = current_player;
    host->active_color = COLOR_CLASH_RED;
    host->direction = 0U;
    host->selected_card = 0U;
    host->winner = UINT8_C(0xff);
    host->uno_pending_player = UINT8_C(0xff);
    host->notice_player = UINT8_C(0xff);
    host->notice = COLOR_CLASH_NOTICE_NONE;
    host->notice_ms = 0U;
    host->phase = COLOR_CLASH_TURN;
    ++host->network_revision;
    host->network_sync_pending = true;
    host->network_sync_cursor = 0U;
    host->network_sync_stage = 0U;
    host->network_sync_offset = 0U;
}

static void test_network_uno_calls(void)
{
    test_link_t link;
    init_link(&link, 2U);
    p4_game_services_t services[2];
    p4_game_instance_t instances[2] = {0};
    color_clash_state_t states[2];
    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        services[slot] = network_services(&link.endpoints[slot]);
        CHECK(p4_game_instance_start(
            &instances[slot], &p4_color_clash_game, &services[slot],
            &states[slot], sizeof(states[slot])));
    }
    flush_network(instances, 2U);

    set_network_uno_round(&states[0], 0U);
    flush_network(instances, 2U);
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    flush_network(instances, 2U);
    CHECK(states[0].uno_pending_player == 0U);
    CHECK(states[1].uno_pending_player == 0U);
    CHECK(update_button(&instances[1], P4_BUTTON_START));
    flush_network(instances, 2U);
    CHECK(states[0].uno_pending_player == UINT8_C(0xff));
    CHECK(states[1].uno_pending_player == UINT8_C(0xff));
    CHECK(states[0].hand_counts[0] == 3U);
    CHECK(states[1].hand_counts[0] == 3U);
    CHECK(states[1].notice == COLOR_CLASH_NOTICE_UNO_CAUGHT);

    set_network_uno_round(&states[0], 1U);
    flush_network(instances, 2U);
    CHECK(update_button(&instances[1], P4_BUTTON_START));
    flush_network(instances, 2U);
    CHECK(states[0].uno_pending_player == UINT8_C(0xff));
    CHECK(states[1].uno_pending_player == UINT8_C(0xff));
    CHECK(states[0].hand_counts[1] == 1U);
    CHECK(states[1].hand_counts[1] == 1U);
    CHECK(states[1].notice == COLOR_CLASH_NOTICE_UNO_CALLED);

    for (uint8_t slot = 0U; slot < 2U; ++slot) {
        p4_game_instance_stop(&instances[slot]);
    }
}

static void test_four_player_network(void)
{
    test_link_t link;
    init_link(&link, 4U);
    p4_game_services_t services[COLOR_CLASH_MAX_PLAYERS];
    p4_game_instance_t instances[COLOR_CLASH_MAX_PLAYERS] = {0};
    color_clash_state_t states[COLOR_CLASH_MAX_PLAYERS];
    for (uint8_t slot = 0U; slot < link.player_count; ++slot) {
        services[slot] = network_services(&link.endpoints[slot]);
        CHECK(p4_game_instance_start(
            &instances[slot], &p4_color_clash_game, &services[slot],
            &states[slot], sizeof(states[slot])));
        CHECK(states[slot].phase == COLOR_CLASH_NETWORK_WAIT);
    }
    CHECK(update_empty(&instances[0], 16U));
    poll_clients(instances, link.player_count);
    CHECK(states[0].network_started);
    for (uint8_t slot = 1U; slot < link.player_count; ++slot) {
        CHECK(states[slot].network_started);
        CHECK(states[slot].color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);
        CHECK(states[slot].player_count == 4U);
        CHECK(states[slot].network_revision == states[0].network_revision);
        CHECK(states[slot].hand_counts[slot] == COLOR_CLASH_STARTING_HAND);
        CHECK(memcmp(states[slot].hands[slot], states[0].hands[slot],
                     COLOR_CLASH_STARTING_HAND) == 0);
        const uint8_t hidden = slot == 1U ? 2U : 1U;
        CHECK(states[slot].hands[hidden][0] == 0U);
    }

    CHECK(update_button(&instances[1], P4_BUTTON_DOWN));
    CHECK(states[1].color_aid == COLOR_CLASH_COLOR_AID_HIGH_CONTRAST);
    CHECK(states[0].color_aid == COLOR_CLASH_COLOR_AID_SYMBOLS);

    states[0].hand_counts[1] = 60U;
    for (uint8_t index = 0U; index < states[0].hand_counts[1]; ++index) {
        states[0].hands[1][index] = color_clash_make_card(
            (color_clash_color_t)(index % COLOR_CLASH_COLOR_COUNT),
            (color_clash_rank_t)(index % (COLOR_CLASH_DRAW_TWO + 1U)));
    }

    force_nonplayable_draw(&states[0]);
    CHECK(update_button(&instances[0], P4_BUTTON_B));
    CHECK(update_empty(&instances[0], 16U));
    poll_clients(instances, link.player_count);
    CHECK(states[0].current_player == 1U);
    CHECK(states[1].current_player == 1U);
    CHECK(states[1].hand_counts[1] == 60U);
    CHECK(memcmp(states[1].hands[1], states[0].hands[1], 60U) == 0);

    force_nonplayable_draw(&states[0]);
    CHECK(update_button(&instances[1], P4_BUTTON_B));
    CHECK(update_empty(&instances[0], 16U));
    poll_clients(instances, link.player_count);
    CHECK(states[0].current_player == 2U);
    CHECK(states[2].current_player == 2U);
    CHECK(states[1].network_revision == states[0].network_revision);

    force_nonplayable_draw(&states[0]);
    CHECK(update_button(&instances[2], P4_BUTTON_B));
    CHECK(update_empty(&instances[0], 16U));
    poll_clients(instances, link.player_count);
    CHECK(states[0].current_player == 3U);
    CHECK(states[3].current_player == 3U);

    link.state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(update_empty(&instances[3], 16U));
    CHECK(states[3].phase == COLOR_CLASH_NETWORK_LOST);
    for (uint8_t slot = 0U; slot < link.player_count; ++slot) {
        p4_game_instance_stop(&instances[slot]);
    }
}

static void test_four_player_gamechanger_network_round(void)
{
    test_link_t link;
    init_link(&link, 4U);
    p4_game_services_t services[COLOR_CLASH_MAX_PLAYERS];
    p4_game_instance_t instances[COLOR_CLASH_MAX_PLAYERS] = {0};
    color_clash_state_t states[COLOR_CLASH_MAX_PLAYERS];
    for (uint8_t slot = 0U; slot < link.player_count; ++slot) {
        services[slot] = network_services(&link.endpoints[slot]);
        CHECK(p4_game_instance_start(
            &instances[slot], &p4_color_clash_game, &services[slot],
            &states[slot], sizeof(states[slot])));
    }
    flush_network(instances, link.player_count);
    set_scripted_network_round(&states[0]);
    flush_network(instances, link.player_count);
    check_private_views(states, link.player_count);

    states[0].selected_card = 0U;
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].current_player == 1U);
    CHECK(states[0].hand_counts[0] == 4U);
    check_private_views(states, link.player_count);

    states[1].selected_card = 0U;
    CHECK(update_button(&instances[1], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].phase == COLOR_CLASH_CHOOSE_COLOR);
    CHECK(states[0].current_player == 1U);
    states[1].selected_color = COLOR_CLASH_VIOLET;
    CHECK(update_button(&instances[1], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].active_color == COLOR_CLASH_VIOLET);
    CHECK(states[0].current_player == 2U);
    check_private_views(states, link.player_count);

    states[2].selected_card = 0U;
    CHECK(update_button(&instances[2], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].phase == COLOR_CLASH_CHOOSE_COLOR);
    states[2].selected_color = COLOR_CLASH_TIFFANY;
    CHECK(update_button(&instances[2], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].active_color == COLOR_CLASH_TIFFANY);
    CHECK(states[0].current_player == 0U);
    CHECK(states[0].hand_counts[2] == 2U);
    CHECK(states[0].hand_counts[3] == 8U);
    check_private_views(states, link.player_count);

    states[0].selected_card = 0U;
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    flush_network(instances, link.player_count);
    CHECK(states[0].phase == COLOR_CLASH_CHOOSE_HAND);
    CHECK(states[0].current_player == 0U);
    states[0].selected_target = 2U;
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    flush_network(instances, link.player_count);

    CHECK(states[0].phase == COLOR_CLASH_TURN);
    CHECK(states[0].current_player == 1U);
    CHECK(states[0].hand_counts[0] == 3U);
    CHECK(states[0].hand_counts[1] == 8U);
    CHECK(states[0].hand_counts[2] == 3U);
    CHECK(states[0].hand_counts[3] == 3U);
    CHECK(states[0].hands[0][0] == color_clash_make_card(
        COLOR_CLASH_TIFFANY, COLOR_CLASH_EIGHT));
    CHECK(states[0].hands[0][1] == color_clash_make_card(
        COLOR_CLASH_GOLD, COLOR_CLASH_SIX));
    CHECK(states[0].deck_count == 19U);
    CHECK(color_clash_card_rank(
        states[0].discard[states[0].discard_count - 1U]) ==
        COLOR_CLASH_GAMECHANGER);
    check_private_views(states, link.player_count);

    for (uint8_t slot = 0U; slot < link.player_count; ++slot) {
        p4_game_instance_stop(&instances[slot]);
    }
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_color_clash_game));
    CHECK(p4_color_clash_game.launcher_id == 114U);
    CHECK(p4_color_clash_game.state_bytes <= P4_GAME_MAX_STATE_BYTES);
    CHECK((p4_color_clash_game.required_capabilities &
           P4_GAME_CAP_VIDEO_HIGH_RES) == 0U);
    CHECK((p4_color_clash_game.optional_capabilities &
           P4_GAME_CAP_VIDEO_HIGH_RES) != 0U);
    test_deck_and_matching();
    test_action_rules();
    test_drawn_card_choice();
    test_uno_calls();
    test_gamechanger_rotation();
    test_uno_controls();
    test_lifecycle_touch_and_framebuffer();
    test_render_resolution_negotiation();
    test_playable_hand_order_and_lift();
    test_hand_touch_scrolling();
    test_color_chooser_render();
    test_network_player_counts();
    test_network_bots();
    test_network_uno_calls();
    test_four_player_network();
    test_four_player_gamechanger_network_round();
    if (s_failures != 0) {
        fprintf(stderr, "%d Color Clash test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Color Clash tests passed");
    return EXIT_SUCCESS;
}
