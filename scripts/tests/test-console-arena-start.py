#!/usr/bin/env python3
"""Execute OS Arena solo/group start gates and real launch config validation."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
BASE=Path(os.environ.get('P4_CONSOLE_BASE',ROOT))
spec=importlib.util.spec_from_file_location('resume_fixture',Path(__file__).with_name('test-console-arena-resume.py'))
fixture=importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
PRELUDE=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "p4/multiplayer.h"
#include "p4/multiplayer_group.h"
#include "p4/doom_multiplayer.h"
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_STATE=-1 };
enum { CONSOLE_MP_TRANSPORT_WIFI, CONSOLE_MP_TRANSPORT_BLE, CONSOLE_MP_TRANSPORT_WIRED };
enum { CONSOLE_MP_LOBBY_HOSTING, CONSOLE_MP_LOBBY_CONNECTED, CONSOLE_MP_LOBBY_BROWSING };
enum { CONSOLE_MP_LAUNCH_NONE, CONSOLE_MP_LAUNCH_DOOM, CONSOLE_MP_LAUNCH_NATIVE };
enum { CONSOLE_MULTIPLAYER_START_HOLD_MS=300, CONSOLE_MULTIPLAYER_START_TIMEOUT_MS=5000, CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS=15000 };
#define ESP_LOGI(...) ((void)0)
static bool arena=true, content_ready=true;
static bool s_arena_launch_frozen, s_multiplayer_launch_due;
static unsigned s_multiplayer_transport, s_multiplayer_lobby_state, s_multiplayer_launch_kind;
static uint32_t s_multiplayer_local_peer_id;
static uint8_t s_multiplayer_player_count,s_multiplayer_local_player_slot;
static uint64_t s_multiplayer_launch_route_id,s_multiplayer_launch_session_seed;
static int64_t s_multiplayer_next_start_ready_us;
static bool s_multiplayer_launch_shared_dice;
static uint32_t s_multiplayer_native_launcher_id, s_multiplayer_target_session_id;
static uint64_t s_multiplayer_target_lobby_id;
static int64_t s_multiplayer_peer_last_seen_us,s_multiplayer_next_control_us,s_multiplayer_next_discovery_us;
static unsigned reset_routes;
static void clear_remote_multiplayer_offer(void) {}
static void multiplayer_transport_reset_route(void) { ++reset_routes; }
static p4_mp_session_t s_multiplayer_session;
static p4_mp_lobby_offer_t s_multiplayer_local_offer;
static p4_mp_group_start_t s_multiplayer_group_start;
static p4_mp_start_barrier_t s_multiplayer_start_barrier;
static p4_doom_mp_launch_config_t s_doom_multiplayer_launch;
static p4_doom_mp_setup_t s_multiplayer_local_setup;
static bool multiplayer_selected_game_is_arena(void) { return arena; }
static bool multiplayer_local_content_ready(void) { return content_ready; }
static int64_t esp_timer_get_time(void) { return 1000000; }
static esp_err_t send_multiplayer_start_ready(p4_mp_packet_type_t type) { (void)type; return ESP_OK; }
'''
CASES=r'''
static void reset_fixture(void) {
    arena=content_ready=true;
    s_arena_launch_frozen=s_multiplayer_launch_due=false;
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_HOSTING;
    s_multiplayer_launch_kind=CONSOLE_MP_LAUNCH_NONE;
    s_multiplayer_local_peer_id=20;
    s_multiplayer_player_count=0; s_multiplayer_local_player_slot=0;
    s_multiplayer_launch_session_seed=40; s_multiplayer_launch_route_id=0; reset_routes=0;
    s_multiplayer_group_start=(p4_mp_group_start_t){0};
    s_doom_multiplayer_launch=(p4_doom_mp_launch_config_t){0};
    s_multiplayer_session=(p4_mp_session_t){.role=P4_MP_ROLE_HOST,
        .state=P4_MP_SESSION_HOSTING,.self_peer_id=20,.session_id=10};
    s_multiplayer_local_setup=(p4_doom_mp_setup_t){.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,
        .mode=P4_DOOM_MP_MODE_ALTDEATH,.episode=1,.map=1,.skill=3,.no_monsters=true};
    s_multiplayer_local_offer=(p4_mp_lobby_offer_t){.player_capacity=4,
        .players_present=1,.game_protocol=P4_DOOM_ARENA_CHECKPOINT_PROTOCOL,.session_seed=40,.input_delay_tics=2};
    assert(p4_doom_mp_setup_encode(&s_multiplayer_local_setup,s_multiplayer_local_offer.game_settings));
}
static void check_solo(void) {
    reset_fixture(); assert(multiplayer_accepting_members());
    assert(multiplayer_solo_arena_host_ready() && multiplayer_start_prerequisites_ready());
    assert(begin_multiplayer_start_sync()==ESP_OK);
    assert(s_multiplayer_launch_due && s_arena_launch_frozen && !multiplayer_accepting_members());
    assert(s_multiplayer_player_count==1 && s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE);
    assert(s_doom_multiplayer_launch.player_count==4 && s_doom_multiplayer_launch.initial_player_mask==1);
    assert(s_doom_multiplayer_launch.self_peer_id==20 && s_doom_multiplayer_launch.session_id==10);
    assert(!s_doom_multiplayer_launch.remote_peer_id && !s_doom_multiplayer_launch.route_id);
    assert(p4_doom_mp_launch_config_valid(&s_doom_multiplayer_launch));
    s_multiplayer_launch_due=false; /* OS consumes due before exclusive handoff. */
    assert(!multiplayer_accepting_members() && begin_multiplayer_start_sync()!=ESP_OK);
}
static void check_solo_gates(void) {
    for(unsigned v=0;v<9;++v) {
        reset_fixture();
        if(v==0) arena=false;
        if(v==1) content_ready=false;
        if(v==2) s_multiplayer_transport=CONSOLE_MP_TRANSPORT_BLE;
        if(v==3) s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIRED;
        if(v==4) s_multiplayer_session.role=P4_MP_ROLE_CLIENT;
        if(v==5) s_multiplayer_session.state=P4_MP_SESSION_CONNECTED;
        if(v==6) s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_BROWSING;
        if(v==7) s_multiplayer_session.peers[0].connected=true;
        if(v==8) s_arena_launch_frozen=true;
        assert(!multiplayer_solo_arena_host_ready());
        assert(begin_multiplayer_start_sync()!=ESP_OK && !s_multiplayer_launch_due);
    }
}
static void group_fixture(unsigned members) {
    reset_fixture();
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_CONNECTED;
    s_multiplayer_session.state=P4_MP_SESSION_CONNECTED;
    s_multiplayer_launch_kind=CONSOLE_MP_LAUNCH_DOOM;
    s_doom_multiplayer_launch=(p4_doom_mp_launch_config_t){.enabled=true,.player_count=4,
        .role=P4_MP_ROLE_HOST,.session_id=10,.self_peer_id=20,.remote_peer_id=30,
        .route_id=90,.session_seed=40,.initial_player_mask=(uint8_t)((1U<<members)-1U),
        .input_delay_tics=2,.setup=s_multiplayer_local_setup};
    for(unsigned i=0;i<members-1;++i) s_multiplayer_session.peers[i]=(p4_mp_peer_t){
        .connected=true,.peer_id=30+i,.route_id=90+i,.player_slot=(uint8_t)(i+1)};
    s_multiplayer_local_offer.players_present=(uint8_t)members;
}
static void check_group(void) {
    for(unsigned count=2;count<=4;++count) {
        group_fixture(count);
        assert(begin_multiplayer_start_sync()==ESP_OK);
        assert(s_multiplayer_group_start.count==count && s_multiplayer_player_count==count);
        assert(s_doom_multiplayer_launch.player_count==4);
        assert(p4_doom_mp_launch_config_valid(&s_doom_multiplayer_launch));
        assert(s_doom_multiplayer_launch.initial_player_mask==(1U<<count)-1U);
        assert(s_doom_multiplayer_launch.lobby_offer.players_present==count);
        assert(s_arena_launch_frozen && !s_multiplayer_launch_due && !multiplayer_accepting_members());
    }
    for(unsigned v=0;v<5;++v) {
        group_fixture(3);
        if(v==0) s_multiplayer_session.peers[1].player_slot=1;
        if(v==1) s_multiplayer_session.peers[1].player_slot=3;
        if(v==2) s_multiplayer_session.peers[1].player_slot=0;
        if(v==3) s_multiplayer_session.peers[1].route_id=0;
        if(v==4) s_multiplayer_session.peers[1].peer_id=0;
        assert(begin_multiplayer_start_sync()!=ESP_OK);
        assert(s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE && !s_arena_launch_frozen);
    }
}
static void check_ordinary(void) {
    group_fixture(3); arena=false; s_doom_multiplayer_launch.initial_player_mask=0;
    s_doom_multiplayer_launch.setup.game=P4_DOOM_MP_GAME_DOOM;
    assert(begin_multiplayer_start_sync()==ESP_OK);
    assert(s_doom_multiplayer_launch.player_count==3 && !s_doom_multiplayer_launch.initial_player_mask);
    assert(!s_arena_launch_frozen);
    reset_fixture(); arena=false; s_multiplayer_local_offer.player_capacity=2;
    assert(begin_multiplayer_start_sync()!=ESP_OK);
    s_multiplayer_session.state=P4_MP_SESSION_CONNECTED;
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_CONNECTED;
    s_multiplayer_launch_kind=CONSOLE_MP_LAUNCH_NATIVE;
    assert(begin_multiplayer_start_sync()==ESP_OK && !s_arena_launch_frozen);
}
static void check_reopen(void) {
    const p4_mp_group_phase_t phases[]={P4_MP_GROUP_WAITING,P4_MP_GROUP_COMMITTED};
    for(unsigned phase=0;phase<2;++phase) {
        group_fixture(2); assert(begin_multiplayer_start_sync()==ESP_OK);
        s_multiplayer_group_start.phase=phases[phase];
        assert(s_arena_launch_frozen && !multiplayer_accepting_members());
        /* Both early guest LEAVE and disconnected launch route use this real
         * recovery helper while the start barrier has not become due. */
        assert(reopen_multiplayer_host_lobby()==ESP_OK);
        assert(s_multiplayer_session.state==P4_MP_SESSION_HOSTING);
        assert(s_multiplayer_session.session_id==10 && s_multiplayer_session.self_peer_id==20);
        assert(!s_arena_launch_frozen && multiplayer_accepting_members());
        assert(s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE && !s_multiplayer_launch_due);
        assert(reset_routes==1 && s_multiplayer_local_offer.players_present==1);
        assert(multiplayer_solo_arena_host_ready());
        assert(begin_multiplayer_start_sync()==ESP_OK && s_multiplayer_launch_due);
        assert(s_doom_multiplayer_launch.initial_player_mask==1);
    }
}
static void check_validator(void) {
    reset_fixture(); assert(begin_multiplayer_start_sync()==ESP_OK);
    const p4_doom_mp_launch_config_t solo=s_doom_multiplayer_launch;
    for(unsigned v=0;v<11;++v) {
        p4_doom_mp_launch_config_t c=solo;
        if(v==0)c.initial_player_mask=0;
        if(v==1)c.initial_player_mask=2;
        if(v==2)c.initial_player_mask=0x11;
        if(v==3)c.player_count=2;
        if(v==4)c.role=P4_MP_ROLE_CLIENT;
        if(v==5)c.local_player_slot=1;
        if(v==6)c.remote_peer_id=30;
        if(v==7)c.route_id=90;
        if(v==8)c.rejoining=true;
        if(v==9)c.session_id=0;
        if(v==10)c.setup.game=P4_DOOM_MP_GAME_DOOM;
        assert(!p4_doom_mp_launch_config_valid(&c));
    }
    p4_doom_mp_launch_config_t guest=solo;
    guest.role=P4_MP_ROLE_CLIENT; guest.local_player_slot=2;
    guest.remote_peer_id=30;guest.route_id=90;guest.rejoining=true;
    assert(p4_doom_mp_launch_config_valid(&guest));
    guest.rejoining=false;assert(!p4_doom_mp_launch_config_valid(&guest));
    guest.initial_player_mask=7;assert(p4_doom_mp_launch_config_valid(&guest));
    guest.initial_player_mask=0;guest.setup.game=P4_DOOM_MP_GAME_DOOM;guest.player_count=3;
    assert(p4_doom_mp_launch_config_valid(&guest));
    guest.initial_player_mask=7;assert(!p4_doom_mp_launch_config_valid(&guest));
    guest=(p4_doom_mp_launch_config_t){0};assert(p4_doom_mp_launch_config_valid(&guest));
    guest.initial_player_mask=1;assert(!p4_doom_mp_launch_config_valid(&guest));
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"solo"))check_solo();
    else if(!strcmp(argv[1],"solo-gates"))check_solo_gates();
    else if(!strcmp(argv[1],"group"))check_group();
    else if(!strcmp(argv[1],"ordinary"))check_ordinary();
    else if(!strcmp(argv[1],"validator"))check_validator();
    else if(!strcmp(argv[1],"reopen"))check_reopen();
    else return 2;
    return 0;
}
'''
class ArenaStart(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='p4-arena-start-')
        cls.addClassCleanup(cls.temp.cleanup)
        path=Path(cls.temp.name)
        source=Path(os.environ.get('P4_CONSOLE_START_SOURCE', ROOT/'apps/console_os/main/console_os_main.c')).read_text()
        functions='\n'.join(fixture.function(source,name) for name in (
            'multiplayer_group_enabled','multiplayer_members','multiplayer_accepting_members',
            'multiplayer_start_token','multiplayer_solo_arena_host_ready',
            'multiplayer_start_prerequisites_ready','multiplayer_arena_initial_mask',
            'begin_multiplayer_start_sync','reopen_multiplayer_host_lobby'))
        (path/'test.c').write_text(PRELUDE+functions+CASES)
        cls.binary=path/'test'
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined',
            '-I'+str(ROOT/'components/doom_multiplayer/include'),
            '-I'+str(BASE/'components/doom_multiplayer/include'),
            '-I'+str(BASE/'components/p4_multiplayer/include'),str(path/'test.c'),
            str(ROOT/'components/doom_multiplayer/src/doom_multiplayer.c'),
            str(BASE/'components/p4_multiplayer/src/group.c'),
            str(BASE/'components/p4_multiplayer/src/session.c'),
            str(BASE/'components/p4_multiplayer/src/packet.c'),
            str(BASE/'components/p4_multiplayer/src/lobby.c'),
            str(BASE/'components/p4_multiplayer/src/start.c'),'-o',str(cls.binary)],check=True)
    def case(self,name):subprocess.run([str(self.binary),name],check=True)
    def test_solo_freezes_capacity_and_original_mask(self):self.case('solo')
    def test_solo_is_only_ready_arena_wifi_host(self):self.case('solo-gates')
    def test_group_freezes_exact_roster_before_barrier(self):self.case('group')
    def test_ordinary_doom_native_start_contract_unchanged(self):self.case('ordinary')
    def test_group_departure_reopens_admission_and_allows_solo_start(self):self.case('reopen')
    def test_validator_gates_role_capacity_mask_and_solo_route(self):self.case('validator')
if __name__=='__main__':unittest.main(verbosity=2)
