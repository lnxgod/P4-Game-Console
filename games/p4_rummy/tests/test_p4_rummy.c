// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "p4_rummy_internal.h"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

enum {
    TEST_QUEUE_CAPACITY = 64,
    TEST_SURFACE_STRIDE = P4_GAME_SURFACE_WIDTH + 3,
    TEST_HIGH_RES_SURFACE_STRIDE = P4_GAME_SURFACE_HIGH_RES_WIDTH + 5,
    TEST_PADDING_SENTINEL = 0xa55a,
};

typedef struct {
    unsigned tones;
    unsigned stops;
} audio_mock_t;

typedef struct {
    p4_game_multiplayer_message_t messages[TEST_QUEUE_CAPACITY];
    size_t read_index;
    size_t write_index;
} message_queue_t;

typedef struct test_link test_link_t;

typedef struct {
    test_link_t *link;
    uint8_t slot;
} test_endpoint_t;

struct test_link {
    p4_game_multiplayer_state_t state;
    uint8_t player_count;
    uint32_t next_sequence[P4_RUMMY_MAX_PLAYERS];
    message_queue_t inbox[P4_RUMMY_MAX_PLAYERS];
    test_endpoint_t endpoints[P4_RUMMY_MAX_PLAYERS];
};

static uint16_t s_surface_pixels[P4_GAME_SURFACE_HEIGHT]
                                [TEST_SURFACE_STRIDE];
static uint16_t s_high_res_surface_pixels[P4_GAME_SURFACE_HIGH_RES_HEIGHT]
                                         [TEST_HIGH_RES_SURFACE_STRIDE];

static const p4_game_multiplayer_profile_t s_profile = {
    .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
    .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
    .min_players = P4_RUMMY_MIN_PLAYERS,
    .max_players = P4_RUMMY_MAX_PLAYERS,
    .tick_rate_hz = 10U,
    .input_delay_ticks = 0U,
    .message_bytes = P4_RUMMY_NETWORK_MESSAGE_BYTES,
    .protocol = P4_RUMMY_NETWORK_PROTOCOL,
    .flags = 0U,
};

static bool play_tone(void *context, const p4_tone_t *tone)
{
    audio_mock_t *const audio = context;
    if (audio == NULL || tone == NULL) {
        return false;
    }
    ++audio->tones;
    return true;
}

static void stop_audio(void *context)
{
    audio_mock_t *const audio = context;
    if (audio != NULL) {
        ++audio->stops;
    }
}

static bool read_status(void *context,
                        p4_game_multiplayer_status_t *status_out)
{
    const test_endpoint_t *const endpoint = context;
    if (endpoint == NULL || endpoint->link == NULL || status_out == NULL) {
        return false;
    }
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x503452554d4d5931),
        .state = endpoint->link->state,
        .role = endpoint->slot == 0U ? P4_GAME_MULTIPLAYER_ROLE_HOST
                                    : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = endpoint->slot,
        .player_count = endpoint->link->player_count,
    };
    return true;
}

static bool send_message(void *context, const uint8_t *data,
                         size_t data_bytes)
{
    test_endpoint_t *const endpoint = context;
    if (endpoint == NULL || endpoint->link == NULL || data == NULL ||
        data_bytes == 0U ||
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES) {
        return false;
    }
    const uint32_t sequence =
        ++endpoint->link->next_sequence[endpoint->slot];
    for (uint8_t destination = 0U;
         destination < endpoint->link->player_count; ++destination) {
        if (destination == endpoint->slot) {
            continue;
        }
        message_queue_t *const queue = &endpoint->link->inbox[destination];
        if (queue->write_index >= TEST_QUEUE_CAPACITY) {
            return false;
        }
        p4_game_multiplayer_message_t *const message =
            &queue->messages[queue->write_index++];
        *message = (p4_game_multiplayer_message_t){
            .sequence = sequence,
            .player_slot = endpoint->slot,
            .bytes = (uint8_t)data_bytes,
        };
        memcpy(message->data, data, data_bytes);
    }
    return true;
}

static bool receive_message(void *context,
                            p4_game_multiplayer_message_t *message_out)
{
    test_endpoint_t *const endpoint = context;
    if (endpoint == NULL || endpoint->link == NULL || message_out == NULL) {
        return false;
    }
    message_queue_t *const queue = &endpoint->link->inbox[endpoint->slot];
    if (queue->read_index >= queue->write_index) {
        return false;
    }
    *message_out = queue->messages[queue->read_index++];
    return true;
}

static void link_init(test_link_t *link, uint8_t player_count)
{
    memset(link, 0, sizeof(*link));
    link->state = P4_GAME_MULTIPLAYER_CONNECTED;
    link->player_count = player_count;
    for (uint8_t player = 0U; player < player_count; ++player) {
        link->endpoints[player].link = link;
        link->endpoints[player].slot = player;
    }
}

static p4_game_services_t local_services(audio_mock_t *audio)
{
    return (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_AUDIO_TONE,
        .audio_context = audio,
        .game_id = "org.p4console.p4-rummy",
        .play_tone = play_tone,
        .stop_audio = stop_audio,
    };
}

static p4_game_services_t network_services(audio_mock_t *audio,
                                           test_endpoint_t *endpoint)
{
    p4_game_services_t services = local_services(audio);
    services.available_capabilities |= P4_GAME_CAP_MULTIPLAYER_SESSION;
    services.multiplayer_context = endpoint;
    services.multiplayer_read_status = read_status;
    services.multiplayer_send = send_message;
    services.multiplayer_receive = receive_message;
    services.multiplayer_profile = &s_profile;
    return services;
}

static bool update_empty(p4_game_instance_t *instance, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {0};
    return p4_game_instance_update(instance, &input, elapsed_ms) ==
        P4_GAME_CONTINUE;
}

static bool update_button(p4_game_instance_t *instance, uint32_t button)
{
    const p4_game_input_t input = {
        .held = button,
        .pressed = button,
    };
    return p4_game_instance_update(instance, &input, 16U) ==
        P4_GAME_CONTINUE;
}

static bool tap(p4_game_instance_t *instance, uint16_t x, uint16_t y)
{
    const p4_game_input_t down = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    const p4_game_input_t up = {0};
    return p4_game_instance_update(instance, &down, 16U) ==
            P4_GAME_CONTINUE &&
        p4_game_instance_update(instance, &up, 16U) == P4_GAME_CONTINUE;
}

static uint8_t card(uint8_t suit, uint8_t rank)
{
    return (uint8_t)(suit * 13U +
                     (rank == 1U ? 12U : (uint8_t)(rank - 2U)));
}

static bool all_dealt_cards_unique(const p4_rummy_state_t *state)
{
    uint64_t seen = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        for (uint8_t index = 0U; index < state->hand_counts[player]; ++index) {
            const uint8_t dealt = state->hands[player][index];
            if (dealt >= P4_RUMMY_DECK_CARDS ||
                (seen & (UINT64_C(1) << dealt)) != 0U) {
                return false;
            }
            seen |= UINT64_C(1) << dealt;
        }
    }
    if (state->discard_count != 0U) {
        const uint8_t top = state->discard[state->discard_count - 1U];
        if (top >= P4_RUMMY_DECK_CARDS ||
            (seen & (UINT64_C(1) << top)) != 0U) {
            return false;
        }
    }
    return true;
}

static bool test_melds_and_deadwood(void)
{
    const uint8_t complete[7] = {
        card(0U, 2U), card(1U, 2U), card(2U, 2U),
        card(3U, 5U), card(3U, 6U), card(3U, 7U), card(3U, 8U),
    };
    const uint8_t ace_low[7] = {
        card(0U, 1U), card(0U, 2U), card(0U, 3U),
        card(1U, 9U), card(2U, 9U), card(3U, 9U), card(0U, 9U),
    };
    const uint8_t incomplete[7] = {
        card(0U, 2U), card(1U, 2U), card(2U, 5U),
        card(3U, 6U), card(3U, 7U), card(0U, 11U), card(1U, 13U),
    };
    CHECK(p4_rummy_hand_is_complete(complete, 7U));
    CHECK(p4_rummy_hand_is_complete(ace_low, 7U));
    CHECK(!p4_rummy_hand_is_complete(incomplete, 7U));
    CHECK(p4_rummy_hand_deadwood(complete, 7U) == 0U);
    CHECK(p4_rummy_hand_deadwood(incomplete, 7U) > 0U);
    const uint8_t hand_points[3] = {
        card(0U, 1U), card(1U, 13U), card(2U, 7U),
    };
    CHECK(p4_rummy_hand_points(hand_points, 3U) == 32U);
    return true;
}

static bool test_rummy_500_scoring(void)
{
    p4_rummy_state_t state;
    p4_rummy_reset_lobby(&state, 1U, UINT32_C(0x35303053));
    CHECK(p4_rummy_begin_round(&state));
    state.phase = P4_RUMMY_PHASE_DISCARD;
    state.current_player = 0U;
    state.hand_counts[0] = 4U;
    const uint8_t low_ace_hand[4] = {
        card(0U, 1U), card(0U, 2U), card(0U, 3U), card(1U, 5U),
    };
    memcpy(state.hands[0], low_ace_hand, sizeof(low_ace_hand));
    state.drawn_card_index = P4_RUMMY_NO_CARD;
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x07)));
    CHECK(state.scores[0] == 6);
    CHECK(state.round_scores[0] == 6);
    CHECK(state.phase == P4_RUMMY_PHASE_DISCARD);

    p4_rummy_reset_lobby(&state, 1U, UINT32_C(0x48494748));
    CHECK(p4_rummy_begin_round(&state));
    state.phase = P4_RUMMY_PHASE_DISCARD;
    state.current_player = 0U;
    state.hand_counts[0] = 4U;
    const uint8_t high_ace_hand[4] = {
        card(3U, 12U), card(3U, 13U), card(3U, 1U), card(1U, 5U),
    };
    memcpy(state.hands[0], high_ace_hand, sizeof(high_ace_hand));
    state.drawn_card_index = P4_RUMMY_NO_CARD;
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x07)));
    CHECK(state.scores[0] == 35);
    CHECK(state.round_scores[0] == 35);

    p4_rummy_reset_lobby(&state, 1U, UINT32_C(0x4d415443));
    CHECK(p4_rummy_begin_round(&state));
    state.phase = P4_RUMMY_PHASE_DISCARD;
    state.current_player = 0U;
    state.hand_counts[0] = 3U;
    const uint8_t winning_set[3] = {
        card(0U, 10U), card(1U, 10U), card(2U, 10U),
    };
    memcpy(state.hands[0], winning_set, sizeof(winning_set));
    state.hand_counts[1] = 3U;
    const uint8_t losing_hand[3] = {
        card(3U, 1U), card(1U, 13U), card(2U, 7U),
    };
    memcpy(state.hands[1], losing_hand, sizeof(losing_hand));
    state.drawn_card_index = P4_RUMMY_NO_CARD;
    state.scores[0] = 480;
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x07)));
    CHECK(state.phase == P4_RUMMY_PHASE_ROUND_OVER);
    CHECK(state.scores[0] == 510);
    CHECK(state.scores[1] == -32);
    CHECK(p4_rummy_match_winner(&state) == 0U);
    state.scores[1] = 510;
    CHECK(p4_rummy_match_winner(&state) == P4_RUMMY_NO_PLAYER);
    state.scores[1] = 509;
    CHECK(p4_rummy_match_winner(&state) == 0U);
    CHECK(p4_rummy_request_new_round(NULL, &state));
    CHECK(state.phase == P4_RUMMY_PHASE_DRAW);
    CHECK(state.round_number == 1U);
    for (uint8_t player = 0U; player < P4_RUMMY_MAX_PLAYERS; ++player) {
        CHECK(state.scores[player] == 0);
        CHECK(state.round_scores[player] == 0);
    }
    return true;
}

static bool test_play_melds_and_go_out(void)
{
    p4_rummy_state_t state;
    p4_rummy_reset_lobby(&state, 1U, UINT32_C(0x4d454c44));
    CHECK(p4_rummy_begin_round(&state));
    state.phase = P4_RUMMY_PHASE_DISCARD;
    state.current_player = 0U;
    state.hand_counts[0] = P4_RUMMY_DRAWN_CARDS;
    const uint8_t hand[P4_RUMMY_DRAWN_CARDS] = {
        card(0U, 2U), card(1U, 2U), card(2U, 2U), card(3U, 2U),
        card(3U, 5U), card(3U, 6U), card(3U, 7U), card(3U, 9U),
    };
    memcpy(state.hands[0], hand, sizeof(hand));
    state.drawn_card_index = P4_RUMMY_NO_CARD;

    CHECK(!p4_rummy_play_meld(&state, 0U, UINT8_C(0x03)));
    CHECK(state.hand_counts[0] == P4_RUMMY_DRAWN_CARDS);
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x07)));
    CHECK(state.meld_count == 1U);
    CHECK(state.meld_counts[0] == 3U);
    CHECK(state.hand_counts[0] == 5U);
    CHECK(state.scores[0] == 6);
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x01)));
    CHECK(state.meld_count == 1U);
    CHECK(state.meld_counts[0] == 4U);
    CHECK(state.hand_counts[0] == 4U);
    CHECK(state.scores[0] == 8);
    CHECK(p4_rummy_play_meld(&state, 0U, UINT8_C(0x07)));
    CHECK(state.meld_count == 2U);
    CHECK(state.meld_counts[1] == 3U);
    CHECK(state.hand_counts[0] == 1U);
    CHECK(state.scores[0] == 26);
    CHECK(p4_rummy_discard_card(&state, 0U, 0U));
    CHECK(state.phase == P4_RUMMY_PHASE_ROUND_OVER);
    CHECK(state.winner == 0U);
    CHECK(state.hand_counts[0] == 0U);
    CHECK(state.scores[0] == 26);
    return true;
}

static bool test_setup_deal_draw_and_win(void)
{
    p4_rummy_state_t state;
    p4_rummy_reset_lobby(&state, 1U, UINT32_C(0x12345678));
    CHECK(state.player_count == 2U);
    CHECK(state.human_player_count == 1U);
    CHECK(state.cpu_mask == UINT8_C(0x02));
    CHECK(p4_rummy_adjust_offline_players(&state, true));
    CHECK(state.player_count == 3U);
    CHECK(state.human_player_count == 1U);
    CHECK(state.cpu_mask == UINT8_C(0x06));
    CHECK(p4_rummy_adjust_offline_players(&state, true));
    CHECK(state.player_count == 4U);
    CHECK(state.human_player_count == 1U);
    CHECK(state.cpu_mask == UINT8_C(0x0e));
    CHECK(p4_rummy_begin_round(&state));
    CHECK(state.phase == P4_RUMMY_PHASE_DRAW);
    CHECK(state.discard_count == 1U);
    CHECK(state.deck_index == 29U);
    CHECK(all_dealt_cards_unique(&state));
    for (uint8_t player = 0U; player < 4U; ++player) {
        CHECK(state.hand_counts[player] == P4_RUMMY_HAND_CARDS);
    }

    const uint8_t top = state.discard[state.discard_count - 1U];
    CHECK(p4_rummy_draw(&state, 0U, P4_RUMMY_DRAW_DISCARD));
    CHECK(state.phase == P4_RUMMY_PHASE_DISCARD);
    CHECK(state.hands[0][7] == top);
    CHECK(!p4_rummy_discard_card(&state, 0U, 7U));
    CHECK(p4_rummy_discard_card(&state, 0U, 0U));

    return true;
}

static bool test_meld_controls(void)
{
    audio_mock_t audio = {0};
    const p4_game_services_t services = local_services(&audio);
    p4_game_instance_t instance = {0};
    p4_rummy_state_t state;
    CHECK(p4_game_instance_start(
        &instance, &p4_p4_rummy_game, &services, &state, sizeof(state)));
    state.phase = P4_RUMMY_PHASE_DISCARD;
    state.current_player = 0U;
    state.hand_counts[0] = P4_RUMMY_DRAWN_CARDS;
    const uint8_t hand[P4_RUMMY_DRAWN_CARDS] = {
        card(0U, 4U), card(1U, 4U), card(2U, 4U), card(0U, 6U),
        card(0U, 7U), card(0U, 8U), card(1U, 10U), card(2U, 13U),
    };
    memcpy(state.hands[0], hand, sizeof(hand));
    state.selected_card = 0U;
    state.selected_mask = 0U;
    state.drawn_card_index = P4_RUMMY_NO_CARD;
    CHECK(update_button(&instance, P4_BUTTON_B));
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(update_button(&instance, P4_BUTTON_B));
    CHECK(update_button(&instance, P4_BUTTON_RIGHT));
    CHECK(update_button(&instance, P4_BUTTON_B));
    CHECK(state.selected_mask == UINT8_C(0x07));
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.meld_count == 1U);
    CHECK(state.hand_counts[0] == 5U);
    CHECK(state.selected_mask == 0U);
    p4_game_instance_stop(&instance);

    p4_game_instance_t touch_instance = {0};
    p4_rummy_state_t touch_state;
    CHECK(p4_game_instance_start(
        &touch_instance, &p4_p4_rummy_game, &services,
        &touch_state, sizeof(touch_state)));
    touch_state.phase = P4_RUMMY_PHASE_DISCARD;
    touch_state.current_player = 0U;
    touch_state.hand_counts[0] = P4_RUMMY_DRAWN_CARDS;
    memcpy(touch_state.hands[0], hand, sizeof(hand));
    touch_state.selected_card = 0U;
    touch_state.selected_mask = 0U;
    touch_state.drawn_card_index = P4_RUMMY_NO_CARD;
    CHECK(tap(&touch_instance, 35U, 160U));
    CHECK(tap(&touch_instance, 68U, 160U));
    CHECK(tap(&touch_instance, 101U, 160U));
    CHECK(touch_state.selected_mask == UINT8_C(0x07));
    CHECK(tap(&touch_instance, 80U, 130U));
    CHECK(touch_state.meld_count == 1U);
    CHECK(touch_state.hand_counts[0] == 5U);
    CHECK(tap(&touch_instance, 210U, 130U));
    CHECK(touch_state.current_player == 1U);
    CHECK(touch_state.phase == P4_RUMMY_PHASE_DRAW);
    p4_game_instance_stop(&touch_instance);
    return true;
}

static bool test_cpu_and_local_lifecycle(void)
{
    audio_mock_t audio = {0};
    const p4_game_services_t services = local_services(&audio);
    p4_game_instance_t instance = {0};
    p4_rummy_state_t state;
    CHECK(p4_game_instance_start(
        &instance, &p4_p4_rummy_game, &services, &state, sizeof(state)));
    CHECK(state.phase == P4_RUMMY_PHASE_SETUP);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == P4_RUMMY_PHASE_DRAW);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == P4_RUMMY_PHASE_DISCARD);
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.current_player == 1U ||
          state.phase == P4_RUMMY_PHASE_ROUND_OVER);
    if (state.phase != P4_RUMMY_PHASE_ROUND_OVER) {
        for (uint8_t tick = 0U; tick < 4U; ++tick) {
            CHECK(update_empty(&instance, 100U));
        }
        CHECK(update_empty(&instance, 20U));
        CHECK(state.phase == P4_RUMMY_PHASE_DISCARD);
        for (uint8_t tick = 0U; tick < 4U; ++tick) {
            CHECK(update_empty(&instance, 100U));
        }
        CHECK(update_empty(&instance, 20U));
        CHECK(state.current_player == 0U ||
              state.phase == P4_RUMMY_PHASE_ROUND_OVER);
    }
    p4_game_surface_t surface = {
        .pixels = &s_surface_pixels[0][0],
        .stride_pixels = TEST_SURFACE_STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    CHECK(p4_game_instance_render(&instance, &surface));
    const p4_game_input_t back = {
        .pressed = P4_BUTTON_BACK,
    };
    CHECK(p4_game_instance_update(&instance, &back, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(audio.tones >= 3U);
    CHECK(audio.stops >= 1U);

    audio_mock_t high_res_audio = {0};
    p4_game_services_t high_res_services = local_services(&high_res_audio);
    high_res_services.available_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    p4_game_instance_t high_res_instance = {0};
    p4_rummy_state_t high_res_state;
    CHECK(p4_game_instance_start(
        &high_res_instance, &p4_p4_rummy_game, &high_res_services,
        &high_res_state, sizeof(high_res_state)));
    CHECK(update_button(&high_res_instance, P4_BUTTON_A));
    for (size_t y = 0U; y < P4_GAME_SURFACE_HIGH_RES_HEIGHT; ++y) {
        for (size_t x = 0U; x < TEST_HIGH_RES_SURFACE_STRIDE; ++x) {
            s_high_res_surface_pixels[y][x] = TEST_PADDING_SENTINEL;
        }
    }
    p4_game_surface_t high_res_surface = {
        .pixels = &s_high_res_surface_pixels[0][0],
        .stride_pixels = TEST_HIGH_RES_SURFACE_STRIDE,
        .width = P4_GAME_SURFACE_HIGH_RES_WIDTH,
        .height = P4_GAME_SURFACE_HIGH_RES_HEIGHT,
    };
    CHECK(p4_game_instance_render(&high_res_instance, &high_res_surface));
    CHECK(s_high_res_surface_pixels[0][0] != TEST_PADDING_SENTINEL);
    CHECK(s_high_res_surface_pixels[P4_GAME_SURFACE_HIGH_RES_HEIGHT - 1U]
                                   [P4_GAME_SURFACE_HIGH_RES_WIDTH - 1U] !=
          TEST_PADDING_SENTINEL);
    for (size_t y = 0U; y < P4_GAME_SURFACE_HIGH_RES_HEIGHT; ++y) {
        for (size_t x = P4_GAME_SURFACE_HIGH_RES_WIDTH;
             x < TEST_HIGH_RES_SURFACE_STRIDE; ++x) {
            CHECK(s_high_res_surface_pixels[y][x] == TEST_PADDING_SENTINEL);
        }
    }
    p4_game_instance_stop(&high_res_instance);
    CHECK(high_res_audio.stops >= 1U);

    p4_game_instance_t touch_instance = {0};
    p4_rummy_state_t touch_state;
    CHECK(p4_game_instance_start(
        &touch_instance, &p4_p4_rummy_game, &services,
        &touch_state, sizeof(touch_state)));
    CHECK(tap(&touch_instance, 180U, 102U));
    CHECK(touch_state.player_count == 3U);
    CHECK(touch_state.human_player_count == 1U);
    CHECK(touch_state.cpu_mask == UINT8_C(0x06));
    const p4_game_input_t exit_touch = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 10U, .y = 8U}},
    };
    CHECK(p4_game_instance_update(&touch_instance, &exit_touch, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&touch_instance);
    return true;
}

static bool start_network_table(
    test_link_t *link, uint8_t player_count,
    audio_mock_t audio[P4_RUMMY_MAX_PLAYERS],
    p4_game_services_t services[P4_RUMMY_MAX_PLAYERS],
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS],
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS])
{
    link_init(link, player_count);
    for (uint8_t player = 0U; player < player_count; ++player) {
        services[player] = network_services(
            &audio[player], &link->endpoints[player]);
        CHECK(p4_game_instance_start(
            &instances[player], &p4_p4_rummy_game, &services[player],
            &states[player], sizeof(states[player])));
        CHECK(states[player].network_mode);
        CHECK(states[player].network_player_count == player_count);
    }
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < player_count; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].network_started);
    }
    return true;
}

static bool sync_from_host(
    uint8_t player_count,
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS],
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS])
{
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < player_count; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].revision == states[0].revision);
        CHECK(states[player].phase == states[0].phase);
        CHECK(states[player].current_player == states[0].current_player);
        CHECK(memcmp(states[player].scores, states[0].scores,
                     sizeof(states[0].scores)) == 0);
    }
    return true;
}

static bool prepare_network_rank_meld(p4_rummy_state_t *state,
                                      uint8_t player)
{
    bool seen[P4_RUMMY_DECK_CARDS] = {false};
    for (uint8_t other = 0U; other < state->player_count; ++other) {
        for (uint8_t index = 0U; index < state->hand_counts[other]; ++index) {
            if (other != player || index >= 3U) {
                seen[state->hands[other][index]] = true;
            }
        }
    }
    if (state->discard_count != 0U) {
        seen[state->discard[state->discard_count - 1U]] = true;
    }
    for (uint8_t rank = 0U; rank < 13U; ++rank) {
        uint8_t found = 0U;
        for (uint8_t suit = 0U; suit < 4U && found < 3U; ++suit) {
            const uint8_t candidate = (uint8_t)(suit * 13U + rank);
            if (!seen[candidate]) {
                state->hands[player][found++] = candidate;
            }
        }
        if (found == 3U) {
            state->phase = P4_RUMMY_PHASE_DISCARD;
            state->current_player = player;
            state->drawn_card_index = P4_RUMMY_NO_CARD;
            state->selected_card = 0U;
            state->selected_mask = 0U;
            ++state->revision;
            p4_rummy_mark_snapshot_dirty(state);
            return true;
        }
    }
    return false;
}

static bool take_network_turn(
    uint8_t actor, uint8_t player_count,
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS],
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS])
{
    CHECK(states[actor].current_player == actor);
    CHECK(states[actor].phase == P4_RUMMY_PHASE_DRAW);
    CHECK(update_button(&instances[actor], P4_BUTTON_A));
    if (actor != 0U) {
        CHECK(states[actor].network_request_pending);
    }
    CHECK(sync_from_host(player_count, instances, states));
    CHECK(states[actor].phase == P4_RUMMY_PHASE_DISCARD);
    if (states[actor].selected_card == states[actor].drawn_card_index) {
        CHECK(update_button(&instances[actor], P4_BUTTON_LEFT));
    }
    CHECK(update_button(&instances[actor], P4_BUTTON_A));
    CHECK(sync_from_host(player_count, instances, states));
    return true;
}

static void stop_network_table(
    uint8_t player_count,
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS])
{
    for (uint8_t player = 0U; player < player_count; ++player) {
        p4_game_instance_stop(&instances[player]);
    }
}

static bool test_two_player_network_cpu_fill_and_peer_loss(void)
{
    test_link_t link;
    audio_mock_t audio[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_game_services_t services[P4_RUMMY_MAX_PLAYERS];
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS];
    CHECK(start_network_table(&link, 2U, audio, services,
                              instances, states));
    CHECK(update_button(&instances[0], P4_BUTTON_RIGHT));
    CHECK(update_button(&instances[0], P4_BUTTON_RIGHT));
    CHECK(states[0].player_count == 4U);
    CHECK(states[0].cpu_mask == UINT8_C(0x0c));
    CHECK(sync_from_host(2U, instances, states));
    CHECK(states[1].player_count == 4U);
    CHECK(states[1].cpu_mask == UINT8_C(0x0c));

    states[0].scores[0] = -25;
    ++states[0].revision;
    p4_rummy_mark_snapshot_dirty(&states[0]);
    CHECK(sync_from_host(2U, instances, states));
    CHECK(states[1].scores[0] == -25);

    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(sync_from_host(2U, instances, states));
    CHECK(states[0].phase == P4_RUMMY_PHASE_DRAW);
    CHECK(update_button(&instances[0], P4_BUTTON_B));
    CHECK(take_network_turn(0U, 2U, instances, states));
    if (states[0].phase != P4_RUMMY_PHASE_ROUND_OVER) {
        CHECK(states[0].current_player == 1U);
        CHECK(take_network_turn(1U, 2U, instances, states));
    }

    link.state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(update_empty(&instances[1], 16U));
    CHECK(!states[1].network_mode);
    CHECK(states[1].peer_lost_fallback);
    CHECK(states[1].phase == P4_RUMMY_PHASE_SETUP);
    CHECK(states[1].human_player_count == 1U);
    CHECK(states[1].player_count == 2U);
    CHECK(states[1].cpu_mask == UINT8_C(0x02));
    stop_network_table(2U, instances);
    return true;
}

static bool test_four_player_host_authority(void)
{
    test_link_t link;
    audio_mock_t audio[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_game_services_t services[P4_RUMMY_MAX_PLAYERS];
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS];
    CHECK(start_network_table(&link, 4U, audio, services,
                              instances, states));
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(sync_from_host(4U, instances, states));
    CHECK(states[0].player_count == 4U);
    CHECK(states[0].cpu_mask == 0U);
    for (uint8_t actor = 0U; actor < 4U; ++actor) {
        if (states[0].phase == P4_RUMMY_PHASE_ROUND_OVER) {
            break;
        }
        CHECK(states[0].current_player == actor);
        CHECK(take_network_turn(actor, 4U, instances, states));
        for (uint8_t observer = 1U; observer < 4U; ++observer) {
            CHECK(memcmp(states[observer].hands, states[0].hands,
                         sizeof(states[0].hands)) == 0);
        }
    }
    CHECK(link.next_sequence[0] > 0U);
    CHECK(link.next_sequence[1] > 0U);
    CHECK(link.next_sequence[2] > 0U);
    CHECK(link.next_sequence[3] > 0U);
    stop_network_table(4U, instances);
    return true;
}

static bool test_network_meld_request_and_snapshot(void)
{
    test_link_t link;
    audio_mock_t audio[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_game_services_t services[P4_RUMMY_MAX_PLAYERS];
    p4_game_instance_t instances[P4_RUMMY_MAX_PLAYERS] = {{0}};
    p4_rummy_state_t states[P4_RUMMY_MAX_PLAYERS];
    CHECK(start_network_table(&link, 2U, audio, services,
                              instances, states));
    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(sync_from_host(2U, instances, states));
    CHECK(prepare_network_rank_meld(&states[0], 1U));
    CHECK(sync_from_host(2U, instances, states));
    states[1].selected_card = 0U;
    CHECK(update_button(&instances[1], P4_BUTTON_B));
    for (uint8_t tick = 0U; tick < 70U; ++tick) {
        CHECK(update_empty(&instances[0], 16U));
        CHECK(update_empty(&instances[1], 16U));
    }
    CHECK(states[1].selected_card == 0U);
    CHECK(states[1].selected_mask == 0x01U);
    CHECK(update_button(&instances[1], P4_BUTTON_RIGHT));
    CHECK(update_button(&instances[1], P4_BUTTON_B));
    for (uint8_t tick = 0U; tick < 70U; ++tick) {
        CHECK(update_empty(&instances[0], 16U));
        CHECK(update_empty(&instances[1], 16U));
    }
    CHECK(states[1].selected_card == 1U);
    CHECK(states[1].selected_mask == 0x03U);
    CHECK(update_button(&instances[1], P4_BUTTON_RIGHT));
    CHECK(update_button(&instances[1], P4_BUTTON_B));
    CHECK(update_button(&instances[1], P4_BUTTON_START));
    CHECK(states[1].network_request_pending);
    CHECK(sync_from_host(2U, instances, states));
    CHECK(states[0].meld_count == 1U);
    CHECK(states[0].meld_counts[0] == 3U);
    CHECK(states[0].hand_counts[1] == 4U);
    CHECK(states[0].scores[1] > 0);
    CHECK(states[1].scores[1] == states[0].scores[1]);
    CHECK(states[1].meld_count == states[0].meld_count);
    CHECK(memcmp(states[1].melds, states[0].melds,
                 sizeof(states[0].melds)) == 0);
    stop_network_table(2U, instances);
    return true;
}

int main(void)
{
    if (!p4_game_descriptor_valid(&p4_p4_rummy_game) ||
        p4_p4_rummy_game.launcher_id != 118U ||
        (p4_p4_rummy_game.optional_capabilities &
         P4_GAME_CAP_VIDEO_HIGH_RES) == 0U ||
        !test_melds_and_deadwood() ||
        !test_rummy_500_scoring() ||
        !test_play_melds_and_go_out() ||
        !test_setup_deal_draw_and_win() ||
        !test_meld_controls() ||
        !test_cpu_and_local_lifecycle() ||
        !test_two_player_network_cpu_fill_and_peer_loss() ||
        !test_four_player_host_authority() ||
        !test_network_meld_request_and_snapshot()) {
        return EXIT_FAILURE;
    }
    puts("P4 Rummy tests passed");
    return EXIT_SUCCESS;
}
