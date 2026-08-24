// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"
#include "p4/multiplayer_ble.h"

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

static void fill_hash(uint8_t hash[P4_MP_SHA256_BYTES], uint8_t seed)
{
    for (size_t index = 0U; index < P4_MP_SHA256_BYTES; ++index) {
        hash[index] = (uint8_t)(seed + (uint8_t)index);
    }
}

static p4_mp_lobby_offer_t test_offer(void)
{
    p4_mp_lobby_offer_t offer = {
        .mode = P4_MP_GAME_MODE_LOCKSTEP,
        .game_api_major = 1U,
        .game_api_minor = 0U,
        .players_present = 1U,
        .player_capacity = 2U,
        .input_delay_tics = 2U,
        .tick_rate_hz = 35U,
        .game_protocol = 1U,
        .session_seed = UINT64_C(0x123456789abcdef0),
    };
    strcpy(offer.game_id, "org.p4console.doom");
    fill_hash(offer.content_sha256, UINT8_C(0x10));
    fill_hash(offer.compatibility_sha256, UINT8_C(0x80));
    for (size_t index = 0U; index < P4_MP_GAME_SETTINGS_BYTES; ++index) {
        offer.game_settings[index] = (uint8_t)(index + 1U);
    }
    return offer;
}

static void encode_test_join(
    uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES],
    uint8_t requested_slot,
    uint32_t nonce)
{
    const p4_mp_lobby_offer_t offer = test_offer();
    p4_mp_lobby_join_t join = {
        .requested_player_slot = requested_slot,
        .join_nonce = nonce,
    };
    memcpy(join.compatibility_sha256,
           offer.compatibility_sha256,
           P4_MP_SHA256_BYTES);
    CHECK(p4_mp_lobby_join_encode(&join, payload) == P4_MP_OK);
}

static void encode_test_accept(
    uint8_t payload[P4_MP_ACCEPT_PAYLOAD_BYTES],
    uint8_t assigned_slot)
{
    const p4_mp_lobby_accept_t accept = {
        .assigned_player_slot = assigned_slot,
        .player_count = 2U,
        .input_delay_tics = 2U,
        .start_tic = 0U,
        .session_seed = UINT64_C(0x123456789abcdef0),
    };
    CHECK(p4_mp_lobby_accept_encode(&accept, payload) == P4_MP_OK);
}

static void test_lobby_codec(void)
{
    p4_mp_lobby_offer_t expected = test_offer();
    uint8_t offer_payload[P4_MP_OFFER_PAYLOAD_BYTES];
    CHECK(p4_mp_lobby_offer_encode(&expected, offer_payload) == P4_MP_OK);
    p4_mp_lobby_offer_t decoded;
    CHECK(p4_mp_lobby_offer_decode(
              offer_payload, sizeof(offer_payload), &decoded) == P4_MP_OK);
    CHECK(decoded.mode == expected.mode);
    CHECK(decoded.game_api_major == expected.game_api_major);
    CHECK(decoded.game_api_minor == expected.game_api_minor);
    CHECK(decoded.players_present == expected.players_present);
    CHECK(decoded.player_capacity == expected.player_capacity);
    CHECK(decoded.input_delay_tics == expected.input_delay_tics);
    CHECK(decoded.tick_rate_hz == expected.tick_rate_hz);
    CHECK(decoded.game_protocol == expected.game_protocol);
    CHECK(decoded.session_seed == expected.session_seed);
    CHECK(memcmp(decoded.game_id,
                 expected.game_id,
                 P4_MP_GAME_ID_BYTES) == 0);
    CHECK(memcmp(decoded.content_sha256,
                 expected.content_sha256,
                 P4_MP_SHA256_BYTES) == 0);
    CHECK(memcmp(decoded.compatibility_sha256,
                 expected.compatibility_sha256,
                 P4_MP_SHA256_BYTES) == 0);
    CHECK(memcmp(decoded.game_settings,
                 expected.game_settings,
                 P4_MP_GAME_SETTINGS_BYTES) == 0);
    uint8_t compatibility_material[P4_MP_COMPATIBILITY_MATERIAL_BYTES];
    CHECK(p4_mp_lobby_compatibility_material(
              &expected, compatibility_material) == P4_MP_OK);
    CHECK(compatibility_material[0] == P4_MP_LOBBY_SCHEMA);
    CHECK(compatibility_material[1] == P4_MP_GAME_MODE_LOCKSTEP);
    CHECK(compatibility_material[4] == 2U);
    CHECK(compatibility_material[6] == 35U &&
          compatibility_material[7] == 0U);
    CHECK(memcmp(compatibility_material + 16,
                 expected.game_id,
                 P4_MP_GAME_ID_BYTES) == 0);
    CHECK(memcmp(compatibility_material + 48,
                 expected.content_sha256,
                 P4_MP_SHA256_BYTES) == 0);

    p4_mp_lobby_offer_t compatible = expected;
    compatible.players_present = 2U;
    compatible.session_seed += 1U;
    CHECK(p4_mp_lobby_offers_compatible(&expected, &compatible));
    compatible.game_settings[0] ^= UINT8_C(0x7f);
    CHECK(p4_mp_lobby_offers_compatible(&expected, &compatible));
    compatible.game_settings[0] ^= UINT8_C(0x7f);
    compatible.content_sha256[0] ^= UINT8_C(0x80);
    CHECK(!p4_mp_lobby_offers_compatible(&expected, &compatible));

    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t offer_datagram_length = encode_packet(
        P4_MP_PACKET_OFFER, 77U, 1U, 1U,
        offer_payload, sizeof(offer_payload), datagram);
    p4_mp_packet_view_t view;
    CHECK(p4_mp_packet_decode(
              datagram, offer_datagram_length, &view) == P4_MP_OK);
    CHECK(view.type == P4_MP_PACKET_OFFER);
    CHECK(view.payload_length == P4_MP_OFFER_PAYLOAD_BYTES);

    p4_mp_lobby_join_t join = {
        .requested_player_slot = P4_MP_PLAYER_SLOT_ANY,
        .join_nonce = UINT32_C(0xaabbccdd),
    };
    memcpy(join.compatibility_sha256,
           expected.compatibility_sha256,
           P4_MP_SHA256_BYTES);
    uint8_t join_payload[P4_MP_JOIN_PAYLOAD_BYTES];
    CHECK(p4_mp_lobby_join_encode(&join, join_payload) == P4_MP_OK);
    p4_mp_lobby_join_t decoded_join;
    CHECK(p4_mp_lobby_join_decode(
              join_payload, sizeof(join_payload), &decoded_join) == P4_MP_OK);
    CHECK(decoded_join.requested_player_slot == P4_MP_PLAYER_SLOT_ANY);
    CHECK(decoded_join.join_nonce == join.join_nonce);
    CHECK(p4_mp_lobby_join_matches_offer(&expected, &decoded_join));
    decoded_join.compatibility_sha256[0] ^= UINT8_C(0x01);
    CHECK(!p4_mp_lobby_join_matches_offer(&expected, &decoded_join));

    p4_mp_lobby_accept_t accept = {
        .assigned_player_slot = 1U,
        .player_count = 2U,
        .input_delay_tics = 2U,
        .start_tic = 0U,
        .session_seed = expected.session_seed,
    };
    memcpy(accept.game_settings,
           expected.game_settings,
           P4_MP_GAME_SETTINGS_BYTES);
    uint8_t accept_payload[P4_MP_ACCEPT_PAYLOAD_BYTES];
    CHECK(p4_mp_lobby_accept_encode(&accept, accept_payload) == P4_MP_OK);
    p4_mp_lobby_accept_t decoded_accept;
    CHECK(p4_mp_lobby_accept_decode(
              accept_payload, sizeof(accept_payload), &decoded_accept) ==
          P4_MP_OK);
    CHECK(decoded_accept.assigned_player_slot == 1U);
    CHECK(decoded_accept.player_count == 2U);
    CHECK(decoded_accept.input_delay_tics == 2U);
    CHECK(decoded_accept.start_tic == 0U);
    CHECK(decoded_accept.session_seed == expected.session_seed);
    CHECK(memcmp(decoded_accept.game_settings,
                 expected.game_settings,
                 P4_MP_GAME_SETTINGS_BYTES) == 0);

    offer_payload[7] = 1U;
    CHECK(p4_mp_lobby_offer_decode(
              offer_payload, sizeof(offer_payload), &decoded) ==
          P4_MP_BAD_FLAGS);
    offer_payload[7] = 0U;
    join_payload[2] = 1U;
    CHECK(p4_mp_lobby_join_decode(
              join_payload, sizeof(join_payload), &decoded_join) ==
          P4_MP_BAD_FLAGS);
    join_payload[2] = 0U;
    accept_payload[0] = (uint8_t)(P4_MP_LOBBY_SCHEMA + 1U);
    CHECK(p4_mp_lobby_accept_decode(
              accept_payload, sizeof(accept_payload), &decoded_accept) ==
          P4_MP_BAD_VERSION);
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

static void test_synchronized_start_barrier(void)
{
    enum {
        TOKEN = 0x4d50,
        HOLD_MS = 1500,
        TIMEOUT_MS = 15000,
    };
    uint8_t payload[P4_MP_START_PAYLOAD_BYTES];
    CHECK(p4_mp_start_ready_encode(TOKEN, payload) == P4_MP_OK);
    uint16_t decoded_token = 0U;
    CHECK(p4_mp_start_ready_decode(
              payload, sizeof(payload), &decoded_token) == P4_MP_OK);
    CHECK(decoded_token == TOKEN);
    payload[0] ^= UINT8_C(0x01);
    CHECK(p4_mp_start_ready_decode(
              payload, sizeof(payload), &decoded_token) == P4_MP_BAD_MAGIC);
    payload[0] ^= UINT8_C(0x01);
    CHECK(p4_mp_start_ready_decode(
              payload, sizeof(payload) - 1U, &decoded_token) ==
          P4_MP_BAD_LENGTH);
    CHECK(p4_mp_start_ready_encode(0U, payload) == P4_MP_BAD_IDENTITY);

    p4_mp_start_barrier_t early;
    p4_mp_start_barrier_t late;
    p4_mp_start_barrier_init(&early);
    p4_mp_start_barrier_init(&late);
    CHECK(p4_mp_start_barrier_begin(
              &early, TOKEN, 1000U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(early.state == P4_MP_START_WAITING);

    /* The late console auto-arms from the early console's READY packet. */
    CHECK(p4_mp_start_barrier_observe_ready(
              &late, TOKEN, 1100U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(late.state == P4_MP_START_ARMED);
    CHECK(late.launch_at_ms == 2600U);

    /* Its reply arms the early console; repeat packets never move deadlines. */
    CHECK(p4_mp_start_barrier_observe_ready(
              &early, TOKEN, 1120U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(early.state == P4_MP_START_ARMED);
    CHECK(early.launch_at_ms == 2620U);
    CHECK(p4_mp_start_barrier_observe_ready(
              &early, TOKEN, 1500U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(early.launch_at_ms == 2620U);
    CHECK(p4_mp_start_barrier_remaining_ms(&early, 2000U) == 620U);
    CHECK(p4_mp_start_barrier_poll(&late, 2599U) == P4_MP_START_ARMED);
    CHECK(p4_mp_start_barrier_poll(&late, 2600U) == P4_MP_START_DUE);
    CHECK(p4_mp_start_barrier_poll(&early, 2619U) == P4_MP_START_ARMED);
    CHECK(p4_mp_start_barrier_poll(&early, 2620U) == P4_MP_START_DUE);
    CHECK(p4_mp_start_barrier_observe_ready(
              &early, TOKEN, 2700U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(early.state == P4_MP_START_DUE);

    p4_mp_start_barrier_t timeout;
    p4_mp_start_barrier_init(&timeout);
    CHECK(p4_mp_start_barrier_begin(
              &timeout, TOKEN, 500U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(p4_mp_start_barrier_poll(&timeout, 15499U) ==
          P4_MP_START_WAITING);
    CHECK(p4_mp_start_barrier_poll(&timeout, 15500U) ==
          P4_MP_START_TIMED_OUT);
    p4_mp_start_barrier_cancel(&timeout);
    CHECK(timeout.state == P4_MP_START_IDLE);

    p4_mp_start_barrier_init(&early);
    CHECK(p4_mp_start_barrier_begin(
              &early, TOKEN, 0U, HOLD_MS, TIMEOUT_MS) == P4_MP_OK);
    CHECK(p4_mp_start_barrier_observe_ready(
              &early, (uint16_t)(TOKEN + 1U), 1U,
              HOLD_MS, TIMEOUT_MS) == P4_MP_WRONG_SESSION);
}

static void test_wired_stream_boundary(void)
{
    p4_mp_wired_transport_info_t info;
    CHECK(p4_mp_wired_transport_info(
              P4_MP_WIRED_TRANSPORT_UART_DIRECT, &info));
    CHECK(!info.requires_host_relay && !info.console_sources_vbus);
    CHECK(!info.console_is_usb_device);
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

    uint8_t join_payload[P4_MP_JOIN_PAYLOAD_BYTES];
    encode_test_join(join_payload, P4_MP_PLAYER_SLOT_ANY, 1U);
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t length = encode_packet(
        P4_MP_PACKET_JOIN, SESSION_ID, CLIENT_ID, 7U,
        join_payload, sizeof(join_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              &host, route, 1000U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_JOIN_REQUEST);
    CHECK(event.peer_id == CLIENT_ID && event.route_id == route);
    CHECK(event.player_slot == P4_MP_PLAYER_SLOT_ANY);
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

    uint8_t game_message[P4_MP_GAME_MESSAGE_MAX_BYTES];
    for (size_t index = 0U; index < sizeof(game_message); ++index) {
        game_message[index] = (uint8_t)(index + 1U);
    }
    length = encode_packet(
        P4_MP_PACKET_GAME_MESSAGE, SESSION_ID, CLIENT_ID, 9U,
        game_message, sizeof(game_message), datagram);
    CHECK(p4_mp_session_receive(
              &host, route, 1150U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_GAME_MESSAGE);
    CHECK(event.player_slot == 1U);
    CHECK(event.packet.payload_length == sizeof(game_message));
    CHECK(memcmp(event.packet.payload, game_message,
                 sizeof(game_message)) == 0);
    size_t ignored_length = 0U;
    CHECK(p4_mp_packet_encode(
              P4_MP_PACKET_GAME_MESSAGE, SESSION_ID, CLIENT_ID, 10U, 0U,
              NULL, 0U, datagram, sizeof(datagram), &ignored_length) ==
          P4_MP_BAD_LENGTH);

    input_payload[21] = 1U;
    length = encode_packet(
        P4_MP_PACKET_INPUT, SESSION_ID, CLIENT_ID, 10U,
        input_payload, sizeof(input_payload), datagram);
    CHECK(p4_mp_session_receive(
              &host, route, 1200U, datagram, length, &event) == P4_MP_BAD_FLAGS);

    const uint8_t leave_payload[2] = {0, 0};
    length = encode_packet(
        P4_MP_PACKET_LEAVE, SESSION_ID, CLIENT_ID, 11U,
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
    uint8_t join_payload[P4_MP_JOIN_PAYLOAD_BYTES];
    encode_test_join(join_payload, slot, peer_id);
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

    uint8_t join_payload[P4_MP_JOIN_PAYLOAD_BYTES];
    encode_test_join(join_payload, P4_MP_PLAYER_SLOT_ANY, 5U);
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
    CHECK(client.peers[0].peer_id == 1U);
    CHECK(client.peers[0].player_slot == 0U);

    uint8_t accept_payload[P4_MP_ACCEPT_PAYLOAD_BYTES];
    encode_test_accept(accept_payload, 1U);
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t length = encode_packet(
        P4_MP_PACKET_ACCEPT, 200U, 1U, 8U,
        accept_payload, sizeof(accept_payload), datagram);
    p4_mp_event_t event;
    CHECK(p4_mp_session_receive(
              &client, 44U, 150U, datagram, length, &event) == P4_MP_OK);
    CHECK(event.type == P4_MP_EVENT_ACCEPTED);
    CHECK(event.player_slot == 0U);
    CHECK(client.state == P4_MP_SESSION_CONNECTED);
    CHECK(p4_mp_session_peer_count(&client) == 1U);
    CHECK(client.peers[0].player_slot == 0U);
    CHECK(!p4_mp_session_tick(&client, 3150U, &event));
    CHECK(p4_mp_session_tick(&client, 3151U, &event));
    CHECK(event.type == P4_MP_EVENT_PEER_TIMED_OUT);
    CHECK(event.neutralize_player && event.player_slot == 0U);
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

static void test_ble_fragment_roundtrip(void)
{
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES];
    for (size_t index = 0U; index < sizeof(payload); ++index) {
        payload[index] = (uint8_t)(index * 17U + 3U);
    }
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t datagram_length = encode_packet(
        P4_MP_PACKET_OFFER, 77U, 11U, 9U,
        payload, (uint16_t)sizeof(payload), datagram);
    static const size_t capacities[] = {20U, 64U, 244U, 512U};
    for (size_t pass = 0U;
         pass < sizeof(capacities) / sizeof(capacities[0]); ++pass) {
        p4_mp_ble_reassembler_t reassembler;
        p4_mp_ble_reassembler_init(&reassembler);
        size_t offset = 0U;
        while (offset < datagram_length) {
            uint8_t fragment[P4_MP_BLE_MAX_FRAGMENT_BYTES];
            size_t fragment_length = 0U;
            size_t next_offset = 0U;
            CHECK(p4_mp_ble_fragment_encode(
                      datagram, datagram_length, 5U, offset,
                      capacities[pass], fragment, sizeof(fragment),
                      &fragment_length, &next_offset) == P4_MP_OK);
            CHECK(next_offset > offset);
            const uint8_t *ready = NULL;
            size_t ready_length = 0U;
            const p4_mp_ble_fragment_result_t result =
                p4_mp_ble_reassembler_consume(
                    &reassembler, fragment, fragment_length,
                    &ready, &ready_length);
            if (next_offset == datagram_length) {
                CHECK(result == P4_MP_BLE_FRAGMENT_DATAGRAM_READY);
                CHECK(ready_length == datagram_length);
                CHECK(memcmp(ready, datagram, datagram_length) == 0);
            } else {
                CHECK(result == P4_MP_BLE_FRAGMENT_NEED_MORE);
            }
            offset = next_offset;
        }
        CHECK(reassembler.completed_frames == 1U);
        CHECK(reassembler.dropped_frames == 0U);
    }

    const uint8_t address[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    CHECK(p4_mp_ble_route_id(address) ==
          (P4_MP_BLE_ROUTE_PREFIX | UINT64_C(0x060504030201)));
    CHECK(p4_mp_ble_route_id(NULL) == 0U);
}

static void test_ble_lobby_beacon(void)
{
    uint8_t compatibility[P4_MP_SHA256_BYTES];
    fill_hash(compatibility, UINT8_C(0x31));
    const uint16_t token = p4_mp_ble_game_token(compatibility);
    CHECK(token != 0U);
    CHECK(p4_mp_ble_game_token(NULL) == 0U);

    const p4_mp_ble_lobby_beacon_t expected = {
        .session_id = UINT32_C(0x89abcdef),
        .game_token = token,
        .players_present = 1U,
        .player_capacity = 2U,
    };
    uint8_t encoded[P4_MP_BLE_LOBBY_BEACON_BYTES];
    CHECK(p4_mp_ble_lobby_beacon_encode(&expected, encoded) == P4_MP_OK);
    p4_mp_ble_lobby_beacon_t decoded;
    CHECK(p4_mp_ble_lobby_beacon_decode(
              encoded, sizeof(encoded), &decoded) == P4_MP_OK);
    CHECK(decoded.session_id == expected.session_id);
    CHECK(decoded.game_token == expected.game_token);
    CHECK(decoded.players_present == expected.players_present);
    CHECK(decoded.player_capacity == expected.player_capacity);

    encoded[1] |= UINT8_C(0x80);
    CHECK(p4_mp_ble_lobby_beacon_decode(
              encoded, sizeof(encoded), &decoded) == P4_MP_BAD_FLAGS);
    encoded[1] = P4_MP_BLE_LOBBY_FLAG_OPEN;
    encoded[9] = 1U;
    CHECK(p4_mp_ble_lobby_beacon_decode(
              encoded, sizeof(encoded), &decoded) == P4_MP_BAD_IDENTITY);
    CHECK(p4_mp_ble_lobby_beacon_decode(
              encoded, sizeof(encoded) - 1U, &decoded) == P4_MP_BAD_LENGTH);
}

static void test_ble_fragment_rejects_malformed_input(void)
{
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES];
    memset(payload, 0xa5, sizeof(payload));
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t datagram_length = encode_packet(
        P4_MP_PACKET_OFFER, 88U, 12U, 3U,
        payload, (uint16_t)sizeof(payload), datagram);
    uint8_t first[P4_MP_BLE_MAX_FRAGMENT_BYTES];
    size_t first_length = 0U;
    size_t next = 0U;
    CHECK(p4_mp_ble_fragment_encode(
              datagram, datagram_length, 7U, 0U, 40U,
              first, sizeof(first), &first_length, &next) == P4_MP_OK);
    p4_mp_ble_reassembler_t reassembler;
    p4_mp_ble_reassembler_init(&reassembler);
    const uint8_t *ready = NULL;
    size_t ready_length = 0U;
    CHECK(p4_mp_ble_reassembler_consume(
              &reassembler, first, first_length, &ready, &ready_length) ==
          P4_MP_BLE_FRAGMENT_NEED_MORE);

    uint8_t continuation[P4_MP_BLE_MAX_FRAGMENT_BYTES];
    size_t continuation_length = 0U;
    size_t ignored = 0U;
    CHECK(p4_mp_ble_fragment_encode(
              datagram, datagram_length, 7U, next, 40U,
              continuation, sizeof(continuation),
              &continuation_length, &ignored) == P4_MP_OK);
    continuation[8] ^= 1U;
    CHECK(p4_mp_ble_reassembler_consume(
              &reassembler, continuation, continuation_length,
              &ready, &ready_length) == P4_MP_BLE_FRAGMENT_DROPPED);
    CHECK(!reassembler.active);

    first[0] = 'X';
    CHECK(p4_mp_ble_reassembler_consume(
              &reassembler, first, first_length, &ready, &ready_length) ==
          P4_MP_BLE_FRAGMENT_DROPPED);
    CHECK(reassembler.last_packet_status == P4_MP_BAD_MAGIC);
    CHECK(p4_mp_ble_reassembler_consume(
              &reassembler, first, 3U, &ready, &ready_length) ==
          P4_MP_BLE_FRAGMENT_DROPPED);
    CHECK(p4_mp_ble_reassembler_consume(
              NULL, first, first_length, &ready, &ready_length) ==
          P4_MP_BLE_FRAGMENT_INVALID_ARGUMENT);
}

int main(void)
{
    test_packet_codec();
    test_lobby_codec();
    test_synchronized_start_barrier();
    test_wired_stream_boundary();
    test_host_session();
    test_capacity_timeout_and_disconnect();
    test_client_session();
    test_ble_lobby_beacon();
    test_ble_fragment_roundtrip();
    test_ble_fragment_rejects_malformed_input();
    test_bounded_decode_fuzz();
    if (s_failures != 0) {
        fprintf(stderr, "%d P4 multiplayer test failure(s)\n", s_failures);
        return EXIT_FAILURE;
    }
    puts("p4 multiplayer tests passed");
    return EXIT_SUCCESS;
}
