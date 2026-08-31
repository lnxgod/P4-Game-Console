// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p4/game.h"
#include "p4_air_hockey_internal.h"

extern const p4_game_descriptor_t p4_p4_air_hockey_game;

enum {
    LINK_QUEUE = 64,
    GUARD_WORDS = 19,
    STRIDE = P4_GAME_SURFACE_WIDTH + 7,
    FRAME_WORDS = STRIDE * P4_GAME_SURFACE_HEIGHT,
    TOTAL_WORDS = GUARD_WORDS + FRAME_WORDS + GUARD_WORDS,
};

static int s_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

typedef struct test_link test_link_t;

typedef struct {
    test_link_t *link;
    uint8_t slot;
    uint32_t next_sequence;
    p4_game_multiplayer_message_t queue[LINK_QUEUE];
    size_t head;
    size_t count;
    uint32_t tone_count;
    p4_game_multiplayer_state_t state;
} endpoint_t;

struct test_link {
    endpoint_t endpoint[P4_AIR_HOCKEY_PLAYERS];
};

static bool read_status(void *context,
                        p4_game_multiplayer_status_t *status_out)
{
    endpoint_t *const endpoint = context;
    *status_out = (p4_game_multiplayer_status_t){
        .generation = 1U,
        .session_seed = UINT64_C(0x1020304050607080),
        .state = endpoint->state,
        .role = endpoint->slot == 0U ? P4_GAME_MULTIPLAYER_ROLE_HOST
                                    : P4_GAME_MULTIPLAYER_ROLE_CLIENT,
        .local_player_slot = endpoint->slot,
        .player_count = P4_AIR_HOCKEY_PLAYERS,
    };
    return true;
}

static bool send_message(void *context, const uint8_t *data,
                         size_t data_bytes)
{
    endpoint_t *const source = context;
    endpoint_t *const destination =
        &source->link->endpoint[source->slot ^ 1U];
    if (data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        destination->count >= LINK_QUEUE) {
        return false;
    }
    const size_t tail = (destination->head + destination->count) % LINK_QUEUE;
    p4_game_multiplayer_message_t *const message = &destination->queue[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++source->next_sequence,
        .player_slot = source->slot,
        .bytes = (uint8_t)data_bytes,
    };
    memcpy(message->data, data, data_bytes);
    ++destination->count;
    return true;
}

static bool receive_message(void *context,
                            p4_game_multiplayer_message_t *message_out)
{
    endpoint_t *const endpoint = context;
    if (endpoint->count == 0U) {
        return false;
    }
    *message_out = endpoint->queue[endpoint->head];
    endpoint->head = (endpoint->head + 1U) % LINK_QUEUE;
    --endpoint->count;
    return true;
}

static bool play_tone(void *context, const p4_tone_t *tone)
{
    endpoint_t *const endpoint = context;
    if (endpoint == NULL || tone == NULL || tone->frequency_hz < 40U ||
        tone->frequency_hz > 4000U || tone->duration_ms == 0U ||
        tone->duration_ms > 5000U || tone->volume_step == 0U ||
        tone->volume_step > 10U) {
        return false;
    }
    ++endpoint->tone_count;
    return true;
}

static void init_link(test_link_t *link)
{
    memset(link, 0, sizeof(*link));
    for (uint8_t slot = 0U; slot < P4_AIR_HOCKEY_PLAYERS; ++slot) {
        link->endpoint[slot].link = link;
        link->endpoint[slot].slot = slot;
        link->endpoint[slot].state = P4_GAME_MULTIPLAYER_CONNECTED;
    }
}

static p4_game_services_t network_services(endpoint_t *endpoint)
{
    static const p4_game_multiplayer_profile_t profile = {
        .schema = P4_GAME_MULTIPLAYER_PROFILE_SCHEMA,
        .style = P4_GAME_MULTIPLAYER_STYLE_REALTIME,
        .min_players = 2U,
        .max_players = 2U,
        .tick_rate_hz = 30U,
        .message_bytes = P4_AIR_HOCKEY_SNAPSHOT_BYTES,
        .protocol = P4_AIR_HOCKEY_PROTOCOL,
    };
    return (p4_game_services_t){
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS |
            P4_GAME_CAP_AUDIO_TONE | P4_GAME_CAP_MULTIPLAYER_SESSION,
        .audio_context = endpoint,
        .play_tone = play_tone,
        .multiplayer_context = endpoint,
        .multiplayer_read_status = read_status,
        .multiplayer_send = send_message,
        .multiplayer_receive = receive_message,
        .multiplayer_profile = &profile,
    };
}

static p4_game_result_t update(p4_game_instance_t *instance, uint32_t held,
                               uint32_t pressed, uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .held = held,
        .pressed = pressed,
        .touch_valid = true,
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
}

static p4_game_result_t update_touch(p4_game_instance_t *instance,
                                     uint16_t x, uint16_t y,
                                     uint32_t elapsed_ms)
{
    const p4_game_input_t input = {
        .touch_valid = true,
        .touch_count = 1U,
        .touches = {{.x = x, .y = y}},
    };
    return p4_game_instance_update(instance, &input, elapsed_ms);
}

static void test_physics_and_scoring(void)
{
    p4_air_hockey_state_t state = {0};
    p4_air_hockey_reset_match(&state, 1U);
    CHECK(state.phase == P4_AIR_HOCKEY_SERVE);
    CHECK(state.paddle_x[0] < state.paddle_x[1]);
    CHECK((p4_air_hockey_step(&state, 704U, true) &
           P4_AIR_HOCKEY_EVENT_SERVE) != 0U);
    CHECK(state.phase == P4_AIR_HOCKEY_PLAY);
    const int32_t before = state.paddle_x[0];
    p4_air_hockey_set_touch_target(&state, 0U, true, 120U, 100U);
    (void)p4_air_hockey_step(&state, 32U, true);
    CHECK(state.paddle_x[0] > before);

    state.phase = P4_AIR_HOCKEY_PLAY;
    state.puck_x = 301 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_y = 100 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vx = 2 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vy = 0;
    const uint32_t event = p4_air_hockey_step(&state, 16U, false);
    CHECK((event & P4_AIR_HOCKEY_EVENT_GOAL) != 0U);
    CHECK(state.score[0] == 1U);
    CHECK(state.phase == P4_AIR_HOCKEY_GOAL);

    state.score[0] = P4_AIR_HOCKEY_WIN_SCORE - 1U;
    state.phase = P4_AIR_HOCKEY_PLAY;
    state.puck_x = 301 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_y = 100 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vx = 2 << P4_AIR_HOCKEY_FIXED_SHIFT;
    state.puck_vy = 0;
    CHECK((p4_air_hockey_step(&state, 16U, false) &
           P4_AIR_HOCKEY_EVENT_WIN) != 0U);
    CHECK(state.phase == P4_AIR_HOCKEY_GAME_OVER);
    CHECK(state.winner == 0U);
}

static void test_offline_lifecycle_and_render_bounds(void)
{
    const p4_game_services_t services = {
        .available_capabilities = P4_GAME_CAP_VIDEO | P4_GAME_CAP_CONTROLS,
    };
    p4_game_instance_t instance = {0};
    p4_air_hockey_state_t state;
    CHECK(p4_game_instance_start(&instance, &p4_p4_air_hockey_game,
                                 &services, &state, sizeof(state)));
    CHECK(state.mode == P4_AIR_HOCKEY_OFFLINE);
    const int32_t before = state.paddle_x[0];
    CHECK(update(&instance, P4_BUTTON_RIGHT, P4_BUTTON_RIGHT, 32U) ==
          P4_GAME_CONTINUE);
    CHECK(state.paddle_x[0] == before);
    CHECK(update_touch(&instance, 120U, 100U, 32U) ==
          P4_GAME_CONTINUE);
    CHECK(state.paddle_x[0] > before);

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
            for (size_t column = P4_GAME_SURFACE_WIDTH; column < STRIDE;
                 ++column) {
                CHECK(surface.pixels[row * STRIDE + column] ==
                      UINT16_C(0x5aa5));
            }
        }
        free(allocation);
    }
    CHECK(update_touch(&instance, 10U, 10U, 16U) ==
          P4_GAME_EXIT_TO_LAUNCHER);
    p4_game_instance_stop(&instance);
}

static void test_two_console_host_authority_and_peer_loss(void)
{
    test_link_t link;
    init_link(&link);
    p4_game_services_t host_services = network_services(&link.endpoint[0]);
    p4_game_services_t client_services = network_services(&link.endpoint[1]);
    p4_game_instance_t host = {0};
    p4_game_instance_t client = {0};
    p4_air_hockey_state_t host_state;
    p4_air_hockey_state_t client_state;
    CHECK(p4_game_instance_start(&host, &p4_p4_air_hockey_game,
                                 &host_services, &host_state,
                                 sizeof(host_state)));
    CHECK(p4_game_instance_start(&client, &p4_p4_air_hockey_game,
                                 &client_services, &client_state,
                                 sizeof(client_state)));
    CHECK(host_state.network_role == P4_GAME_MULTIPLAYER_ROLE_HOST);
    CHECK(client_state.phase == P4_AIR_HOCKEY_NETWORK_WAIT);

    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.snapshot_received);
    CHECK(client_state.snapshot_revision == host_state.snapshot_revision);

    const uint32_t client_tones_before = link.endpoint[1].tone_count;
    host_state.network_audio_events = P4_AIR_HOCKEY_EVENT_HIT;
    p4_air_hockey_network_publish(&host.context, &host_state, true);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(link.endpoint[1].tone_count == client_tones_before + 2U);
    CHECK(client_state.network_audio_events == P4_AIR_HOCKEY_EVENT_NONE);

    const int32_t client_world_x = host_state.paddle_x[1];
    CHECK(update_touch(&client, 40U, 100U, 50U) ==
          P4_GAME_CONTINUE);
    CHECK(update(&host, 0U, 0U, 32U) == P4_GAME_CONTINUE);
    CHECK(host_state.paddle_x[1] > client_world_x);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.snapshot_revision == host_state.snapshot_revision);

    host_state.phase = P4_AIR_HOCKEY_GAME_OVER;
    host_state.winner = 0U;
    host_state.score[0] = P4_AIR_HOCKEY_WIN_SCORE;
    p4_air_hockey_network_publish(&host.context, &host_state, true);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.phase == P4_AIR_HOCKEY_GAME_OVER);
    CHECK(update_touch(&client, 160U, 100U, 50U) == P4_GAME_CONTINUE);
    CHECK(update(&host, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(host_state.phase == P4_AIR_HOCKEY_SERVE);
    CHECK(host_state.score[0] == 0U);
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.phase == P4_AIR_HOCKEY_SERVE);

    link.endpoint[1].state = P4_GAME_MULTIPLAYER_PEER_LEFT;
    CHECK(update(&client, 0U, 0U, 16U) == P4_GAME_CONTINUE);
    CHECK(client_state.mode == P4_AIR_HOCKEY_OFFLINE);
    CHECK(client_state.disconnect_banner_ms != 0U);
    p4_game_instance_stop(&client);
    p4_game_instance_stop(&host);
}

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_p4_air_hockey_game));
    CHECK(p4_p4_air_hockey_game.launcher_id == 115U);
    CHECK(sizeof(p4_air_hockey_state_t) <= P4_GAME_MAX_STATE_BYTES);
    uint16_t world_x = 0U;
    uint16_t world_y = 0U;
    p4_air_hockey_canonical_touch(1U, 40U, 77U, &world_x, &world_y);
    CHECK(world_x == 279U);
    CHECK(world_y == 77U);
    CHECK(p4_air_hockey_striker_skin(0U) ==
          P4_AIR_HOCKEY_STRIKER_CYAN);
    CHECK(p4_air_hockey_striker_skin(1U) ==
          P4_AIR_HOCKEY_STRIKER_MAGENTA);
    test_physics_and_scoring();
    test_offline_lifecycle_and_render_bounds();
    test_two_console_host_authority_and_peer_loss();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 Air Hockey test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("P4 Air Hockey tests passed");
    return EXIT_SUCCESS;
}
