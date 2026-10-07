// SPDX-License-Identifier: GPL-2.0-or-later
/* Production adapter, public P4MP admission/codec, and a virtual clock. WAD
 * callbacks simulate the storage lock: transport may run, engine delivery may
 * not. The remote engine is a real lockstep instance behind the transport. */
#include "doom_gc_p4mp.h"
#include "p4/doom_lockstep.h"
#include "platform/readonly_blob_loading.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef enum { LOCAL, LOCAL_CAP, INCIDENTAL, PRECONFIG_CAP, WAIT_CAP, SILENCE, REPLAY, MALFORMED } scenario_t;
static scenario_t scenario;
static uint64_t now_ms = 100, phase_start, next_heartbeat;
static uint8_t local_slot, last_mask;
static p4_mp_session_t peers[2];
static p4_doom_lockstep_t remote_host;
static p4_doom_p4mp_frame_handler_t handler;
static void *handler_context;
static uint32_t remote_ack, pending_tick;
static bool configured, inside_read, input_pending, remote_output, exercise_reentry, preconfigure_phase;
static unsigned delivered, begins, transport_polls, wait_sends, reentries, receive_depth;
static unsigned replay_sends, malformed_sends;
static uint8_t replay_frame[P4_MP_MAX_DATAGRAM_BYTES];
static size_t replay_length;

int64_t esp_timer_get_time(void) { return (int64_t)now_ms * 1000; }
void vTaskDelay(unsigned ms) { now_ms += ms; }
void p4_doom_gc_engine_begin_mask(uint8_t count, uint8_t mask, uint8_t map)
{
    assert(!inside_read && count == 4 && mask == 3 && map == 1);
    ++begins;
}
void D_ReceiveTic(ticcmd_t *commands, boolean *mask)
{
    (void)commands;
    assert(!inside_read && begins == 1 && receive_depth == 0);
    ++receive_depth;
    last_mask = (uint8_t)((mask[0] ? 1U : 0U) | (mask[1] ? 2U : 0U));
    if (exercise_reentry) {
        inside_read = true;
        assert(platform_readonly_blob_loading_progress());
        inside_read = false;
        ++reentries;
    }
    ++delivered;
    --receive_depth;
}
static size_t encode(uint8_t slot, p4_mp_packet_type_t type, uint32_t ack,
                     const uint8_t *payload, uint16_t length, uint8_t *bytes)
{
    size_t size = 0;
    assert(p4_mp_session_encode(&peers[slot], type, ack, payload, length,
        bytes, P4_MP_MAX_DATAGRAM_BYTES, &size) == P4_MP_OK);
    return size;
}
static void receive(p4_mp_packet_type_t type, const uint8_t *payload,
                    uint16_t length, uint32_t ack)
{
    uint8_t bytes[P4_MP_MAX_DATAGRAM_BYTES];
    const uint8_t remote = (uint8_t)(1U - local_slot);
    const size_t size = encode(remote, type, ack, payload, length, bytes);
    handler(handler_context, remote + 1U, bytes, size);
}
static void heartbeat(const char *marker)
{
    receive(P4_MP_PACKET_PING, (const uint8_t *)marker, 8, remote_ack);
}
static void admit(void)
{
    assert(p4_mp_session_host_start(&peers[0], 7, 100, 3000) == P4_MP_OK);
    assert(p4_mp_session_client_start(&peers[1], 7, 101, 100, 1,
        now_ms, 3000) == P4_MP_OK);
    p4_mp_lobby_join_t join = {.requested_player_slot = 1, .join_nonce = 1001};
    memset(join.compatibility_sha256, 0xa5, sizeof(join.compatibility_sha256));
    uint8_t payload[P4_MP_JOIN_PAYLOAD_BYTES], bytes[P4_MP_MAX_DATAGRAM_BYTES];
    assert(p4_mp_lobby_join_encode(&join, payload) == P4_MP_OK);
    size_t size = encode(1, P4_MP_PACKET_JOIN, 0, payload, sizeof(payload), bytes);
    p4_mp_event_t event;
    assert(p4_mp_session_receive(&peers[0], 2, now_ms, bytes, size, &event) == P4_MP_OK);
    assert(event.type == P4_MP_EVENT_JOIN_REQUEST);
    assert(p4_mp_session_accept_peer(&peers[0], event.peer_id, event.route_id,
        1, event.packet.sequence, now_ms) == P4_MP_OK);
    const p4_mp_lobby_accept_t accept = {.assigned_player_slot = 1,
        .player_count = 2, .input_delay_tics = 2, .session_seed = 1};
    uint8_t accepted[P4_MP_ACCEPT_PAYLOAD_BYTES];
    assert(p4_mp_lobby_accept_encode(&accept, accepted) == P4_MP_OK);
    size = encode(0, P4_MP_PACKET_ACCEPT, 0, accepted, sizeof(accepted), bytes);
    assert(p4_mp_session_receive(&peers[1], 1, now_ms, bytes, size, &event) == P4_MP_OK);
    assert(event.type == P4_MP_EVENT_ACCEPTED);
}
static esp_err_t set_handler(void *context, p4_doom_p4mp_frame_handler_t fn, void *frame_context)
{
    (void)context; handler = fn; handler_context = frame_context; return ESP_OK;
}
static bool connected(void *context, uint64_t route)
{
    (void)context;
    assert(route == (local_slot ? 1U : 2U));
    /* Route existence and protocol lease are independent. Session tick below
     * remains real and is never replaced by this transport link fixture. */
    return true;
}
static esp_err_t send_to(void *context, uint64_t route, const uint8_t *bytes, size_t size)
{
    (void)context;
    assert(route == (local_slot ? 1U : 2U));
    p4_mp_event_t event;
    assert(p4_mp_session_receive(&peers[1U - local_slot], local_slot + 1U,
        now_ms, bytes, size, &event) == P4_MP_OK);
    const p4_mp_packet_view_t *packet = &event.packet;
    const bool waiting = packet->type == P4_MP_PACKET_PING &&
        packet->payload_length == 8 && !memcmp(packet->payload, "GCAWAIT!", 8);
    if (waiting) ++wait_sends;
    if (inside_read) assert(waiting); /* No input, READY ACK, or canonical sends. */
    if (packet->type == P4_MP_PACKET_GAME_MESSAGE) {
        assert(local_slot == 0 && packet->payload_length == P4_DOOM_LOCKSTEP_BYTES);
        const uint8_t *b = packet->payload + 4;
        const uint32_t tick = (uint32_t)b[0] | (uint32_t)b[1] << 8 |
            (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        if ((!configured || scenario <= LOCAL_CAP) && tick == remote_ack) ++remote_ack;
    } else if (local_slot && event.type == P4_MP_EVENT_INPUT) {
        p4_mp_input_t input; p4_doom_mp_tic_t tic;
        assert(p4_mp_input_decode(packet->payload, packet->payload_length, &input) == P4_MP_OK);
        assert(p4_doom_mp_tic_from_input(&input, &tic));
        assert(p4_doom_lockstep_input(&remote_host, local_slot, &tic, packet->ack));
    } else if (local_slot && event.type == P4_MP_EVENT_PING &&
               packet->payload_length == 8 && !memcmp(packet->payload, "GCAREADY", 8)) {
        assert(p4_doom_lockstep_ack(&remote_host, local_slot, packet->ack));
    }
    return ESP_OK;
}
static void poll(void *context)
{
    (void)context;
    ++transport_polls;
    if (preconfigure_phase) {
        if (now_ms >= next_heartbeat) {
            next_heartbeat = now_ms + 500;
            heartbeat("GCAWAIT!");
        }
        return;
    }
    if (configured && scenario >= WAIT_CAP) {
        if (now_ms < next_heartbeat) return;
        next_heartbeat = now_ms + 500;
        if (!replay_length) {
            replay_length = encode((uint8_t)(1U - local_slot), P4_MP_PACKET_PING,
                remote_ack, (const uint8_t *)"GCAWAIT!", 8, replay_frame);
            handler(handler_context, local_slot ? 1U : 2U, replay_frame, replay_length);
        } else if (scenario == WAIT_CAP) heartbeat("GCAWAIT!");
        else if (scenario == REPLAY) {
            handler(handler_context, local_slot ? 1U : 2U, replay_frame, replay_length);
            ++replay_sends;
        } else if (scenario == MALFORMED) {
            /* Fresh authenticated P4MP sequence, invalid Arena loading marker. */
            heartbeat("GCAWAIT?");
            ++malformed_sends;
        }
        return;
    }
    if (!local_slot && input_pending) {
        const p4_doom_mp_tic_t tic = {.tick = pending_tick};
        p4_mp_input_t input; uint8_t payload[P4_MP_INPUT_PAYLOAD_BYTES];
        p4_doom_mp_tic_to_input(&tic, &input);
        p4_mp_input_encode(&input, payload);
        receive(P4_MP_PACKET_INPUT, payload, sizeof(payload), remote_ack);
        input_pending = false;
    }
    if (local_slot && (!configured || remote_output)) {
        p4_doom_lockstep_pump(&remote_host);
        uint8_t packet[P4_DOOM_LOCKSTEP_BYTES];
        if (p4_doom_lockstep_packet(&remote_host, local_slot, packet))
            receive(P4_MP_PACKET_GAME_MESSAGE, packet, sizeof(packet), 0);
    }
    if (now_ms >= next_heartbeat) {
        next_heartbeat = now_ms + 500;
        heartbeat(local_slot ? "GCAHOST!" : "GCAREADY");
    }
}
static void begin_next_tic(uint32_t tick)
{
    ticcmd_t neutral = {0};
    p4_doom_gc_submit(&neutral, (int)tick);
    if (local_slot) {
        /* Send the client's input before the remote host gets its own input;
         * no complete canonical tic exists until the simulated WAD read. */
        remote_output = false;
        now_ms += 20;
        p4_doom_gc_poll();
        const p4_doom_mp_tic_t tic = {.tick = tick};
        assert(p4_doom_lockstep_submit(&remote_host, &tic));
        remote_output = true;
    } else { pending_tick = tick; input_pending = true; }
}
static void local_load(void)
{
    const unsigned before = delivered, polls_before = transport_polls, waits_before = wait_sends;
    phase_start = now_ms;
    for (unsigned i = 0; i < 90; ++i) {
        now_ms += 100;
        inside_read = true;
        const bool serviced = platform_readonly_blob_loading_progress();
        inside_read = false;
        assert(serviced && !p4_doom_gc_failed());
        assert(delivered == before && begins == 1);
        p4_mp_event_t event;
        assert(!p4_mp_session_tick(&peers[1U - local_slot], now_ms, &event));
        assert(peers[local_slot].timeout_ms == 30000);
    }
    assert(transport_polls - polls_before == 90);
    assert(wait_sends - waits_before >= 17);
    exercise_reentry = true;
    now_ms += 20;
    p4_doom_gc_poll();
    exercise_reentry = false;
    assert(delivered == before + 1 && last_mask == 3 && reentries > 0);
    /* A nested WAD read starts another local phase; the next ordinary poll
     * ends it and restores the original protocol timeout. */
    now_ms += 20;
    p4_doom_gc_poll();
    assert(peers[local_slot].timeout_ms == 3000 && !p4_doom_gc_failed());
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    local_slot = (uint8_t)(strcmp(argv[1], "client") == 0);
    assert(local_slot || !strcmp(argv[1], "host"));
    static const char *const names[] = {"local", "local-cap", "incidental", "preconfigure-cap", "wait-cap", "silence", "replay", "malformed"};
    unsigned choice = 0;
    while (choice < sizeof(names) / sizeof(names[0]) && strcmp(argv[2], names[choice])) ++choice;
    assert(choice < sizeof(names) / sizeof(names[0]));
    scenario = (scenario_t)choice;
    admit();
    if (local_slot) {
        assert(p4_doom_lockstep_init(&remote_host, 0, 4));
        assert(p4_doom_lockstep_depart(&remote_host, 2));
        assert(p4_doom_lockstep_depart(&remote_host, 3));
        for (uint32_t tick = 0; tick < 2; ++tick) {
            const p4_doom_mp_tic_t neutral = {.tick = tick};
            for (uint8_t slot = 0; slot < 2; ++slot)
                assert(p4_doom_mp_tic_queue_submit(&remote_host.input, slot, &neutral));
        }
    }
    const p4_doom_mp_launch_config_t config = {.enabled = true,
        .role = local_slot ? P4_MP_ROLE_CLIENT : P4_MP_ROLE_HOST,
        .session_id = 7, .self_peer_id = 100U + local_slot,
        .remote_peer_id = local_slot ? 100 : 101, .route_id = local_slot ? 1 : 2,
        .local_player_slot = local_slot, .player_count = 4, .initial_player_mask = 3, .input_delay_tics = 2, .session_seed = 1,
        .setup = {.game = P4_DOOM_MP_GAME_GAME_CHANGERS_AI, .mode = P4_DOOM_MP_MODE_ALTDEATH,
            .episode = 1, .map = 1, .skill = 3, .no_monsters = true}};
    const p4_doom_p4mp_transport_t transport = {.set_handler = set_handler,
        .send_to = send_to, .poll = poll, .connected = connected};
    assert(p4_doom_gc_prepare(&peers[local_slot], &config, &transport) == ESP_OK);
    if (scenario == PRECONFIG_CAP) {
        preconfigure_phase = true;
        phase_start = now_ms;
        for (;;) {
            inside_read = true;
            const bool serviced = platform_readonly_blob_loading_progress();
            inside_read = false;
            assert(begins == 0 && delivered == 0);
            if (!serviced) break;
            p4_mp_event_t event;
            assert(!p4_mp_session_tick(&peers[1U - local_slot], now_ms, &event));
            assert(now_ms - phase_start < 300100);
            now_ms += 100;
        }
        assert(p4_doom_gc_failed() && now_ms - phase_start == 300000);
        assert(peers[local_slot].timeout_ms == 3000 && wait_sends >= 599);
        p4_doom_gc_quit();
        printf("PASS: %s preconfigure-cap (waits=%u, no engine/tics)\n", argv[1], wait_sends);
        return 0;
    }
    net_gamesettings_t settings;
    assert(p4_doom_gc_configure(&settings));
    assert(delivered == 2 && begins == 1 && peers[local_slot].timeout_ms == 3000);
    /* Drain the host's second startup frame/ACK before selecting a scenario. */
    for (unsigned i = 0; i < 30; ++i) { now_ms += 20; p4_doom_gc_poll(); }
    configured = true;
    phase_start = now_ms;
    next_heartbeat = now_ms;
    if (scenario == LOCAL) {
        begin_next_tic(2); local_load();
        begin_next_tic(3); local_load(); /* A later map gets the same safe path. */
        assert(delivered == 4 && begins == 1 && reentries == 2);
    } else if (scenario == LOCAL_CAP) {
        for (;;) {
            inside_read = true;
            const bool serviced = platform_readonly_blob_loading_progress();
            inside_read = false;
            if (!serviced) break;
            assert(delivered == 2 && !p4_doom_gc_failed());
            assert(now_ms - phase_start < 300100);
            now_ms += 100;
        }
        assert(p4_doom_gc_failed() && now_ms - phase_start == 300000);
    } else if (scenario == INCIDENTAL) {
        assert(!local_slot);
        ticcmd_t neutral = {0};
        p4_doom_gc_submit(&neutral, 2);
        unsigned reads = 0;
        while (delivered == 2) {
            now_ms += 100;
            inside_read = true;
            assert(platform_readonly_blob_loading_progress());
            inside_read = false;
            ++reads;
            now_ms += 100; /* Only this 100 ms is spent in the loading phase. */
            p4_doom_gc_poll();
            if (delivered > 2) break;
            now_ms += 300;
            p4_doom_gc_poll();
            assert(now_ms - phase_start <= 13500);
        }
        const uint64_t normal_time = now_ms - phase_start - reads * 100U;
        assert(!p4_doom_gc_failed() && delivered == 3 && last_mask == 1);
        assert(reads >= 25 && normal_time > 10000 && normal_time <= 10500);
    } else {
        if (!local_slot) { ticcmd_t neutral = {0}; p4_doom_gc_submit(&neutral, 2); }
        for (;;) {
            p4_doom_gc_poll();
            if (p4_doom_gc_failed() || delivered > 2) break;
            assert(peers[local_slot].timeout_ms == 30000);
            assert(now_ms - phase_start < 300100);
            now_ms += 100;
        }
        const uint64_t expected = scenario == WAIT_CAP ? 300000 : 30000;
        fprintf(stderr, "%s %s expired_ms=%" PRIu64 " failed=%u mask=%u\n",
            argv[1], argv[2], now_ms - phase_start, (unsigned)p4_doom_gc_failed(), last_mask);
        assert(now_ms - phase_start >= expected && now_ms - phase_start <= expected + 100);
        if (local_slot) assert(p4_doom_gc_failed() && delivered == 2);
        else assert(!p4_doom_gc_failed() && delivered == 3 && last_mask == 1);
        if (scenario == REPLAY) assert(replay_sends >= 59);
        if (scenario == MALFORMED) assert(malformed_sends >= 59);
    }
    p4_doom_gc_quit();
    assert(peers[local_slot].timeout_ms == 3000);
    printf("PASS: %s %s (polls=%u waits=%u delivered=%u)\n",
        argv[1], argv[2], transport_polls, wait_sends, delivered);
    return 0;
}
