#!/usr/bin/env python3
"""Run the real Doom poll/statistics boundary with a deterministic clock.

The runtime dispatcher and prepare entry point are extracted unchanged. Narrow
transport/session/engine fakes select their return paths; this does not simulate
packet traffic or qualify device timing. Both public poll entry points execute
the same production accounting, including inactive and early-return calls.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("P4_DOOM_TEST_ROOT", Path(__file__).resolve().parents[2]))
OVERLAY = Path(os.environ.get("P4_DOOM_TEST_OVERLAY", ROOT))
SOURCE = Path(os.environ.get(
    "P4_DOOM_NET_STATS_SOURCE", OVERLAY / "apps/console_os/main/doom_p4mp_adapter.c"))


def function(source, name):
    match = re.search(r"^(?:static\s+)?[^;{}\n]*?\b" + re.escape(name)
                      + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"No function {name}")
    brace = match.end() - 1
    depth = 0
    for token in re.finditer(
            r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
            source[brace:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():brace + token.end()]
    raise ValueError(f"Unclosed function {name}")


INCLUDES = r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "doom_p4mp_adapter.h"
#include "doom_gc_p4mp.h"
#include "p4_doom_net.h"
#define ESP_LOGI(...) ((void)0)
'''

FIXTURE = r'''
static p4_doom_p4mp_state_t s_net;
static p4_doom_net_stats_t s_poll_stats, arena_stats;
static boolean arena_active, s_frame_tail_ordinary;
static unsigned opportunistic_polls;
static bool route_connected, session_timed_out;
static unsigned transport_polls, arena_polls, route_losses, departures;
static unsigned session_ticks, retries, flushes, stall_checks, sends;
static unsigned arena_prepares;
static int64_t clock_values[3];
static unsigned clock_count, clock_index;

static int64_t esp_timer_get_time(void) {
    assert(clock_index < clock_count);
    return clock_values[clock_index++];
}
static void clock_two(int64_t start, int64_t end) {
    clock_values[0]=start; clock_values[1]=end;
    clock_count=2; clock_index=0;
}
static void clock_three(int64_t start, int64_t middle, int64_t end) {
    clock_values[0]=start; clock_values[1]=middle; clock_values[2]=end;
    clock_count=3; clock_index=0;
}
boolean p4_doom_gc_active(void) { return arena_active; }
void p4_doom_gc_poll(void) { ++arena_polls; }
void p4_doom_gc_poll_opportunistic(void) { ++opportunistic_polls; }
void p4_doom_gc_get_stats(p4_doom_net_stats_t *out) { *out=arena_stats; }
esp_err_t p4_doom_gc_prepare(p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config,
    const p4_doom_p4mp_transport_t *transport) {
    (void)session; (void)transport; ++arena_prepares;
    arena_active=config != NULL;
    memset(&arena_stats,0,sizeof(arena_stats));
    return ESP_OK;
}
bool p4_mp_session_route_disconnected(p4_mp_session_t *session,
    uint64_t route, p4_mp_event_t *event) {
    (void)session; (void)route; (void)event; ++route_losses; return true;
}
bool p4_mp_session_tick(p4_mp_session_t *session,
    uint64_t now, p4_mp_event_t *event) {
    (void)session; (void)now; ++session_ticks;
    event->player_slot=1; return session_timed_out;
}
static void disconnect_remote(uint8_t slot,const char *reason) {
    (void)slot; (void)reason; ++departures;
}
static void retry_oldest_unacknowledged(int64_t now) { (void)now; ++retries; }
static void flush_complete_tics(void) { ++flushes; }
static void fail_lockstep_if_stalled(int64_t now) { (void)now; ++stall_checks; }
static esp_err_t send_packet(p4_mp_packet_type_t type,uint32_t ack,
    const uint8_t *payload,uint16_t length) {
    (void)type; (void)ack; (void)payload; (void)length; ++sends; return ESP_OK;
}
static void frame_received(void *context,uint64_t route,
    const uint8_t *datagram,size_t length) {
    (void)context; (void)route; (void)datagram; (void)length;
}
bool p4_doom_mp_launch_config_valid(const p4_doom_mp_launch_config_t *config) {
    return config != NULL;
}
bool p4_doom_mp_tic_queue_init(p4_doom_mp_tic_queue_t *queue,
    uint8_t players,uint32_t start) {
    (void)players; queue->next_tick=start; return true;
}
void p4_doom_mp_tx_window_init(p4_doom_mp_tx_window_t *window,uint32_t start) {
    (void)window; (void)start;
}
static void transport_poll(void *context) { (void)context; ++transport_polls; }
static bool transport_connected(void *context,uint64_t route) {
    (void)context; (void)route; return route_connected;
}
static esp_err_t transport_set_handler(void *context,
    p4_doom_p4mp_frame_handler_t handler,void *handler_context) {
    (void)context; assert(handler==frame_received && handler_context==&s_net);
    return ESP_OK;
}
static esp_err_t transport_send(void *context,const uint8_t *bytes,size_t length) {
    (void)context; (void)bytes; (void)length; ++sends; return ESP_OK;
}
static const char *transport_route_name(void *context,uint64_t route) {
    (void)context; (void)route; return "fixture";
}
static void reset_fixture(void) {
    memset(&s_net,0,sizeof(s_net));
    memset(&s_poll_stats,0,sizeof(s_poll_stats));
    memset(&arena_stats,0,sizeof(arena_stats));
    s_frame_tail_ordinary=false;opportunistic_polls=0;
    arena_active=false; route_connected=true; session_timed_out=false;
    transport_polls=arena_polls=route_losses=departures=0;
    session_ticks=retries=flushes=stall_checks=sends=arena_prepares=0;
    clock_count=clock_index=0;
    s_net.transport.set_handler=transport_set_handler;
    s_net.transport.poll=transport_poll;
    s_net.transport.send=transport_send;
    s_net.transport.connected=transport_connected;
    s_net.transport.route_name=transport_route_name;
}
static void expect_timing(uint32_t calls,uint64_t total,uint64_t maximum) {
    assert(clock_index==clock_count);
    p4_doom_net_stats_t stats;
    P4_DoomNetGetStats(&stats);
    assert(stats.poll_calls==calls);
    assert(stats.poll_us==total);
    assert(stats.poll_max_us==maximum);
}
'''

CASES = r'''
static void check_hints(void) {
    reset_fixture();arena_active=true;
    clock_two(100,101);P4_DoomNetPollFrameTail(true);
    assert(arena_polls==1 && opportunistic_polls==0);
    P4_DoomNetSetFrameTail(true);
    clock_two(102,103);P4_DoomNetPollFrameTail(true);
    assert(arena_polls==1 && opportunistic_polls==1);
    clock_two(104,105);P4_DoomNetPollFrameTail(false);
    assert(arena_polls==2 && opportunistic_polls==1);
    P4_DoomNetSetFrameTail(false);
    clock_two(106,107);P4_DoomNetPollFrameTail(true);
    assert(arena_polls==3 && opportunistic_polls==1);
    clock_two(108,109);P4_DoomNetPollOpportunistic();
    assert(arena_polls==3 && opportunistic_polls==2);
    expect_timing(5,5,1);
    /* Legacy runtime never selects Arena's receive-only policy. */
    arena_active=false;clock_two(110,111);P4_DoomNetPollOpportunistic();
    assert(arena_polls==3 && opportunistic_polls==2);
    expect_timing(6,6,1);
}
static void check_inactive(void) {
    reset_fixture();
    clock_two(100,107); P4_DoomNetPoll(); expect_timing(1,7,7);
    clock_two(200,211); p4_doom_p4mp_poll(); expect_timing(2,18,11);
    clock_two(300,302); P4_DoomNetPoll(); expect_timing(3,20,11);
    assert(!transport_polls && !arena_polls && !flushes && !departures);
}
static void check_arena(void) {
    reset_fixture(); arena_active=true; s_net.prepared=true;
    clock_two(100,119); P4_DoomNetPoll(); expect_timing(1,19,19);
    clock_two(200,203); p4_doom_p4mp_poll(); expect_timing(2,22,19);
    assert(arena_polls==2 && !transport_polls && !flushes && !departures);
}
static void check_disconnected(void) {
    reset_fixture(); s_net.prepared=true; route_connected=false;
    clock_two(100,123); P4_DoomNetPoll(); expect_timing(1,23,23);
    assert(transport_polls==1 && route_losses==1 && departures==1);
    assert(!session_ticks && !retries && !flushes && !stall_checks);
}
static void check_ordinary(void) {
    reset_fixture(); s_net.prepared=true;
    p4_mp_session_t session={0}; s_net.session=&session;
    clock_three(100,105,131); P4_DoomNetPoll(); expect_timing(1,31,31);
    session_timed_out=true;
    clock_three(200,210,241); p4_doom_p4mp_poll(); expect_timing(2,72,41);
    assert(transport_polls==2 && session_ticks==2 && departures==1);
    assert(retries==2 && flushes==2 && stall_checks==2 && !arena_polls);
}
static void expect_counters(const p4_doom_net_stats_t *stats,uint32_t base) {
    assert(stats->tx_attempts==base+1 && stats->tx_failures==base+2);
    assert(stats->rx_packets==base+3 && stats->rx_session_rejected==base+4);
    assert(stats->peer_departures==base+5 && stats->host_blocked_peer_polls==base+6);
}
static p4_doom_net_stats_t counters(uint32_t base) {
    return (p4_doom_net_stats_t){.poll_calls=999,.poll_us=999,.poll_max_us=999,
        .tx_attempts=base+1,.tx_failures=base+2,.rx_packets=base+3,
        .rx_session_rejected=base+4,.peer_departures=base+5,
        .host_blocked_peer_polls=base+6};
}
static void check_snapshots(void) {
    reset_fixture(); s_net.stats=counters(10); arena_stats=counters(20);
    clock_two(100,108); P4_DoomNetPoll();
    p4_doom_net_stats_t output;
    for(unsigned read=0;read<3;++read) {
        P4_DoomNetGetStats(&output); expect_counters(&output,10);
        assert(output.poll_calls==1 && output.poll_us==8 && output.poll_max_us==8);
    }
    arena_active=true;
    for(unsigned read=0;read<3;++read) {
        P4_DoomNetGetStats(&output); expect_counters(&output,20);
        assert(output.poll_calls==1 && output.poll_us==8 && output.poll_max_us==8);
    }
    P4_DoomNetGetStats(NULL);
    assert(clock_index==2 && s_net.stats.poll_calls==999 && arena_stats.poll_us==999);
    expect_counters(&s_net.stats,10); expect_counters(&arena_stats,20);
    assert(!transport_polls && !arena_polls && !sends);
}
static void check_saturation(void) {
    reset_fixture(); s_poll_stats.poll_calls=UINT32_MAX-1;
    s_poll_stats.poll_us=UINT64_MAX-3; s_poll_stats.poll_max_us=2;
    clock_two(100,105); P4_DoomNetPoll(); expect_timing(UINT32_MAX,UINT64_MAX,5);
    clock_two(200,204); p4_doom_p4mp_poll(); expect_timing(UINT32_MAX,UINT64_MAX,5);
}
static void check_clock_edges(void) {
    reset_fixture();
    clock_two(100,100); P4_DoomNetPoll(); expect_timing(1,0,0);
    clock_two(200,199); P4_DoomNetPoll(); expect_timing(2,0,0);
    clock_two(300,312); P4_DoomNetPoll(); expect_timing(3,12,12);
}
static void check_prepare_reset(void) {
    for(unsigned mode=0;mode<4;++mode) {
        reset_fixture(); s_net.stats=counters(10); arena_stats=counters(20);
        clock_two(100,109); P4_DoomNetPoll(); expect_timing(1,9,9);
        p4_doom_mp_launch_config_t config={0};
        if(mode==1) config.setup.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI;
        p4_mp_session_t session={0};
        p4_doom_p4mp_transport_t transport=s_net.transport;
        if(mode==3) {
            config.enabled=true; config.role=P4_MP_ROLE_HOST;
            config.session_id=10; config.self_peer_id=20;
            config.remote_peer_id=30; config.route_id=40; config.player_count=2;
            session.state=P4_MP_SESSION_CONNECTED; session.role=config.role;
            session.session_id=config.session_id; session.self_peer_id=config.self_peer_id;
            session.timeout_ms=1234;
            session.peers[0]=(p4_mp_peer_t){.connected=true,.peer_id=30,.route_id=40,
                .player_slot=1};
            clock_values[2]=200; clock_count=3;
        }
        esp_err_t result=p4_doom_p4mp_prepare(mode==3 ? &session : NULL,
            mode ? &config : NULL,mode==3 ? &transport : NULL);
        assert(result==(mode==2 ? ESP_ERR_INVALID_ARG : ESP_OK));
        p4_doom_net_stats_t output;
        memset(&output,0xff,sizeof(output)); P4_DoomNetGetStats(&output);
        expect_timing(0,0,0);
        assert(!output.tx_attempts && !output.tx_failures && !output.rx_packets);
        assert(!output.rx_session_rejected && !output.peer_departures);
        assert(!output.host_blocked_peer_polls);
        assert(arena_prepares==(mode==1 ? 2U : 1U));
        if(mode==3) {
            assert(s_net.prepared && s_net.session==&session);
            assert(s_net.saved_session_timeout_ms==1234);
            assert(session.timeout_ms==P4_DOOM_MP_HANDOFF_TIMEOUT_MS);
        }
    }
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"hints")) check_hints();
    else if(!strcmp(argv[1],"inactive")) check_inactive();
    else if(!strcmp(argv[1],"arena")) check_arena();
    else if(!strcmp(argv[1],"disconnected")) check_disconnected();
    else if(!strcmp(argv[1],"ordinary")) check_ordinary();
    else if(!strcmp(argv[1],"snapshots")) check_snapshots();
    else if(!strcmp(argv[1],"saturation")) check_saturation();
    else if(!strcmp(argv[1],"clock-edges")) check_clock_edges();
    else if(!strcmp(argv[1],"prepare-reset")) check_prepare_reset();
    else return 2;
    return 0;
}
'''


class DoomNetStats(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-doom-net-stats-")
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name)
        source = SOURCE.read_text()
        constants = re.search(r"\benum\s*\{[\s\S]*?\};", source)
        state = re.search(r"\btypedef struct\s*\{[\s\S]*?\}\s*p4_doom_p4mp_state_t;", source)
        if not constants or not state:
            raise ValueError("Missing production adapter constants/state")
        functions = "\n".join(function(source, name) for name in (
            "count_stat", "restore_session_timeout", "remote_peer",
            "p4_doom_p4mp_prepare", "poll_network_runtime", "poll_network_timed",
            "P4_DoomNetPoll", "P4_DoomNetPollOpportunistic",
            "P4_DoomNetSetFrameTail", "P4_DoomNetPollFrameTail",
            "P4_DoomNetGetStats", "p4_doom_p4mp_poll"))
        (path / "test.c").write_text(
            INCLUDES + constants.group() + state.group() + FIXTURE + functions + CASES)
        cls.binary = path / "test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            *("-I" + str(tree / item) for tree in (OVERLAY, ROOT) for item in (
                "apps/console_os/main", "components/doom_multiplayer/tests/mocks",
                "components/p4_multiplayer/include", "components/doom_multiplayer/include",
                "components/p4_game_api/include",
                "apps/doom_audio_probe/components/doom_engine_audio")),
            "-isystem", str(ROOT / "third_party/doomgeneric/doomgeneric"),
            str(path / "test.c"), "-o", str(cls.binary)], check=True)

    def case(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_explicit_hints_preserve_forced_legacy_and_scope(self): self.case("hints")
    def test_inactive_calls_and_os_wrapper_accumulate(self): self.case("inactive")
    def test_arena_early_return_is_timed(self): self.case("arena")
    def test_disconnected_ordinary_early_return_is_timed(self): self.case("disconnected")
    def test_ordinary_runtime_and_timeout_are_timed(self): self.case("ordinary")
    def test_snapshot_selects_role_counters_without_clearing_or_polling(self): self.case("snapshots")
    def test_counter_and_time_saturate_without_wrapping(self): self.case("saturation")
    def test_equal_or_reversed_clock_does_not_underflow(self): self.case("clock-edges")
    def test_prepare_resets_inactive_arena_ordinary_and_rejected_launch(self): self.case("prepare-reset")


if __name__ == "__main__":
    unittest.main(verbosity=2)
