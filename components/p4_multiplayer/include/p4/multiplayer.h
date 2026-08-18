// SPDX-License-Identifier: MIT

#ifndef P4_MULTIPLAYER_H
#define P4_MULTIPLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    P4_MP_VERSION = 1,
    P4_MP_HEADER_BYTES = 28,
    P4_MP_TRAILER_BYTES = 4,
    P4_MP_MAX_PAYLOAD_BYTES = 1024,
    P4_MP_MAX_DATAGRAM_BYTES =
        P4_MP_HEADER_BYTES + P4_MP_MAX_PAYLOAD_BYTES + P4_MP_TRAILER_BYTES,
    P4_MP_MAX_PLAYERS = 4,
    P4_MP_MAX_REMOTE_PEERS = P4_MP_MAX_PLAYERS - 1,
    P4_MP_DEFAULT_TIMEOUT_MS = 3000,
    P4_MP_INPUT_PAYLOAD_BYTES = 24,
};

typedef enum {
    P4_MP_WIRED_TRANSPORT_NONE = 0,
    P4_MP_WIRED_TRANSPORT_UART_RELAY,
    P4_MP_WIRED_TRANSPORT_USB2_DEVICE_RELAY,
} p4_mp_wired_transport_kind_t;

typedef struct {
    p4_mp_wired_transport_kind_t kind;
    const char *name;
    bool requires_host_relay;
    bool console_is_usb_device;
    bool console_sources_vbus;
} p4_mp_wired_transport_info_t;

bool p4_mp_wired_transport_info(
    p4_mp_wired_transport_kind_t kind,
    p4_mp_wired_transport_info_t *info_out);

typedef enum {
    P4_MP_PACKET_DISCOVER = 1,
    P4_MP_PACKET_OFFER = 2,
    P4_MP_PACKET_JOIN = 3,
    P4_MP_PACKET_ACCEPT = 4,
    P4_MP_PACKET_INPUT = 5,
    P4_MP_PACKET_STATE_HASH = 6,
    P4_MP_PACKET_PING = 7,
    P4_MP_PACKET_PONG = 8,
    P4_MP_PACKET_LEAVE = 9,
    P4_MP_PACKET_REJECT = 10,
} p4_mp_packet_type_t;

typedef enum {
    P4_MP_OK = 0,
    P4_MP_INVALID_ARGUMENT,
    P4_MP_TOO_SHORT,
    P4_MP_BAD_MAGIC,
    P4_MP_BAD_VERSION,
    P4_MP_BAD_TYPE,
    P4_MP_BAD_FLAGS,
    P4_MP_BAD_LENGTH,
    P4_MP_BAD_CRC,
    P4_MP_BAD_IDENTITY,
    P4_MP_WRONG_SESSION,
    P4_MP_UNKNOWN_PEER,
    P4_MP_ROUTE_MISMATCH,
    P4_MP_REPLAYED,
    P4_MP_FULL,
    P4_MP_INVALID_STATE,
} p4_mp_status_t;

typedef struct {
    p4_mp_packet_type_t type;
    uint16_t flags;
    uint32_t session_id;
    uint32_t peer_id;
    uint32_t sequence;
    uint32_t ack;
    const uint8_t *payload;
    uint16_t payload_length;
} p4_mp_packet_view_t;

uint32_t p4_mp_crc32(const uint8_t *bytes, size_t length);

p4_mp_status_t p4_mp_packet_encode(
    p4_mp_packet_type_t type,
    uint32_t session_id,
    uint32_t peer_id,
    uint32_t sequence,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

p4_mp_status_t p4_mp_packet_decode(
    const uint8_t *datagram,
    size_t datagram_length,
    p4_mp_packet_view_t *packet_out);

/**
 * Allocation-free framing for noisy/chunked byte streams such as UART and
 * USB CDC/vendor endpoints. The decoder searches for P4MP magic, bounds the
 * advertised payload before buffering it, and validates the complete packet
 * CRC before returning a datagram to the session core.
 */
typedef struct {
    uint8_t datagram[P4_MP_MAX_DATAGRAM_BYTES];
    size_t buffered_bytes;
    size_t expected_bytes;
    uint32_t discarded_bytes;
    uint32_t dropped_frames;
    p4_mp_status_t last_packet_status;
    uint8_t magic_bytes;
} p4_mp_stream_decoder_t;

typedef enum {
    P4_MP_STREAM_NEED_MORE = 0,
    P4_MP_STREAM_FRAME_READY,
    P4_MP_STREAM_FRAME_DROPPED,
    P4_MP_STREAM_INVALID_ARGUMENT,
} p4_mp_stream_result_t;

void p4_mp_stream_decoder_init(p4_mp_stream_decoder_t *decoder);

p4_mp_stream_result_t p4_mp_stream_consume(
    p4_mp_stream_decoder_t *decoder,
    const uint8_t *bytes,
    size_t bytes_length,
    size_t *bytes_consumed,
    uint8_t *datagram_out,
    size_t datagram_capacity,
    size_t *datagram_length);

typedef struct {
    uint32_t tick;
    uint32_t buttons;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    uint8_t dpad;
    uint8_t flags;
} p4_mp_input_t;

void p4_mp_input_encode(
    const p4_mp_input_t *input,
    uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES]);

p4_mp_status_t p4_mp_input_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_input_t *input_out);

typedef enum {
    P4_MP_ROLE_NONE = 0,
    P4_MP_ROLE_HOST,
    P4_MP_ROLE_CLIENT,
} p4_mp_role_t;

typedef enum {
    P4_MP_SESSION_IDLE = 0,
    P4_MP_SESSION_HOSTING,
    P4_MP_SESSION_JOINING,
    P4_MP_SESSION_CONNECTED,
    P4_MP_SESSION_CLOSED,
} p4_mp_session_state_t;

typedef struct {
    uint32_t peer_id;
    uint64_t route_id;
    uint64_t last_seen_ms;
    uint32_t last_sequence;
    uint8_t player_slot;
    bool connected;
} p4_mp_peer_t;

typedef struct {
    p4_mp_role_t role;
    p4_mp_session_state_t state;
    uint32_t session_id;
    uint32_t self_peer_id;
    uint32_t next_sequence;
    uint32_t timeout_ms;
    p4_mp_peer_t peers[P4_MP_MAX_REMOTE_PEERS];
} p4_mp_session_t;

typedef enum {
    P4_MP_EVENT_NONE = 0,
    P4_MP_EVENT_JOIN_REQUEST,
    P4_MP_EVENT_ACCEPTED,
    P4_MP_EVENT_INPUT,
    P4_MP_EVENT_STATE_HASH,
    P4_MP_EVENT_PING,
    P4_MP_EVENT_PONG,
    P4_MP_EVENT_REJECTED,
    P4_MP_EVENT_PEER_LEFT,
    P4_MP_EVENT_PEER_TIMED_OUT,
    P4_MP_EVENT_ROUTE_DISCONNECTED,
} p4_mp_event_type_t;

typedef struct {
    p4_mp_event_type_t type;
    uint32_t peer_id;
    uint64_t route_id;
    uint8_t player_slot;
    bool neutralize_player;
    p4_mp_packet_view_t packet;
} p4_mp_event_t;

void p4_mp_session_init(p4_mp_session_t *session);

p4_mp_status_t p4_mp_session_host_start(
    p4_mp_session_t *session,
    uint32_t session_id,
    uint32_t self_peer_id,
    uint32_t timeout_ms);

p4_mp_status_t p4_mp_session_client_start(
    p4_mp_session_t *session,
    uint32_t session_id,
    uint32_t self_peer_id,
    uint32_t host_peer_id,
    uint64_t host_route_id,
    uint64_t now_ms,
    uint32_t timeout_ms);

p4_mp_status_t p4_mp_session_accept_peer(
    p4_mp_session_t *session,
    uint32_t peer_id,
    uint64_t route_id,
    uint8_t player_slot,
    uint32_t initial_sequence,
    uint64_t now_ms);

p4_mp_status_t p4_mp_session_encode(
    p4_mp_session_t *session,
    p4_mp_packet_type_t type,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

p4_mp_status_t p4_mp_session_receive(
    p4_mp_session_t *session,
    uint64_t route_id,
    uint64_t now_ms,
    const uint8_t *datagram,
    size_t datagram_length,
    p4_mp_event_t *event_out);

bool p4_mp_session_tick(
    p4_mp_session_t *session,
    uint64_t now_ms,
    p4_mp_event_t *event_out);

bool p4_mp_session_route_disconnected(
    p4_mp_session_t *session,
    uint64_t route_id,
    p4_mp_event_t *event_out);

size_t p4_mp_session_peer_count(const p4_mp_session_t *session);

#ifdef __cplusplus
}
#endif

#endif
