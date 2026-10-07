#!/usr/bin/env python3
"""Execute Console OS Arena resume admission with real wire/session codecs.

Extract unchanged OS admission, launch configuration, and credential storage
functions. Fixture the clock/random/radio, unrelated native-game services, and
lobby reset; observe storage before the direct-launch flag can be set. This is
host control-flow coverage, not an engine handoff or device gameplay test.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("P4_CONSOLE_BASE", ROOT))
def source_path(relative):
    staged = ROOT / relative
    return staged if staged.exists() else BASE / relative
SOURCE = Path(os.environ.get("P4_CONSOLE_RESUME_SOURCE", ROOT / "apps/console_os/main/console_os_main.c"))


def function(source, name):
    match = re.search(r"^(?:static\s+)?[^;{}\n]*?\b" + re.escape(name) + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if not match:
        raise ValueError(f"No function {name}")
    brace = match.end() - 1
    depth = 0
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():brace + token.end()]
    raise ValueError(f"Unclosed function {name}")


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "p4/multiplayer.h"
#include "p4/multiplayer_group.h"
#include "p4/game_package.h"
#include "p4/doom_multiplayer.h"
#include "p4/doom_resume.h"
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=-1, ESP_ERR_INVALID_STATE=-2 };
enum { CONSOLE_MP_LOBBY_BROWSING, CONSOLE_MP_LOBBY_JOINING, CONSOLE_MP_LOBBY_CONNECTED };
enum { CONSOLE_MP_TRANSPORT_WIFI, CONSOLE_MP_TRANSPORT_BLE };
enum { CONSOLE_MP_LAUNCH_NONE, CONSOLE_MP_LAUNCH_DOOM, CONSOLE_MP_LAUNCH_NATIVE };
#define CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS 3000
#define ESP_LOGI(...) ((void)0)
static p4_mp_session_t s_multiplayer_session;
static p4_doom_resume_record_t s_arena_resume;
static uint8_t s_arena_resume_rtc[P4_DOOM_RESUME_RECORD_BYTES];
static bool s_arena_resume_valid;
static uint64_t s_arena_resume_nonce;
static uint32_t s_arena_join_nonce;
static uint32_t s_multiplayer_local_peer_id, s_multiplayer_remote_peer_id, s_multiplayer_remote_session_id;
static uint64_t s_multiplayer_remote_route_id;
static unsigned s_multiplayer_lobby_state, s_multiplayer_transport, s_multiplayer_launch_kind;
static char s_multiplayer_status[128];
static p4_mp_lobby_offer_t s_multiplayer_local_offer, s_multiplayer_remote_offer;
static p4_doom_mp_launch_config_t s_doom_multiplayer_launch;
static p4_doom_mp_setup_t s_multiplayer_local_setup;
static p4_mp_start_barrier_t s_multiplayer_start_barrier;
static p4_mp_group_start_t s_multiplayer_group_start;
static uint8_t s_multiplayer_local_player_slot, s_multiplayer_player_count;
static uint32_t s_multiplayer_native_launcher_id, s_multiplayer_launch_session_seed;
static uint64_t s_multiplayer_launch_route_id;
static int64_t s_multiplayer_next_control_us, s_multiplayer_next_start_ready_us;
static bool s_multiplayer_launch_shared_dice;
static size_t s_multiplayer_game_selection;
static bool s_multiplayer_launch_due;
static bool receiving_reply, fail_store;
static unsigned stores, resets, sent;
static char reset_reason[64];
static uint8_t sent_payload[P4_MP_MAX_PAYLOAD_BYTES];
static uint16_t sent_length;
static p4_mp_packet_type_t sent_type;
static uint32_t random_counter;
static uint32_t random_nonzero(void) { return ++random_counter; }
static int64_t esp_timer_get_time(void) { return 1000000; }
static esp_err_t send_session_multiplayer_packet(p4_mp_packet_type_t type, uint32_t ack,
    const uint8_t *payload, uint16_t length) {
    (void)ack; ++sent; sent_type=type; sent_length=length;
    memcpy(sent_payload,payload,length); return ESP_OK;
}
static void reset_multiplayer_lobby(const char *reason) {
    ++resets; snprintf(reset_reason,sizeof(reset_reason),"%s",reason);
    s_arena_resume_nonce=0; s_arena_join_nonce=0;
    p4_mp_session_init(&s_multiplayer_session);
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_BROWSING;
    s_doom_multiplayer_launch=(p4_doom_mp_launch_config_t){0};
    s_multiplayer_launch_kind=CONSOLE_MP_LAUNCH_NONE;
    s_multiplayer_launch_due=false;
}
/* These services are compiled only to preserve the production function's
 * unrelated native branch. Entering that branch is a fixture failure. */
typedef struct { p4_game_package_info_t package; } platform_game_catalog_entry_t;
static const platform_game_catalog_entry_t *multiplayer_selected_native_game(void) {
    assert(false); return NULL;
}
static bool native_game_supports_multiplayer(const platform_game_catalog_entry_t *game) {
    (void)game; assert(false); return false;
}
static bool native_multiplayer_content_identity(const platform_game_catalog_entry_t *game,
    uint8_t identity[P4_MP_SHA256_BYTES]) {
    (void)game; (void)identity; assert(false); return false;
}
static p4_mp_game_mode_t native_multiplayer_mode(const p4_game_multiplayer_profile_t *profile) {
    (void)profile; assert(false); return P4_MP_GAME_MODE_NONE;
}
static bool p4_mp_game_dice_settings_decode(const p4_game_package_info_t *package,
    const uint8_t settings[P4_MP_GAME_SETTINGS_BYTES], bool *enabled) {
    (void)package; (void)settings; (void)enabled; assert(false); return false;
}
static bool multiplayer_dice_available(void) { assert(false); return false; }
'''

STORE_OBSERVER = r'''
#undef p4_doom_arena_resume_store
/* Only rename the extracted storage symbol; its body and RTC codec are real.
 * This observer checks ordering and provides a deliberate storage-failure
 * injection, without replacing launch configuration or successful storage. */
static bool p4_doom_arena_resume_store(const p4_doom_mp_launch_config_t *config,
    const uint8_t ticket[16]) {
    if (receiving_reply) {
        ++stores;
        assert(!s_multiplayer_launch_due);
        assert(s_multiplayer_session.state==P4_MP_SESSION_CONNECTED);
        assert(s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_CONNECTED);
        assert(s_multiplayer_launch_kind==CONSOLE_MP_LAUNCH_DOOM);
        assert(config==&s_doom_multiplayer_launch && config->rejoining);
        assert(p4_doom_mp_launch_config_valid(config));
        assert(s_multiplayer_start_barrier.state==P4_MP_START_IDLE);
        assert(s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE);
        if (fail_store) return false;
    }
    return observed_arena_resume_store(config,ticket);
}
'''

CASES = r'''
_Static_assert(P4_DOOM_ARENA_CHECKPOINT_PROTOCOL == 8U, "checkpoint protocol must differ from legacy Arena 7");
static void reset_fixture(void) {
    stores=resets=sent=0; random_counter=100;
    s_multiplayer_game_selection=P4_DOOM_MP_GAME_GAME_CHANGERS_AI;
    receiving_reply=fail_store=false; reset_reason[0]=0;
    s_multiplayer_local_peer_id=999; s_multiplayer_launch_kind=CONSOLE_MP_LAUNCH_NONE;
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_JOINING;
    s_multiplayer_launch_due=false; s_arena_resume_nonce=0; s_arena_join_nonce=0; s_arena_resume_valid=true;
    s_doom_multiplayer_launch=(p4_doom_mp_launch_config_t){0};
    s_arena_resume=(p4_doom_resume_record_t){ .session_id=10,.self_peer_id=20,
        .host_peer_id=30,.session_seed=40,.slot=2,.player_count=4,.initial_player_mask=7,.ticket={5},
        .compatibility_sha256={6} };
    s_multiplayer_remote_offer=(p4_mp_lobby_offer_t){.session_seed=40,.input_delay_tics=2,
        .compatibility_sha256={6},.player_capacity=4,.game_protocol=P4_DOOM_ARENA_CHECKPOINT_PROTOCOL};
    const p4_doom_mp_setup_t setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,
        .mode=P4_DOOM_MP_MODE_ALTDEATH,.episode=1,.map=1,.skill=3,.no_monsters=true};
    s_multiplayer_local_setup=setup;
    /* Nonzero stale launch bookkeeping must be replaced by real configure. */
    s_multiplayer_native_launcher_id=123;
    s_multiplayer_local_player_slot=P4_MP_PLAYER_SLOT_ANY;
    s_multiplayer_player_count=0;
    s_multiplayer_launch_route_id=s_multiplayer_launch_session_seed=0;
    s_multiplayer_next_control_us=s_multiplayer_next_start_ready_us=123;
    s_multiplayer_start_barrier=(p4_mp_start_barrier_t){.state=P4_MP_START_ARMED,
        .token=123,.hold_ms=300,.timeout_ms=5000,.started_ms=500,.launch_at_ms=800};
    s_multiplayer_group_start=(p4_mp_group_start_t){.phase=P4_MP_GROUP_WAITING};
    assert(p4_doom_mp_setup_encode(&setup,s_multiplayer_remote_offer.game_settings));
    s_multiplayer_local_offer=s_multiplayer_remote_offer;
    assert(p4_doom_resume_record_encode(&s_arena_resume,s_arena_resume_rtc));
}
static void start_resume(void) {
    reset_fixture();
    assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
    assert(s_multiplayer_session.state==P4_MP_SESSION_JOINING);
    assert(s_multiplayer_session.self_peer_id==20);
    assert(s_arena_resume_nonce!=0 && !stores && !s_multiplayer_launch_due);
}
static p4_doom_resume_control_t accepted(void) {
    p4_doom_resume_control_t response={ .type=P4_DOOM_RESUME_ACCEPTED,
        .slot=2,.player_count=4,.initial_player_mask=7,.ticket={5},.nonce=s_arena_resume_nonce,
        .accept={.assigned_player_slot=2,.player_count=4,.input_delay_tics=2,
        .start_tic=0,.session_seed=40} };
    memcpy(response.accept.game_settings,s_multiplayer_remote_offer.game_settings,
        sizeof(response.accept.game_settings));
    return response;
}
static bool receive_reply(p4_doom_resume_control_t *response, uint32_t session,
    uint32_t peer, uint64_t route, uint32_t sequence) {
    uint8_t payload[P4_DOOM_RESUME_CONTROL_BYTES]; size_t length=0;
    p4_doom_resume_control_t wire=*response;
    wire.accept.start_tic=0;
    assert(p4_doom_resume_control_encode(&wire,payload,&length));
    /* Invalid wire tic bypasses the sender-side codec guard intentionally. */
    if(response->accept.start_tic) payload[36]=(uint8_t)response->accept.start_tic;
    p4_mp_packet_view_t packet={.type=P4_MP_PACKET_GAME_MESSAGE,.session_id=session,
        .peer_id=peer,.sequence=sequence,.payload=payload,.payload_length=(uint16_t)length};
    receiving_reply=true;
    const bool handled=receive_multiplayer_arena_resume(route,&packet);
    receiving_reply=false;
    return handled;
}
static void check_launch_and_record(const p4_doom_resume_control_t *response,
    uint32_t self_peer) {
    assert(stores==1 && !resets && s_multiplayer_launch_due);
    assert(s_multiplayer_session.state==P4_MP_SESSION_CONNECTED);
    assert(s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_CONNECTED);
    assert(s_multiplayer_launch_kind==CONSOLE_MP_LAUNCH_DOOM);
    assert(!s_multiplayer_native_launcher_id);
    assert(s_multiplayer_local_player_slot==response->slot && s_multiplayer_player_count==4);
    assert(s_multiplayer_launch_route_id==90 && s_multiplayer_launch_session_seed==40);
    assert(!s_multiplayer_next_control_us && !s_multiplayer_next_start_ready_us);
    assert(s_multiplayer_start_barrier.state==P4_MP_START_IDLE && !s_multiplayer_start_barrier.token);
    assert(s_multiplayer_group_start.phase==P4_MP_GROUP_IDLE);
    const p4_doom_mp_launch_config_t *config=&s_doom_multiplayer_launch;
    assert(p4_doom_mp_launch_config_valid(config));
    assert(config->enabled && config->rejoining && config->role==P4_MP_ROLE_CLIENT);
    assert(config->session_id==10 && config->self_peer_id==self_peer);
    assert(config->remote_peer_id==30 && config->route_id==90);
    assert(config->local_player_slot==response->slot && config->player_count==4);
    assert(config->initial_player_mask==response->initial_player_mask);
    assert(config->session_seed==40 && !config->start_tic && config->input_delay_tics==2);
    assert(config->resume_nonce==response->nonce);
    assert(!memcmp(config->resume_ticket,response->ticket,16));
    assert(!memcmp(&config->lobby_offer,&s_multiplayer_remote_offer,sizeof(config->lobby_offer)));
    uint8_t settings[P4_DOOM_MP_SETUP_BYTES];
    assert(p4_doom_mp_setup_encode(&config->setup,settings));
    assert(!memcmp(settings,response->accept.game_settings,sizeof(settings)));
    p4_doom_resume_record_t record;
    assert(s_arena_resume_valid && p4_doom_resume_record_decode(s_arena_resume_rtc,&record));
    assert(record.session_id==10 && record.self_peer_id==self_peer && record.host_peer_id==30);
    assert(record.session_seed==40 && record.slot==response->slot && record.player_count==4);
    assert(record.initial_player_mask==response->initial_player_mask);
    assert(!memcmp(record.ticket,response->ticket,16));
    assert(!memcmp(record.compatibility_sha256,s_multiplayer_remote_offer.compatibility_sha256,32));
}
static void check_request(void) {
    start_resume(); p4_doom_resume_control_t request;
    assert(sent_type==P4_MP_PACKET_GAME_MESSAGE && sent_length==64);
    assert(p4_doom_resume_control_decode(sent_payload,sent_length,&request));
    assert(request.type==P4_DOOM_RESUME_REQUEST && request.slot==2 && request.player_count==4 && request.initial_player_mask==7);
    assert(request.nonce==s_arena_resume_nonce && !memcmp(request.ticket,s_arena_resume.ticket,16));
    assert(!memcmp(request.compatibility_sha256,s_arena_resume.compatibility_sha256,32));
    uint8_t original[64]; memcpy(original,sent_payload,64);
    assert(send_multiplayer_join()==ESP_OK && sent==2);
    assert(sent_type==P4_MP_PACKET_GAME_MESSAGE && !memcmp(original,sent_payload,64));
}
static void check_old_protocol(void) {
    for(unsigned returning=0; returning<2; ++returning) {
        reset_fixture(); s_arena_resume_valid=returning!=0;
        s_multiplayer_remote_offer.game_protocol=7U;
        assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
        assert(!s_arena_resume_nonce && !s_arena_join_nonce);
        assert(s_multiplayer_session.self_peer_id==999);
        assert(sent_type==P4_MP_PACKET_JOIN && !stores && !s_multiplayer_launch_due);
        p4_mp_lobby_join_t join;
        assert(p4_mp_lobby_join_decode(sent_payload,sent_length,&join)==P4_MP_OK);
        /* Ordinary JOIN retains its identity-derived nonce, without an Arena ticket. */
        assert(join.join_nonce==(999U ^ 30U ^ 10U) && random_counter==100);

        reset_fixture(); s_arena_resume_valid=returning!=0;
        assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
        assert(returning ? s_arena_resume_nonce!=0 : s_arena_join_nonce!=0);
        p4_doom_resume_control_t response=accepted();
        if(!returning) { response.nonce=s_arena_join_nonce; response.initial_player_mask=1; response.ticket[0]=9; }
        uint8_t saved[P4_DOOM_RESUME_RECORD_BYTES]; memcpy(saved,s_arena_resume_rtc,sizeof(saved));
        /* The otherwise valid reply cannot cross the protocol gate. */
        s_multiplayer_remote_offer.game_protocol=7U;
        assert(!receive_reply(&response,10,30,90,1));
        assert(!stores && !resets && !s_multiplayer_launch_due);
        assert(s_multiplayer_session.state==P4_MP_SESSION_JOINING);
        assert(!memcmp(saved,s_arena_resume_rtc,sizeof(saved)));
    }
}
static void check_other_room(void) {
    /* Every independent identity dimension has to match before using the ticket. */
    for(unsigned variant=0; variant<7; ++variant) {
        reset_fixture(); uint32_t session=10,host=30;
        if(variant==0) s_arena_resume_valid=false;
        if(variant==1) s_multiplayer_game_selection=P4_DOOM_MP_GAME_DOOM;
        if(variant==2) s_multiplayer_transport=CONSOLE_MP_TRANSPORT_BLE;
        if(variant==3) ++session;
        if(variant==4) ++host;
        if(variant==5) ++s_multiplayer_remote_offer.session_seed;
        if(variant==6) ++s_multiplayer_remote_offer.compatibility_sha256[0];
        assert(start_multiplayer_client_for_offer(session,host,90)==ESP_OK);
        assert(!s_arena_resume_nonce && s_multiplayer_session.self_peer_id==999);
        assert(sent_type==P4_MP_PACKET_JOIN && !stores && !s_multiplayer_launch_due);
    }
}
static void check_untrusted_response(void) {
    for(unsigned variant=0; variant<13; ++variant) {
        start_resume(); p4_doom_resume_control_t response=accepted();
        uint32_t session=10,peer=30,sequence=1; uint64_t route=90;
        if(variant==0) ++session;
        if(variant==1) ++peer;
        if(variant==2) ++route;
        if(variant==3) ++response.nonce;
        if(variant==4) ++response.ticket[0];
        if(variant==5) response.slot=response.accept.assigned_player_slot=1;
        if(variant==6) response.player_count=response.accept.player_count=3;
        if(variant==7) ++response.accept.session_seed;
        if(variant==8) ++response.accept.input_delay_tics;
        if(variant==9) ++response.accept.game_settings[0];
        if(variant==10) sequence=0;
        if(variant==11) response.initial_player_mask=3;
        if(variant==12) response.accept.start_tic=1;
        (void)receive_reply(&response,session,peer,route,sequence);
        assert(!stores && !resets && !s_multiplayer_launch_due);
        assert(s_multiplayer_session.state==P4_MP_SESSION_JOINING);
    }
}
static void check_accepted(void) {
    start_resume(); p4_doom_resume_control_t response=accepted();
    assert(receive_reply(&response,10,30,90,1));
    check_launch_and_record(&response,20);
    assert(!receive_reply(&response,10,30,90,2) && stores==1);
}
static void check_unavailable(void) {
    start_resume(); p4_doom_resume_control_t response=accepted();
    response.type=P4_DOOM_RESUME_UNAVAILABLE; response.reason=1;
    uint64_t nonce=response.nonce; ++response.nonce;
    assert(receive_reply(&response,10,30,90,1) && !resets);
    response.nonce=nonce;
    assert(receive_reply(&response,10,30,90,2));
    assert(resets==1 && !stores && !s_multiplayer_launch_due && !s_arena_resume_nonce);
    assert(s_multiplayer_lobby_state==CONSOLE_MP_LOBBY_BROWSING);
    assert(strstr(s_multiplayer_status,"cannot admit") && s_arena_resume_valid);
}
static void check_retained_credential(void) {
    reset_fixture();
    p4_doom_mp_launch_config_t config={.role=P4_MP_ROLE_CLIENT,.session_id=10,
        .self_peer_id=20,.remote_peer_id=30,.session_seed=40,.local_player_slot=2,
        .player_count=4,.initial_player_mask=7,.setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI},
        .lobby_offer={.compatibility_sha256={6}}};
    const uint8_t ticket[16]={8};
    assert(p4_doom_arena_resume_store(&config,ticket));
    p4_doom_resume_record_t record;
    assert(p4_doom_resume_record_decode(s_arena_resume_rtc,&record));
    assert(record.self_peer_id==20 && record.host_peer_id==30 && record.slot==2 && record.ticket[0]==8 && record.initial_player_mask==7);
    uint8_t original[80]; memcpy(original,s_arena_resume_rtc,80);
    config.role=P4_MP_ROLE_HOST;
    assert(!p4_doom_arena_resume_store(&config,ticket));
    assert(!memcmp(original,s_arena_resume_rtc,80));
    config.role=P4_MP_ROLE_CLIENT; config.setup.game=P4_DOOM_MP_GAME_DOOM;
    assert(!p4_doom_arena_resume_store(&config,ticket));
    assert(!memcmp(original,s_arena_resume_rtc,80));
}

static void check_fresh(void) {
    reset_fixture(); s_arena_resume_valid=false;
    assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
    assert(sent_type==P4_MP_PACKET_JOIN && s_arena_join_nonce && !s_arena_resume_nonce);
    p4_mp_lobby_join_t join;
    assert(p4_mp_lobby_join_decode(sent_payload,sent_length,&join)==P4_MP_OK);
    assert(join.join_nonce==s_arena_join_nonce);
    uint8_t original[P4_MP_JOIN_PAYLOAD_BYTES]; memcpy(original,sent_payload,sizeof(original));
    assert(send_multiplayer_join()==ESP_OK && !memcmp(original,sent_payload,sizeof(original)));
    p4_doom_resume_control_t response=accepted();
    response.nonce=s_arena_join_nonce; response.initial_player_mask=1; response.ticket[0]=9;
    ++response.nonce;
    assert(receive_reply(&response,10,30,90,1) && !stores && !s_arena_resume_valid);
    --response.nonce;
    assert(receive_reply(&response,10,30,90,2));
    check_launch_and_record(&response,999);
    assert(!receive_reply(&response,10,30,90,3) && stores==1);
}
static void check_acceptance_rosters(void) {
    const uint8_t masks[]={1,3,7,15};
    for(unsigned returning=0;returning<2;++returning) {
        for(unsigned mask=0;mask<sizeof(masks);++mask) {
            for(uint8_t slot=1;slot<4;++slot) {
                /* A fresh seat was absent at tic zero. Returning guests can
                 * be either original members or previously activated joins. */
                if(!returning && (masks[mask] & (1U<<slot))) continue;
                reset_fixture(); s_arena_resume_valid=returning;
                s_arena_resume.slot=slot; s_arena_resume.initial_player_mask=masks[mask];
                assert(p4_doom_resume_record_encode(&s_arena_resume,s_arena_resume_rtc));
                p4_doom_mp_setup_t setup=s_multiplayer_local_setup;
                setup.map=29; setup.skill=P4_DOOM_MP_MAX_SKILL;
                assert(p4_doom_mp_setup_encode(&setup,s_multiplayer_remote_offer.game_settings));
                assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
                p4_doom_resume_control_t response=accepted();
                response.slot=response.accept.assigned_player_slot=slot;
                response.initial_player_mask=masks[mask];
                response.nonce=returning?s_arena_resume_nonce:s_arena_join_nonce;
                assert(receive_reply(&response,10,30,90,1));
                check_launch_and_record(&response,returning?20:999);
                assert(s_doom_multiplayer_launch.setup.map==29);
                assert(s_doom_multiplayer_launch.setup.skill==P4_DOOM_MP_MAX_SKILL);
            }
        }
    }
}
static void check_config_rejection(void) {
    for(unsigned returning=0;returning<2;++returning) {
        reset_fixture(); s_arena_resume_valid=returning;
        /* A peer-consistent accepted payload still must pass the actual
         * selected-game check inside configure_multiplayer_launch. */
        const p4_doom_mp_setup_t wrong_game={.game=P4_DOOM_MP_GAME_DOOM,
            .mode=P4_DOOM_MP_MODE_ALTDEATH,.episode=1,.map=1,.skill=3};
        assert(p4_doom_mp_setup_encode(&wrong_game,s_multiplayer_remote_offer.game_settings));
        assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
        p4_doom_resume_control_t response=accepted();
        response.nonce=returning?s_arena_resume_nonce:s_arena_join_nonce;
        uint8_t retained[P4_DOOM_RESUME_RECORD_BYTES];
        memcpy(retained,s_arena_resume_rtc,sizeof(retained));
        assert(receive_reply(&response,10,30,90,1));
        assert(resets==1 && !strcmp(reset_reason,"invalid-doom-setup"));
        assert(!stores && !s_multiplayer_launch_due);
        assert(s_multiplayer_launch_kind==CONSOLE_MP_LAUNCH_NONE);
        assert(s_arena_resume_valid==(bool)returning);
        assert(!memcmp(retained,s_arena_resume_rtc,sizeof(retained)));
    }
}
static void check_store_failure(void) {
    for(unsigned returning=0;returning<2;++returning) {
        reset_fixture(); s_arena_resume_valid=returning;
        assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
        p4_doom_resume_control_t response=accepted();
        response.nonce=returning?s_arena_resume_nonce:s_arena_join_nonce;
        if(!returning) { response.initial_player_mask=1; response.ticket[0]=9; }
        uint8_t retained[P4_DOOM_RESUME_RECORD_BYTES];
        memcpy(retained,s_arena_resume_rtc,sizeof(retained));
        fail_store=true;
        assert(receive_reply(&response,10,30,90,1));
        assert(stores==1 && resets==1 && !s_multiplayer_launch_due);
        assert(!strcmp(reset_reason,"arena-ticket-store-failed"));
        assert(s_multiplayer_launch_kind==CONSOLE_MP_LAUNCH_NONE);
        assert(s_arena_resume_valid==(bool)returning);
        assert(!memcmp(retained,s_arena_resume_rtc,sizeof(retained)));
    }
}
static void check_revoked(void) {
    start_resume(); p4_doom_resume_control_t response=accepted();
    response.type=P4_DOOM_RESUME_UNAVAILABLE;
    response.reason=P4_DOOM_RESUME_REASON_PROVISIONAL_EXPIRED;
    ++response.ticket[0];
    assert(receive_reply(&response,10,30,90,1) && !resets && s_arena_resume_valid);
    --response.ticket[0];
    assert(receive_reply(&response,10,30,90,2) && resets==1 && !s_arena_resume_valid);
    p4_doom_resume_record_t record;
    assert(!p4_doom_resume_record_decode(s_arena_resume_rtc,&record));
    assert(strstr(s_multiplayer_status,"Select this room") && !s_multiplayer_launch_due);
    s_multiplayer_lobby_state=CONSOLE_MP_LOBBY_JOINING;
    assert(start_multiplayer_client_for_offer(10,30,90)==ESP_OK);
    assert(sent_type==P4_MP_PACKET_JOIN && !s_arena_resume_nonce && s_arena_join_nonce);
}
static void check_full_room(void) {
    reset_fixture();
    assert(multiplayer_full_arena_room_returnable(10));
    assert(!multiplayer_full_arena_room_returnable(11));
    s_arena_resume_valid=false; assert(!multiplayer_full_arena_room_returnable(10));
    s_arena_resume_valid=true; s_multiplayer_game_selection=P4_DOOM_MP_GAME_DOOM;
    assert(!multiplayer_full_arena_room_returnable(10));
    s_multiplayer_game_selection=P4_DOOM_MP_GAME_GAME_CHANGERS_AI; s_multiplayer_transport=CONSOLE_MP_TRANSPORT_BLE;
    assert(!multiplayer_full_arena_room_returnable(10));
}

int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"request")) check_request();
    else if(!strcmp(argv[1],"old-protocol")) check_old_protocol();
    else if(!strcmp(argv[1],"other-room")) check_other_room();
    else if(!strcmp(argv[1],"untrusted-response")) check_untrusted_response();
    else if(!strcmp(argv[1],"accepted")) check_accepted();
    else if(!strcmp(argv[1],"unavailable")) check_unavailable();
    else if(!strcmp(argv[1],"retained-credential")) check_retained_credential();
    else if(!strcmp(argv[1],"fresh")) check_fresh();
    else if(!strcmp(argv[1],"acceptance-rosters")) check_acceptance_rosters();
    else if(!strcmp(argv[1],"config-rejection")) check_config_rejection();
    else if(!strcmp(argv[1],"store-failure")) check_store_failure();
    else if(!strcmp(argv[1],"revoked")) check_revoked();
    else if(!strcmp(argv[1],"full-room")) check_full_room();
    else return 2;
    return 0;
}
'''


class ArenaResume(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-console-resume-")
        cls.addClassCleanup(cls.temp.cleanup)
        path = Path(cls.temp.name)
        source = SOURCE.read_text()
        functions = "\n".join(function(source, name) for name in (
            "multiplayer_selected_game_is_doom", "multiplayer_selected_game_is_arena",
            "multiplayer_group_enabled", "multiplayer_full_arena_room_returnable"))
        functions += "\n#define p4_doom_arena_resume_store observed_arena_resume_store\n"
        functions += function(source, "p4_doom_arena_resume_store") + STORE_OBSERVER
        functions += "\n".join(function(source, name) for name in (
            "send_multiplayer_join", "start_multiplayer_client_for_offer",
            "configure_multiplayer_launch", "receive_multiplayer_arena_resume"))
        (path / "test.c").write_text(PRELUDE + functions + CASES)
        cls.binary = path / "test"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-I" + str(ROOT / "components/p4_multiplayer/include"),
            "-I" + str(BASE / "components/p4_multiplayer/include"),
            "-I" + str(ROOT / "components/doom_multiplayer/include"),
            "-I" + str(BASE / "components/doom_multiplayer/include"),
            "-I" + str(BASE / "components/p4_game_api/include"),
            "-I" + str(BASE / "components/p4_game_package/include"), str(path / "test.c"),
            *(str(source_path(item)) for item in ("components/doom_multiplayer/src/doom_resume.c",
                "components/doom_multiplayer/src/doom_multiplayer.c",
                "components/p4_multiplayer/src/session.c", "components/p4_multiplayer/src/lobby.c",
                "components/p4_multiplayer/src/packet.c",
                "components/p4_multiplayer/src/start.c")), "-o", str(cls.binary)], check=True)

    def case(self, name):
        subprocess.run([str(self.binary), name], check=True)

    def test_legacy_arena_cannot_use_checkpoint_join_or_resume(self): self.case("old-protocol")
    def test_first_join_replays_only_after_storing_credential(self): self.case("fresh")
    def test_real_config_preserves_initial_roster_capacity_and_host_settings(self): self.case("acceptance-rosters")
    def test_real_config_rejects_peer_consistent_wrong_game_without_storage(self): self.case("config-rejection")
    def test_storage_failure_cannot_schedule_fresh_or_returning_replay(self): self.case("store-failure")
    def test_exact_revocation_allows_explicit_fresh_retry(self): self.case("revoked")
    def test_full_room_return_is_bound_to_saved_session(self): self.case("full-room")
    def test_request_and_retry_keep_reserved_identity(self): self.case("request")
    def test_other_room_cannot_receive_saved_ticket(self): self.case("other-room")
    def test_wrong_identity_nonce_settings_sequence_cannot_launch(self): self.case("untrusted-response")
    def test_exact_reply_starts_one_guest_catchup(self): self.case("accepted")
    def test_unavailable_is_explicit_and_does_not_launch(self): self.case("unavailable")
    def test_only_arena_guest_can_save_credential(self): self.case("retained-credential")


if __name__ == "__main__":
    unittest.main(verbosity=2)
