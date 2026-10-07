// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/audio.h"
#include "p4/game.h"
#include "p4_yahtzee_internal.h"

extern const p4_game_descriptor_t p4_p4_yahtzee_game;

enum {
    GUARD_WORDS = 23,
    STRIDE = P4_GAME_SURFACE_WIDTH + 9,
    FRAME_WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
    TOTAL_WORDS = GUARD_WORDS + FRAME_WORDS + GUARD_WORDS,
    LINK_QUEUE = 32,
    SCORE_Y = 88,
    SCORE_ROW_H = 12,
    SCORE_COL_W = 158,
    COLOR_PANEL = 0x10c7,
    COLOR_OPTION_PINK = 0xfb56,
    COLOR_ACCENT = 0x5fea,
    COLOR_GOLD = 0xfe60,
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
    test_endpoint_t endpoints[P4_YAHTZEE_PLAYERS];
    uint8_t player_count;
    size_t last_send_bytes;
};

static bool link_status(
    void *context, p4_game_multiplayer_status_t *status_out)
{
    test_endpoint_t *const endpoint = context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x123456789abcdef0),
        .state = P4_GAME_MULTIPLAYER_CONNECTED,
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
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        source->slot >= source->link->player_count) {
        return false;
    }
    for (uint8_t slot = 0U; slot < source->link->player_count; ++slot) {
        if (slot != source->slot &&
            source->link->endpoints[slot].queue_count >= LINK_QUEUE) {
            return false;
        }
    }
    const uint32_t sequence = ++source->next_sequence;
    source->link->last_send_bytes = data_bytes;
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
    for (uint8_t slot = 0U; slot < P4_YAHTZEE_PLAYERS; ++slot) {
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

static void settle_roll(p4_game_instance_t *instance)
{
    for (size_t step = 0U; step < 4U; ++step) {
        CHECK(update_empty(instance, 100U));
    }
}

static uint16_t score_marker_pixel(
    const p4_game_surface_t *surface, uint8_t category)
{
    const uint8_t row = category < P4_YAHTZEE_THREE_KIND
        ? category : (uint8_t)(category - P4_YAHTZEE_THREE_KIND);
    const size_t x = category < P4_YAHTZEE_THREE_KIND
        ? 2U : (size_t)SCORE_COL_W + 2U;
    const size_t y = (size_t)SCORE_Y + (size_t)row * SCORE_ROW_H + 6U;
    return surface->pixels[y * surface->stride_pixels + x];
}

static void check_fresh_match(
    const p4_yahtzee_state_t *state, p4_yahtzee_mode_t mode,
    uint8_t player_count)
{
    CHECK(state->phase == P4_YAHTZEE_TURN);
    CHECK(state->mode == mode);
    CHECK(state->player_count == player_count);
    CHECK(state->current_player == 0U);
    CHECK(state->roll_count == 0U);
    CHECK(state->held_mask == 0U);
    for (size_t player = 0U; player < P4_YAHTZEE_PLAYERS; ++player) {
        CHECK(state->turns_scored[player] == 0U);
        for (size_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            CHECK(state->scores[player][category] == -1);
        }
    }
}

static void test_scoring_rules(void)
{
    static const uint8_t five_kind[5] = {6U, 6U, 6U, 6U, 6U};
    static const uint8_t full_house[5] = {2U, 2U, 5U, 5U, 5U};
    static const uint8_t small_straight[5] = {1U, 2U, 3U, 4U, 4U};
    static const uint8_t large_straight[5] = {2U, 3U, 4U, 5U, 6U};
    static const uint8_t four_kind[5] = {3U, 3U, 3U, 3U, 5U};
    CHECK(p4_yahtzee_score_dice(five_kind, P4_YAHTZEE_SIXES) == 30);
    CHECK(p4_yahtzee_score_dice(five_kind, P4_YAHTZEE_FIVE_KIND) == 50);
    CHECK(p4_yahtzee_score_dice(full_house, P4_YAHTZEE_FULL_HOUSE) == 25);
    CHECK(p4_yahtzee_score_dice(small_straight,
                                P4_YAHTZEE_SMALL_STRAIGHT) == 30);
    CHECK(p4_yahtzee_score_dice(large_straight,
                                P4_YAHTZEE_LARGE_STRAIGHT) == 40);
    CHECK(p4_yahtzee_score_dice(four_kind,
                                P4_YAHTZEE_THREE_KIND) == 17);
    CHECK(p4_yahtzee_score_dice(four_kind,
                                P4_YAHTZEE_FOUR_KIND) == 17);
    CHECK(p4_yahtzee_score_dice(full_house, P4_YAHTZEE_CHANCE) == 19);

    p4_yahtzee_state_t state = {.mode = P4_YAHTZEE_LOCAL};
    p4_yahtzee_reset_match(&state, 1U);
    state.scores[0][P4_YAHTZEE_ONES] = 3;
    state.scores[0][P4_YAHTZEE_TWOS] = 6;
    state.scores[0][P4_YAHTZEE_THREES] = 9;
    state.scores[0][P4_YAHTZEE_FOURS] = 12;
    state.scores[0][P4_YAHTZEE_FIVES] = 15;
    state.scores[0][P4_YAHTZEE_SIXES] = 18;
    CHECK(p4_yahtzee_upper_total(&state, 0U) == 63);
    CHECK(p4_yahtzee_total(&state, 0U) == 98);
}

static void test_local_lifecycle_and_framebuffer(void)
{
    p4_audio_mixer_t mixer;
    p4_audio_mixer_init(&mixer);
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
                                  P4_GAME_CAP_AUDIO_TONE,
        .audio_context = &mixer,
        .play_tone = p4_audio_mixer_service_play_tone,
        .stop_audio = p4_audio_mixer_service_stop,
    };
    p4_game_instance_t instance = {0};
    p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    CHECK(state.phase == P4_YAHTZEE_MENU);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == P4_YAHTZEE_TURN);
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.roll_count == 1U);
    p4_audio_mixer_stats_t audio_stats;
    p4_audio_mixer_get_stats(&mixer, &audio_stats);
    CHECK(audio_stats.tones_started >= 5U);
    settle_roll(&instance);
    CHECK(state.roll_animation_ms == 0U);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK((state.held_mask & 1U) != 0U);
    CHECK(update_button(&instance, P4_BUTTON_B));
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == P4_YAHTZEE_PASS);
    CHECK(state.current_player == 1U);
    CHECK(state.turns_scored[0] == 1U);

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
    CHECK(state.phase == P4_YAHTZEE_MENU);
    const p4_game_input_t back = {
        .held = P4_BUTTON_BACK,
        .pressed = P4_BUTTON_BACK,
        .touch_valid = true,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_local_four_player_rotation(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    CHECK(state.player_count == 2U);
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.player_count == 3U);
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(state.player_count == 4U);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == P4_YAHTZEE_TURN);

    for (uint8_t player = 0U; player < 4U; ++player) {
        CHECK(state.current_player == player);
        CHECK(update_button(&instance, P4_BUTTON_START));
        settle_roll(&instance);
        CHECK(update_button(&instance, P4_BUTTON_B));
        CHECK(update_button(&instance, P4_BUTTON_A));
        CHECK(state.turns_scored[player] == 1U);
        CHECK(state.phase == P4_YAHTZEE_PASS);
        CHECK(state.current_player == (uint8_t)((player + 1U) % 4U));
        CHECK(update_button(&instance, P4_BUTTON_A));
        CHECK(state.phase == P4_YAHTZEE_TURN);
    }

    for (uint8_t player = 0U; player < 4U; ++player) {
        state.turns_scored[player] = P4_YAHTZEE_CATEGORIES;
    }
    state.turns_scored[3] = P4_YAHTZEE_CATEGORIES - 1U;
    state.current_player = 3U;
    state.phase = P4_YAHTZEE_TURN;
    state.roll_count = 1U;
    state.roll_animation_ms = 0U;
    state.scores[3][P4_YAHTZEE_CHANCE] = -1;
    CHECK(p4_yahtzee_score_turn(&state, P4_YAHTZEE_CHANCE));
    CHECK(state.phase == P4_YAHTZEE_GAME_OVER);

    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        free(pixels);
    }
    CHECK(update_button(&instance, P4_BUTTON_A));
    check_fresh_match(&state, P4_YAHTZEE_LOCAL, 4U);
    p4_game_instance_stop(&instance);
}

static void test_scorecard_selection_highlights(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    CHECK(update_button(&instance, P4_BUTTON_A));

    uint16_t *const pixels = calloc(
        (size_t)P4_GAME_SURFACE_WIDTH * P4_GAME_SURFACE_HEIGHT,
        sizeof(*pixels));
    CHECK(pixels != NULL);
    if (pixels != NULL) {
        p4_game_surface_t surface = {
            .pixels = pixels,
            .stride_pixels = P4_GAME_SURFACE_WIDTH,
            .width = P4_GAME_SURFACE_WIDTH,
            .height = P4_GAME_SURFACE_HEIGHT,
        };
        CHECK(p4_game_instance_render(&instance, &surface));
        for (uint8_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            CHECK(score_marker_pixel(&surface, category) == COLOR_PANEL);
        }

        CHECK(update_button(&instance, P4_BUTTON_START));
        settle_roll(&instance);
        const uint8_t preview_dice[P4_YAHTZEE_DICE] = {
            2U, 2U, 3U, 4U, 6U,
        };
        memcpy(state.dice, preview_dice, sizeof(preview_dice));
        memcpy(state.animation_dice, preview_dice, sizeof(preview_dice));
        CHECK(p4_game_instance_render(&instance, &surface));
        for (uint8_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            const int preview = p4_yahtzee_score_dice(
                preview_dice, (p4_yahtzee_category_t)category);
            CHECK(score_marker_pixel(&surface, category) ==
                  (preview > 0 ? COLOR_OPTION_PINK : COLOR_PANEL));
        }

        CHECK(update_button(&instance, P4_BUTTON_B));
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(score_marker_pixel(&surface, P4_YAHTZEE_ONES) == COLOR_ACCENT);
        CHECK(score_marker_pixel(&surface, P4_YAHTZEE_TWOS) ==
              COLOR_OPTION_PINK);

        state.scores[0][P4_YAHTZEE_THREES] = 0;
        CHECK(p4_game_instance_render(&instance, &surface));
        CHECK(score_marker_pixel(&surface, P4_YAHTZEE_THREES) == COLOR_GOLD);
        CHECK(score_marker_pixel(&surface, P4_YAHTZEE_FOURS) ==
              COLOR_OPTION_PINK);
        free(pixels);
    }
    p4_game_instance_stop(&instance);
}

static void test_play_again_preserves_local_mode(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    CHECK(update_button(&instance, P4_BUTTON_A));

    state.phase = P4_YAHTZEE_GAME_OVER;
    state.current_player = 1U;
    state.turns_scored[0] = P4_YAHTZEE_CATEGORIES;
    state.turns_scored[1] = P4_YAHTZEE_CATEGORIES;
    state.scores[0][P4_YAHTZEE_ONES] = 3;
    state.scores[1][P4_YAHTZEE_CHANCE] = 22;
    CHECK(update_button(&instance, P4_BUTTON_A));
    check_fresh_match(&state, P4_YAHTZEE_LOCAL, 2U);

    state.phase = P4_YAHTZEE_GAME_OVER;
    state.current_player = 1U;
    state.turns_scored[0] = P4_YAHTZEE_CATEGORIES;
    state.turns_scored[1] = P4_YAHTZEE_CATEGORIES;
    state.scores[0][P4_YAHTZEE_TWOS] = 6;
    tap(&instance, 160U, 100U);
    check_fresh_match(&state, P4_YAHTZEE_LOCAL, 2U);

    p4_game_instance_stop(&instance);
}

static void test_network_host_authority(void)
{
    test_link_t link;
    init_link(&link, 2U);
    p4_game_services_t host_services = network_services(&link.endpoints[0]);
    p4_game_services_t client_services = network_services(&link.endpoints[1]);
    p4_game_instance_t host = {0};
    p4_game_instance_t client = {0};
    p4_yahtzee_state_t host_state;
    p4_yahtzee_state_t client_state;
    CHECK(p4_game_instance_start(&host, &p4_p4_yahtzee_game, &host_services,
                                 &host_state, sizeof(host_state)));
    CHECK(p4_game_instance_start(&client, &p4_p4_yahtzee_game,
                                 &client_services,
                                 &client_state, sizeof(client_state)));
    CHECK(host_state.phase == P4_YAHTZEE_NETWORK_WAIT);
    CHECK(client_state.phase == P4_YAHTZEE_NETWORK_WAIT);
    CHECK(update_empty(&host, 16U));
    CHECK(update_empty(&client, 16U));
    CHECK(host_state.network_started);
    CHECK(client_state.network_started);
    CHECK(host_state.network_revision == client_state.network_revision);

    CHECK(update_button(&host, P4_BUTTON_START));
    CHECK(update_empty(&client, 16U));
    CHECK(host_state.roll_count == 1U);
    CHECK(client_state.roll_count == 1U);
    CHECK(memcmp(host_state.dice, client_state.dice,
                 sizeof(host_state.dice)) == 0);
    settle_roll(&host);
    settle_roll(&client);
    CHECK(update_button(&host, P4_BUTTON_B));
    CHECK(update_button(&host, P4_BUTTON_A));
    CHECK(update_empty(&client, 16U));
    CHECK(host_state.current_player == 1U);
    CHECK(client_state.current_player == 1U);
    CHECK(host_state.scores[0][0] == client_state.scores[0][0]);

    CHECK(update_button(&client, P4_BUTTON_START));
    CHECK(client_state.roll_count == 0U);
    CHECK(update_empty(&host, 16U));
    CHECK(update_empty(&client, 16U));
    CHECK(host_state.roll_count == 1U);
    CHECK(client_state.roll_count == 1U);
    CHECK(host_state.network_revision == client_state.network_revision);
    CHECK(memcmp(host_state.dice, client_state.dice,
                 sizeof(host_state.dice)) == 0);

    host_state.phase = P4_YAHTZEE_GAME_OVER;
    client_state.phase = P4_YAHTZEE_GAME_OVER;
    host_state.current_player = 0U;
    client_state.current_player = 0U;
    host_state.turns_scored[0] = P4_YAHTZEE_CATEGORIES;
    host_state.turns_scored[1] = P4_YAHTZEE_CATEGORIES;
    client_state.turns_scored[0] = P4_YAHTZEE_CATEGORIES;
    client_state.turns_scored[1] = P4_YAHTZEE_CATEGORIES;
    host_state.scores[1][P4_YAHTZEE_CHANCE] = 23;
    client_state.scores[1][P4_YAHTZEE_CHANCE] = 23;
    const uint32_t revision_before_replay = host_state.network_revision;

    /* Slot 1 may request a rematch even though slot 0 ended with the turn. */
    CHECK(update_button(&client, P4_BUTTON_A));
    CHECK(client_state.phase == P4_YAHTZEE_GAME_OVER);
    CHECK(update_empty(&host, 16U));
    check_fresh_match(&host_state, P4_YAHTZEE_NETWORK, 2U);
    CHECK(host_state.network_started);
    CHECK(host_state.local_player_slot == 0U);
    CHECK(host_state.network_role == P4_GAME_MULTIPLAYER_ROLE_HOST);
    CHECK(host_state.network_revision == revision_before_replay + 1U);

    CHECK(update_empty(&client, 16U));
    check_fresh_match(&client_state, P4_YAHTZEE_NETWORK, 2U);
    CHECK(client_state.network_started);
    CHECK(client_state.local_player_slot == 1U);
    CHECK(client_state.network_role == P4_GAME_MULTIPLAYER_ROLE_CLIENT);
    CHECK(client_state.network_revision == host_state.network_revision);
    p4_game_instance_stop(&client);
    p4_game_instance_stop(&host);
}

static void test_four_player_network_snapshot(void)
{
    test_link_t link;
    init_link(&link, 4U);
    p4_game_services_t services[P4_YAHTZEE_PLAYERS];
    p4_game_instance_t instances[P4_YAHTZEE_PLAYERS] = {{0}};
    p4_yahtzee_state_t states[P4_YAHTZEE_PLAYERS];

    for (uint8_t player = 0U; player < 4U; ++player) {
        services[player] = network_services(&link.endpoints[player]);
        CHECK(p4_game_instance_start(
            &instances[player], &p4_p4_yahtzee_game, &services[player],
            &states[player], sizeof(states[player])));
        CHECK(states[player].player_count == 4U);
    }
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].network_started);
        CHECK(states[player].network_revision == states[0].network_revision);
    }

    for (uint8_t player = 0U; player < 4U; ++player) {
        states[0].turns_scored[player] = (uint8_t)(player + 1U);
        for (uint8_t category = 0U; category < P4_YAHTZEE_CATEGORIES;
             ++category) {
            states[0].scores[player][category] = (int16_t)(
                ((unsigned)player * 11U + (unsigned)category * 3U) % 51U);
        }
    }
    CHECK(update_button(&instances[0], P4_BUTTON_START));
    CHECK(link.last_send_bytes == 61U);
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].player_count == 4U);
        CHECK(memcmp(states[player].scores, states[0].scores,
                     sizeof(states[0].scores)) == 0);
        CHECK(memcmp(states[player].turns_scored, states[0].turns_scored,
                     sizeof(states[0].turns_scored)) == 0);
    }

    for (uint8_t player = 0U; player < 4U; ++player) {
        states[player].current_player = 3U;
        states[player].roll_count = 0U;
        states[player].held_mask = 0U;
        states[player].roll_animation_ms = 0U;
        states[player].animation_step_ms = 0U;
        states[player].phase = P4_YAHTZEE_TURN;
    }
    CHECK(update_button(&instances[3], P4_BUTTON_START));
    CHECK(states[3].roll_count == 0U);
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].current_player == 3U);
        CHECK(states[player].roll_count == 1U);
        CHECK(states[player].network_revision == states[0].network_revision);
    }

    for (uint8_t player = 0U; player < 4U; ++player) {
        p4_game_instance_stop(&instances[player]);
    }
}

static void test_touch_regions(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    tap(&instance, 245U, 117U); /* Local player-count increase. */
    CHECK(state.player_count == 3U);
    tap(&instance, 245U, 117U);
    CHECK(state.player_count == 4U);
    tap(&instance, 160U, 117U); /* Start local four-player match. */
    CHECK(state.phase == P4_YAHTZEE_TURN);
    CHECK(state.player_count == 4U);
    tap(&instance, 270U, 191U); /* Roll. */
    CHECK(state.roll_count == 1U);
    settle_roll(&instance);
    tap(&instance, 38U, 52U); /* First die. */
    CHECK((state.held_mask & 1U) != 0U);
    tap(&instance, 80U, 94U); /* Ones score row. */
    CHECK(state.turns_scored[0] == 1U);
    CHECK(state.phase == P4_YAHTZEE_PASS);
    const p4_game_input_t exit_touch = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 20U, .y = 12U}},
    };
    CHECK(p4_game_instance_update(&instance, &exit_touch, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

typedef struct { p4_dice_request_t request; p4_dice_status_t reply; } dice_fixture_t;
static bool dice_exchange(void *opaque,const p4_dice_request_t *r,p4_dice_status_t *out)
{
    dice_fixture_t *f=opaque; f->request=*r; *out=f->reply; return true;
}
static void test_dice_accessory(void)
{
    dice_fixture_t fixture={0};
    p4_game_services_t services={.available_capabilities=P4_GAME_CAP_DICE_ACCESSORY,
        .dice_context=&fixture,.dice_exchange=dice_exchange};
    p4_game_context_t context={.services=&services};
    p4_yahtzee_state_t state={0};p4_yahtzee_reset_match(&state,7);
    p4_yahtzee_poll_dice(&context,&state);
    CHECK(fixture.request.enabled && fixture.request.player_slot==0 && !fixture.request.can_hold);
    fixture.reply=(p4_dice_status_t){.token=fixture.request.token,.player_slot=1,.phase=P4_DICE_ROLLED};
    p4_yahtzee_poll_dice(&context,&state);CHECK(state.roll_count==0);
    fixture.reply.player_slot=0;
    p4_yahtzee_poll_dice(&context,&state);CHECK(state.roll_count==1);
    p4_yahtzee_poll_dice(&context,&state);CHECK(state.roll_count==1 && !fixture.request.enabled);
    p4_yahtzee_update_animation(&state,1000);state.held_mask=1;
    p4_yahtzee_poll_dice(&context,&state);
    CHECK(fixture.request.enabled && fixture.request.held_mask==1);
    uint8_t held=state.dice[0];
    fixture.reply=(p4_dice_status_t){.token=fixture.request.token,.player_slot=0,.phase=P4_DICE_ROLLED};
    p4_yahtzee_poll_dice(&context,&state);CHECK(state.roll_count==2 && state.dice[0]==held);
    p4_yahtzee_update_animation(&state,1000);state.roll_count=3;
    p4_yahtzee_poll_dice(&context,&state);CHECK(!fixture.request.enabled);
    state.roll_count=0;state.current_player=1;state.mode=P4_YAHTZEE_NETWORK;state.local_player_slot=0;
    p4_yahtzee_poll_dice(&context,&state);CHECK(!fixture.request.enabled);
    state.mode=P4_YAHTZEE_LOCAL;state.phase=P4_YAHTZEE_PASS;
    p4_yahtzee_poll_dice(&context,&state);CHECK(!fixture.request.enabled);
    uint32_t previous=fixture.request.token;
    p4_yahtzee_reset_match(&state,8);p4_yahtzee_poll_dice(&context,&state);
    CHECK(fixture.request.token>previous);
}

static void test_network_dice_accessory(void)
{
    test_link_t link; init_link(&link,2);
    dice_fixture_t fixture={0};
    p4_game_services_t host_services=network_services(&link.endpoints[0]);
    p4_game_services_t client_services=network_services(&link.endpoints[1]);
    client_services.available_capabilities|=P4_GAME_CAP_DICE_ACCESSORY;
    client_services.dice_context=&fixture;client_services.dice_exchange=dice_exchange;
    p4_game_instance_t host={0},client={0};
    p4_yahtzee_state_t hs,cs;
    CHECK(p4_game_instance_start(&host,&p4_p4_yahtzee_game,&host_services,&hs,sizeof(hs)));
    CHECK(p4_game_instance_start(&client,&p4_p4_yahtzee_game,&client_services,&cs,sizeof(cs)));
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(!fixture.request.enabled);
    CHECK(update_button(&host,P4_BUTTON_START));CHECK(update_empty(&client,16));
    settle_roll(&host);settle_roll(&client);
    CHECK(update_button(&host,P4_BUTTON_B));CHECK(update_button(&host,P4_BUTTON_A));
    CHECK(update_empty(&client,16));CHECK(fixture.request.enabled && fixture.request.player_slot==1);
    fixture.reply=(p4_dice_status_t){.token=fixture.request.token,.player_slot=1,.phase=P4_DICE_ROLLED};
    CHECK(update_empty(&client,16));CHECK(cs.accessory_pending && cs.roll_count==0);
    CHECK(update_empty(&client,16));CHECK(link.endpoints[0].queue_count==1);
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.roll_count==1 && cs.roll_count==1 && !cs.accessory_pending);
    CHECK(!memcmp(hs.dice,cs.dice,sizeof(hs.dice)));
    p4_game_instance_stop(&client);p4_game_instance_stop(&host);
}

static void test_shared_host_dice(void)
{
    test_link_t link; init_link(&link,2);
    dice_fixture_t fixture={0};
    p4_game_services_t host_services=network_services(&link.endpoints[0]);
    p4_game_services_t client_services=network_services(&link.endpoints[1]);
    host_services.available_capabilities|=P4_GAME_CAP_DICE_ACCESSORY;
    host_services.dice_context=&fixture;host_services.dice_exchange=dice_exchange;
    p4_game_instance_t host={0},client={0}; p4_yahtzee_state_t hs,cs;
    CHECK(p4_game_instance_start(&host,&p4_p4_yahtzee_game,&host_services,&hs,sizeof(hs)));
    CHECK(p4_game_instance_start(&client,&p4_p4_yahtzee_game,&client_services,&cs,sizeof(cs)));
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.shared_accessory && cs.shared_accessory && fixture.request.enabled);
    CHECK(!strcmp(fixture.request.player_name,"PLAYER 1"));
    const uint32_t old_token=fixture.request.token;
    fixture.reply=(p4_dice_status_t){.token=old_token,.player_slot=0,.phase=P4_DICE_ROLLED};
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.roll_count==1 && cs.roll_count==1);
    settle_roll(&host);settle_roll(&client);
    CHECK(update_button(&host,P4_BUTTON_B));CHECK(update_button(&host,P4_BUTTON_A));
    CHECK(update_empty(&client,16));CHECK(update_empty(&host,16));
    CHECK(hs.current_player==1 && fixture.request.enabled && fixture.request.player_slot==1);
    CHECK(!strcmp(fixture.request.player_name,"PLAYER 2") && fixture.request.token!=old_token);
    CHECK(update_empty(&host,16));CHECK(hs.roll_count==0); /* stale player-one shake */
    CHECK(update_button(&host,P4_BUTTON_START));CHECK(hs.roll_count==0); /* host pad cannot roll peer turn */
    fixture.reply=(p4_dice_status_t){.token=fixture.request.token,.player_slot=1,.phase=P4_DICE_ROLLED};
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.roll_count==1 && cs.roll_count==1 && !memcmp(hs.dice,cs.dice,sizeof(hs.dice)));
    CHECK(update_empty(&host,16));CHECK(hs.roll_count==1); /* duplicate */
    settle_roll(&host);settle_roll(&client);
    CHECK(update_button(&client,P4_BUTTON_A));CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.held_mask==1 && fixture.request.held_mask==1);
    CHECK(fixture.request.can_hold);
    uint32_t hold_token=fixture.request.token;
    fixture.reply=(p4_dice_status_t){.token=hold_token,.player_slot=1,
        .phase=P4_DICE_WAITING,.held_mask=31,.hold_changed=true,.hold_sequence=1};
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.held_mask==31 && cs.held_mask==31);
    CHECK(update_empty(&host,16));CHECK(fixture.request.can_hold && !fixture.request.enabled);
    CHECK(fixture.request.token!=hold_token);
    fixture.reply.token=fixture.request.token;fixture.reply.held_mask=5;fixture.reply.hold_sequence=2;
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.held_mask==5 && cs.held_mask==5);
    CHECK(update_empty(&host,16));CHECK(fixture.request.enabled && fixture.request.held_mask==5);
    hold_token=fixture.request.token;
    fixture.reply=(p4_dice_status_t){.token=hold_token,.player_slot=1,
        .phase=P4_DICE_WAITING,.held_mask=5,.hold_changed=true,.hold_sequence=3};
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));CHECK(update_empty(&host,16));
    CHECK(hs.held_mask==5 && fixture.request.hold_ack==3 && fixture.request.token!=hold_token);
    uint8_t held=hs.dice[0],held2=hs.dice[2];
    fixture.reply=(p4_dice_status_t){.token=fixture.request.token,.player_slot=1,.phase=P4_DICE_ROLLED};
    CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.roll_count==2 && cs.roll_count==2 && hs.dice[0]==held && cs.dice[0]==held && hs.dice[2]==held2 && cs.dice[2]==held2);
    settle_roll(&host);settle_roll(&client);
    fixture.reply.phase=P4_DICE_OFFLINE;
    CHECK(update_button(&client,P4_BUTTON_START));CHECK(update_empty(&host,16));CHECK(update_empty(&client,16));
    CHECK(hs.roll_count==3 && cs.roll_count==3); /* ordinary roll survives accessory loss */
    settle_roll(&host);CHECK(!fixture.request.enabled);
    hs.network_error=true;CHECK(update_empty(&host,16));CHECK(!fixture.request.enabled);
    p4_game_instance_stop(&client);p4_game_instance_stop(&host);
}

static void test_roll_distribution(void)
{
    p4_yahtzee_state_t state={0};p4_yahtzee_reset_match(&state,1234567U);
    unsigned counts[5][6]={{0}},mixed_parity=0,repeated=0;
    uint8_t previous[5]={0};
    for (unsigned roll=0;roll<12000U;++roll) {
        state.roll_count=0;state.roll_animation_ms=0;
        CHECK(p4_yahtzee_roll(&state));
        bool odd=false,even=false;
        for (unsigned i=0;i<5;++i) {
            CHECK(state.dice[i]>=1 && state.dice[i]<=6);
            ++counts[i][state.dice[i]-1U];
            odd |= (state.dice[i]&1U)!=0;even |= (state.dice[i]&1U)==0;
        }
        mixed_parity+=(unsigned)(odd&&even);
        repeated+=(unsigned)(memcmp(previous,state.dice,sizeof(previous))==0);
        memcpy(previous,state.dice,sizeof(previous));
    }
    CHECK(mixed_parity>10800U); /* rejects the old every-other-output parity defect */
    CHECK(repeated<20U);
    for (unsigned i=0;i<5;++i) for (unsigned face=0;face<6;++face)
        CHECK(counts[i][face]>1800U && counts[i][face]<2200U);
    p4_yahtzee_state_t quick={0},slow={0};
    p4_yahtzee_reset_match(&quick,42U);p4_yahtzee_reset_match(&slow,42U);
    for (unsigned roll=0;roll<3U;++roll) {
        CHECK(p4_yahtzee_roll(&quick));CHECK(p4_yahtzee_roll(&slow));
        CHECK(!memcmp(quick.dice,slow.dice,sizeof(quick.dice)));
        p4_yahtzee_update_animation(&quick,1000U);
        for (unsigned frame=0;frame<25U;++frame) p4_yahtzee_update_animation(&slow,16U);
    }
    p4_yahtzee_reset_match(&state,0U);
    CHECK(p4_yahtzee_roll(&state));
}

static void test_animation_elapsed_partition(void)
{
    p4_yahtzee_state_t seed = {0};
    p4_yahtzee_reset_match(&seed, 42U);
    CHECK(p4_yahtzee_roll(&seed));
    seed.held_mask = 1U;
    seed.animation_dice[0] = seed.dice[0];
    p4_yahtzee_state_t regular = seed, delayed = seed;
    for (unsigned frame = 0; frame < 6U; ++frame)
        p4_yahtzee_update_animation(&regular, 33U);
    p4_yahtzee_update_animation(&delayed, 198U);
    CHECK(regular.roll_animation_ms == delayed.roll_animation_ms);
    CHECK(regular.animation_step_ms == delayed.animation_step_ms);
    CHECK(regular.rng == delayed.rng);
    CHECK(memcmp(regular.animation_dice, delayed.animation_dice,
                 sizeof(regular.animation_dice)) == 0);
    CHECK(regular.animation_dice[0] == seed.dice[0]);
    CHECK(regular.roll_rng == seed.roll_rng);
    CHECK(memcmp(regular.dice, seed.dice, sizeof(seed.dice)) == 0);
    for (unsigned frame = 0; frame < 4U; ++frame)
        p4_yahtzee_update_animation(&regular, 33U);
    p4_yahtzee_update_animation(&delayed, 1000U);
    CHECK(regular.roll_animation_ms == 0U && delayed.roll_animation_ms == 0U);
    CHECK(regular.rng == delayed.rng);
    CHECK(memcmp(regular.animation_dice, seed.dice, sizeof(seed.dice)) == 0);
    CHECK(memcmp(delayed.animation_dice, seed.dice, sizeof(seed.dice)) == 0);
}

static void touch_with_buttons(p4_game_instance_t *instance, bool down,
                               uint16_t x, uint16_t y)
{
    const uint32_t synthetic = P4_BUTTON_A | P4_BUTTON_B | P4_BUTTON_START |
        P4_BUTTON_BACK | P4_BUTTON_RIGHT | P4_BUTTON_DOWN;
    const p4_game_input_t input = {.pressed = synthetic, .held = synthetic,
        .touch_valid = true, .touch_count = down ? 1U : 0U, .touches = {{x, y}}};
    CHECK(p4_game_instance_update(instance, &input, 16U) == P4_GAME_CONTINUE);
}

static void test_touch_owns_gesture(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0}; p4_yahtzee_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_yahtzee_game, &services,
                                 &state, sizeof(state)));
    touch_with_buttons(&instance, true, 245U, 117U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.player_count == 3U && state.phase == P4_YAHTZEE_MENU);
    touch_with_buttons(&instance, true, 245U, 117U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.player_count == 4U);
    touch_with_buttons(&instance, true, 160U, 117U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.phase == P4_YAHTZEE_TURN && state.roll_count == 0U);
    touch_with_buttons(&instance, true, 270U, 191U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.roll_count == 1U);
    settle_roll(&instance);
    touch_with_buttons(&instance, true, 38U, 52U);
    touch_with_buttons(&instance, true, 38U, 52U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.held_mask == 1U && state.selected_die == 0U);
    CHECK(state.focus == P4_YAHTZEE_FOCUS_DICE);
    touch_with_buttons(&instance, true, 80U, 166U); /* Bonus summary. */
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.turns_scored[0] == 0U && state.phase == P4_YAHTZEE_TURN);
    touch_with_buttons(&instance, true, 80U, 94U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.turns_scored[0] == 1U && state.phase == P4_YAHTZEE_PASS);
    CHECK(state.current_player == 1U);
    touch_with_buttons(&instance, true, 160U, 124U);
    touch_with_buttons(&instance, false, 0U, 0U);
    CHECK(state.phase == P4_YAHTZEE_TURN && state.roll_count == 0U);
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.roll_count == 1U); /* Physical controller resumes normally. */
    p4_game_instance_stop(&instance);
}

/* Share the paired transport fixture with the lifecycle regression cases. */
#include "test_network_lifecycle.h"

int main(void)
{
    test_touch_owns_gesture();
    test_animation_elapsed_partition();
    test_roll_distribution();
    test_shared_host_dice();
    test_dice_accessory();
    test_network_dice_accessory();
    CHECK(p4_game_descriptor_valid(&p4_p4_yahtzee_game));
    CHECK(p4_p4_yahtzee_game.launcher_id == 113U);
    test_scoring_rules();
    test_local_lifecycle_and_framebuffer();
    test_local_four_player_rotation();
    test_scorecard_selection_highlights();
    test_play_again_preserves_local_mode();
    test_network_host_authority();
    test_four_player_network_snapshot();
    test_touch_regions();
    test_network_lifecycle();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 Yahtzee test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("P4 Yahtzee tests passed");
    return EXIT_SUCCESS;
}
