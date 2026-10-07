// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/draw.h"
#include "p4/game.h"
#include "p4/input.h"
#include "texas_holdem_internal.h"

extern const p4_game_descriptor_t p4_texas_holdem_game;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",               \
                    __FILE__, __LINE__, #condition);                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

enum {
    TEST_QUEUE_CAPACITY = 128,
    TEST_SURFACE_STRIDE = P4_GAME_SURFACE_WIDTH + 3,
    TEST_NET_CPU_MASK_OFFSET = 17,
    TEST_NET_HOLE_OFFSET = 50,
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
    uint32_t next_sequence[TEXAS_HOLDEM_PLAYERS];
    size_t last_send_bytes;
    message_queue_t inbox[TEXAS_HOLDEM_PLAYERS];
    test_endpoint_t endpoints[TEXAS_HOLDEM_PLAYERS];
};

static uint16_t s_surface_pixels[P4_GAME_SURFACE_HEIGHT]
                                [TEST_SURFACE_STRIDE];

static const p4_game_multiplayer_profile_t s_profile = {
    .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
    .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
    .min_players = TEXAS_HOLDEM_MIN_PLAYERS,
    .max_players = TEXAS_HOLDEM_PLAYERS,
    .tick_rate_hz = 10U,
    .input_delay_ticks = 0U,
    .message_bytes = 63U,
    .protocol = TEXAS_HOLDEM_NETWORK_PROTOCOL,
    .flags = 0U,
};

static bool play_tone(void *context, const p4_tone_t *tone)
{
    audio_mock_t *const mock = context;
    if (mock == NULL || tone == NULL) {
        return false;
    }
    ++mock->tones;
    return true;
}

static void stop_audio(void *context)
{
    audio_mock_t *const mock = context;
    if (mock != NULL) {
        ++mock->stops;
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
        .session_seed = UINT64_C(0x54455841534c4f54),
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
    test_link_t *const link = endpoint->link;
    const uint32_t sequence = ++link->next_sequence[endpoint->slot];
    link->last_send_bytes = data_bytes;
    for (uint8_t destination = 0U; destination < link->player_count;
         ++destination) {
        if (destination == endpoint->slot) {
            continue;
        }
        message_queue_t *const queue = &link->inbox[destination];
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
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_VIDEO_HIGH_RES,
        .audio_context = audio,
        .game_id = "org.p4console.texas-holdem",
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
    p4_game_input_mapper_t mapper;
    p4_game_input_mapper_init(&mapper);
    const p4_physical_touch_t point = {
        .x = (uint16_t)(P4_INPUT_VIEWPORT_LEFT +
            ((uint32_t)x * P4_INPUT_VIEWPORT_WIDTH + 319U) / 320U),
        .y = (uint16_t)(P4_INPUT_VIEWPORT_TOP +
            ((uint32_t)y * P4_INPUT_VIEWPORT_HEIGHT + 199U) / 200U),
    };
    p4_game_input_t down, up;
    p4_game_input_mapper_update(&mapper, true, &point, 1U, 0U, &down);
    p4_game_input_mapper_update(&mapper, true, NULL, 0U, 0U, &up);
    return p4_game_instance_update(instance, &down, 16U) ==
            P4_GAME_CONTINUE &&
        p4_game_instance_update(instance, &up, 16U) == P4_GAME_CONTINUE;
}

static uint8_t card(uint8_t suit, uint8_t rank)
{
    return (uint8_t)(suit * 13U + rank - 2U);
}

static uint32_t chip_total(const texas_holdem_state_t *state)
{
    uint32_t total = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        total += state->stacks[player];
        total += state->contribution[player];
    }
    return total;
}

static bool state_invariants(const texas_holdem_state_t *state,
                             uint32_t expected_chips)
{
    if (state == NULL || state->player_count < TEXAS_HOLDEM_MIN_PLAYERS ||
        state->player_count > TEXAS_HOLDEM_PLAYERS ||
        state->phase > TEXAS_HOLDEM_PHASE_MATCH_OVER ||
        state->current_player >= state->player_count ||
        chip_total(state) != expected_chips) {
        return false;
    }
    const uint8_t valid = (uint8_t)(
        (UINT8_C(1) << state->player_count) - 1U);
    if (((state->cpu_mask | state->active_mask | state->folded_mask |
          state->all_in_mask | state->acted_mask | state->winner_mask) &
         (uint8_t)~valid) != 0U ||
        (state->folded_mask & (uint8_t)~state->active_mask) != 0U ||
        (state->all_in_mask & (uint8_t)~state->active_mask) != 0U) {
        return false;
    }
    uint32_t contributions = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        if (state->round_bet[player] > state->contribution[player]) {
            return false;
        }
        contributions += state->contribution[player];
    }
    const bool betting = state->phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
        state->phase <= TEXAS_HOLDEM_PHASE_RIVER;
    if (betting) {
        const uint8_t actor = (uint8_t)(
            state->active_mask & (uint8_t)~state->folded_mask &
            (uint8_t)~state->all_in_mask);
        if ((actor & (UINT8_C(1) << state->current_player)) == 0U ||
            state->pot != contributions) {
            return false;
        }
    }
    const uint8_t expected_community =
        state->phase == TEXAS_HOLDEM_PHASE_FLOP ? 3U :
        (state->phase == TEXAS_HOLDEM_PHASE_TURN ? 4U :
         (state->phase == TEXAS_HOLDEM_PHASE_RIVER ? 5U : 0U));
    if (state->phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) {
        if (state->community_count != 0U && state->community_count != 3U &&
            state->community_count != 4U && state->community_count != 5U) {
            return false;
        }
    } else if (state->community_count != expected_community) {
        return false;
    }
    uint64_t seen = 0U;
    for (uint8_t player = 0U; player < state->player_count; ++player) {
        for (uint8_t index = 0U; index < 2U; ++index) {
            const uint8_t dealt = state->hole[player][index];
            if (dealt == TEXAS_HOLDEM_NO_CARD) {
                if ((betting || state->phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) &&
                    (state->active_mask & (UINT8_C(1) << player)) != 0U) {
                    return false;
                }
            } else if (dealt >= TEXAS_HOLDEM_DECK_CARDS ||
                       (seen & (UINT64_C(1) << dealt)) != 0U) {
                return false;
            } else {
                seen |= UINT64_C(1) << dealt;
            }
        }
    }
    for (uint8_t index = 0U; index < state->community_count; ++index) {
        const uint8_t dealt = state->community[index];
        if (dealt >= TEXAS_HOLDEM_DECK_CARDS ||
            (seen & (UINT64_C(1) << dealt)) != 0U) {
            return false;
        }
        seen |= UINT64_C(1) << dealt;
    }
    return true;
}

static bool surface_padding_intact(uint16_t value)
{
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH;
             column < TEST_SURFACE_STRIDE; ++column) {
            if (s_surface_pixels[row][column] != value) {
                return false;
            }
        }
    }
    return true;
}

static bool test_hand_ranking(void)
{
    const uint8_t royal_hole[2] = {card(3U, 14U), card(3U, 13U)};
    const uint8_t royal_board[5] = {
        card(3U, 12U), card(3U, 11U), card(3U, 10U),
        card(0U, 2U), card(1U, 3U),
    };
    const uint8_t quads_hole[2] = {card(0U, 14U), card(1U, 14U)};
    const uint8_t quads_board[5] = {
        card(2U, 14U), card(3U, 14U), card(0U, 2U),
        card(1U, 3U), card(2U, 4U),
    };
    const uint8_t full_hole[2] = {card(0U, 13U), card(1U, 13U)};
    const uint8_t full_board[5] = {
        card(2U, 13U), card(0U, 9U), card(1U, 9U),
        card(2U, 2U), card(3U, 4U),
    };
    const uint8_t wheel_hole[2] = {card(0U, 14U), card(1U, 2U)};
    const uint8_t wheel_board[5] = {
        card(2U, 3U), card(3U, 4U), card(0U, 5U),
        card(1U, 9U), card(2U, 11U),
    };
    const uint32_t royal = texas_holdem_best_hand_rank(
        royal_hole, royal_board);
    const uint32_t quads = texas_holdem_best_hand_rank(
        quads_hole, quads_board);
    const uint32_t full_house = texas_holdem_best_hand_rank(
        full_hole, full_board);
    const uint32_t wheel = texas_holdem_best_hand_rank(
        wheel_hole, wheel_board);
    CHECK((royal >> 20U) == 8U);
    CHECK((quads >> 20U) == 7U);
    CHECK((full_house >> 20U) == 6U);
    CHECK((wheel >> 20U) == 4U);
    CHECK(royal > quads && quads > full_house && full_house > wheel);
    return true;
}

static bool test_exhaustive_five_card_categories(void)
{
    static const uint32_t expected[9] = {
        UINT32_C(1302540), UINT32_C(1098240), UINT32_C(123552),
        UINT32_C(54912), UINT32_C(10200), UINT32_C(5108),
        UINT32_C(3744), UINT32_C(624), UINT32_C(40),
    };
    uint32_t counts[9] = {0U};
    uint8_t cards[5];
    for (cards[0] = 0U; cards[0] < 48U; ++cards[0]) {
        for (cards[1] = (uint8_t)(cards[0] + 1U);
             cards[1] < 49U; ++cards[1]) {
            for (cards[2] = (uint8_t)(cards[1] + 1U);
                 cards[2] < 50U; ++cards[2]) {
                for (cards[3] = (uint8_t)(cards[2] + 1U);
                     cards[3] < 51U; ++cards[3]) {
                    for (cards[4] = (uint8_t)(cards[3] + 1U);
                         cards[4] < 52U; ++cards[4]) {
                        const uint8_t category = (uint8_t)(
                            texas_holdem_five_card_rank(cards) >> 20U);
                        CHECK(category < 9U);
                        ++counts[category];
                    }
                }
            }
        }
    }
    for (uint8_t category = 0U; category < 9U; ++category) {
        CHECK(counts[category] == expected[category]);
    }
    return true;
}

static bool test_equal_stacks_blinds_and_betting(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 4U, UINT32_C(0x12345678));
    CHECK(state.starting_stack == 1000U);
    CHECK(texas_holdem_adjust_stack(&state, true));
    CHECK(state.starting_stack == 2000U);
    for (uint8_t player = 0U; player < 4U; ++player) {
        CHECK(state.stacks[player] == 2000U);
    }
    CHECK(texas_holdem_adjust_stack(&state, false));
    CHECK(texas_holdem_begin_match(&state));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_PREFLOP);
    CHECK(state.dealer == 0U);
    CHECK(state.current_player == 3U);
    CHECK(state.stacks[0] == 1000U);
    CHECK(state.stacks[1] == 990U);
    CHECK(state.stacks[2] == 980U);
    CHECK(state.stacks[3] == 1000U);
    CHECK(state.pot == 30U);
    CHECK(chip_total(&state) == 4000U);

    uint64_t cards = 0U;
    for (uint8_t player = 0U; player < 4U; ++player) {
        for (uint8_t index = 0U; index < 2U; ++index) {
            const uint8_t dealt = state.hole[player][index];
            CHECK(dealt < 52U);
            CHECK((cards & (UINT64_C(1) << dealt)) == 0U);
            cards |= UINT64_C(1) << dealt;
        }
    }

    CHECK(texas_holdem_apply_action(
        &state, 3U, TEXAS_HOLDEM_ACTION_CALL));
    CHECK(texas_holdem_apply_action(
        &state, 0U, TEXAS_HOLDEM_ACTION_CALL));
    CHECK(texas_holdem_apply_action(
        &state, 1U, TEXAS_HOLDEM_ACTION_CALL));
    CHECK(texas_holdem_apply_action(
        &state, 2U, TEXAS_HOLDEM_ACTION_CALL));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_FLOP);
    CHECK(state.community_count == 3U);
    CHECK(state.current_player == 1U);
    CHECK(state.pot == 80U);
    for (uint8_t player = 0U; player < 4U; ++player) {
        CHECK(state.stacks[player] == 980U);
        CHECK(state.round_bet[player] == 0U);
        CHECK(state.contribution[player] == 20U);
    }
    CHECK(chip_total(&state) == 4000U);
    CHECK(texas_holdem_action_legal(
        &state, 1U, TEXAS_HOLDEM_ACTION_RAISE));
    CHECK(texas_holdem_apply_action(
        &state, 1U, TEXAS_HOLDEM_ACTION_RAISE));
    CHECK(state.current_bet == TEXAS_HOLDEM_BIG_BLIND);
    CHECK(state.current_player == 2U);
    CHECK(chip_total(&state) == 4000U);
    return true;
}

static bool test_folds_and_side_pots(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 4U, UINT32_C(0xabcdef01));
    CHECK(texas_holdem_begin_match(&state));
    CHECK(texas_holdem_apply_action(
        &state, 3U, TEXAS_HOLDEM_ACTION_FOLD));
    CHECK(texas_holdem_apply_action(
        &state, 0U, TEXAS_HOLDEM_ACTION_FOLD));
    CHECK(texas_holdem_apply_action(
        &state, 1U, TEXAS_HOLDEM_ACTION_FOLD));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_SHOWDOWN);
    CHECK(state.winner_mask == (UINT8_C(1) << 2U));
    CHECK(state.stacks[2] == 1010U);
    CHECK(chip_total(&state) == 4000U);

    texas_holdem_reset_lobby(&state, 3U, UINT32_C(0x10203040));
    state.phase = TEXAS_HOLDEM_PHASE_RIVER;
    state.active_mask = UINT8_C(0x07);
    state.community_count = 5U;
    state.dealer = 0U;
    const uint8_t board[5] = {
        card(3U, 12U), card(3U, 11U), card(3U, 10U),
        card(0U, 2U), card(1U, 3U),
    };
    memcpy(state.community, board, sizeof(board));
    state.hole[0][0] = card(3U, 14U);
    state.hole[0][1] = card(3U, 13U);
    state.hole[1][0] = card(2U, 14U);
    state.hole[1][1] = card(1U, 14U);
    state.hole[2][0] = card(2U, 2U);
    state.hole[2][1] = card(1U, 2U);
    state.contribution[0] = 50U;
    state.contribution[1] = 100U;
    state.contribution[2] = 100U;
    state.stacks[0] = 950U;
    state.stacks[1] = 900U;
    state.stacks[2] = 900U;
    texas_holdem_resolve_showdown(&state);
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_SHOWDOWN);
    CHECK(state.pot == 250U);
    CHECK(state.stacks[0] == 1100U);
    CHECK(state.stacks[1] == 900U);
    CHECK(state.stacks[2] == 1000U);
    CHECK(state.winner_mask ==
          ((UINT8_C(1) << 0U) | (UINT8_C(1) << 2U)));
    CHECK(chip_total(&state) == 3000U);
    return true;
}

static bool test_layered_side_pots_and_odd_chip(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 4U, UINT32_C(0x51de900d));
    state.phase = TEXAS_HOLDEM_PHASE_RIVER;
    state.active_mask = UINT8_C(0x0f);
    state.folded_mask = UINT8_C(0x08);
    state.community_count = 5U;
    state.dealer = 0U;
    const uint8_t board[5] = {
        card(0U, 2U), card(1U, 3U), card(2U, 7U),
        card(3U, 9U), card(0U, 11U),
    };
    memcpy(state.community, board, sizeof(board));
    state.hole[0][0] = card(1U, 14U);
    state.hole[0][1] = card(2U, 14U);
    state.hole[1][0] = card(1U, 13U);
    state.hole[1][1] = card(2U, 13U);
    state.hole[2][0] = card(1U, 12U);
    state.hole[2][1] = card(2U, 12U);
    state.hole[3][0] = card(1U, 10U);
    state.hole[3][1] = card(2U, 10U);
    state.contribution[0] = 50U;
    state.contribution[1] = 100U;
    state.contribution[2] = 200U;
    state.contribution[3] = 200U;
    state.stacks[0] = 950U;
    state.stacks[1] = 900U;
    state.stacks[2] = 800U;
    state.stacks[3] = 800U;
    texas_holdem_resolve_showdown(&state);
    CHECK(state.stacks[0] == 1150U);
    CHECK(state.stacks[1] == 1050U);
    CHECK(state.stacks[2] == 1000U);
    CHECK(state.stacks[3] == 800U);
    CHECK(state.winner_mask == UINT8_C(0x07));
    CHECK(chip_total(&state) == 4000U);

    texas_holdem_reset_lobby(&state, 3U, UINT32_C(0x0ddc41f0));
    state.phase = TEXAS_HOLDEM_PHASE_RIVER;
    state.active_mask = UINT8_C(0x07);
    state.folded_mask = UINT8_C(0x04);
    state.community_count = 5U;
    state.dealer = 0U;
    const uint8_t tie_board[5] = {
        card(0U, 10U), card(0U, 11U), card(0U, 12U),
        card(0U, 13U), card(0U, 14U),
    };
    memcpy(state.community, tie_board, sizeof(tie_board));
    state.hole[0][0] = card(1U, 2U);
    state.hole[0][1] = card(2U, 3U);
    state.hole[1][0] = card(1U, 4U);
    state.hole[1][1] = card(2U, 5U);
    state.hole[2][0] = card(1U, 6U);
    state.hole[2][1] = card(2U, 7U);
    for (uint8_t player = 0U; player < 3U; ++player) {
        state.contribution[player] = 1U;
        state.stacks[player] = 9U;
    }
    texas_holdem_resolve_showdown(&state);
    CHECK(state.stacks[0] == 10U);
    CHECK(state.stacks[1] == 11U);
    CHECK(state.stacks[2] == 9U);
    CHECK(state.winner_mask == UINT8_C(0x03));
    CHECK(chip_total(&state) == 30U);
    return true;
}

static bool test_short_big_blind_and_clean_match_over(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 3U, UINT32_C(0xb11d5afe));
    CHECK(texas_holdem_begin_match(&state));
    state.phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
    state.stacks[0] = 5U;
    state.stacks[1] = 1000U;
    state.stacks[2] = 1000U;
    memset(state.round_bet, 0, sizeof(state.round_bet));
    memset(state.contribution, 0, sizeof(state.contribution));
    CHECK(texas_holdem_next_hand(&state));
    CHECK(state.dealer == 1U);
    CHECK(state.current_player == 1U);
    CHECK(state.current_bet == TEXAS_HOLDEM_BIG_BLIND);
    CHECK(state.stacks[0] == 0U);
    CHECK(state.stacks[2] == 990U);
    CHECK(state.pot == 15U);
    CHECK(texas_holdem_apply_action(
        &state, 1U, TEXAS_HOLDEM_ACTION_CALL));
    CHECK(state.contribution[1] == TEXAS_HOLDEM_BIG_BLIND);

    texas_holdem_reset_lobby(&state, 2U, UINT32_C(0xc1ea4e0f));
    state.phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
    state.stacks[0] = 2000U;
    state.stacks[1] = 0U;
    state.active_mask = UINT8_C(0x03);
    state.folded_mask = UINT8_C(0x02);
    state.all_in_mask = UINT8_C(0x02);
    state.acted_mask = UINT8_C(0x03);
    state.community_count = 5U;
    memset(state.community, 0, sizeof(state.community));
    CHECK(texas_holdem_next_hand(&state));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_MATCH_OVER);
    CHECK(state.current_player == 0U);
    CHECK(state.active_mask == UINT8_C(0x01));
    CHECK(state.winner_mask == UINT8_C(0x01));
    CHECK(state.folded_mask == 0U);
    CHECK(state.all_in_mask == 0U);
    CHECK(state.acted_mask == 0U);
    CHECK(state.community_count == 0U);
    CHECK(state.pot == 0U);
    CHECK(state.current_bet == 0U);
    for (uint8_t index = 0U; index < TEXAS_HOLDEM_COMMUNITY_CARDS; ++index) {
        CHECK(state.community[index] == TEXAS_HOLDEM_NO_CARD);
    }
    CHECK(state_invariants(&state, 2000U));
    return true;
}

static bool test_bounded_multi_hand_simulation(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 4U, UINT32_C(0x0badcafe));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(texas_holdem_begin_match(&state));
    uint32_t actions = 0U;
    uint32_t hands = 0U;
    while (actions < 12000U && hands < 160U &&
           state.phase != TEXAS_HOLDEM_PHASE_MATCH_OVER) {
        CHECK(chip_total(&state) == 4000U);
        if (state.phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) {
            CHECK(texas_holdem_next_hand(&state));
            ++hands;
            continue;
        }
        CHECK(state.phase >= TEXAS_HOLDEM_PHASE_PREFLOP &&
              state.phase <= TEXAS_HOLDEM_PHASE_RIVER);
        texas_holdem_action_t action =
            texas_holdem_player_is_cpu(&state, state.current_player)
            ? texas_holdem_choose_cpu_action(&state, state.current_player)
            : TEXAS_HOLDEM_ACTION_CALL;
        if (!texas_holdem_player_is_cpu(&state, state.current_player) &&
            actions % 11U == 0U && texas_holdem_action_legal(
                &state, state.current_player, TEXAS_HOLDEM_ACTION_RAISE)) {
            action = TEXAS_HOLDEM_ACTION_RAISE;
        } else if (!texas_holdem_player_is_cpu(
                       &state, state.current_player) &&
                   actions % 17U == 0U) {
            action = TEXAS_HOLDEM_ACTION_FOLD;
        }
        CHECK(texas_holdem_apply_action(
            &state, state.current_player, action));
        ++actions;
    }
    CHECK(actions < 12000U);
    CHECK(chip_total(&state) == 4000U);
    return true;
}

static bool test_seed_matrix_simulation(void)
{
    for (uint32_t seed = 1U; seed <= 32U; ++seed) {
        texas_holdem_state_t state;
        texas_holdem_reset_lobby(
            &state, 4U, seed * UINT32_C(0x9e3779b9));
        for (uint8_t step = 0U; step < (uint8_t)(seed & 3U); ++step) {
            CHECK(texas_holdem_adjust_stack(&state, true));
        }
        for (uint8_t cpu = 0U; cpu < (uint8_t)((seed >> 2U) & 3U);
             ++cpu) {
            CHECK(texas_holdem_adjust_cpu_players(&state, true));
        }
        CHECK(texas_holdem_begin_match(&state));
        const uint32_t expected_chips =
            (uint32_t)state.starting_stack * state.player_count;
        uint32_t actions = 0U;
        uint32_t hands = 0U;
        while (actions < 6000U && hands < 250U &&
               state.phase != TEXAS_HOLDEM_PHASE_MATCH_OVER) {
            CHECK(state_invariants(&state, expected_chips));
            if (state.phase == TEXAS_HOLDEM_PHASE_SHOWDOWN) {
                CHECK(texas_holdem_next_hand(&state));
                ++hands;
                continue;
            }
            texas_holdem_action_t action;
            if (texas_holdem_player_is_cpu(
                    &state, state.current_player)) {
                action = texas_holdem_choose_cpu_action(
                    &state, state.current_player);
            } else if ((actions + seed) % 19U == 0U) {
                action = TEXAS_HOLDEM_ACTION_FOLD;
            } else if ((actions + seed) % 7U == 0U &&
                       texas_holdem_action_legal(
                           &state, state.current_player,
                           TEXAS_HOLDEM_ACTION_RAISE)) {
                action = TEXAS_HOLDEM_ACTION_RAISE;
            } else {
                action = TEXAS_HOLDEM_ACTION_CALL;
            }
            CHECK(texas_holdem_action_legal(
                &state, state.current_player, action));
            CHECK(texas_holdem_apply_action(
                &state, state.current_player, action));
            ++actions;
        }
        CHECK(actions < 6000U);
        CHECK(hands == 250U ||
              state.phase == TEXAS_HOLDEM_PHASE_MATCH_OVER);
        CHECK(state_invariants(&state, expected_chips));
    }
    return true;
}

static bool test_cpu_configuration_and_legal_actions(void)
{
    texas_holdem_state_t state;
    texas_holdem_reset_lobby(&state, 4U, UINT32_C(0xc0ffee11));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(state.player_count == 4U);
    CHECK(state.cpu_mask == UINT8_C(0x08));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(state.cpu_mask == UINT8_C(0x0c));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(state.cpu_mask == UINT8_C(0x0e));
    CHECK(texas_holdem_adjust_cpu_players(&state, true));
    CHECK(state.cpu_mask == 0U);

    CHECK(texas_holdem_adjust_cpu_players(&state, false));
    CHECK(state.cpu_mask == UINT8_C(0x0e));
    CHECK(texas_holdem_begin_match(&state));
    CHECK(state.current_player == 3U);
    CHECK(texas_holdem_player_is_cpu(&state, state.current_player));
    CHECK(!state.pass_required);
    const texas_holdem_action_t action = texas_holdem_choose_cpu_action(
        &state, state.current_player);
    CHECK(texas_holdem_action_legal(&state, state.current_player, action));
    CHECK(texas_holdem_apply_action(&state, state.current_player, action));
    CHECK(chip_total(&state) == 4000U);
    return true;
}

static bool test_local_cpu_lifecycle(void)
{
    audio_mock_t audio = {0};
    const p4_game_services_t services = local_services(&audio);
    p4_game_instance_t instance = {0};
    texas_holdem_state_t state;
    CHECK(p4_game_instance_start(
        &instance, &p4_texas_holdem_game, &services,
        &state, sizeof(state)));
    CHECK(tap(&instance, 254U, 139U));
    CHECK(tap(&instance, 254U, 139U));
    CHECK(state.cpu_mask == UINT8_C(0x0c));
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_PREFLOP);
    CHECK(state.current_player == 3U);
    CHECK(!state.pass_required);
    const uint32_t revision = state.revision;
    for (uint8_t tick = 0U; tick < 5U; ++tick) {
        CHECK(update_empty(&instance, 100U));
    }
    CHECK(update_empty(&instance, 99U));
    CHECK(state.revision == revision);
    CHECK(update_empty(&instance, 1U));
    CHECK(state.revision > revision);
    CHECK(state.current_player == 0U);
    CHECK(state.pass_required);
    CHECK(chip_total(&state) == 4000U);
    p4_game_instance_stop(&instance);
    return true;
}

static bool test_local_lifecycle_render_and_touch(void)
{
    audio_mock_t audio = {0};
    p4_game_services_t services = local_services(&audio);
    services.available_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    /* Retain this explicit legacy renderer fixture until the instance is stopped. */
    p4_game_descriptor_t legacy = p4_texas_holdem_game;
    legacy.required_capabilities &= ~(uint32_t)P4_GAME_CAP_VIDEO_HIGH_RES;
    legacy.optional_capabilities |= P4_GAME_CAP_VIDEO_HIGH_RES;
    p4_game_instance_t instance = {0};
    texas_holdem_state_t state;
    CHECK(!p4_game_instance_start(
        &instance, &p4_texas_holdem_game, &services,
        &state, sizeof(state)));
    CHECK(p4_game_instance_start(
        &instance, &legacy, &services,
        &state, sizeof(state)));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_SETUP);
    CHECK(state.player_count == 4U);
    CHECK(tap(&instance, 254U, 110U));
    CHECK(state.starting_stack == 2000U);
    CHECK(tap(&instance, 160U, 168U));
    CHECK(state.phase == TEXAS_HOLDEM_PHASE_PREFLOP);
    CHECK(state.pass_required);

    p4_game_surface_t surface = {
        .pixels = &s_surface_pixels[0][0],
        .stride_pixels = TEST_SURFACE_STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = 0U; column < TEST_SURFACE_STRIDE; ++column) {
            s_surface_pixels[row][column] = UINT16_C(0x5aa5);
        }
    }
    CHECK(p4_game_instance_render(&instance, &surface));
    CHECK(surface_padding_intact(UINT16_C(0x5aa5)));
    CHECK(update_button(&instance, P4_BUTTON_A));
    CHECK(!state.pass_required);
    CHECK(update_button(&instance, P4_BUTTON_START));
    CHECK(state.current_player == 0U);
    CHECK(state.pass_required);
    CHECK(p4_game_instance_render(&instance, &surface));
    const p4_game_input_t exit_touch = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = 10U, .y = 8U}},
    };
    CHECK(p4_game_instance_update(&instance, &exit_touch, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(audio.stops >= 1U);
    return true;
}

static bool test_touch_targets_do_not_trigger_virtual_gamepad(void)
{
    for (uint16_t raise_x = 240U; raise_x <= 280U; raise_x += 40U) {
        audio_mock_t audio = {0};
        const p4_game_services_t services = local_services(&audio);
        p4_game_instance_t instance = {0};
        texas_holdem_state_t state;
        CHECK(p4_game_instance_start(&instance, &p4_texas_holdem_game,
            &services, &state, sizeof(state)));
        /* These are outside visible Exit/Deal but inside old Back/Start. */
        CHECK(tap(&instance, 48U, 8U));
        CHECK(tap(&instance, 290U, 8U));
        CHECK(state.phase == TEXAS_HOLDEM_PHASE_SETUP);
        CHECK(tap(&instance, 160U, 168U));
        CHECK(state.pass_required);
        CHECK(tap(&instance, 290U, 8U));
        CHECK(state.pass_required);
        CHECK(tap(&instance, 160U, 134U));
        CHECK(!state.pass_required);
        const uint8_t actor = state.current_player;
        /* The Raise button overlaps former B and A regions. Both points
         * must raise, never fold or execute the selected Check/Call action. */
        CHECK(tap(&instance, raise_x, 174U));
        CHECK(state.round_bet[actor] == 40U);
        CHECK(state.current_bet == 40U);
        CHECK((state.folded_mask & (UINT8_C(1) << actor)) == 0U);
        CHECK(state.stacks[actor] == 960U);
        CHECK(state.pass_required);
        CHECK(update_button(&instance, P4_BUTTON_A));
        CHECK(!state.pass_required);
        p4_game_instance_stop(&instance);
    }
    return true;
}

static bool test_two_humans_with_host_cpu_seats(void)
{
    test_link_t link;
    link_init(&link, 2U);
    audio_mock_t audio[2] = {{0}};
    p4_game_services_t services[2];
    p4_game_instance_t instances[2] = {{0}};
    texas_holdem_state_t states[2];
    for (uint8_t player = 0U; player < 2U; ++player) {
        services[player] = network_services(
            &audio[player], &link.endpoints[player]);
        CHECK(p4_game_instance_start(
            &instances[player], &p4_texas_holdem_game, &services[player],
            &states[player], sizeof(states[player])));
        CHECK(states[player].network_player_count == 2U);
        CHECK(states[player].player_count == 2U);
    }
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].network_started);

    CHECK(update_button(&instances[0], P4_BUTTON_DOWN));
    CHECK(update_button(&instances[0], P4_BUTTON_RIGHT));
    CHECK(update_button(&instances[0], P4_BUTTON_RIGHT));
    CHECK(states[0].player_count == 4U);
    CHECK(states[0].cpu_mask == UINT8_C(0x0c));
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].player_count == 4U);
    CHECK(states[1].network_player_count == 2U);
    CHECK(states[1].cpu_mask == UINT8_C(0x0c));

    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[0].current_player == 3U);
    CHECK(states[1].current_player == 3U);
    CHECK(texas_holdem_player_is_cpu(&states[0], 3U));

    const uint32_t revision = states[0].revision;
    for (uint8_t tick = 0U; tick < 6U; ++tick) {
        CHECK(update_empty(&instances[0], 100U));
    }
    CHECK(states[0].revision > revision);
    CHECK(states[0].current_player == 0U);
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].revision == states[0].revision);
    CHECK(states[1].current_player == 0U);
    CHECK(states[1].pot == states[0].pot);
    CHECK(chip_total(&states[0]) == 4000U);

    states[0].phase = TEXAS_HOLDEM_PHASE_SHOWDOWN;
    states[0].stacks[0] = 4000U;
    for (uint8_t player = 1U; player < 4U; ++player) {
        states[0].stacks[player] = 0U;
    }
    memset(states[0].round_bet, 0, sizeof(states[0].round_bet));
    memset(states[0].contribution, 0, sizeof(states[0].contribution));
    states[0].active_mask = UINT8_C(0x0f);
    states[0].folded_mask = UINT8_C(0x0e);
    states[0].all_in_mask = UINT8_C(0x0e);
    states[0].acted_mask = UINT8_C(0x0f);
    states[0].pot = 0U;
    CHECK(texas_holdem_request_next_hand(
        &instances[0].context, &states[0]));
    CHECK(states[0].phase == TEXAS_HOLDEM_PHASE_MATCH_OVER);
    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].phase == TEXAS_HOLDEM_PHASE_MATCH_OVER);
    CHECK(states[1].winner_mask == UINT8_C(0x01));
    CHECK(states[1].folded_mask == 0U);
    CHECK(states[1].all_in_mask == 0U);
    CHECK(states[1].acted_mask == 0U);
    CHECK(states[1].revision == states[0].revision);

    p4_game_instance_stop(&instances[0]);
    p4_game_instance_stop(&instances[1]);
    return true;
}

static bool test_network_rejects_and_recovers_from_bad_snapshots(void)
{
    test_link_t link;
    link_init(&link, 2U);
    audio_mock_t audio[2] = {{0}};
    p4_game_services_t services[2];
    p4_game_instance_t instances[2] = {{0}};
    texas_holdem_state_t states[2];
    for (uint8_t player = 0U; player < 2U; ++player) {
        services[player] = network_services(
            &audio[player], &link.endpoints[player]);
        CHECK(p4_game_instance_start(
            &instances[player], &p4_texas_holdem_game, &services[player],
            &states[player], sizeof(states[player])));
    }

    CHECK(update_empty(&instances[0], 16U));
    CHECK(link.inbox[1].write_index == 1U);
    link.inbox[1].messages[0].data[TEST_NET_CPU_MASK_OFFSET] =
        UINT8_C(0x04);
    CHECK(update_empty(&instances[1], 16U));
    CHECK(!states[1].network_started);
    CHECK(states[1].phase == TEXAS_HOLDEM_PHASE_SETUP);

    CHECK(update_empty(&instances[0], 16U));
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].network_started);
    CHECK(states[1].revision == states[0].revision);

    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(update_empty(&instances[0], 16U));
    CHECK(link.inbox[1].read_index < link.inbox[1].write_index);
    p4_game_multiplayer_message_t *const bad_cards =
        &link.inbox[1].messages[link.inbox[1].read_index];
    CHECK(bad_cards->bytes == 63U);
    bad_cards->data[TEST_NET_HOLE_OFFSET + 1U] =
        bad_cards->data[TEST_NET_HOLE_OFFSET];
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].phase == TEXAS_HOLDEM_PHASE_SETUP);

    for (uint8_t tick = 0U; tick < 10U; ++tick) {
        CHECK(update_empty(&instances[0], 100U));
    }
    CHECK(update_empty(&instances[1], 16U));
    CHECK(states[1].phase == TEXAS_HOLDEM_PHASE_PREFLOP);
    CHECK(states[1].revision == states[0].revision);
    CHECK(memcmp(states[1].hole, states[0].hole,
                 sizeof(states[0].hole)) == 0);

    p4_game_instance_stop(&instances[0]);
    p4_game_instance_stop(&instances[1]);
    return true;
}

static bool test_four_player_host_authority_and_peer_loss(void)
{
    test_link_t link;
    link_init(&link, 4U);
    audio_mock_t audio[TEXAS_HOLDEM_PLAYERS] = {{0}};
    p4_game_services_t services[TEXAS_HOLDEM_PLAYERS];
    p4_game_instance_t instances[TEXAS_HOLDEM_PLAYERS] = {{0}};
    texas_holdem_state_t states[TEXAS_HOLDEM_PLAYERS];
    for (uint8_t player = 0U; player < 4U; ++player) {
        services[player] = network_services(
            &audio[player], &link.endpoints[player]);
        CHECK(p4_game_instance_start(
            &instances[player], &p4_texas_holdem_game, &services[player],
            &states[player], sizeof(states[player])));
        CHECK(states[player].network_mode);
        CHECK(states[player].player_count == 4U);
    }
    CHECK(states[0].network_started);
    CHECK(!states[1].network_started);
    CHECK(update_empty(&instances[0], 16U));
    CHECK(link.last_send_bytes == 63U);
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].network_started);
        CHECK(states[player].starting_stack == 1000U);
    }

    CHECK(update_button(&instances[0], P4_BUTTON_RIGHT));
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].starting_stack == 2000U);
        for (uint8_t seat = 0U; seat < 4U; ++seat) {
            CHECK(states[player].stacks[seat] == 2000U);
        }
    }

    CHECK(update_button(&instances[0], P4_BUTTON_A));
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].phase == TEXAS_HOLDEM_PHASE_PREFLOP);
        CHECK(states[player].current_player == 3U);
        CHECK(memcmp(states[player].hole, states[0].hole,
                     sizeof(states[0].hole)) == 0);
    }

    CHECK(update_button(&instances[3], P4_BUTTON_A));
    CHECK(states[3].network_request_pending);
    CHECK(states[3].current_player == 3U);
    CHECK(update_empty(&instances[0], 16U));
    for (uint8_t player = 1U; player < 4U; ++player) {
        CHECK(update_empty(&instances[player], 16U));
        CHECK(states[player].current_player == 0U);
        CHECK(states[player].revision == states[0].revision);
        CHECK(states[player].pot == states[0].pot);
    }

    const uint32_t total_before_loss = chip_total(&states[2]);
    link.state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(update_empty(&instances[2], 16U));
    CHECK(!states[2].network_mode);
    CHECK(states[2].peer_lost_fallback);
    CHECK(states[2].phase == TEXAS_HOLDEM_PHASE_PREFLOP);
    CHECK(chip_total(&states[2]) == total_before_loss);

    for (uint8_t player = 0U; player < 4U; ++player) {
        p4_game_instance_stop(&instances[player]);
    }
    return true;
}

int main(void)
{
    if (!p4_game_descriptor_valid(&p4_texas_holdem_game) ||
        p4_texas_holdem_game.launcher_id != 117U ||
        !test_hand_ranking() ||
        !test_exhaustive_five_card_categories() ||
        !test_equal_stacks_blinds_and_betting() ||
        !test_folds_and_side_pots() ||
        !test_layered_side_pots_and_odd_chip() ||
        !test_short_big_blind_and_clean_match_over() ||
        !test_bounded_multi_hand_simulation() ||
        !test_seed_matrix_simulation() ||
        !test_cpu_configuration_and_legal_actions() ||
        !test_local_cpu_lifecycle() ||
        !test_local_lifecycle_render_and_touch() ||
        !test_touch_targets_do_not_trigger_virtual_gamepad() ||
        !test_two_humans_with_host_cpu_seats() ||
        !test_network_rejects_and_recovers_from_bad_snapshots() ||
        !test_four_player_host_authority_and_peer_loss()) {
        return EXIT_FAILURE;
    }
    puts("Texas Hold'em tests passed");
    return EXIT_SUCCESS;
}
