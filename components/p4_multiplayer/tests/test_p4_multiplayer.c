// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

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

static size_t encode_packet(
    p4_mp_packet_type_t type,
    uint32_t session_id,
    uint32_t peer_id,
    uint32_t sequence,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t output[P4_MP_MAX_DATAGRAM_BYTES])
{
    size_t length = 0U;
    CHECK(p4_mp_packet_encode(
              type, session_id, peer_id, sequence, 0U,
              payload, payload_length, output,
              P4_MP_MAX_DATAGRAM_BYTES, &length) == P4_MP_OK);
    return length;
}

static void test_packet_codec(void)
{
    static const uint8_t crc_vector[] = "123456789";
    CHECK(p4_mp_crc32(crc_vector, sizeof(crc_vector) - 1U) ==
          UINT32_C(0xcbf43926));

    const p4_mp_input_t expected = {
        .tick = UINT32_C(0x12345678),
        .buttons = UINT32_C(0xa5a55a5a),
        .left_x = INT16_C(-32768),
        .left_y = INT16_C(32767),
        .right_x = INT16_C(-1234),
        .right_y = INT16_C(5678),
        .left_trigger = UINT16_C(17),
        .right_trigger = UINT16_C(65535),
        .dpad = UINT8_C(9),
    };
    uint8_t input_payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&expected, input_payload);
    p4_mp_input_t decoded_input;
    CHECK(p4_mp_input_decode(
              input_payload, sizeof(input_payload), &decoded_input) == P4_MP_OK);
    CHECK(memcmp(&expected, &decoded_input, sizeof(expected)) == 0);

    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t length = encode_packet(
        P4_MP_PACKET_INPUT, 11U, 22U, 33U,
        input_payload, sizeof(input_payload), datagram);
    CHECK(length == P4_MP_HEADER_BYTES + P4_MP_INPUT_PAYLOAD_BYTES +
                    P4_MP_TRAILER_BYTES);
    p4_mp_packet_view_t view;
    CHECK(p4_mp_packet_decode(datagram, length, &view) == P4_MP_OK);
    CHECK(view.type == P4_MP_PACKET_INPUT);
    CHECK(view.session_id == 11U && view.peer_id == 22U);
    CHECK(view.sequence == 33U && view.payload_length == sizeof(input_payload));
    CHECK(memcmp(view.payload, input_payload, sizeof(input_payload)) == 0);

    datagram[P4_MP_HEADER_BYTES + 3U] ^= UINT8_C(0x80);
    CHECK(p4_mp_packet_decode(datagram, length, &view) == P4_MP_BAD_CRC);
    datagram[P4_MP_HEADER_BYTES + 3U] ^= UINT8_C(0x80);
    CHECK(p4_mp_packet_decode(datagram, length - 1U, &view) == P4_MP_BAD_LENGTH);

    input_payload[21] = 1U;
    CHECK(p4_mp_input_decode(
              input_payload, sizeof(input_payload), &decoded_input) ==
          P4_MP_BAD_FLAGS);
}

static void test_wired_stream_boundary(void)
{
    p4_mp_wired_transport_info_t info;
    CHECK(p4_mp_wired_transport_info(
              P4_MP_WIRED_TRANSPORT_UART_RELAY, &info));
    CHECK(info.requires_host_relay && !info.console_sources_vbus);
    CHECK(!info.console_is_usb_device);
    CHECK(p4_mp_wired_transport_info(
              P4_MP_WIRED_TRANSPORT_USB2_DEVICE_RELAY, &info));
    CHECK(info.requires_host_relay && info.console_is_usb_device);
    CHECK(!info.console_sources_vbus);
    CHECK(!p4_mp_wired_transport_info(P4_MP_WIRED_TRANSPORT_NONE, &info));
    CHECK(!p4_mp_wired_transport_info(
              P4_MP_WIRED_TRANSPORT_UART_RELAY, NULL));

    const p4_mp_input_t input = {.tick = 9U, .buttons = 5U, .left_x = -7};
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&input, payload);
    uint8_t packet[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t packet_length = encode_packet(
        P4_MP_PACKET_INPUT, 10U, 20U, 30U,
        payload, sizeof(payload), packet);

    uint8_t stream[3U + P4_MP_MAX_DATAGRAM_BYTES];
    stream[0] = UINT8_C(0x00);
    stream[1] = (uint8_t)'P';
    stream[2] = UINT8_C(0xff);
    memcpy(stream + 3U, packet, packet_length);
    p4_mp_stream_decoder_t decoder;
    p4_mp_stream_decoder_init(&decoder);
    uint8_t decoded[P4_MP_MAX_DATAGRAM_BYTES];
    size_t consumed = 0U;
    size_t decoded_length = 0U;
    const size_t first_chunk = 13U;
    CHECK(p4_mp_stream_consume(
              &decoder, stream, first_chunk, &consumed,
              decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_NEED_MORE);
    CHECK(consumed == first_chunk && decoded_length == 0U);
    CHECK(p4_mp_stream_consume(
              &decoder, stream + first_chunk,
              3U + packet_length - first_chunk, &consumed,
              decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_FRAME_READY);
    CHECK(decoded_length == packet_length);
    CHECK(memcmp(decoded, packet, packet_length) == 0);
    CHECK(decoder.discarded_bytes >= 3U);

    uint8_t two_packets[2U * P4_MP_MAX_DATAGRAM_BYTES];
    memcpy(two_packets, packet, packet_length);
    memcpy(two_packets + packet_length, packet, packet_length);
    p4_mp_stream_decoder_init(&decoder);
    CHECK(p4_mp_stream_consume(
              &decoder, two_packets, 2U * packet_length, &consumed,
              decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_FRAME_READY);
    CHECK(consumed == packet_length);
    CHECK(p4_mp_stream_consume(
              &decoder, two_packets + consumed, packet_length, &consumed,
              decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_FRAME_READY);
    CHECK(consumed == packet_length);

    packet[P4_MP_HEADER_BYTES + 1U] ^= UINT8_C(0x80);
    p4_mp_stream_decoder_init(&decoder);
    CHECK(p4_mp_stream_consume(
              &decoder, packet, packet_length, &consumed,
              decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_FRAME_DROPPED);
    CHECK(decoder.last_packet_status == P4_MP_BAD_CRC);
    CHECK(decoder.dropped_frames == 1U);

    uint8_t oversized_header[P4_MP_HEADER_BYTES] = {
        'P', '4', 'M', 'P', P4_MP_VERSION, P4_MP_PACKET_INPUT,
    };
    oversized_header[24] = UINT8_C(0xff);
    oversized_header[25] = UINT8_C(0xff);
    p4_mp_stream_decoder_init(&decoder);
    CHECK(p4_mp_stream_consume(
              &decoder, oversized_header, sizeof(oversized_header),
              &consumed, decoded, sizeof(decoded), &decoded_length) ==
          P4_MP_STREAM_FRAME_DROPPED);
    CHECK(decoder.last_packet_status == P4_MP_BAD_LENGTH);
    CHECK(p4_mp_stream_consume(
              &decoder, NULL, 0U, &consumed,
              decoded, P4_MP_HEADER_BYTES, &decoded_length) ==
          P4_MP_STREAM_INVALID_ARGUMENT);
}

static void test_host_session(void)
{
    enum { SESSION_ID = 101, HOST_ID = 1, CLIENT_ID = 2 };
    const uint64_t route = UINT64_C(0xfeedbeef);
    p4_mp_session_t host;
    p4_mp_session_init(&host);
    CHECK(p4_mp_session_host_start(
              &host, SESSION_ID, HOST_ID, P4_MP_DEFAULT_TIMEOUT_MS) == P4_MP_OK);
    CHECK(host.state == P4_MP_SESSION_HOSTING);

    uint8_t join_payload[36] = {0};
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t length = encode_packet(
        P4_MP_PACKET_JOIN, SESSION_ID, CLIENT_ID, 7U,
        join_payload, sizeof(join_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              &host, route, 1000U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_JOIN_REQUEST);
    CHECK(event.peer_id == CLIENT_ID && event.route_id == route);
    CHECK(!event.neutralize_player);
    CHECK(p4_mp_session_peer_count(&host) == 0U);

    CHECK(p4_mp_session_accept_peer(
              &host, CLIENT_ID, route, 1U, event.packet.sequence, 1000U) ==
          P4_MP_OK);
    CHECK(host.state == P4_MP_SESSION_CONNECTED);
    CHECK(p4_mp_session_peer_count(&host) == 1U);

    p4_mp_input_t input = {.tick = 22U, .buttons = 3U, .left_x = -100};
    uint8_t input_payload[P4_MP_INPUT_PAYLOAD_BYTES];
    p4_mp_input_encode(&input, input_payload);
    length = encode_packet(
        P4_MP_PACKET_INPUT, SESSION_ID, CLIENT_ID, 8U,
        input_payload, sizeof(input_payload), datagram);
    CHECK(p4_mp_session_receive(
              &host, route, 1100U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_INPUT && event.player_slot == 1U);
    CHECK(p4_mp_session_receive(
              &host, route, 1101U, datagram, length, &event) == P4_MP_REPLAYED);
    CHECK(p4_mp_session_receive(
              &host, route + 1U, 1101U, datagram, length, &event) ==
          P4_MP_ROUTE_MISMATCH);

    input_payload[21] = 1U;
    length = encode_packet(
        P4_MP_PACKET_INPUT, SESSION_ID, CLIENT_ID, 9U,
        input_payload, sizeof(input_payload), datagram);
    CHECK(p4_mp_session_receive(
              &host, route, 1200U, datagram, length, &event) == P4_MP_BAD_FLAGS);

    const uint8_t leave_payload[2] = {0, 0};
    length = encode_packet(
        P4_MP_PACKET_LEAVE, SESSION_ID, CLIENT_ID, 10U,
        leave_payload, sizeof(leave_payload), datagram);
    CHECK(p4_mp_session_receive(
              &host, route, 1300U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_PEER_LEFT);
    CHECK(event.neutralize_player && event.player_slot == 1U);
    CHECK(p4_mp_session_peer_count(&host) == 0U);
    CHECK(host.state == P4_MP_SESSION_HOSTING);
}

static void add_host_peer(
    p4_mp_session_t *host,
    uint32_t peer_id,
    uint64_t route_id,
    uint8_t slot,
    uint64_t now_ms)
{
    uint8_t join_payload[36] = {0};
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t length = encode_packet(
        P4_MP_PACKET_JOIN, host->session_id, peer_id, 1U,
        join_payload, sizeof(join_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              host, route_id, now_ms, datagram, length, &event) == P4_MP_OK);
    CHECK(p4_mp_session_accept_peer(
              host, peer_id, route_id, slot, 1U, now_ms) == P4_MP_OK);
}

static void test_capacity_timeout_and_disconnect(void)
{
    p4_mp_session_t host;
    CHECK(p4_mp_session_host_start(&host, 77U, 1U, 3000U) == P4_MP_OK);
    add_host_peer(&host, 2U, 102U, 1U, 50U);
    add_host_peer(&host, 3U, 103U, 2U, 50U);
    add_host_peer(&host, 4U, 104U, 3U, 50U);
    CHECK(p4_mp_session_peer_count(&host) == P4_MP_MAX_REMOTE_PEERS);

    uint8_t join_payload[36] = {0};
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t length = encode_packet(
        P4_MP_PACKET_JOIN, 77U, 5U, 1U,
        join_payload, sizeof(join_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              &host, 105U, 60U, datagram, length, &event) == P4_MP_FULL);

    CHECK(!p4_mp_session_tick(&host, 3050U, &event));
    CHECK(p4_mp_session_tick(&host, 3051U, &event));
    CHECK(event.type == P4_MP_EVENT_PEER_TIMED_OUT);
    CHECK(event.neutralize_player && event.player_slot == 1U);
    CHECK(p4_mp_session_peer_count(&host) == 2U);

    CHECK(p4_mp_session_route_disconnected(&host, 103U, &event));
    CHECK(event.type == P4_MP_EVENT_ROUTE_DISCONNECTED);
    CHECK(event.neutralize_player && event.player_slot == 2U);
    CHECK(!p4_mp_session_route_disconnected(&host, 999U, &event));
}

static void test_client_session(void)
{
    p4_mp_session_t client;
    CHECK(p4_mp_session_client_start(
              &client, 200U, 9U, 1U, 44U, 100U, 3000U) == P4_MP_OK);
    CHECK(client.state == P4_MP_SESSION_JOINING);
    CHECK(p4_mp_session_peer_count(&client) == 0U);

    uint8_t accept_payload[16] = {0};
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t length = encode_packet(
        P4_MP_PACKET_ACCEPT, 200U, 1U, 8U,
        accept_payload, sizeof(accept_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              &client, 44U, 150U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_ACCEPTED);
    CHECK(client.state == P4_MP_SESSION_CONNECTED);
    CHECK(p4_mp_session_peer_count(&client) == 1U);
    CHECK(!p4_mp_session_tick(&client, 3150U, &event));
    CHECK(p4_mp_session_tick(&client, 3151U, &event));
    CHECK(event.type == P4_MP_EVENT_PEER_TIMED_OUT);
    CHECK(event.neutralize_player);
    CHECK(client.state == P4_MP_SESSION_CLOSED);

    CHECK(p4_mp_session_client_start(
              &client, 201U, 9U, 1U, 45U, 0U, 3000U) == P4_MP_OK);
    const uint8_t reject_payload[2] = {1, 0};
    length = encode_packet(
        P4_MP_PACKET_REJECT, 201U, 1U, 1U,
        reject_payload, sizeof(reject_payload), datagram);
    CHECK(p4_mp_session_receive(
              &client, 45U, 1U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_REJECTED);
    CHECK(!event.neutralize_player);
    CHECK(client.state == P4_MP_SESSION_CLOSED);
}

static uint32_t next_random(uint32_t *state)
{
    *state = (*state * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *state;
}

static void test_bounded_decode_fuzz(void)
{
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES + 16U];
    uint32_t state = UINT32_C(0xc0decafe);
    for (size_t pass = 0; pass < 2000U; ++pass) {
        const size_t length = (size_t)(next_random(&state) % sizeof(bytes));
        for (size_t index = 0; index < length; ++index) {
            bytes[index] = (uint8_t)next_random(&state);
        }
        p4_mp_packet_view_t view;
        (void)p4_mp_packet_decode(bytes, length, &view);
    }
}

int main(void)
{
    test_packet_codec();
    test_wired_stream_boundary();
    test_host_session();
    test_capacity_timeout_and_disconnect();
    test_client_session();
    test_bounded_decode_fuzz();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 multiplayer test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 multiplayer tests passed");
    return EXIT_SUCCESS;
}
