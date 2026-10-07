// SPDX-License-Identifier: GPL-2.0-or-later
/* Run the production Arena adapter and P4MP sessions with a virtual clock.
 * The remote engine/transport boundaries are controlled; no private adapter
 * state, network sockets, or wall-clock sleeps are used. */
#include "doom_gc_p4mp.h"
#include "p4/doom_lockstep.h"
#include "p4/multiplayer_group.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    PLAYERS = 4,
    HEARTBEAT_MS = 500,
    READY_AFTER_MS = 45000,
    STARTUP_IDLE_MS = 30000,
    STARTUP_LIMIT_MS = 300000,
};
typedef enum {
    HOST_DELAYED, CLIENT_DELAYED, WAITING, SILENCE, REPLAY,
    MALFORMED, LOBBY_ONLY, SILENT_PEER,
} scenario_t;

static scenario_t scenario;
static uint64_t now_ms = 100, started_ms, next_heartbeat_ms;
static uint8_t local_slot;
static p4_mp_session_t peers[PLAYERS];
static p4_doom_lockstep_t remote_host;
static p4_doom_p4mp_frame_handler_t handler;
static void *handler_context;
static uint32_t remote_ack[PLAYERS];
static unsigned delivered, engine_begins, host_game_frames, lobby_replies;
static unsigned rejected_replays, malformed_heartbeats;
static unsigned waiting_presentations,failed_presentations;
static uint64_t last_lobby_reply_ms;
static bool local_engine_ready;
static uint8_t replay_frame[P4_MP_MAX_DATAGRAM_BYTES];
static size_t replay_length;

static void assert_loading(p4_doom_loading_phase_t phase)
{
    p4_doom_loading_progress_t progress={.phase=P4_DOOM_LOADING_FAILED,.completed=1,.total=1};
    const uint64_t before=now_ms;
    const unsigned before_delivered=delivered,before_begins=engine_begins;
    for (unsigned i=0;i<2;++i) {
        p4_doom_gc_get_loading_progress(&progress);
        assert(progress.phase==phase && progress.completed==0 && progress.total==0);
    }
    p4_doom_gc_get_loading_progress(NULL);
    assert(now_ms==before && delivered==before_delivered && engine_begins==before_begins);
}

/* Override the production weak presentation hook. Deadline failure must be
 * published while the attempt still exists, before its owner tears it down. */
void p4_doom_startup_status(bool waiting)
{
    assert(waiting);
    if (p4_doom_gc_failed()) {
        assert_loading(P4_DOOM_LOADING_FAILED);++failed_presentations;
    } else {
        assert_loading(P4_DOOM_LOADING_WAITING_HOST);++waiting_presentations;
    }
}

static uint64_t elapsed(void) { return now_ms - started_ms; }
static bool engines_ready(void)
{
    return (scenario == HOST_DELAYED || scenario == CLIENT_DELAYED) &&
        elapsed() >= READY_AFTER_MS;
}

int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
void vTaskDelay(unsigned ms) { now_ms += ms; }
void p4_doom_gc_engine_begin_mask(uint8_t count, uint8_t mask, uint8_t map)
{
    assert(engines_ready() && count == PLAYERS && mask == 15 && map == 1);
    ++engine_begins;
}
void D_ReceiveTic(ticcmd_t *commands, boolean *mask)
{
    assert(engines_ready() && engine_begins == 1);
    for (unsigned i = 0; i < PLAYERS; ++i) {
        assert(mask[i]);
        assert(commands[i].forwardmove == 0 && commands[i].sidemove == 0);
        assert(commands[i].angleturn == 0 && commands[i].buttons == 0);
        assert(commands[i].consistancy == 0 && commands[i].chatchar == 0);
    }
    ++delivered;
}

static size_t encode(uint8_t slot, p4_mp_packet_type_t type, uint32_t ack,
                     const uint8_t *payload, uint16_t length, uint8_t *bytes)
{
    size_t size = 0;
    assert(p4_mp_session_encode(&peers[slot], type, ack, payload, length,
        bytes, P4_MP_MAX_DATAGRAM_BYTES, &size) == P4_MP_OK);
    return size;
}

/* Establish the same admitted sessions used by the OS handoff, through public
 * JOIN/ACCEPT APIs rather than assigning session state or sequence counters. */
static void admit_peers(void)
{
    assert(p4_mp_session_host_start(&peers[0], 7, 100, 3000) == P4_MP_OK);
    for (uint8_t slot = 1; slot < PLAYERS; ++slot) {
        assert(p4_mp_session_client_start(&peers[slot], 7, 100U + slot,
            100, 1, now_ms, 3000) == P4_MP_OK);
        p4_mp_lobby_join_t join = {
            .requested_player_slot = slot, .join_nonce = 1000U + slot,
        };
        memset(join.compatibility_sha256, 0xa5, sizeof(join.compatibility_sha256));
        uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES];
        assert(p4_mp_lobby_join_encode(&join, payload) == P4_MP_OK);
        uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];
        size_t size = encode(slot, P4_MP_PACKET_JOIN, 0, payload, sizeof(payload), bytes);
        p4_mp_event_t event;
        assert(p4_mp_session_receive(&peers[0], slot + 1U, now_ms,
            bytes, size, &event) == P4_MP_OK);
        assert(event.type == P4_MP_EVENT_JOIN_REQUEST);
        assert(p4_mp_session_accept_peer(&peers[0], event.peer_id,
            event.route_id, slot, event.packet.sequence, now_ms) == P4_MP_OK);
        const p4_mp_lobby_accept_t accept = {
            .assigned_player_slot = slot, .player_count = PLAYERS,
            .input_delay_tics = 2, .session_seed = 1,
        };
        uint8_t accepted[P4_MP_ACCEPT_PAYLOAD_BYTES];
        assert(p4_mp_lobby_accept_encode(&accept, accepted) == P4_MP_OK);
        size = encode(0, P4_MP_PACKET_ACCEPT, 0, accepted, sizeof(accepted), bytes);
        assert(p4_mp_session_receive(&peers[slot], 1, now_ms,
            bytes, size, &event) == P4_MP_OK);
        assert(event.type == P4_MP_EVENT_ACCEPTED);
    }
}

static void receive(uint8_t slot, p4_mp_packet_type_t type,
                    const uint8_t *payload, uint16_t length, uint32_t ack)
{
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];
    const size_t size = encode(slot, type, ack, payload, length, bytes);
    handler(handler_context, slot + 1U, bytes, size);
}
static void heartbeat(uint8_t slot, const char *marker)
{
    receive(slot, P4_MP_PACKET_PING, (const uint8_t *)marker, 8, remote_ack[slot]);
}
static esp_err_t set_handler(void *context, p4_doom_p4mp_frame_handler_t fn,
                             void *frame_context)
{
    (void)context; handler = fn; handler_context = frame_context; return ESP_OK;
}
static bool connected(void *context, uint64_t route)
{
    (void)context;
    assert(route >= 1 && route <= PLAYERS);
    /* Isolate the application liveness decision from the transport lease. */
    return true;
}
static esp_err_t send_to(void *context, uint64_t route,
                         const uint8_t *bytes, size_t size)
{
    (void)context;
    assert(route >= 1 && route <= PLAYERS);
    const uint8_t remote = (uint8_t)(route - 1U);
    p4_mp_event_t event;
    assert(p4_mp_session_receive(&peers[remote], local_slot + 1U,
        now_ms, bytes, size, &event) == P4_MP_OK);
    const p4_mp_packet_view_t *packet = &event.packet;
    if (packet->type == P4_MP_PACKET_GAME_MESSAGE &&
        packet->payload_length == P4_MP_GROUP_BYTES) {
        assert(scenario == LOBBY_ONLY && remote == 3);
        assert(!memcmp(packet->payload, "P4GS", 4) && packet->payload[5] == 3);
        if (lobby_replies) assert(now_ms - last_lobby_reply_ms >= 100);
        last_lobby_reply_ms = now_ms;
        ++lobby_replies;
    } else if (packet->type == P4_MP_PACKET_GAME_MESSAGE) {
        assert(local_slot == 0 && engines_ready());
        assert(packet->payload_length == P4_DOOM_LOCKSTEP_BYTES);
        assert(!memcmp(packet->payload, "GC", 2));
        const uint8_t *tick = packet->payload + 4;
        const uint32_t value = (uint32_t)tick[0] | (uint32_t)tick[1] << 8 |
            (uint32_t)tick[2] << 16 | (uint32_t)tick[3] << 24;
        if (value == remote_ack[remote]) ++remote_ack[remote];
        ++host_game_frames;
    } else if (local_slot && event.type == P4_MP_EVENT_INPUT) {
        p4_mp_input_t input; p4_doom_mp_tic_t tic;
        assert(p4_mp_input_decode(packet->payload, packet->payload_length, &input) == P4_MP_OK);
        assert(p4_doom_mp_tic_from_input(&input, &tic));
        assert(p4_doom_lockstep_input(&remote_host, local_slot, &tic, packet->ack));
    } else if (local_slot && event.type == P4_MP_EVENT_PING &&
               !memcmp(packet->payload, "GCAREADY", 8)) {
        local_engine_ready = true;
        assert(p4_doom_lockstep_ack(&remote_host, local_slot, packet->ack));
    }
    return ESP_OK;
}

static void poll(void *context)
{
    (void)context;
    if (!engines_ready()) assert(engine_begins == 0 && delivered == 0 && host_game_frames == 0);
    if (scenario == CLIENT_DELAYED && engines_ready() && local_engine_ready) {
        p4_doom_lockstep_pump(&remote_host);
        uint8_t packet[P4_DOOM_LOCKSTEP_BYTES];
        if (p4_doom_lockstep_packet(&remote_host, local_slot, packet))
            receive(0, P4_MP_PACKET_GAME_MESSAGE, packet, sizeof(packet), 0);
    }
    if (now_ms < next_heartbeat_ms) return;
    next_heartbeat_ms = now_ms + HEARTBEAT_MS;
    if (scenario == CLIENT_DELAYED) {
        /* First the host is loading; then it is ready but waiting for another
         * guest. Both valid markers prove liveness without releasing tics. */
        heartbeat(0, elapsed() < 20000 ? "GCAWAIT!" : "GCAHOST!");
        return;
    }
    for (uint8_t slot = 1; slot < PLAYERS; ++slot) {
        if (scenario == SILENT_PEER && slot == 1) {
            heartbeat(slot, "GCAWAIT!");
        } else if (slot != 3) {
            heartbeat(slot, "GCAREADY");
        } else if (scenario == HOST_DELAYED || scenario == WAITING) {
            heartbeat(slot, engines_ready() ? "GCAREADY" : "GCAWAIT!");
        } else if (scenario == SILENCE) {
            if (elapsed() <= 2000) heartbeat(slot, "GCAWAIT!");
        } else if (scenario == REPLAY) {
            if (!replay_length) {
                replay_length = encode(slot, P4_MP_PACKET_PING, 0,
                    (const uint8_t *)"GCAWAIT!", 8, replay_frame);
            } else ++rejected_replays;
            handler(handler_context, slot + 1U, replay_frame, replay_length);
        } else if (scenario == MALFORMED) {
            heartbeat(slot, "GCAWAIT?");
            ++malformed_heartbeats;
        } else if (scenario == LOBBY_ONLY) {
            const uint16_t token = p4_mp_group_token(7, 1);
            const uint8_t ready[P4_MP_GROUP_BYTES] = {
                'P', '4', 'G', 'S', 1, 2, PLAYERS, 0,
                (uint8_t)token, (uint8_t)(token >> 8), 0, 0,
            };
            receive(slot, P4_MP_GROUP_PACKET_TYPE, ready, sizeof(ready), 0);
        } else assert(scenario == SILENT_PEER);
    }
}

int main(int argc, char **argv)
{
    static const char *const names[] = {
        "host-delayed", "client-delayed", "waiting", "silence", "replay",
        "malformed", "lobby-only", "silent-peer",
    };
    assert(argc == 2);
    unsigned choice = 0;
    while (choice < sizeof(names) / sizeof(names[0]) && strcmp(argv[1], names[choice])) ++choice;
    assert(choice < sizeof(names) / sizeof(names[0]));
    scenario = (scenario_t)choice;
    local_slot = scenario == CLIENT_DELAYED ? 1 : 0;
    admit_peers();
    if (local_slot) {
        assert(p4_doom_lockstep_init(&remote_host, 0, PLAYERS));
        for (uint32_t tick = 0; tick < 2; ++tick) {
            const p4_doom_mp_tic_t neutral = {.tick = tick};
            for (uint8_t slot = 0; slot < PLAYERS; ++slot)
                assert(p4_doom_mp_tic_queue_submit(&remote_host.input, slot, &neutral));
        }
    }
    const p4_doom_mp_launch_config_t config = {
        .enabled = true, .role = local_slot ? P4_MP_ROLE_CLIENT : P4_MP_ROLE_HOST,
        .session_id = 7, .self_peer_id = 100U + local_slot,
        .remote_peer_id = local_slot ? 100 : 101, .route_id = local_slot ? 1 : 2,
        .local_player_slot = local_slot, .player_count = PLAYERS, .initial_player_mask = 15,
        .input_delay_tics = 2, .session_seed = 1,
        .setup = {.game = P4_DOOM_MP_GAME_GAME_CHANGERS_AI,
            .mode = P4_DOOM_MP_MODE_ALTDEATH, .episode = 1, .map = 1,
            .skill = 3, .no_monsters = true},
    };
    const p4_doom_p4mp_transport_t transport = {
        .set_handler = set_handler, .send_to = send_to, .poll = poll, .connected = connected,
    };
    assert(p4_doom_gc_prepare(&peers[local_slot], &config, &transport) == ESP_OK);
    assert_loading(P4_DOOM_LOADING_IDLE);
    started_ms = now_ms;
    net_gamesettings_t settings;
    const boolean configured = p4_doom_gc_configure(&settings);
    fprintf(stderr, "%s configured=%u failed=%u elapsed_ms=%" PRIu64 " delivered=%u\n",
        argv[1], (unsigned)configured, (unsigned)p4_doom_gc_failed(), elapsed(), delivered);
    if (scenario == HOST_DELAYED || scenario == CLIENT_DELAYED) {
        assert(configured && !p4_doom_gc_failed());
        assert(elapsed() >= READY_AFTER_MS && elapsed() <= READY_AFTER_MS + 50U);
        assert(engine_begins == 1 && delivered == 2);
        assert(settings.consoleplayer == local_slot && settings.num_players == PLAYERS);
        assert(peers[local_slot].timeout_ms == 3000);
        assert_loading(P4_DOOM_LOADING_READY);
        assert(waiting_presentations && !failed_presentations);
    } else {
        assert(!configured && p4_doom_gc_failed());
        assert(engine_begins == 0 && delivered == 0 && host_game_frames == 0);
        assert_loading(P4_DOOM_LOADING_FAILED);
        assert(waiting_presentations && failed_presentations);
        const uint64_t expected = scenario == WAITING ? STARTUP_LIMIT_MS :
            scenario == SILENCE ? STARTUP_IDLE_MS + 2000U : STARTUP_IDLE_MS;
        assert(elapsed() >= expected && elapsed() <= expected + 50U);
        if (scenario == REPLAY) assert(rejected_replays >= 59);
        if (scenario == MALFORMED) assert(malformed_heartbeats >= 60);
        if (scenario == LOBBY_ONLY) assert(lobby_replies >= 59 && lobby_replies <= 61);
    }
    assert(peers[local_slot].timeout_ms == 3000);
    p4_doom_gc_quit();
    assert(peers[local_slot].timeout_ms == 3000);
    assert_loading(configured?P4_DOOM_LOADING_IDLE:P4_DOOM_LOADING_FAILED);
    assert(p4_doom_gc_prepare(NULL,NULL,NULL)==ESP_OK);
    assert_loading(P4_DOOM_LOADING_IDLE);
    printf("PASS: %s\n", argv[1]);
    return 0;
}
