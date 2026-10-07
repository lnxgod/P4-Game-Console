// SPDX-License-Identifier: MIT

#include "p4/doom_multiplayer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(condition) do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", \
                    __FILE__, __LINE__, #condition); \
            ++s_failures; \
        } \
    } while (0)

static p4_doom_mp_tic_t tic(uint32_t tick, int movement)
{
    return (p4_doom_mp_tic_t){
        .tick = tick,
        .forward_move = (int8_t)movement,
        .side_move = (int8_t)-movement,
        .angle_turn = (int16_t)(movement * 100),
        .buttons = UINT8_C(3),
        .consistency = UINT8_C(7),
        .chat_char = (uint8_t)'x',
    };
}

static void test_input_mapping(void)
{
    const p4_doom_mp_tic_t expected = tic(42U, -12);
    p4_mp_input_t input;
    p4_doom_mp_tic_to_input(&expected, &input);
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&input, payload);
    p4_mp_input_t decoded_input;
    CHECK(p4_mp_input_decode(
              payload, sizeof(payload), &decoded_input) == P4_MP_OK);
    p4_doom_mp_tic_t decoded;
    CHECK(p4_doom_mp_tic_from_input(&decoded_input, &decoded));
    CHECK(decoded.tick == expected.tick);
    CHECK(decoded.forward_move == expected.forward_move);
    CHECK(decoded.side_move == expected.side_move);
    CHECK(decoded.angle_turn == expected.angle_turn);
    CHECK(decoded.buttons == expected.buttons);
    CHECK(decoded.consistency == expected.consistency);
    CHECK(decoded.chat_char == expected.chat_char);

    decoded_input.left_x = 128;
    CHECK(!p4_doom_mp_tic_from_input(&decoded_input, &decoded));
    decoded_input.left_x = expected.side_move;
    decoded_input.dpad = 1U;
    CHECK(!p4_doom_mp_tic_from_input(&decoded_input, &decoded));
}

static void test_launch_config(void)
{
    const p4_doom_mp_launch_config_t single_player = {0};
    CHECK(p4_doom_mp_launch_config_valid(&single_player));
    p4_doom_mp_launch_config_t invalid_single_player = {0};
    invalid_single_player.setup.game = P4_DOOM_MP_GAME_CHEX_QUEST;
    CHECK(!p4_doom_mp_launch_config_valid(&invalid_single_player));
    p4_doom_mp_launch_config_t multiplayer = {
        .enabled = true,
        .role = P4_MP_ROLE_CLIENT,
        .session_id = 7U,
        .self_peer_id = 2U,
        .remote_peer_id = 1U,
        .route_id = 1U,
        .local_player_slot = 1U,
        .player_count = 2U,
        .input_delay_tics = 2U,
        .start_tic = 0U,
        .session_seed = 99U,
        .setup = {
            .game = P4_DOOM_MP_GAME_DOOM,
            .mode = P4_DOOM_MP_MODE_DEATHMATCH,
            .episode = 1U,
            .map = 1U,
            .skill = 3U,
        },
    };
    CHECK(p4_doom_mp_launch_config_valid(&multiplayer));
    multiplayer.remote_peer_id = multiplayer.self_peer_id;
    CHECK(!p4_doom_mp_launch_config_valid(&multiplayer));
}

static void test_setup_codec(void)
{
    const p4_doom_mp_setup_t expected = {
        .game = P4_DOOM_MP_GAME_CHEX_QUEST,
        .mode = P4_DOOM_MP_MODE_ALTDEATH,
        .episode = 1U,
        .map = 5U,
        .skill = 4U,
        .time_limit_minutes = 15U,
        .no_monsters = true,
        .fast_monsters = true,
        .respawn_monsters = true,
    };
    uint8_t bytes[P4_DOOM_MP_SETUP_BYTES];
    CHECK(p4_doom_mp_setup_encode(&expected, bytes));
    p4_doom_mp_setup_t decoded;
    CHECK(p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
    CHECK(decoded.game == expected.game);
    CHECK(decoded.mode == expected.mode);
    CHECK(decoded.episode == expected.episode);
    CHECK(decoded.map == expected.map);
    CHECK(decoded.skill == expected.skill);
    CHECK(decoded.time_limit_minutes == expected.time_limit_minutes);
    CHECK(decoded.no_monsters == expected.no_monsters);
    CHECK(decoded.fast_monsters == expected.fast_monsters);
    CHECK(decoded.respawn_monsters == expected.respawn_monsters);

    bytes[5] |= UINT8_C(0x80);
    CHECK(!p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
    bytes[5] &= UINT8_C(0x7f);
    bytes[7] = (uint8_t)P4_DOOM_MP_GAME_COUNT;
    CHECK(!p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
    bytes[7] = (uint8_t)P4_DOOM_MP_GAME_CHEX_QUEST;
    bytes[4] = 6U;
    CHECK(!p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
    bytes[4] = 4U;
    bytes[3] = P4_DOOM_MP_MAX_CHEX_MAP + 1U;
    CHECK(!p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
    bytes[3] = P4_DOOM_MP_MAX_CHEX_MAP;
    bytes[2] = 2U;
    CHECK(!p4_doom_mp_setup_decode(bytes, sizeof(bytes), &decoded));
}

static void test_arena_setup(void)
{
    p4_doom_mp_setup_t setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,
        .mode=P4_DOOM_MP_MODE_ALTDEATH,.episode=1,.map=1,.skill=3,.no_monsters=true};
    uint8_t bytes[P4_DOOM_MP_SETUP_BYTES];
    CHECK(p4_doom_mp_setup_encode(&setup,bytes));
    p4_doom_mp_setup_t result;
    CHECK(p4_doom_mp_setup_decode(bytes,sizeof(bytes),&result));
    CHECK(result.game==setup.game && result.map==1);
    setup.map=2; CHECK(p4_doom_mp_setup_valid(&setup));
    for (uint8_t map=3;map<=29;++map) {
        setup.map=map; CHECK(p4_doom_mp_setup_encode(&setup,bytes));
        CHECK(p4_doom_mp_setup_decode(bytes,sizeof(bytes),&result) && result.map==map);
    }
    setup.map=30; CHECK(!p4_doom_mp_setup_valid(&setup));
    setup.map=32; CHECK(!p4_doom_mp_setup_valid(&setup));
    setup.map=1; setup.no_monsters=false; CHECK(!p4_doom_mp_setup_valid(&setup));
    setup.no_monsters=true; setup.mode=P4_DOOM_MP_MODE_COOPERATIVE;
    CHECK(!p4_doom_mp_setup_valid(&setup));
}

static void test_engine_barrier(void)
{
    uint8_t ready[P4_DOOM_MP_ENGINE_CONTROL_BYTES];
    uint8_t ack[P4_DOOM_MP_ENGINE_CONTROL_BYTES];
    CHECK(p4_doom_mp_engine_control_encode(
              P4_DOOM_MP_ENGINE_CONTROL_READY, ready));
    CHECK(p4_doom_mp_engine_control_encode(
              P4_DOOM_MP_ENGINE_CONTROL_ACK, ack));
    CHECK(!p4_doom_mp_engine_control_encode(
              P4_DOOM_MP_ENGINE_CONTROL_NONE, ready));

    p4_doom_mp_engine_barrier_t barrier;
    p4_doom_mp_engine_barrier_init(&barrier);
    CHECK(!p4_doom_mp_engine_barrier_complete(&barrier));

    /* Launcher teardown may receive READY but must never acknowledge it. */
    CHECK(p4_doom_mp_engine_barrier_observe_ping(
              &barrier, ready, sizeof(ready)) ==
          P4_DOOM_MP_ENGINE_CONTROL_NONE);
    CHECK(!barrier.peer_ready);

    p4_doom_mp_engine_barrier_begin(&barrier);
    CHECK(barrier.local_ready);
    CHECK(!barrier.peer_ready && !barrier.peer_acknowledged);
    p4_doom_mp_engine_barrier_observe_pong(
        &barrier, ready, sizeof(ready));
    CHECK(!barrier.peer_acknowledged);
    CHECK(p4_doom_mp_engine_barrier_observe_ping(
              &barrier, ready, sizeof(ready)) ==
          P4_DOOM_MP_ENGINE_CONTROL_ACK);
    CHECK(barrier.peer_ready);
    CHECK(!p4_doom_mp_engine_barrier_complete(&barrier));
    p4_doom_mp_engine_barrier_observe_pong(
        &barrier, ack, sizeof(ack));
    CHECK(p4_doom_mp_engine_barrier_complete(&barrier));

    /* A new configure attempt must not inherit a stale completed handshake. */
    p4_doom_mp_engine_barrier_begin(&barrier);
    CHECK(barrier.local_ready);
    CHECK(!barrier.peer_ready && !barrier.peer_acknowledged);
    CHECK(p4_doom_mp_engine_barrier_observe_ping(
              &barrier, ready, sizeof(ready) - 1U) ==
          P4_DOOM_MP_ENGINE_CONTROL_NONE);
    ready[0] ^= UINT8_C(0x01);
    CHECK(p4_doom_mp_engine_barrier_observe_ping(
              &barrier, ready, sizeof(ready)) ==
          P4_DOOM_MP_ENGINE_CONTROL_NONE);
    p4_doom_mp_engine_barrier_observe_pong(
        &barrier, ack, sizeof(ack) - 1U);
    CHECK(!barrier.peer_acknowledged);
}

static void test_lockstep_queue(void)
{
    p4_doom_mp_tic_queue_t queue;
    CHECK(p4_doom_mp_tic_queue_init(&queue, 2U, 100U));
    const p4_doom_mp_tic_t left = tic(100U, 1);
    const p4_doom_mp_tic_t right = tic(100U, 2);
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 1U, &right));
    CHECK(!p4_doom_mp_tic_queue_ready(&queue));
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 0U, &left));
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 0U, &left));
    p4_doom_mp_tic_t conflict = left;
    conflict.buttons ^= 1U;
    CHECK(!p4_doom_mp_tic_queue_submit(&queue, 0U, &conflict));
    CHECK(p4_doom_mp_tic_queue_ready(&queue));
    p4_doom_mp_tic_t output[P4_MP_MAX_PLAYERS];
    uint8_t connected = 0U;
    CHECK(p4_doom_mp_tic_queue_pop(&queue, output, &connected));
    CHECK(connected == UINT8_C(0x03));
    CHECK(output[0].forward_move == 1 && output[1].forward_move == 2);
    CHECK(queue.next_tick == 101U);
    CHECK(!p4_doom_mp_tic_queue_ready(&queue));

    p4_doom_mp_tic_t stale = tic(100U, 3);
    p4_doom_mp_tic_t far = tic(
        101U + P4_DOOM_MP_TIC_RING_SIZE, 3);
    CHECK(!p4_doom_mp_tic_queue_submit(&queue, 0U, &stale));
    CHECK(!p4_doom_mp_tic_queue_submit(&queue, 0U, &far));

    const p4_doom_mp_tic_t next = tic(101U, 4);
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 0U, &next));
    CHECK(!p4_doom_mp_tic_queue_ready(&queue));
    CHECK(p4_doom_mp_tic_queue_disconnect(&queue, 1U));
    CHECK(p4_doom_mp_tic_queue_ready(&queue));
    CHECK(p4_doom_mp_tic_queue_pop(&queue, output, &connected));
    CHECK(connected == UINT8_C(0x01));
    CHECK(output[0].forward_move == 4);
    CHECK(output[1].tick == 101U && output[1].forward_move == 0);
    CHECK(!p4_doom_mp_tic_queue_disconnect(&queue, 1U));
}

static void test_tick_wrap(void)
{
    p4_doom_mp_tic_queue_t queue;
    CHECK(p4_doom_mp_tic_queue_init(&queue, 2U, UINT32_MAX));
    const p4_doom_mp_tic_t left = tic(UINT32_MAX, 1);
    const p4_doom_mp_tic_t right = tic(UINT32_MAX, 2);
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 0U, &left));
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 1U, &right));
    p4_doom_mp_tic_t output[P4_MP_MAX_PLAYERS];
    uint8_t connected = 0U;
    CHECK(p4_doom_mp_tic_queue_pop(&queue, output, &connected));
    CHECK(queue.next_tick == 0U);
    const p4_doom_mp_tic_t zero_left = tic(0U, 3);
    const p4_doom_mp_tic_t zero_right = tic(0U, 4);
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 0U, &zero_left));
    CHECK(p4_doom_mp_tic_queue_submit(&queue, 1U, &zero_right));
    CHECK(p4_doom_mp_tic_queue_ready(&queue));
}

static void test_reliable_tx_window(void)
{
    p4_doom_mp_tx_window_t window;
    p4_doom_mp_tx_window_init(&window, 100U);
    const p4_doom_mp_tic_t first = tic(100U, 1);
    const p4_doom_mp_tic_t second = tic(101U, 2);
    CHECK(p4_doom_mp_tx_window_track(&window, &first));
    CHECK(p4_doom_mp_tx_window_track(&window, &first));
    p4_doom_mp_tic_t conflict = first;
    conflict.buttons ^= UINT8_C(1);
    CHECK(!p4_doom_mp_tx_window_track(&window, &conflict));
    CHECK(p4_doom_mp_tx_window_track(&window, &second));
    CHECK(p4_doom_mp_tx_window_pending(&window) == 2U);

    p4_doom_mp_tic_t oldest;
    CHECK(p4_doom_mp_tx_window_oldest(&window, &oldest));
    CHECK(oldest.tick == 100U);
    CHECK(!p4_doom_mp_tx_window_acknowledge(&window, 99U));
    CHECK(!p4_doom_mp_tx_window_acknowledge(&window, 103U));
    CHECK(p4_doom_mp_tx_window_acknowledge(&window, 101U));
    CHECK(p4_doom_mp_tx_window_pending(&window) == 1U);
    CHECK(p4_doom_mp_tx_window_oldest(&window, &oldest));
    CHECK(oldest.tick == 101U);
    CHECK(p4_doom_mp_tx_window_acknowledge(&window, 102U));
    CHECK(p4_doom_mp_tx_window_pending(&window) == 0U);
    CHECK(!p4_doom_mp_tx_window_oldest(&window, &oldest));
}

static void test_reliable_tx_window_wrap_and_capacity(void)
{
    p4_doom_mp_tx_window_t window;
    p4_doom_mp_tx_window_init(&window, UINT32_MAX);
    const p4_doom_mp_tic_t last = tic(UINT32_MAX, 1);
    const p4_doom_mp_tic_t zero = tic(0U, 2);
    CHECK(p4_doom_mp_tx_window_track(&window, &last));
    CHECK(p4_doom_mp_tx_window_track(&window, &zero));
    CHECK(p4_doom_mp_tx_window_acknowledge(&window, 0U));
    p4_doom_mp_tic_t oldest;
    CHECK(p4_doom_mp_tx_window_oldest(&window, &oldest));
    CHECK(oldest.tick == 0U);
    CHECK(p4_doom_mp_tx_window_acknowledge(&window, 1U));

    p4_doom_mp_tx_window_init(&window, 0U);
    for (uint32_t tick_number = 0U;
         tick_number < P4_DOOM_MP_TX_WINDOW_SIZE; ++tick_number) {
        const p4_doom_mp_tic_t item = tic(tick_number, (int)tick_number);
        CHECK(p4_doom_mp_tx_window_track(&window, &item));
    }
    const p4_doom_mp_tic_t overflow =
        tic(P4_DOOM_MP_TX_WINDOW_SIZE, 1);
    CHECK(!p4_doom_mp_tx_window_track(&window, &overflow));
    CHECK(p4_doom_mp_tx_window_acknowledge(
              &window, P4_DOOM_MP_TX_WINDOW_SIZE / 2U));
    CHECK(p4_doom_mp_tx_window_track(&window, &overflow));
}

int main(void)
{
    test_input_mapping();
    test_launch_config();
    test_setup_codec();
    test_arena_setup();
    test_engine_barrier();
    test_lockstep_queue();
    test_tick_wrap();
    test_reliable_tx_window();
    test_reliable_tx_window_wrap_and_capacity();
    if (s_failures != 0) {
        fprintf(stderr, "%d Doom multiplayer test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Doom multiplayer tests passed");
    return EXIT_SUCCESS;
}
