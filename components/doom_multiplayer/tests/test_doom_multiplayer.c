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
    };
    CHECK(p4_doom_mp_launch_config_valid(&multiplayer));
    multiplayer.remote_peer_id = multiplayer.self_peer_id;
    CHECK(!p4_doom_mp_launch_config_valid(&multiplayer));
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

int main(void)
{
    test_input_mapping();
    test_launch_config();
    test_lockstep_queue();
    test_tick_wrap();
    if (s_failures != 0) {
        fprintf(stderr, "%d Doom multiplayer test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("Doom multiplayer tests passed");
    return EXIT_SUCCESS;
}
