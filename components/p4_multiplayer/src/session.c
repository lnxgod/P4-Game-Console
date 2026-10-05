// SPDX-License-Identifier: MIT

#include "p4/multiplayer.h"

#include <limits.h>
#include <string.h>

static bool timeout_valid(uint32_t timeout_ms)
{
    return timeout_ms >= 250U && timeout_ms <= 60000U;
}

static bool sequence_newer(uint32_t incoming, uint32_t previous)
{
    return incoming != previous &&
        (previous == 0U || (uint32_t)(incoming - previous) < UINT32_C(0x80000000));
}

static p4_mp_peer_t *find_peer(p4_mp_session_t *session, uint32_t peer_id)
{
    if (session == NULL || peer_id == 0U) {
        return NULL;
    }
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].peer_id == peer_id) {
            return &session->peers[index];
        }
    }
    return NULL;
}

static p4_mp_peer_t *find_route(p4_mp_session_t *session, uint64_t route_id)
{
    if (session == NULL || route_id == 0U) {
        return NULL;
    }
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].peer_id != 0U &&
            session->peers[index].route_id == route_id) {
            return &session->peers[index];
        }
    }
    return NULL;
}

static p4_mp_peer_t *find_free_peer(p4_mp_session_t *session)
{
    if (session == NULL) {
        return NULL;
    }
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].peer_id == 0U) {
            return &session->peers[index];
        }
    }
    return NULL;
}

static bool slot_in_use(const p4_mp_session_t *session, uint8_t slot)
{
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].connected &&
            session->peers[index].player_slot == slot) {
            return true;
        }
    }
    return false;
}

static void refresh_host_state(p4_mp_session_t *session)
{
    if (session->role != P4_MP_ROLE_HOST) {
        return;
    }
    session->state = P4_MP_SESSION_HOSTING;
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].connected) {
            session->state = P4_MP_SESSION_CONNECTED;
            return;
        }
    }
}

static void event_clear(p4_mp_event_t *event)
{
    if (event != NULL) {
        *event = (p4_mp_event_t){0};
    }
}

static void event_from_peer(
    p4_mp_event_t *event,
    p4_mp_event_type_t type,
    const p4_mp_peer_t *peer,
    bool neutralize)
{
    *event = (p4_mp_event_t){
        .type = type,
        .peer_id = peer->peer_id,
        .route_id = peer->route_id,
        .player_slot = peer->player_slot,
        .neutralize_player = neutralize,
    };
}

void p4_mp_session_init(p4_mp_session_t *session)
{
    if (session != NULL) {
        *session = (p4_mp_session_t){0};
    }
}

p4_mp_status_t p4_mp_session_host_start(
    p4_mp_session_t *session,
    uint32_t session_id,
    uint32_t self_peer_id,
    uint32_t timeout_ms)
{
    if (session == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (session_id == 0U || self_peer_id == 0U || !timeout_valid(timeout_ms)) {
        return P4_MP_BAD_IDENTITY;
    }
    *session = (p4_mp_session_t){
        .role = P4_MP_ROLE_HOST,
        .state = P4_MP_SESSION_HOSTING,
        .session_id = session_id,
        .self_peer_id = self_peer_id,
        .next_sequence = 1U,
        .timeout_ms = timeout_ms,
    };
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_session_client_start(
    p4_mp_session_t *session,
    uint32_t session_id,
    uint32_t self_peer_id,
    uint32_t host_peer_id,
    uint64_t host_route_id,
    uint64_t now_ms,
    uint32_t timeout_ms)
{
    if (session == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (session_id == 0U || self_peer_id == 0U || host_peer_id == 0U ||
        host_peer_id == self_peer_id || host_route_id == 0U ||
        !timeout_valid(timeout_ms)) {
        return P4_MP_BAD_IDENTITY;
    }
    *session = (p4_mp_session_t){
        .role = P4_MP_ROLE_CLIENT,
        .state = P4_MP_SESSION_JOINING,
        .session_id = session_id,
        .self_peer_id = self_peer_id,
        .next_sequence = 1U,
        .timeout_ms = timeout_ms,
        .peers = {{
            .peer_id = host_peer_id,
            .route_id = host_route_id,
            .last_seen_ms = now_ms,
            .last_sequence = 0U,
            .player_slot = 0U,
            .connected = false,
        }},
    };
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_session_accept_peer(
    p4_mp_session_t *session,
    uint32_t peer_id,
    uint64_t route_id,
    uint8_t player_slot,
    uint32_t initial_sequence,
    uint64_t now_ms)
{
    if (session == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (session->role != P4_MP_ROLE_HOST ||
        (session->state != P4_MP_SESSION_HOSTING &&
         session->state != P4_MP_SESSION_CONNECTED)) {
        return P4_MP_INVALID_STATE;
    }
    if (peer_id == 0U || peer_id == session->self_peer_id || route_id == 0U ||
        player_slot == 0U || player_slot >= P4_MP_MAX_PLAYERS ||
        initial_sequence == 0U) {
        return P4_MP_BAD_IDENTITY;
    }
    p4_mp_peer_t *peer = find_peer(session, peer_id);
    if (peer != NULL) {
        if (peer->route_id != route_id) {
            return P4_MP_ROUTE_MISMATCH;
        }
        if (peer->connected) {
            return peer->player_slot == player_slot ? P4_MP_OK : P4_MP_BAD_IDENTITY;
        }
    } else {
        if (find_route(session, route_id) != NULL) {
            return P4_MP_ROUTE_MISMATCH;
        }
        peer = find_free_peer(session);
        if (peer == NULL) {
            return P4_MP_FULL;
        }
    }
    if (slot_in_use(session, player_slot)) {
        return P4_MP_FULL;
    }
    *peer = (p4_mp_peer_t){
        .peer_id = peer_id,
        .route_id = route_id,
        .last_seen_ms = now_ms,
        .last_sequence = initial_sequence,
        .player_slot = player_slot,
        .connected = true,
    };
    session->state = P4_MP_SESSION_CONNECTED;
    return P4_MP_OK;
}

p4_mp_status_t p4_mp_session_encode(
    p4_mp_session_t *session,
    p4_mp_packet_type_t type,
    uint32_t ack,
    const uint8_t *payload,
    uint16_t payload_length,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (session == NULL) {
        return P4_MP_INVALID_ARGUMENT;
    }
    if (session->role == P4_MP_ROLE_NONE || session->state == P4_MP_SESSION_IDLE ||
        session->state == P4_MP_SESSION_CLOSED || session->next_sequence == 0U) {
        return P4_MP_INVALID_STATE;
    }
    const uint32_t sequence = session->next_sequence;
    const p4_mp_status_t status = p4_mp_packet_encode(
        type, session->session_id, session->self_peer_id, sequence, ack,
        payload, payload_length, output, output_capacity, output_length);
    if (status == P4_MP_OK) {
        ++session->next_sequence;
        if (session->next_sequence == 0U) {
            session->next_sequence = 1U;
        }
    }
    return status;
}

static p4_mp_event_type_t event_type_for_packet(p4_mp_packet_type_t type)
{
    switch (type) {
        case P4_MP_PACKET_ACCEPT: return P4_MP_EVENT_ACCEPTED;
        case P4_MP_PACKET_INPUT: return P4_MP_EVENT_INPUT;
        case P4_MP_PACKET_STATE_HASH: return P4_MP_EVENT_STATE_HASH;
        case P4_MP_PACKET_PING: return P4_MP_EVENT_PING;
        case P4_MP_PACKET_PONG: return P4_MP_EVENT_PONG;
        case P4_MP_PACKET_REJECT: return P4_MP_EVENT_REJECTED;
        case P4_MP_PACKET_GAME_MESSAGE: return P4_MP_EVENT_GAME_MESSAGE;
        default: return P4_MP_EVENT_NONE;
    }
}

p4_mp_status_t p4_mp_session_receive(
    p4_mp_session_t *session,
    uint64_t route_id,
    uint64_t now_ms,
    const uint8_t *datagram,
    size_t datagram_length,
    p4_mp_event_t *event_out)
{
    if (session == NULL || event_out == NULL || route_id == 0U) {
        return P4_MP_INVALID_ARGUMENT;
    }
    event_clear(event_out);
    if (session->role == P4_MP_ROLE_NONE || session->state == P4_MP_SESSION_IDLE ||
        session->state == P4_MP_SESSION_CLOSED) {
        return P4_MP_INVALID_STATE;
    }
    p4_mp_packet_view_t packet;
    p4_mp_status_t status = p4_mp_packet_decode(
        datagram, datagram_length, &packet);
    if (status != P4_MP_OK) {
        return status;
    }
    /* Accessories have their own route/session and never refresh a player. */
    if (packet.type == P4_MP_PACKET_ACCESSORY) {
        return P4_MP_BAD_TYPE;
    }
    if (packet.type == P4_MP_PACKET_DISCOVER ||
        packet.type == P4_MP_PACKET_OFFER ||
        packet.session_id != session->session_id) {
        return P4_MP_WRONG_SESSION;
    }
    if (packet.peer_id == session->self_peer_id) {
        return P4_MP_BAD_IDENTITY;
    }

    p4_mp_lobby_join_t join = {0};
    p4_mp_lobby_accept_t accept = {0};
    if (packet.type == P4_MP_PACKET_JOIN) {
        status = p4_mp_lobby_join_decode(
            packet.payload, packet.payload_length, &join);
        if (status != P4_MP_OK) {
            return status;
        }
    } else if (packet.type == P4_MP_PACKET_ACCEPT) {
        status = p4_mp_lobby_accept_decode(
            packet.payload, packet.payload_length, &accept);
        if (status != P4_MP_OK) {
            return status;
        }
    }

    p4_mp_peer_t *peer = find_peer(session, packet.peer_id);
    if (peer == NULL && session->role == P4_MP_ROLE_HOST &&
        packet.type == P4_MP_PACKET_JOIN) {
        if (find_route(session, route_id) != NULL) {
            return P4_MP_ROUTE_MISMATCH;
        }
        peer = find_free_peer(session);
        if (peer == NULL) {
            return P4_MP_FULL;
        }
        *peer = (p4_mp_peer_t){
            .peer_id = packet.peer_id,
            .route_id = route_id,
            .last_seen_ms = now_ms,
            .last_sequence = packet.sequence,
            .player_slot = UINT8_MAX,
            .connected = false,
        };
        *event_out = (p4_mp_event_t){
            .type = P4_MP_EVENT_JOIN_REQUEST,
            .peer_id = peer->peer_id,
            .route_id = peer->route_id,
            .player_slot = join.requested_player_slot,
            .packet = packet,
        };
        return P4_MP_OK;
    }
    const bool pending_client_control =
        session->role == P4_MP_ROLE_CLIENT &&
        session->state == P4_MP_SESSION_JOINING &&
        (packet.type == P4_MP_PACKET_ACCEPT ||
         packet.type == P4_MP_PACKET_REJECT ||
         packet.type == P4_MP_PACKET_LEAVE);
    if (peer == NULL ||
        (!peer->connected && packet.type != P4_MP_PACKET_JOIN &&
         !pending_client_control)) {
        return P4_MP_UNKNOWN_PEER;
    }
    if (peer->route_id != route_id) {
        return P4_MP_ROUTE_MISMATCH;
    }
    if (!sequence_newer(packet.sequence, peer->last_sequence)) {
        return P4_MP_REPLAYED;
    }
    if (packet.type == P4_MP_PACKET_INPUT) {
        p4_mp_input_t input;
        status = p4_mp_input_decode(packet.payload, packet.payload_length, &input);
        if (status != P4_MP_OK) {
            return status;
        }
    } else if (packet.type == P4_MP_PACKET_GAME_MESSAGE &&
               (packet.payload_length == 0U ||
                packet.payload_length > P4_MP_GAME_MESSAGE_MAX_BYTES)) {
        return P4_MP_BAD_LENGTH;
    }
    peer->last_sequence = packet.sequence;
    peer->last_seen_ms = now_ms;

    if (packet.type == P4_MP_PACKET_JOIN) {
        if (session->role != P4_MP_ROLE_HOST) {
            return P4_MP_INVALID_STATE;
        }
        *event_out = (p4_mp_event_t){
            .type = P4_MP_EVENT_JOIN_REQUEST,
            .peer_id = peer->peer_id,
            .route_id = peer->route_id,
            .player_slot = join.requested_player_slot,
            .packet = packet,
        };
        return P4_MP_OK;
    }
    if (packet.type == P4_MP_PACKET_ACCEPT) {
        if (session->role != P4_MP_ROLE_CLIENT ||
            session->state != P4_MP_SESSION_JOINING) {
            return P4_MP_INVALID_STATE;
        }
        /*
         * The ACCEPT payload assigns the local client slot. This peer is the
         * remote host, whose slot was fixed to zero by client_start(). Do not
         * overwrite the host slot with the client's assignment.
         */
        peer->connected = true;
        session->state = P4_MP_SESSION_CONNECTED;
    }
    if (packet.type == P4_MP_PACKET_LEAVE) {
        event_from_peer(event_out, P4_MP_EVENT_PEER_LEFT, peer, peer->connected);
        event_out->packet = packet;
        *peer = (p4_mp_peer_t){0};
        if (session->role == P4_MP_ROLE_CLIENT) {
            session->state = P4_MP_SESSION_CLOSED;
        } else {
            refresh_host_state(session);
        }
        return P4_MP_OK;
    }

    const p4_mp_event_type_t type = event_type_for_packet(packet.type);
    if (type == P4_MP_EVENT_NONE) {
        return P4_MP_BAD_TYPE;
    }
    *event_out = (p4_mp_event_t){
        .type = type,
        .peer_id = peer->peer_id,
        .route_id = peer->route_id,
        .player_slot = peer->player_slot,
        .packet = packet,
    };
    if (type == P4_MP_EVENT_REJECTED && session->role == P4_MP_ROLE_CLIENT) {
        session->state = P4_MP_SESSION_CLOSED;
    }
    return P4_MP_OK;
}

bool p4_mp_session_tick(
    p4_mp_session_t *session,
    uint64_t now_ms,
    p4_mp_event_t *event_out)
{
    if (session == NULL || event_out == NULL ||
        session->state == P4_MP_SESSION_IDLE ||
        session->state == P4_MP_SESSION_CLOSED) {
        return false;
    }
    event_clear(event_out);
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        p4_mp_peer_t *const peer = &session->peers[index];
        if (peer->peer_id == 0U || now_ms < peer->last_seen_ms ||
            now_ms - peer->last_seen_ms <= session->timeout_ms) {
            continue;
        }
        event_from_peer(
            event_out, P4_MP_EVENT_PEER_TIMED_OUT, peer, peer->connected);
        *peer = (p4_mp_peer_t){0};
        if (session->role == P4_MP_ROLE_CLIENT) {
            session->state = P4_MP_SESSION_CLOSED;
        } else {
            refresh_host_state(session);
        }
        return true;
    }
    return false;
}

bool p4_mp_session_route_disconnected(
    p4_mp_session_t *session,
    uint64_t route_id,
    p4_mp_event_t *event_out)
{
    if (session == NULL || event_out == NULL || route_id == 0U) {
        return false;
    }
    event_clear(event_out);
    p4_mp_peer_t *const peer = find_route(session, route_id);
    if (peer == NULL) {
        return false;
    }
    event_from_peer(
        event_out, P4_MP_EVENT_ROUTE_DISCONNECTED, peer, peer->connected);
    *peer = (p4_mp_peer_t){0};
    if (session->role == P4_MP_ROLE_CLIENT) {
        session->state = P4_MP_SESSION_CLOSED;
    } else {
        refresh_host_state(session);
    }
    return true;
}

size_t p4_mp_session_peer_count(const p4_mp_session_t *session)
{
    if (session == NULL) {
        return 0U;
    }
    size_t count = 0U;
    for (size_t index = 0; index < P4_MP_MAX_REMOTE_PEERS; ++index) {
        if (session->peers[index].connected) {
            ++count;
        }
    }
    return count;
}
