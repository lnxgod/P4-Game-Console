// SPDX-License-Identifier: MIT
#include "p4/multiplayer.h"
#include "p4/multiplayer_group.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { SESSION = 7, HOST = 11, GUEST = 22, ROUTE = 33 };

static size_t encode(p4_mp_session_t *session, p4_mp_packet_type_t type,
                     const uint8_t *payload, uint16_t payload_length,
                     uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES])
{
    size_t length = 0;
    assert(p4_mp_session_encode(session, type, 0, payload, payload_length,
                               wire, P4_MP_MAX_DATAGRAM_BYTES, &length) == P4_MP_OK);
    return length;
}

static void dropped_accept(bool group_start)
{
    p4_mp_session_t host, guest;
    assert(p4_mp_session_host_start(&host, SESSION, HOST, 3000) == P4_MP_OK);
    assert(p4_mp_session_client_start(&guest, SESSION, GUEST, HOST, ROUTE,
                                      0, 3000) == P4_MP_OK);
    p4_mp_lobby_join_t join = {
        .requested_player_slot = 1, .join_nonce = 123,
        .compatibility_sha256 = {1},
    };
    uint8_t request[P4_MP_JOIN_PAYLOAD_BYTES], accept[P4_MP_ACCEPT_PAYLOAD_BYTES];
    assert(p4_mp_lobby_join_encode(&join, request) == P4_MP_OK);
    const p4_mp_lobby_accept_t accepted = {
        .assigned_player_slot = 1, .player_count = 2,
        .input_delay_tics = 2, .session_seed = 123,
    };
    assert(p4_mp_lobby_accept_encode(&accepted, accept) == P4_MP_OK);
    uint8_t wire[P4_MP_MAX_DATAGRAM_BYTES];
    size_t length = encode(&guest, P4_MP_PACKET_JOIN, request, sizeof(request), wire);
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, true) == P4_MP_JOIN_NEW);
    p4_mp_event_t event;
    assert(p4_mp_session_receive(&host, ROUTE, 10, wire, length, &event) == P4_MP_OK);
    assert(event.type == P4_MP_EVENT_JOIN_REQUEST);
    assert(p4_mp_session_accept_peer(&host, GUEST, ROUTE, 1,
                                     event.packet.sequence, 10) == P4_MP_OK);
    /* The transport reports success, but this first ACCEPT never reaches the
     * guest. The host has already closed ordinary two-player admission. */
    (void)encode(&host, P4_MP_PACKET_ACCEPT, accept, sizeof(accept), wire);
    assert(host.state == P4_MP_SESSION_CONNECTED);
    assert(guest.state == P4_MP_SESSION_JOINING);

    p4_mp_group_start_t barrier = {0}, guest_barrier = {0};
    uint8_t control[P4_MP_GROUP_BYTES];
    const uint16_t token = p4_mp_group_token(SESSION, accepted.session_seed);
    if (group_start) {
        /* Arena's host can press Start before the guest receives ACCEPT.
         * Its PROPOSE cannot establish a pending guest's session. */
        assert(p4_mp_group_begin(&barrier, token, 2, 20));
        assert(p4_mp_group_poll(&barrier, 20, control));
        length = encode(&host, P4_MP_GROUP_PACKET_TYPE, control, sizeof(control), wire);
        assert(p4_mp_session_receive(&guest, ROUTE, 20, wire, length, &event) ==
               P4_MP_UNKNOWN_PEER);
    }
    const p4_mp_group_start_t barrier_before = barrier;
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, false) == P4_MP_JOIN_RETRY);
    assert(p4_mp_session_join_admission(&host, GUEST + 1, ROUTE + 1, false) ==
           P4_MP_JOIN_REJECT);
    length = encode(&guest, P4_MP_PACKET_JOIN, request, sizeof(request), wire);
    assert(p4_mp_session_receive(&host, ROUTE, 120, wire, length, &event) == P4_MP_OK);
    assert(event.type == P4_MP_EVENT_JOIN_REQUEST && event.player_slot == 1);
    assert(p4_mp_session_accept_peer(&host, GUEST, ROUTE, 1,
                                     event.packet.sequence, 120) == P4_MP_OK);
    /* RETRY only resends ACCEPT: do not rebuild launch state/cancel barrier. */
    length = encode(&host, P4_MP_PACKET_ACCEPT, accept, sizeof(accept), wire);
    assert(p4_mp_session_receive(&guest, ROUTE, 120, wire, length, &event) == P4_MP_OK);
    assert(event.type == P4_MP_EVENT_ACCEPTED && event.player_slot == 0);
    p4_mp_lobby_accept_t retried_accept;
    assert(p4_mp_lobby_accept_decode(event.packet.payload, event.packet.payload_length,
                                     &retried_accept) == P4_MP_OK);
    assert(retried_accept.assigned_player_slot == 1);
    assert(guest.state == P4_MP_SESSION_CONNECTED);
    assert(p4_mp_session_peer_count(&host) == 1);
    assert(memcmp(&barrier, &barrier_before, sizeof(barrier)) == 0);

    if (group_start) {
        assert(p4_mp_group_poll(&barrier, 140, control));
        length = encode(&host, P4_MP_GROUP_PACKET_TYPE, control, sizeof(control), wire);
        assert(p4_mp_session_receive(&guest, ROUTE, 140, wire, length, &event) == P4_MP_OK);
        assert(p4_mp_group_receive(&guest_barrier, 1, 0, token,
                                  event.packet.payload, event.packet.payload_length, 140));
        assert(p4_mp_group_poll(&guest_barrier, 140, control));
        length = encode(&guest, P4_MP_GROUP_PACKET_TYPE, control, sizeof(control), wire);
        assert(p4_mp_session_receive(&host, ROUTE, 140, wire, length, &event) == P4_MP_OK);
        assert(p4_mp_group_receive(&barrier, 0, 1, token,
                                  event.packet.payload, event.packet.payload_length, 140));
        assert(barrier.phase == P4_MP_GROUP_COMMITTED);
        assert(p4_mp_group_poll(&barrier, 160, control));
        length = encode(&host, P4_MP_GROUP_PACKET_TYPE, control, sizeof(control), wire);
        assert(p4_mp_session_receive(&guest, ROUTE, 160, wire, length, &event) == P4_MP_OK);
        assert(p4_mp_group_receive(&guest_barrier, 1, 0, token,
                                  event.packet.payload, event.packet.payload_length, 160));
        (void)p4_mp_group_poll(&barrier, 1200, control);
        (void)p4_mp_group_poll(&guest_barrier, 1200, control);
        assert(barrier.phase == P4_MP_GROUP_DUE && guest_barrier.phase == P4_MP_GROUP_DUE);
    }
}

static void admission_bounds(void)
{
    p4_mp_session_t host;
    assert(p4_mp_session_host_start(&host, SESSION, HOST, 3000) == P4_MP_OK);
    assert(p4_mp_session_join_admission(NULL, GUEST, ROUTE, true) == P4_MP_JOIN_REJECT);
    assert(p4_mp_session_join_admission(&host, 0, ROUTE, true) == P4_MP_JOIN_REJECT);
    assert(p4_mp_session_join_admission(&host, GUEST, 0, true) == P4_MP_JOIN_REJECT);
    assert(p4_mp_session_join_admission(&host, HOST, ROUTE, true) == P4_MP_JOIN_REJECT);
    host.peers[0] = (p4_mp_peer_t){.peer_id = GUEST, .route_id = ROUTE};
    /* A pending JOIN is not admitted and cannot enter after Start. */
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, false) == P4_MP_JOIN_REJECT);
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, true) == P4_MP_JOIN_NEW);
    assert(p4_mp_session_accept_peer(&host, GUEST, ROUTE, 1, 1, 0) == P4_MP_OK);
    for (unsigned open = 0; open < 2; ++open) {
        assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, open != 0) == P4_MP_JOIN_RETRY);
        assert(p4_mp_session_join_admission(&host, GUEST, ROUTE + 1, open != 0) == P4_MP_JOIN_REJECT);
        assert(p4_mp_session_join_admission(&host, GUEST + 1, ROUTE, open != 0) == P4_MP_JOIN_REJECT);
    }
    host.state = P4_MP_SESSION_CLOSED;
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, true) == P4_MP_JOIN_REJECT);
    host.state = P4_MP_SESSION_CONNECTED;
    host.role = P4_MP_ROLE_CLIENT;
    assert(p4_mp_session_join_admission(&host, GUEST, ROUTE, true) == P4_MP_JOIN_REJECT);
}

int main(void)
{
    dropped_accept(false);
    dropped_accept(true);
    admission_bounds();
    puts("PASS: lost ACCEPT retries, closed admission, identity binding and active arena barrier");
}
