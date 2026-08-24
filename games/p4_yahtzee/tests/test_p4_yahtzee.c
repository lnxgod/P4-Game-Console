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
        .player_count = P4_YAHTZEE_PLAYERS,
    };
    return true;
}

static bool link_send(void *context, const uint8_t *data, size_t data_bytes)
{
    test_endpoint_t *const source = context;
    test_endpoint_t *const destination =
        &source->link->endpoints[source->slot ^ 1U];
    if (data == NULL || data_bytes == 0U ||
        data_bytes > P4_GAME_MULTIPLAYER_MAX_MESSAGE_BYTES ||
        destination->queue_count >= LINK_QUEUE) {
        return false;
    }
    const size_t tail = (destination->queue_head +
                         destination->queue_count) % LINK_QUEUE;
    p4_game_multiplayer_message_t *const message =
        &destination->queue[tail];
    *message = (p4_game_multiplayer_message_t){
        .sequence = ++source->next_sequence,
        .player_slot = source->slot,
        .bytes = (uint8_t)data_bytes,
    };
    memcpy(message->data, data, data_bytes);
    ++destination->queue_count;
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

static void init_link(test_link_t *link)
{
    memset(link, 0, sizeof(*link));
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

static void test_network_host_authority(void)
{
    test_link_t link;
    init_link(&link);
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
    p4_game_instance_stop(&client);
    p4_game_instance_stop(&host);
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
    tap(&instance, 160U, 117U); /* Local 2 Players. */
    CHECK(state.phase == P4_YAHTZEE_TURN);
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

int main(void)
{
    CHECK(p4_game_descriptor_valid(&p4_p4_yahtzee_game));
    CHECK(p4_p4_yahtzee_game.launcher_id == 113U);
    test_scoring_rules();
    test_local_lifecycle_and_framebuffer();
    test_network_host_authority();
    test_touch_regions();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 Yahtzee test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("P4 Yahtzee tests passed");
    return EXIT_SUCCESS;
}
