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
    P4_MP_LOBBY_SCHEMA = 2,
    P4_MP_GAME_ID_BYTES = 32,
    P4_MP_SHA256_BYTES = 32,
    P4_MP_GAME_SETTINGS_BYTES = 8,
    P4_MP_COMPATIBILITY_MATERIAL_BYTES = 80,
    P4_MP_OFFER_PAYLOAD_BYTES = 128,
    P4_MP_JOIN_PAYLOAD_BYTES = 40,
    P4_MP_ACCEPT_PAYLOAD_BYTES = 24,
    P4_MP_START_PAYLOAD_BYTES = 8,
    P4_MP_START_SCHEMA = 1,
    P4_MP_GAME_MESSAGE_MAX_BYTES = 64,
    /** Dedicated engine checkpoints fit one 1024-byte transport datagram. */
    P4_MP_CHECKPOINT_MAX_DATAGRAM_BYTES = 1024,
    P4_MP_CHECKPOINT_MAX_PAYLOAD_BYTES = P4_MP_CHECKPOINT_MAX_DATAGRAM_BYTES -
        P4_MP_HEADER_BYTES - P4_MP_TRAILER_BYTES,
    P4_MP_PLAYER_SLOT_ANY = 0xff,
};

typedef enum {
    P4_MP_WIRED_TRANSPORT_NONE = 0,
    P4_MP_WIRED_TRANSPORT_UART_DIRECT,
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
    /** Transport-neutral payload copied to a connected native cartridge. */
    P4_MP_PACKET_GAME_MESSAGE = 11,
    P4_MP_PACKET_ACCESSORY = 12,
    /** Negotiated engine checkpoint path; never a native cartridge message.
     * The generic session receiver rejects this type without changing state.
     * The owning OS adapter must validate the negotiated engine protocol,
     * session, peer, exact route and transfer attempt before accepting it. */
    P4_MP_PACKET_CHECKPOINT = 13,
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
    P4_MP_GAME_MODE_NONE = 0,
    P4_MP_GAME_MODE_HOST_AUTHORITATIVE = 1,
    P4_MP_GAME_MODE_LOCKSTEP = 2,
} p4_mp_game_mode_t;

/**
 * Fixed lobby identity advertised by a host. compatibility_sha256 is supplied
 * by the OS and covers the exact game ID, Game API version, package/content
 * hash, game protocol, simulation rate, input delay, capacity, and mode. It
 * deliberately excludes mutable player count, the per-session seed, and the
 * host-authored game setup.
 */
typedef struct {
    p4_mp_game_mode_t mode;
    uint8_t game_api_major;
    uint8_t game_api_minor;
    uint8_t players_present;
    uint8_t player_capacity;
    uint8_t input_delay_tics;
    uint16_t tick_rate_hz;
    uint16_t game_protocol;
    uint64_t session_seed;
    char game_id[P4_MP_GAME_ID_BYTES];
    uint8_t content_sha256[P4_MP_SHA256_BYTES];
    uint8_t compatibility_sha256[P4_MP_SHA256_BYTES];
    /** Host-authored, game-specific setup; ignored by compatibility matching. */
    uint8_t game_settings[P4_MP_GAME_SETTINGS_BYTES];
} p4_mp_lobby_offer_t;

typedef struct {
    uint8_t requested_player_slot;
    uint32_t join_nonce;
    uint8_t compatibility_sha256[P4_MP_SHA256_BYTES];
} p4_mp_lobby_join_t;

typedef struct {
    uint8_t assigned_player_slot;
    uint8_t player_count;
    uint8_t input_delay_tics;
    uint32_t start_tic;
    uint64_t session_seed;
    /** Exact host setup accepted for this session. */
    uint8_t game_settings[P4_MP_GAME_SETTINGS_BYTES];
} p4_mp_lobby_accept_t;

/** Build the canonical fixed bytes the OS hashes into compatibility_sha256. */
p4_mp_status_t p4_mp_lobby_compatibility_material(
    const p4_mp_lobby_offer_t *offer,
    uint8_t material[P4_MP_COMPATIBILITY_MATERIAL_BYTES]);

p4_mp_status_t p4_mp_lobby_offer_encode(
    const p4_mp_lobby_offer_t *offer,
    uint8_t payload[P4_MP_OFFER_PAYLOAD_BYTES]);

p4_mp_status_t p4_mp_lobby_offer_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_offer_t *offer_out);

p4_mp_status_t p4_mp_lobby_join_encode(
    const p4_mp_lobby_join_t *join,
    uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES]);

p4_mp_status_t p4_mp_lobby_join_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_join_t *join_out);

p4_mp_status_t p4_mp_lobby_accept_encode(
    const p4_mp_lobby_accept_t *accept,
    uint8_t payload[P4_MP_ACCEPT_PAYLOAD_BYTES]);

p4_mp_status_t p4_mp_lobby_accept_decode(
    const uint8_t *payload,
    size_t payload_length,
    p4_mp_lobby_accept_t *accept_out);

bool p4_mp_lobby_offers_compatible(
    const p4_mp_lobby_offer_t *local,
    const p4_mp_lobby_offer_t *remote);

bool p4_mp_lobby_join_matches_offer(
    const p4_mp_lobby_offer_t *offer,
    const p4_mp_lobby_join_t *join);

/**
 * Symmetric pre-game launch barrier. A READY control received from either
 * peer automatically arms the local side, so one player can start a match
 * without racing a second button press. Both sides then hold in the launcher
 * for the same interval before handing hardware ownership to the game.
 */
typedef enum {
    P4_MP_START_IDLE = 0,
    P4_MP_START_WAITING,
    P4_MP_START_ARMED,
    P4_MP_START_DUE,
    P4_MP_START_TIMED_OUT,
} p4_mp_start_state_t;

typedef struct {
    p4_mp_start_state_t state;
    uint16_t token;
    uint32_t hold_ms;
    uint32_t timeout_ms;
    uint64_t started_ms;
    uint64_t launch_at_ms;
} p4_mp_start_barrier_t;

void p4_mp_start_barrier_init(p4_mp_start_barrier_t *barrier);

p4_mp_status_t p4_mp_start_barrier_begin(
    p4_mp_start_barrier_t *barrier,
    uint16_t token,
    uint64_t now_ms,
    uint32_t hold_ms,
    uint32_t timeout_ms);

p4_mp_status_t p4_mp_start_barrier_observe_ready(
    p4_mp_start_barrier_t *barrier,
    uint16_t token,
    uint64_t now_ms,
    uint32_t hold_ms,
    uint32_t timeout_ms);

p4_mp_start_state_t p4_mp_start_barrier_poll(
    p4_mp_start_barrier_t *barrier,
    uint64_t now_ms);

uint32_t p4_mp_start_barrier_remaining_ms(
    const p4_mp_start_barrier_t *barrier,
    uint64_t now_ms);

void p4_mp_start_barrier_cancel(p4_mp_start_barrier_t *barrier);

p4_mp_status_t p4_mp_start_ready_encode(
    uint16_t token,
    uint8_t payload[P4_MP_START_PAYLOAD_BYTES]);

p4_mp_status_t p4_mp_start_ready_decode(
    const uint8_t *payload,
    size_t payload_length,
    uint16_t *token_out);

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
    P4_MP_JOIN_REJECT = 0,
    P4_MP_JOIN_NEW,
    P4_MP_JOIN_RETRY,
} p4_mp_join_admission_t;

/**
 * Keep ACCEPT delivery retryable after closing admission or starting a game
 * barrier. Only an already-admitted peer on its exact route may retry while
 * accepting_new is false. Callers must still validate the offered game and
 * pass the packet through session_receive for session, route and replay checks.
 * A RETRY resends ACCEPT without reconfiguring the launch or its active barrier.
 */
p4_mp_join_admission_t p4_mp_session_join_admission(
    const p4_mp_session_t *session,
    uint32_t peer_id,
    uint64_t route_id,
    bool accepting_new);

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
    P4_MP_EVENT_GAME_MESSAGE,
    /** Dedicated opt-in ingress; never emitted by generic session_receive. */
    P4_MP_EVENT_CHECKPOINT,
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

/** Complete a game-specific admission after its credential/content checks.
 * Only the exact pending host identity/route with a fresh sequence is accepted. */
p4_mp_status_t p4_mp_session_accept_host(
    p4_mp_session_t *session, uint32_t host_peer_id, uint64_t route_id,
    uint32_t initial_sequence, uint64_t now_ms);

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

/** Accept one prevalidated engine checkpoint from an already connected peer.
 * The owning adapter MUST first validate negotiated engine protocol, content,
 * admission attempt and checkpoint-transfer semantics without session mutation.
 * This explicit ingress then checks framing, type, connected session/peer,
 * exact route and the common receive sequence before refreshing liveness.
 * It does not admit a joining peer or validate the engine-specific payload.
 * Failures leave the session unchanged; success emits only CHECKPOINT.
 * The event borrows datagram bytes for the duration of the caller's handling. */
p4_mp_status_t p4_mp_session_receive_checkpoint(
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
