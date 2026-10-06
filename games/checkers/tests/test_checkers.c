// SPDX-License-Identifier: MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "checkers_internal.h"
#include "p4/game.h"

extern const p4_game_descriptor_t p4_checkers_game;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                \
                    __FILE__, __LINE__, #condition);                         \
            return false;                                                    \
        }                                                                    \
    } while (0)

enum {
    TEST_QUEUE_CAPACITY = 32,
    TEST_SURFACE_STRIDE = P4_GAME_SURFACE_WIDTH + 3,
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
    uint32_t next_sequence[CHECKERS_PLAYER_COUNT];
    message_queue_t inbox[CHECKERS_PLAYER_COUNT];
    test_endpoint_t endpoint[CHECKERS_PLAYER_COUNT];
};

static uint16_t s_surface_pixels[P4_GAME_SURFACE_HEIGHT]
                                [TEST_SURFACE_STRIDE];

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
        .session_seed = UINT64_C(0x434845434b455253),
        .state = endpoint->link->state,
        .role = endpoint->slot == 0U ? P4_GAME_MULTIPLAYER_ROLE_HOST
                                    : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = endpoint->slot,
        .player_count = CHECKERS_PLAYER_COUNT,
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
    const uint8_t destination = (uint8_t)(endpoint->slot ^ 1U);
    message_queue_t *const queue = &endpoint->link->inbox[destination];
    if (queue->write_index >= TEST_QUEUE_CAPACITY) {
        return false;
    }
    p4_game_multiplayer_message_t *const message =
        &queue->messages[queue->write_index++];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++endpoint->link->next_sequence[endpoint->slot],
        .player_slot = endpoint->slot,
        .bytes = (uint8_t)data_bytes,
    };
    memcpy(message->data, data, data_bytes);
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

static void write_test_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8U);
    bytes[2] = (uint8_t)(value >> 16U);
    bytes[3] = (uint8_t)(value >> 24U);
}

static const p4_game_multiplayer_profile_t s_profile = {
    .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
    .style = P4_GAME_MULTIPLAYER_STYLE_TURN_BASED,
    .min_players = CHECKERS_PLAYER_COUNT,
    .max_players = CHECKERS_PLAYER_COUNT,
    .tick_rate_hz = 10U,
    .input_delay_ticks = 0U,
    .message_bytes = 38U,
    .protocol = CHECKERS_NETWORK_PROTOCOL,
    .flags = 0U,
};

static void link_init(test_link_t *link)
{
    memset(link, 0, sizeof(*link));
    link->state = P4_GAME_MULTIPLAYER_CONNECTED;
    for (uint8_t slot = 0U; slot < CHECKERS_PLAYER_COUNT; ++slot) {
        link->endpoint[slot].link = link;
        link->endpoint[slot].slot = slot;
    }
}

static p4_game_services_t local_services(audio_mock_t *audio)
{
    return (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO |
            P4_GAME_CAP_CONTROLS | P4_GAME_CAP_AUDIO_TONE,
        .audio_context = audio,
        .game_id = "org.p4console.checkers",
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

static void empty_position(checkers_state_t *state, uint8_t player)
{
    memset(state, 0, sizeof(*state));
    state->current_player = player;
    state->winner = CHECKERS_NO_WINNER;
    state->selected = CHECKERS_NO_SQUARE;
    state->forced_piece = CHECKERS_NO_SQUARE;
    state->phase = CHECKERS_PHASE_PLAYING;
    state->revision = 1U;
}

static bool test_initial_position_and_simple_move(void)
{
    checkers_state_t state = {0};
    checkers_reset_board(&state, 1U);
    CHECK(state.red_count == 12U);
    CHECK(state.white_count == 12U);
    CHECK(state.current_player == CHECKERS_PLAYER_RED);
    CHECK(state.phase == CHECKERS_PHASE_PLAYING);
    CHECK(state.cursor == 17U);
    for (uint8_t square = 0U; square < CHECKERS_BOARD_SQUARES; ++square) {
        const int row = (int)square / CHECKERS_BOARD_SIDE;
        const int column = (int)square % CHECKERS_BOARD_SIDE;
        if (state.board[square] != CHECKERS_EMPTY) {
            CHECK(((row + column) & 1) != 0);
        }
    }
    CHECK(checkers_move_is_legal(&state, 17U, 24U));
    uint8_t flags = UINT8_MAX;
    CHECK(checkers_apply_move(&state, 17U, 24U, &flags));
    CHECK(flags == 0U);
    CHECK(state.board[17] == CHECKERS_EMPTY);
    CHECK(state.board[24] == CHECKERS_RED_MAN);
    CHECK(state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(state.quiet_ply == 1U);
    CHECK(state.revision == 2U);
    return true;
}

static bool test_compulsory_capture_and_multi_jump(void)
{
    checkers_state_t state;
    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[17] = CHECKERS_RED_MAN;
    state.board[26] = CHECKERS_WHITE_MAN;
    state.board[44] = CHECKERS_WHITE_MAN;
    state.board[62] = CHECKERS_WHITE_MAN;
    state.red_count = 1U;
    state.white_count = 3U;
    CHECK(checkers_player_has_capture(&state, CHECKERS_PLAYER_RED));
    CHECK(!checkers_move_is_legal(&state, 17U, 24U));
    CHECK(checkers_move_is_legal(&state, 17U, 35U));
    uint8_t flags = 0U;
    CHECK(checkers_apply_move(&state, 17U, 35U, &flags));
    CHECK((flags & CHECKERS_MOVE_CAPTURED) != 0U);
    CHECK((flags & CHECKERS_MOVE_CONTINUES) != 0U);
    CHECK(state.current_player == CHECKERS_PLAYER_RED);
    CHECK(state.forced_piece == 35U);
    CHECK(state.selected == 35U);
    CHECK(!checkers_move_is_legal(&state, 35U, 42U));
    CHECK(checkers_apply_move(&state, 35U, 53U, &flags));
    CHECK((flags & CHECKERS_MOVE_CAPTURED) != 0U);
    CHECK((flags & CHECKERS_MOVE_CONTINUES) == 0U);
    CHECK(state.white_count == 1U);
    CHECK(state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(state.forced_piece == CHECKERS_NO_SQUARE);
    return true;
}

static bool test_crowning_king_and_game_endings(void)
{
    checkers_state_t state;
    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[49] = CHECKERS_RED_MAN;
    state.board[62] = CHECKERS_WHITE_MAN;
    state.red_count = 1U;
    state.white_count = 1U;
    uint8_t flags = 0U;
    CHECK(checkers_apply_move(&state, 49U, 56U, &flags));
    CHECK((flags & CHECKERS_MOVE_CROWNED) != 0U);
    CHECK(state.board[56] == CHECKERS_RED_KING);
    CHECK(state.current_player == CHECKERS_PLAYER_WHITE);

    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[26] = CHECKERS_RED_KING;
    state.board[62] = CHECKERS_WHITE_MAN;
    state.red_count = 1U;
    state.white_count = 1U;
    CHECK(checkers_move_is_legal(&state, 26U, 17U));
    CHECK(checkers_apply_move(&state, 26U, 17U, &flags));

    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[49] = CHECKERS_RED_MAN;
    state.board[1] = CHECKERS_WHITE_MAN;
    state.red_count = 1U;
    state.white_count = 1U;
    CHECK(checkers_apply_move(&state, 49U, 56U, &flags));
    CHECK(state.phase == CHECKERS_PHASE_GAME_OVER);
    CHECK(state.winner == CHECKERS_PLAYER_RED);
    CHECK((flags & CHECKERS_MOVE_GAME_OVER) != 0U);

    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[17] = CHECKERS_RED_KING;
    state.board[46] = CHECKERS_WHITE_KING;
    state.red_count = 1U;
    state.white_count = 1U;
    state.quiet_ply = CHECKERS_DRAW_QUIET_PLY - 1U;
    CHECK(checkers_apply_move(&state, 17U, 24U, &flags));
    CHECK(state.phase == CHECKERS_PHASE_GAME_OVER);
    CHECK(state.winner == CHECKERS_WINNER_DRAW);
    return true;
}

static bool test_descriptor_lifecycle_controller_touch_and_render(void)
{
    CHECK(p4_game_descriptor_valid(&p4_checkers_game));
    CHECK(p4_checkers_game.state_bytes == sizeof(checkers_state_t));
    audio_mock_t audio = {0};
    const p4_game_services_t services = local_services(&audio);
    checkers_state_t state;
    p4_game_instance_t instance = {0};
    CHECK(p4_game_instance_start(&instance, &p4_checkers_game, &services,
                                 &state, sizeof(state)));
    CHECK(!state.network_mode);
    CHECK(audio.tones == 1U);

    p4_game_input_t input = {.pressed = P4_BUTTON_A};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.selected == 17U);
    input = (p4_game_input_t){
        .pressed = P4_BUTTON_LEFT | P4_BUTTON_DOWN,
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.cursor == 24U);
    input = (p4_game_input_t){.pressed = P4_BUTTON_A};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.current_player == CHECKERS_PLAYER_WHITE);

    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = 0U; column < TEST_SURFACE_STRIDE; ++column) {
            s_surface_pixels[row][column] = UINT16_C(0x55aa);
        }
    }
    p4_game_surface_t surface = {
        .pixels = &s_surface_pixels[0][0],
        .stride_pixels = TEST_SURFACE_STRIDE,
        .width = P4_GAME_SURFACE_WIDTH,
        .height = P4_GAME_SURFACE_HEIGHT,
    };
    CHECK(p4_game_instance_render(&instance, &surface));
    CHECK(s_surface_pixels[0][0] != UINT16_C(0x55aa));
    for (size_t row = 0U; row < P4_GAME_SURFACE_HEIGHT; ++row) {
        for (size_t column = P4_GAME_SURFACE_WIDTH;
             column < TEST_SURFACE_STRIDE; ++column) {
            CHECK(s_surface_pixels[row][column] == UINT16_C(0x55aa));
        }
    }

    state.phase = CHECKERS_PHASE_GAME_OVER;
    state.winner = CHECKERS_PLAYER_RED;
    state.revision = 10U;
    input = (p4_game_input_t){.pressed = P4_BUTTON_START};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.phase == CHECKERS_PHASE_PLAYING);
    CHECK(state.red_count == 12U && state.white_count == 12U);
    CHECK(state.revision == 11U);

    input = (p4_game_input_t){.pressed = P4_BUTTON_BACK};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
    CHECK(audio.stops == 2U);

    audio = (audio_mock_t){0};
    instance = (p4_game_instance_t){0};
    CHECK(p4_game_instance_start(&instance, &p4_checkers_game, &services,
                                 &state, sizeof(state)));
    input = (p4_game_input_t){
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{36U, 78U}},
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.selected == 17U);
    input = (p4_game_input_t){0};
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    input = (p4_game_input_t){
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{16U, 98U}},
    };
    CHECK(p4_game_instance_update(&instance, &input, 16U) ==
          P4_GAME_CONTINUE);
    CHECK(state.board[24] == CHECKERS_RED_MAN);
    p4_game_instance_stop(&instance);
    return true;
}

static bool test_network_host_authority_and_peer_loss(void)
{
    test_link_t link;
    link_init(&link);
    audio_mock_t host_audio = {0};
    audio_mock_t client_audio = {0};
    const p4_game_services_t host_services = network_services(
        &host_audio, &link.endpoint[0]);
    const p4_game_services_t client_services = network_services(
        &client_audio, &link.endpoint[1]);
    checkers_state_t host_state;
    checkers_state_t client_state;
    p4_game_instance_t host = {0};
    p4_game_instance_t client = {0};
    CHECK(p4_game_instance_start(&host, &p4_checkers_game, &host_services,
                                 &host_state, sizeof(host_state)));
    CHECK(p4_game_instance_start(&client, &p4_checkers_game,
                                 &client_services,
                                 &client_state, sizeof(client_state)));
    CHECK(host_state.network_mode && host_state.network_started);
    CHECK(client_state.network_mode && !client_state.network_started);
    CHECK(!checkers_local_turn(&client_state));

    p4_game_input_t input = {0};
    CHECK(p4_game_instance_update(&host, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.network_started);
    CHECK(memcmp(host_state.board, client_state.board,
                 sizeof(host_state.board)) == 0);

    uint8_t flags = 0U;
    CHECK(checkers_perform_move(&host.context, &host_state,
                                17U, 24U, &flags));
    CHECK(host_state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(p4_game_instance_update(&host, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(client_state.revision == host_state.revision);

    CHECK(checkers_perform_move(&client.context, &client_state,
                                40U, 33U, &flags));
    CHECK(client_state.network_request_pending);
    CHECK(client_state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(p4_game_instance_update(&host, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(host_state.current_player == CHECKERS_PLAYER_RED);
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(!client_state.network_request_pending);
    CHECK(client_state.current_player == CHECKERS_PLAYER_RED);
    CHECK(memcmp(host_state.board, client_state.board,
                 sizeof(host_state.board)) == 0);

    const uint32_t synchronized_revision = host_state.revision;
    uint8_t stale_request[8] = {
        CHECKERS_NETWORK_PROTOCOL, CHECKERS_NET_MOVE,
        0U, 0U, 0U, 0U, 42U, 35U,
    };
    write_test_u32(stale_request + 2U, synchronized_revision - 1U);
    CHECK(send_message(&link.endpoint[1], stale_request,
                       sizeof(stale_request)));
    CHECK(p4_game_instance_update(&host, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(host_state.revision == synchronized_revision);
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);

    uint8_t malformed_snapshot[38] = {0};
    malformed_snapshot[0] = CHECKERS_NETWORK_PROTOCOL;
    malformed_snapshot[1] = 16U;
    write_test_u32(malformed_snapshot + 2U, synchronized_revision + 10U);
    CHECK(send_message(&link.endpoint[0], malformed_snapshot,
                       sizeof(malformed_snapshot)));
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.revision == synchronized_revision);

    const uint32_t revision_before_loss = client_state.revision;
    link.state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(p4_game_instance_update(&client, &input, 16U) == P4_GAME_CONTINUE);
    CHECK(!client_state.network_mode);
    CHECK(client_state.peer_lost_fallback);
    CHECK(client_state.revision == revision_before_loss);
    CHECK(checkers_local_turn(&client_state));

    p4_game_instance_stop(&host);
    p4_game_instance_stop(&client);
    return true;
}

static bool touch_step(p4_game_instance_t *instance, bool down,
                        uint16_t x, uint16_t y, uint32_t buttons)
{
    const p4_game_input_t input = {
        .pressed = buttons, .held = buttons, .touch_valid = true,
        .touch_count = down ? 1U : 0U, .touches = {{x, y}},
    };
    return p4_game_instance_update(instance, &input, 16U) == P4_GAME_CONTINUE;
}

static bool test_direct_touch_drag_and_capture(void)
{
    audio_mock_t audio = {0};
    const p4_game_services_t services = local_services(&audio);
    checkers_state_t state;
    p4_game_instance_t instance = {0};
    CHECK(p4_game_instance_start(&instance, &p4_checkers_game, &services,
                                 &state, sizeof(state)));
    const uint32_t synthetic = P4_BUTTON_RIGHT | P4_BUTTON_A |
        P4_BUTTON_B | P4_BUTTON_BACK | P4_BUTTON_START;
    CHECK(touch_step(&instance, true, 36U, 78U, synthetic));
    CHECK(state.selected == 17U && state.cursor == 17U);
    CHECK(touch_step(&instance, true, 16U, 98U, synthetic));
    CHECK(state.touch_dragging && state.board[17] == CHECKERS_RED_MAN);
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.board[17] == CHECKERS_EMPTY && state.board[24] == CHECKERS_RED_MAN);
    CHECK(state.revision == 2U && state.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(!state.touch_dragging);
    CHECK(touch_step(&instance, false, 0U, 0U, P4_BUTTON_A));
    CHECK(state.selected == state.cursor); /* Physical controller resumes. */

    checkers_reset_board(&state, 10U);
    CHECK(touch_step(&instance, true, 36U, 78U, synthetic));
    CHECK(touch_step(&instance, true, 200U, 155U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.revision == 10U && state.selected == 17U);
    CHECK(state.board[17] == CHECKERS_RED_MAN); /* Outside drop snaps back. */
    CHECK(touch_step(&instance, true, 36U, 78U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.selected == CHECKERS_NO_SQUARE); /* Tap selected piece cancels. */

    empty_position(&state, CHECKERS_PLAYER_RED);
    state.board[17] = CHECKERS_RED_MAN;
    state.board[26] = CHECKERS_WHITE_MAN;
    state.board[44] = CHECKERS_WHITE_MAN;
    state.board[62] = CHECKERS_WHITE_MAN;
    state.red_count = 1U; state.white_count = 3U;
    CHECK(touch_step(&instance, true, 36U, 78U, synthetic));
    CHECK(touch_step(&instance, true, 16U, 98U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.revision == 1U); /* Compulsory capture rejects a simple step. */
    CHECK(touch_step(&instance, true, 36U, 78U, synthetic));
    CHECK(touch_step(&instance, true, 76U, 118U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.forced_piece == 35U && state.selected == 35U);
    CHECK(state.white_count == 2U && state.current_player == CHECKERS_PLAYER_RED);
    CHECK(touch_step(&instance, true, 76U, 118U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.selected == 35U); /* A required continued jump cannot cancel. */
    CHECK(touch_step(&instance, true, 76U, 118U, synthetic));
    CHECK(touch_step(&instance, true, 116U, 158U, synthetic));
    CHECK(touch_step(&instance, false, 0U, 0U, synthetic));
    CHECK(state.board[53] == CHECKERS_RED_MAN && state.white_count == 1U);
    CHECK(state.forced_piece == CHECKERS_NO_SQUARE);
    CHECK(state.current_player == CHECKERS_PLAYER_WHITE);
    p4_game_instance_stop(&instance);
    return true;
}

static bool test_network_flipped_drag(void)
{
    test_link_t link; link_init(&link);
    audio_mock_t host_audio = {0}, client_audio = {0};
    const p4_game_services_t hs = network_services(&host_audio, &link.endpoint[0]);
    const p4_game_services_t cs = network_services(&client_audio, &link.endpoint[1]);
    checkers_state_t h, c; p4_game_instance_t host = {0}, client = {0};
    CHECK(p4_game_instance_start(&host, &p4_checkers_game, &hs, &h, sizeof(h)));
    CHECK(p4_game_instance_start(&client, &p4_checkers_game, &cs, &c, sizeof(c)));
    CHECK(touch_step(&host, false, 0U, 0U, 0U));
    CHECK(touch_step(&client, false, 0U, 0U, 0U));
    CHECK(touch_step(&host, true, 36U, 78U, P4_BUTTON_A));
    CHECK(touch_step(&host, true, 16U, 98U, P4_BUTTON_A));
    CHECK(touch_step(&host, false, 0U, 0U, P4_BUTTON_A));
    CHECK(touch_step(&host, false, 0U, 0U, 0U)); /* Publish the move snapshot. */
    CHECK(touch_step(&client, false, 0U, 0U, 0U));
    CHECK(c.current_player == CHECKERS_PLAYER_WHITE);
    CHECK(touch_step(&client, true, 156U, 78U, P4_BUTTON_LEFT));
    CHECK(c.selected == 40U); /* White sees the board rotated 180 degrees. */
    CHECK(touch_step(&client, true, 136U, 98U, P4_BUTTON_A));
    CHECK(touch_step(&client, false, 0U, 0U, P4_BUTTON_A));
    CHECK(c.network_request_pending);
    CHECK(touch_step(&host, false, 0U, 0U, 0U));
    CHECK(touch_step(&client, false, 0U, 0U, 0U));
    CHECK(h.board[33] == CHECKERS_WHITE_MAN && h.board[40] == CHECKERS_EMPTY);
    CHECK(memcmp(h.board, c.board, sizeof(h.board)) == 0);
    CHECK(h.revision == 3U && c.revision == h.revision);
    p4_game_instance_stop(&host); p4_game_instance_stop(&client);
    return true;
}

int main(void)
{
    if (!test_direct_touch_drag_and_capture() ||
        !test_network_flipped_drag() ||
        !test_initial_position_and_simple_move() ||
        !test_compulsory_capture_and_multi_jump() ||
        !test_crowning_king_and_game_endings() ||
        !test_descriptor_lifecycle_controller_touch_and_render() ||
        !test_network_host_authority_and_peer_loss()) {
        return 1;
    }
    puts("checkers tests passed");
    return 0;
}
