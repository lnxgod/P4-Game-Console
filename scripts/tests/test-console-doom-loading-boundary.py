#!/usr/bin/env python3
"""Exercise the production Tab5 Doom handoff with controlled hardware boundaries.

Console functions and the main-loop transfer gate are extracted unchanged. The
real debug and snapshot protocols and Doom launch validator are linked; USB transfer parsers,
storage ownership/validation, display teardown and the engine entry are fixtures.
Parser fixtures count every call and simulate opening a file only after receiving
a complete request, including a request split across the loading transition.
No filesystem transfers, devices, network sockets or firmware builds are used.

Set P4_CONSOLE_LOADING_SOURCE to a saved console_os_main.c for a baseline run.
Sources predating sync_usb_transfer_availability use the old, unchanged block
extracted from poll_multiplayer_link; launch and dispatch are never substituted.
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
    path=ROOT / relative
    return path if path.exists() else BASE / relative

SOURCE = Path(os.environ.get(
    "P4_CONSOLE_LOADING_SOURCE", ROOT / "apps/console_os/main/console_os_main.c"))


def function(source, name):
    """Balance definition braces outside comments and C string literals."""
    match = re.search(r"^(?:static\s+)?[\w\s*]+?\b" + re.escape(name)
                      + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"No definition for {name}")
    brace = match.end() - 1
    tokens = re.finditer(
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
        source[brace:])
    depth = 0
    for token in tokens:
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():brace + token.end()]
    raise ValueError(f"Unclosed definition for {name}")


PRELUDE = r'''
#include <assert.h>
#include <inttypes.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console/shell.h"
#include "p4/debug_control.h"
#include "p4/debug_snapshot.h"
#include "p4/clock_control.h"
#include "p4/file_transfer.h"
#include "platform/game_storage.h"
#include "doom_p4mp_adapter.h"
#define P4_CONSOLE_H1_USB_DRIVE_CONTROL 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
enum { CONSOLE_APP_DOOM=1, CONSOLE_APP_CHEX_QUEST=14 };
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #x); exit(1); } } while (0)

static console_shell_t s_shell;
static p4_debug_control_t s_debug_control;
static p4_debug_snapshot_t s_debug_snapshot;
static p4_clock_control_t s_clock_control;
static const char *s_debug_runtime = "shell";
static char s_debug_game_id[P4_GAME_ID_MAX_BYTES];
static bool s_debug_touch_was_down, s_display_initialized;
static platform_game_storage_status_t s_game_storage_status;
static p4_mp_session_t s_multiplayer_session;
static p4_doom_p4mp_transport_t s_doom_multiplayer_transport;
static struct { unsigned game_volume_step; } s_console_settings;
static unsigned s_doom_handoff_count;
static void *s_pixels;
static uint8_t fixture_framebuffer;
static bool s_multiplayer_launch_due, s_arena_launch_frozen;
static char s_multiplayer_status[96];
static unsigned lobby_resets;
enum { CONSOLE_MP_TRANSPORT_WIFI, CONSOLE_MP_TRANSPORT_BLE, CONSOLE_MP_TRANSPORT_WIRED };
static unsigned s_multiplayer_transport;

static p4_file_transfer_info_t file_info;
static p4_content_transfer_info_t content_info;
static bool file_available, content_available;
static unsigned file_calls, content_calls, simulated_opens, clock_calls;
static unsigned usb_polls, network_polls, prepare_calls, cleanup_calls;
static unsigned loading_views, storage_calls, home_calls, handler_restores;
static unsigned engine_calls, replies, loading_status_replies, tick_waits;
static bool check_boundary, inject_request, inject_input, partial_request, inject_snapshot;
static bool service_usb, cleanup_terminal;
static unsigned request_kind;
static uint8_t last_reply[P4_DEBUG_RESPONSE_BYTES];
static size_t parser_used[2];
static const uint8_t requests[2][9] = {"P4F1OPEN", "P4M1OPEN"};
static esp_err_t prepare_result, storage_result, handler_result;
static platform_game_storage_state_t storage_after;
static bool validation_after;
static uint64_t fixture_now_ms;
static uint32_t fixture_paint_ms, s_ui_timing_last_render_us, s_ui_timing_last_submit_us;
static platform_game_storage_doom_load_progress_t fixture_load_progress;
static esp_err_t progress_result;
static console_shell_loading_info_t last_loading;
static unsigned snapshot_captures, snapshot_releases, snapshot_replies;
static uint8_t *snapshot_allocation;
static uint8_t snapshot_reply[P4_DEBUG_SNAPSHOT_MAX_RESPONSE];
static size_t snapshot_reply_size;
static jmp_buf engine_jump;
static p4_doom_mp_launch_config_t launch;
static const p4_doom_mp_launch_config_t base_launch = {
    .enabled=true, .role=P4_MP_ROLE_HOST, .session_id=1, .self_peer_id=2,
    .remote_peer_id=3, .route_id=4, .player_count=4, .initial_player_mask=3, .session_seed=5,
    .setup={.game=P4_DOOM_MP_GAME_GAME_CHANGERS_AI,
        .mode=P4_DOOM_MP_MODE_ALTDEATH, .episode=1, .map=1, .skill=3,
        .no_monsters=true}
};
'''

FIXTURES = r'''
static int64_t esp_timer_get_time(void) { return (int64_t)(fixture_now_ms*1000U); }
static uint32_t crc32(const uint8_t *bytes, size_t size) {
    uint32_t value=UINT32_MAX;
    for (size_t i=0;i<size;++i) {
        value ^= bytes[i];
        for (unsigned j=0;j<8;++j)
            value=(value>>1)^((value&1)?UINT32_C(0xedb88320):0);
    }
    return ~value;
}
static void put32(uint8_t *out, uint32_t value) {
    for (unsigned i=0;i<4;++i) out[i]=(uint8_t)(value>>(8*i));
}
static uint32_t get32(const uint8_t *in) {
    return (uint32_t)in[0]|(uint32_t)in[1]<<8|(uint32_t)in[2]<<16|(uint32_t)in[3]<<24;
}
static bool snapshot_capture(void *context,uint8_t **data,size_t *size,
    uint16_t *width,uint16_t *height) {
    REQUIRE(debug_snapshot_available(context) && snapshot_allocation==NULL);
    snapshot_allocation=malloc(16); REQUIRE(snapshot_allocation!=NULL);
    for (unsigned i=0;i<16;++i) snapshot_allocation[i]=(uint8_t)(i+32U);
    *data=snapshot_allocation; *size=16; *width=4; *height=2;
    ++snapshot_captures; return true;
}
static void snapshot_release(void *context,uint8_t *data) {
    REQUIRE(data!=NULL && data==snapshot_allocation);
    free(data); snapshot_allocation=NULL; ++snapshot_releases;
}
static bool snapshot_send(void *context,const uint8_t *bytes,size_t size) {
    REQUIRE(size>=36 && size<=sizeof(snapshot_reply));
    REQUIRE(!memcmp(bytes,"P4T1",4) && bytes[4]==1);
    const size_t payload=(size_t)bytes[24]|(size_t)bytes[25]<<8;
    REQUIRE(size==36+payload && get32(bytes+size-4)==crc32(bytes,size-4));
    memcpy(snapshot_reply,bytes,size); snapshot_reply_size=size;
    ++snapshot_replies; return true;
}
static void snapshot_packet(uint8_t *bytes,unsigned command,uint32_t id) {
    memset(bytes,0,P4_DEBUG_SNAPSHOT_REQUEST_BYTES);
    memcpy(bytes,"P4S1",4); bytes[4]=1; bytes[5]=(uint8_t)command;
    put32(bytes+8,id); put32(bytes+12,7);
    if (command==P4_DEBUG_SNAPSHOT_READ) bytes[20]=16;
    put32(bytes+28,crc32(bytes,28));
}
static void snapshot_request(unsigned command,uint32_t id,p4_debug_snapshot_result_t expected) {
    uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES]; snapshot_packet(bytes,command,id);
    const unsigned before=snapshot_replies;
    REQUIRE(content_uart_consume(NULL,bytes,sizeof(bytes)));
    REQUIRE(snapshot_replies==before+1 && snapshot_reply[5]==command);
    REQUIRE(snapshot_reply[6]==expected && get32(snapshot_reply+8)==id);
    REQUIRE(get32(snapshot_reply+12)==7);
}
static bool debug_send(void *context, const uint8_t *bytes, size_t length) {
    REQUIRE(length==sizeof(last_reply));
    REQUIRE(memcmp(bytes,"P4E1",4)==0 && bytes[6]==P4_DEBUG_OK);
    REQUIRE(get32(bytes+124)==crc32(bytes,124));
    memcpy(last_reply,bytes,length); ++replies;
    if (bytes[5]==P4_DEBUG_STATUS && strstr((const char *)bytes+32,"mode=loading"))
        ++loading_status_replies;
    return true;
}
static void debug_request(unsigned command, unsigned sequence, bool dispatch) {
    uint8_t bytes[P4_DEBUG_REQUEST_BYTES]={0};
    memcpy(bytes,"P4D1",4); bytes[4]=1; bytes[5]=(uint8_t)command;
    if (command!=P4_DEBUG_STATUS) { put32(bytes+8,42); put32(bytes+12,sequence); }
    if (command==P4_DEBUG_INPUT) {
        bytes[6]=1; bytes[16]=0x81; bytes[20]=10; bytes[22]=20;
        bytes[24]=0xe8; bytes[25]=3;
    }
    put32(bytes+28,crc32(bytes,28));
    const unsigned before=replies;
    if (dispatch) REQUIRE(content_uart_consume(NULL,bytes,sizeof(bytes)));
    else REQUIRE(p4_debug_control_consume(&s_debug_control,bytes,sizeof(bytes),debug_now_ms()));
    REQUIRE(replies==before+1);
}
static void require_neutral(void) {
    const p4_debug_input_t input=p4_debug_control_sample(&s_debug_control,debug_now_ms());
    REQUIRE(!input.buttons && !input.touch_down && !s_debug_touch_was_down);
}
static void require_loading(void) {
    if (!check_boundary) return;
    REQUIRE(strcmp(s_debug_runtime,"loading")==0);
    REQUIRE(!file_available && !content_available);
}
p4_file_transfer_info_t p4_file_transfer_info(void) { return file_info; }
p4_content_transfer_info_t p4_content_transfer_info(void) { return content_info; }
void p4_file_transfer_set_available(bool value) { file_available=value; }
void p4_content_transfer_set_available(bool value) { content_available=value; }
void p4_file_transfer_poll(void) {
    if (cleanup_terminal && (file_info.state==P4_FILE_TRANSFER_COMPLETE ||
        file_info.state==P4_FILE_TRANSFER_FAILED)) file_info.state=P4_FILE_TRANSFER_IDLE;
}
void p4_content_transfer_poll(void) {
    if (cleanup_terminal && (content_info.state==P4_CONTENT_TRANSFER_INSTALLED ||
        content_info.state==P4_CONTENT_TRANSFER_FAILED)) content_info.state=P4_CONTENT_TRANSFER_IDLE;
}
static bool parser_consume(unsigned kind, const uint8_t *bytes, size_t length) {
    bool claimed=false;
    for (size_t i=0;i<length;++i) {
        size_t *used=&parser_used[kind];
        if (bytes[i]!=requests[kind][*used]) { *used=bytes[i]=='P'?1:0; continue; }
        if (++*used>=4) claimed=true;
        if (*used==8) {
            if (kind==0?file_available:content_available) ++simulated_opens;
            *used=0;
        }
    }
    return claimed;
}
bool p4_file_transfer_consume(const uint8_t *bytes,size_t length) {
    ++file_calls; return parser_consume(0,bytes,length);
}
bool p4_content_transfer_consume(const uint8_t *bytes,size_t length) {
    ++content_calls; return parser_consume(1,bytes,length);
}
bool p4_clock_control_consume(p4_clock_control_t *control,const uint8_t *bytes,
    size_t length,uint64_t now) { ++clock_calls; return false; }
static void p4_mp_uart_endpoint_poll(void) {
    ++usb_polls;
    if (!service_usb) return;
    /* Poll 1 is prepare, 2 is pre-validation, 3 is a storage progress callback. */
    if (inject_request && usb_polls==3) {
        inject_request=false;
        size_t offset=partial_request?5:0;
        (void)content_uart_consume(NULL,requests[request_kind]+offset,8-offset);
    }
    if (inject_snapshot && usb_polls==3) {
        inject_snapshot=false;
        snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,501,P4_DEBUG_SNAPSHOT_OK);
        snapshot_request(P4_DEBUG_SNAPSHOT_READ,501,P4_DEBUG_SNAPSHOT_OK);
    }
    debug_request(P4_DEBUG_STATUS,0,true);
    if (inject_input) { inject_input=false; debug_request(P4_DEBUG_INPUT,3,true); }
}
void p4_doom_p4mp_poll(void) { ++network_polls; require_loading(); }
esp_err_t p4_doom_p4mp_prepare(p4_mp_session_t *session,
    const p4_doom_mp_launch_config_t *config,const p4_doom_p4mp_transport_t *transport) {
    if (!config && !session && !transport) { ++cleanup_calls; return ESP_OK; }
    ++prepare_calls; require_loading();
    if (check_boundary) require_neutral();
    /* Prepare is allowed to service USB too: admission must close before it. */
    console_os_debug_poll();
    return prepare_result;
}
static void present_game_loading(console_shell_t *shell,unsigned app) {
    ++loading_views; require_loading();
    if (check_boundary && loading_views==1) require_neutral();
    last_loading=shell->loading;
    fixture_now_ms+=fixture_paint_ms;
    s_ui_timing_last_render_us=fixture_paint_ms*500U;
    s_ui_timing_last_submit_us=fixture_paint_ms*500U;
}
bool console_shell_get_native_update(const console_shell_t *shell, console_shell_native_update_t *out) {
    *out=(console_shell_native_update_t){.kind=CONSOLE_SHELL_NATIVE_UPDATE_FULL}; return true;
}
esp_err_t platform_game_storage_get_doom_load_progress(
    platform_game_storage_doom_load_progress_t *out) {
    *out=fixture_load_progress; return progress_result;
}
esp_err_t platform_game_storage_lock_for_doom_title_with_progress(
    platform_game_storage_doom_title_t title,
    platform_game_storage_progress_fn_t progress,void *context) {
    ++storage_calls; require_loading();
    fixture_load_progress=(platform_game_storage_doom_load_progress_t){
        .active=true,.bytes_total=1000000,.file_count=16};
    for (unsigned i=0;i<3;++i) { progress(context); require_loading(); }
    fixture_load_progress.active=false;
    fixture_load_progress.complete=storage_result==ESP_OK;
    if (fixture_load_progress.complete) fixture_load_progress.bytes_checked=1000000;
    s_game_storage_status.state=storage_after;
    s_game_storage_status.content_validation_running=validation_after;
    return storage_result;
}
esp_err_t platform_game_storage_lock_for_doom_title(platform_game_storage_doom_title_t title) {
    ++storage_calls; require_loading(); return storage_result;
}
static void sync_game_storage(void) {}
static void multiplayer_frame_received(void *context,uint64_t route,const uint8_t *data,size_t size) {}
static esp_err_t multiplayer_transport_set_handler(p4_doom_p4mp_frame_handler_t handler,void *context) {
    REQUIRE(handler==multiplayer_frame_received); ++handler_restores; return handler_result;
}
static void reset_multiplayer_lobby(const char *reason) { ++lobby_resets; s_arena_launch_frozen=false; }
void console_shell_show_home(console_shell_t *shell) { ++home_calls; }
static console_shell_runtime_info_t runtime_info(void) { return (console_shell_runtime_info_t){0}; }
void console_shell_set_runtime_info(console_shell_t *shell,const console_shell_runtime_info_t *info) {}
static esp_err_t platform_display_set_brightness(unsigned value) { return ESP_OK; }
static esp_err_t destroy_touch_for_handoff(void) { return ESP_OK; }
static esp_err_t destroy_bus_for_handoff(void) { return ESP_OK; }
static esp_err_t platform_display_deinit(void) {
    REQUIRE(snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
    return ESP_OK;
}
static void heap_caps_free(void *allocation) {
    REQUIRE(allocation==NULL || allocation==&fixture_framebuffer);
    REQUIRE(snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
}
static void halt_dark(const char *where,esp_err_t result) { REQUIRE(false); }
static void console_os_launch_doom(unsigned volume,platform_game_storage_doom_title_t title,
    const p4_doom_mp_launch_config_t *config) { ++engine_calls; longjmp(engine_jump,1); }
static void wait_for_console_tick(int *scheduler,int *last_wake) {
    REQUIRE(s_multiplayer_launch_due); ++tick_waits;
}
'''

CASES = r'''
static void reset_fixture(void) {
    p4_debug_snapshot_cancel(&s_debug_snapshot);
    REQUIRE(snapshot_allocation==NULL);
    snapshot_captures=snapshot_releases=snapshot_replies=0; snapshot_reply_size=0;
    fixture_now_ms=1000;fixture_paint_ms=0;
    fixture_load_progress=(platform_game_storage_doom_load_progress_t){0};
    progress_result=ESP_OK;
    memset(&s_shell.loading,0,sizeof(s_shell.loading));
    memset(&last_loading,0,sizeof(last_loading));
    s_multiplayer_status[0]=0;
    file_info=(p4_file_transfer_info_t){0}; content_info=(p4_content_transfer_info_t){0};
    file_available=content_available=true;
    file_calls=content_calls=simulated_opens=clock_calls=0;
    usb_polls=network_polls=prepare_calls=cleanup_calls=0;
    loading_views=storage_calls=home_calls=handler_restores=engine_calls=0;
    replies=loading_status_replies=tick_waits=s_doom_handoff_count=0;
    s_debug_runtime="shell"; s_debug_game_id[0]=0; s_debug_touch_was_down=true;
    s_display_initialized=false; s_pixels=NULL; inject_snapshot=false;
    s_game_storage_status=(platform_game_storage_status_t){.state=PLATFORM_GAME_STORAGE_APP_READY};
    s_multiplayer_session=(p4_mp_session_t){.state=P4_MP_SESSION_CONNECTED,
        .role=P4_MP_ROLE_HOST,.session_id=1,.self_peer_id=2};
    launch=base_launch; s_arena_launch_frozen=true; lobby_resets=0;
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    s_multiplayer_launch_due=false;
    prepare_result=storage_result=handler_result=ESP_OK;
    storage_after=PLATFORM_GAME_STORAGE_GAME_LOCKED; validation_after=false;
    check_boundary=true; inject_request=false; inject_input=true; partial_request=false;
    service_usb=true; cleanup_terminal=false;
    memset(parser_used,0,sizeof(parser_used));
    p4_debug_snapshot_init(&s_debug_snapshot,snapshot_send,snapshot_capture,
        snapshot_release,debug_snapshot_available,NULL);
    p4_debug_control_init(&s_debug_control,debug_send,debug_usb_status,NULL);
    debug_request(P4_DEBUG_OPEN,1,false); debug_request(P4_DEBUG_INPUT,2,false);
    REQUIRE(p4_debug_control_sample(&s_debug_control,debug_now_ms()).buttons==0x81);
}
static bool run_launch(void) {
    if (setjmp(engine_jump)) return true;
    launch_doom_exclusive(&s_shell,PLATFORM_GAME_STORAGE_DOOM_TITLE_GAME_CHANGERS_AI,&launch);
    return false;
}
static void verify_success(void) {
    REQUIRE(engine_calls==1 && s_doom_handoff_count==1);
    REQUIRE(simulated_opens==0);
    REQUIRE(file_calls==0 && content_calls==0 && clock_calls==0);
    REQUIRE(strcmp(s_debug_runtime,"doom")==0);
    REQUIRE(!file_available && !content_available);
    REQUIRE(prepare_calls==1 && storage_calls==1 && cleanup_calls==0);
    REQUIRE(network_polls==5 && usb_polls==6 && loading_status_replies==6);
    REQUIRE(!s_display_initialized && s_pixels==NULL);
    require_neutral();
}
static void test_request(unsigned kind,bool partial) {
    reset_fixture(); request_kind=kind; partial_request=partial;
    /* Let an old launch reach the injected request, so its red failure proves
     * an actual parser admission/open, not merely a missing mode label. */
    check_boundary=false;
    /* A partial header remains publicly IDLE, so launch may still be admitted. */
    if (partial) {
        REQUIRE(content_uart_consume(NULL,requests[kind],5));
        REQUIRE(parser_used[kind]==5 && simulated_opens==0);
        REQUIRE(file_info.state==P4_FILE_TRANSFER_IDLE && content_info.state==P4_CONTENT_TRANSFER_IDLE);
    }
    file_calls=content_calls=clock_calls=0; inject_request=true;
    REQUIRE(run_launch()); verify_success();
}
#ifdef P4_HAS_DOOM_LOADING_PROGRESS
static void test_loading_progress(void) {
    reset_fixture();debug_transition("loading");
    inject_input=false;sync_usb_transfer_availability();
    doom_loading_context_t context={.shell=&s_shell,.app_id=CONSOLE_APP_DOOM,
        .started_ms=fixture_now_ms,.arena=true};
    (void)snprintf(s_shell.loading.detail,sizeof(s_shell.loading.detail),"preserved");
    present_doom_file_loading(&context,NULL);
    REQUIRE(loading_views==1 && !strcmp(last_loading.title,"Doom Arena by Game Changers"));
    REQUIRE(!strcmp(last_loading.stage,"1/3 Check files") && last_loading.progress_percent==0);
    REQUIRE(!s_shell.loading.active && !strcmp(s_shell.loading.detail,"preserved"));
    fixture_load_progress=(platform_game_storage_doom_load_progress_t){
        .active=true,.bytes_checked=250000,.bytes_total=1000000,.file_count=16};
    fixture_now_ms+=499;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==1 && network_polls==1);
    ++fixture_now_ms;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==2 && last_loading.progress_percent==25 && last_loading.progress_visible);
    REQUIRE(strstr(last_loading.detail,"25%")!=NULL && !s_shell.loading.active);
    fixture_load_progress.checking_structure=true;
    fixture_now_ms+=500;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==3 && !last_loading.progress_visible && last_loading.elapsed_seconds==1);
    REQUIRE(!strcmp(last_loading.stage,"1/3 Check WAD structure") && !strcmp(last_loading.detail,"File 1 of 16"));
    fixture_load_progress.checking_structure=false;
    fixture_load_progress.bytes_checked=fixture_load_progress.bytes_total;
    fixture_now_ms+=500;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==4 && last_loading.progress_percent==100);
    REQUIRE(!strcmp(last_loading.stage,"1/3 Check files"));
    fixture_load_progress.active=false;fixture_load_progress.complete=true;
    ++fixture_now_ms;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==5 && last_loading.progress_percent==100 && last_loading.progress_visible);
    REQUIRE(!strcmp(last_loading.stage,"1/3 Files verified") && !strcmp(last_loading.detail,"Starting engine..."));
    poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==5); /* The post-lock poll does not repaint completion. */
    fixture_load_progress.complete=false;
    ++fixture_now_ms;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==6 && !last_loading.progress_visible);
    REQUIRE(!strcmp(last_loading.stage,"1/3 File check failed"));
    progress_result=ESP_FAIL;
    fixture_now_ms+=500;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==6 && network_polls==8);
    REQUIRE(!s_shell.loading.active && !strcmp(s_shell.loading.detail,"preserved"));
    context.arena=false;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==6 && network_polls==9);
#ifdef P4_HAS_DOOM_LOADING_REGION_COST
    /* Slow rendering must not consume the 500 ms work interval. Service
     * polls continue on suppressed frames; a failure still paints now. */
    reset_fixture();debug_transition("loading");inject_input=false;
    sync_usb_transfer_availability();fixture_paint_ms=300;
    context=(doom_loading_context_t){.shell=&s_shell,.app_id=CONSOLE_APP_DOOM,
        .started_ms=fixture_now_ms,.arena=true};
    present_doom_file_loading(&context,NULL);
    REQUIRE(fixture_now_ms==1300 && context.last_painted_ms==1300);
    fixture_load_progress=(platform_game_storage_doom_load_progress_t){
        .active=true,.bytes_checked=250000,.bytes_total=1000000,.file_count=16};
    fixture_now_ms+=499;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==1 && network_polls==1);
    ++fixture_now_ms;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==2 && context.last_painted_ms==2100 && network_polls==2);
    fixture_load_progress.active=false;
    ++fixture_now_ms;poll_doom_multiplayer_loading(&context);
    REQUIRE(loading_views==3 && network_polls==3 && !last_loading.progress_visible);
    REQUIRE(context.paint_calls==3 && context.region_calls==0);
    REQUIRE(context.paint_us==900000 && context.render_us==450000 && context.submit_us==450000);
    REQUIRE(context.paint_max_us==300000 && !s_shell.loading.active);
#endif
}
#endif
static void test_shell_control(void) {
    for (unsigned kind=0;kind<2;++kind) for (unsigned partial=0;partial<2;++partial) {
        reset_fixture(); sync_usb_transfer_availability();
        if (partial) REQUIRE(content_uart_consume(NULL,requests[kind],5));
        REQUIRE(content_uart_consume(NULL,requests[kind]+(partial?5:0),partial?3:8));
        REQUIRE(simulated_opens==1);
        REQUIRE(kind?content_calls>0:file_calls>0);
        debug_request(P4_DEBUG_STATUS,0,true);
        REQUIRE(strstr((const char *)last_reply+32,"mode=shell")!=NULL);
    }
}
static void test_failure(bool prepare_failure) {
    const platform_game_storage_state_t states[]={PLATFORM_GAME_STORAGE_APP_READY,
        PLATFORM_GAME_STORAGE_APP_INVALID,PLATFORM_GAME_STORAGE_USB_HOST,PLATFORM_GAME_STORAGE_FAULT};
    for (size_t i=0;i<sizeof(states)/sizeof(states[0]);++i) for (unsigned validation=0;validation<2;++validation) {
        reset_fixture();
        if (prepare_failure) {
            prepare_result=ESP_FAIL;
            s_game_storage_status.state=states[i];
            s_game_storage_status.content_validation_running=validation!=0;
        } else {
            storage_result=ESP_FAIL; storage_after=states[i]; validation_after=validation!=0;
        }
        sync_usb_transfer_availability();
        REQUIRE(!run_launch());
        REQUIRE(strcmp(s_debug_runtime,"shell")==0 && engine_calls==0 && s_doom_handoff_count==0);
        const bool owned=states[i]==PLATFORM_GAME_STORAGE_APP_READY || states[i]==PLATFORM_GAME_STORAGE_APP_INVALID;
        REQUIRE(file_available==owned && content_available==(owned && !validation));
        REQUIRE(prepare_calls==1 && storage_calls==(prepare_failure?0U:1U));
        REQUIRE(cleanup_calls==(prepare_failure?0U:1U));
        REQUIRE(lobby_resets==1 && !s_arena_launch_frozen);
        REQUIRE(handler_restores==(prepare_failure?0U:1U) && home_calls==(prepare_failure?0U:1U));
        REQUIRE(loading_status_replies>0 && file_calls==0 && content_calls==0);
#ifdef P4_HAS_DOOM_LOADING_PROGRESS
        if (!prepare_failure) {
            REQUIRE(s_shell.page==CONSOLE_PAGE_MULTIPLAYER &&
                s_shell.multiplayer_view==CONSOLE_MULTIPLAYER_VIEW_ROLE);
            REQUIRE(strstr(s_multiplayer_status,"Check the SD card and game files")!=NULL);
        }
#endif
        require_neutral();
    }
}
static void test_nonidle(void) {
    for (unsigned kind=0;kind<2;++kind) {
        const unsigned last=kind?P4_CONTENT_TRANSFER_FAILED:P4_FILE_TRANSFER_FAILED;
        for (unsigned state=1;state<=last;++state) {
            reset_fixture(); check_boundary=false; service_usb=false;
            if (kind) content_info.state=(p4_content_transfer_state_t)state;
            else file_info.state=(p4_file_transfer_state_t)state;
            content_info.busy=content_info.state==P4_CONTENT_TRANSFER_RECEIVING;
            file_info.busy=file_info.state==P4_FILE_TRANSFER_RECEIVING || file_info.state==P4_FILE_TRANSFER_SENDING;
            /* COMPLETE/FAILED are non-busy; they must still prevent handoff. */
            REQUIRE(!run_launch());
            REQUIRE(loading_views==0 && prepare_calls==0 && storage_calls==0 && engine_calls==0);
            REQUIRE(lobby_resets==1 && !s_arena_launch_frozen);
            REQUIRE(strcmp(s_debug_runtime,"shell")==0 && file_available && content_available);
        }
    }
}
static void solo_launch_fixture(void) {
    reset_fixture();
    launch.remote_peer_id=0; launch.route_id=0; launch.initial_player_mask=1;
    s_multiplayer_session.state=P4_MP_SESSION_HOSTING;
}
static void test_solo_handoff(void) {
    solo_launch_fixture();
    REQUIRE(run_launch()); verify_success();
    REQUIRE(lobby_resets==0 && s_arena_launch_frozen);
}
static void test_solo_handoff_gates(void) {
    for (unsigned mutation=0;mutation<11;++mutation) {
        solo_launch_fixture();
        if(mutation==0) s_arena_launch_frozen=false;
        if(mutation==1) s_multiplayer_transport=CONSOLE_MP_TRANSPORT_BLE;
        if(mutation==2) s_multiplayer_session.role=P4_MP_ROLE_CLIENT;
        if(mutation==3) s_multiplayer_session.session_id=99;
        if(mutation==4) s_multiplayer_session.self_peer_id=99;
        if(mutation==5) s_multiplayer_session.peers[0].connected=true;
        if(mutation==6) launch.role=P4_MP_ROLE_CLIENT;
        if(mutation==7) launch.initial_player_mask=3;
        if(mutation==8) launch.setup.game=P4_DOOM_MP_GAME_DOOM;
        if(mutation==9) launch.route_id=4;
        if(mutation==10) launch.remote_peer_id=3;
        REQUIRE(!run_launch());
        REQUIRE(prepare_calls==0 && storage_calls==0 && engine_calls==0);
        REQUIRE(!s_arena_launch_frozen && lobby_resets==1U);
    }
}
static void test_solo_failed_handoff_recovers(void) {
    for (unsigned failure=0;failure<3;++failure) {
        solo_launch_fixture();
        if(failure==0) prepare_result=ESP_FAIL;
        if(failure==1) { storage_result=ESP_FAIL; storage_after=PLATFORM_GAME_STORAGE_APP_READY; }
        if(failure==2) file_info.state=P4_FILE_TRANSFER_COMPLETE;
        REQUIRE(!run_launch());
        REQUIRE(lobby_resets==1 && !s_arena_launch_frozen && engine_calls==0);
        REQUIRE(!strcmp(s_debug_runtime,"shell"));
    }
}
static void test_guest_failed_handoff_recovers(void) {
    for(unsigned failure=0;failure<5;++failure) {
        reset_fixture(); s_arena_launch_frozen=false;
        launch.role=P4_MP_ROLE_CLIENT; launch.local_player_slot=1;
        launch.initial_player_mask=1; launch.rejoining=true;
        s_multiplayer_session.role=P4_MP_ROLE_CLIENT;
        if(failure==0) prepare_result=ESP_FAIL;
        if(failure==1) { storage_result=ESP_FAIL; storage_after=PLATFORM_GAME_STORAGE_APP_READY; }
        if(failure==2) file_info.state=P4_FILE_TRANSFER_COMPLETE;
        if(failure==3) s_multiplayer_session.state=P4_MP_SESSION_JOINING;
        if(failure==4) { storage_result=ESP_FAIL; handler_result=ESP_FAIL; }
        REQUIRE(!run_launch());
        REQUIRE(lobby_resets==1 && !s_arena_launch_frozen && engine_calls==0);
        REQUIRE(!strcmp(s_debug_runtime,"shell"));
    }
}
static void test_terminal_gate(void) {
    for (unsigned kind=0;kind<2;++kind) for (unsigned failed=0;failed<2;++failed) {
        reset_fixture(); s_multiplayer_launch_due=true;
        if (kind) content_info.state=failed?P4_CONTENT_TRANSFER_FAILED:P4_CONTENT_TRANSFER_INSTALLED;
        else file_info.state=failed?P4_FILE_TRANSFER_FAILED:P4_FILE_TRANSFER_COMPLETE;
        REQUIRE(!file_info.busy && !content_info.busy);
        REQUIRE(!run_launcher_gate(1));
        REQUIRE(s_multiplayer_launch_due && tick_waits==1 && loading_views==0);
        /* The next ordinary launcher service poll performs terminal cleanup. */
        cleanup_terminal=true;
        REQUIRE(run_launcher_gate(1));
        REQUIRE(file_info.state==P4_FILE_TRANSFER_IDLE && content_info.state==P4_CONTENT_TRANSFER_IDLE);
        REQUIRE(!s_multiplayer_launch_due && tick_waits==1);
        REQUIRE(run_launch()); verify_success();
    }
}
static void test_availability(void) {
    const char *modes[]={"shell","loading","doom"};
    for (unsigned mode=0;mode<3;++mode) for (unsigned file_busy=0;file_busy<2;++file_busy)
    for (unsigned content_busy=0;content_busy<2;++content_busy) for (unsigned validation=0;validation<2;++validation) {
        reset_fixture(); s_debug_runtime=modes[mode]; file_info.busy=file_busy!=0;
        content_info.busy=content_busy!=0; s_game_storage_status.content_validation_running=validation!=0;
        sync_usb_transfer_availability();
        REQUIRE(file_available==(mode==0 && !content_busy));
        REQUIRE(content_available==(mode==0 && !file_busy && !validation));
    }
}
static void test_snapshot_foreground(const char *mode) {
    reset_fixture();
    if (!strcmp(mode,"loading")) {
        s_display_initialized=true; s_pixels=&fixture_framebuffer;
    }
    debug_transition(mode);
    REQUIRE(debug_snapshot_available(NULL));
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,101,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_captures==1 && snapshot_releases==0 && snapshot_allocation!=NULL);
    REQUIRE(get32(snapshot_reply+20)==16 && snapshot_reply[26]==4 && snapshot_reply[28]==2);
    REQUIRE(file_calls==0 && content_calls==0 && clock_calls==0);
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,101,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_reply_size==52);
    for (unsigned i=0;i<16;++i) REQUIRE(snapshot_reply[32+i]==i+32U);
    snapshot_request(P4_DEBUG_SNAPSHOT_END,101,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_releases==1 && snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
}
static void test_snapshot_transition(void) {
    reset_fixture();
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,201,P4_DEBUG_SNAPSHOT_OK);
    s_display_initialized=true; s_pixels=&fixture_framebuffer;
    debug_transition("loading"); require_neutral();
    REQUIRE(snapshot_releases==1 && snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,202,P4_DEBUG_SNAPSHOT_OK);
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,202,P4_DEBUG_SNAPSHOT_OK);
    debug_request(P4_DEBUG_STATUS,0,true);
    REQUIRE(strstr((const char *)last_reply+32,"mode=loading")!=NULL);
    debug_transition("doom");
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,203,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_captures==3 && snapshot_releases==2);
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,203,P4_DEBUG_SNAPSHOT_OK);
    debug_transition("native"); require_neutral();
    REQUIRE(snapshot_releases==3 && snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,204,P4_DEBUG_SNAPSHOT_OK);
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,204,P4_DEBUG_SNAPSHOT_OK);
    debug_transition("shell"); require_neutral();
    REQUIRE(snapshot_releases==4 && snapshot_allocation==NULL);
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,204,P4_DEBUG_SNAPSHOT_EXPIRED);
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,205,P4_DEBUG_SNAPSHOT_OK);
    snapshot_request(P4_DEBUG_SNAPSHOT_END,205,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_captures==5 && snapshot_releases==5);
    debug_transition("other");
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,206,P4_DEBUG_SNAPSHOT_UNAVAILABLE);
    REQUIRE(file_calls==0 && content_calls==0 && clock_calls==0);
}
static void test_snapshot_transfer_priority(void) {
    const char *modes[]={"shell","doom","native","loading"};
    for (unsigned mode=0;mode<4;++mode)
    for (unsigned kind=0;kind<2;++kind) {
        const unsigned last=kind?P4_CONTENT_TRANSFER_FAILED:P4_FILE_TRANSFER_FAILED;
        for (unsigned state=1;state<=last;++state) {
            reset_fixture();
            s_display_initialized=true; s_pixels=&fixture_framebuffer;
            debug_transition(modes[mode]);
            snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,301,P4_DEBUG_SNAPSHOT_OK);
            if (kind) content_info.state=(p4_content_transfer_state_t)state;
            else file_info.state=(p4_file_transfer_state_t)state;
            REQUIRE(!debug_snapshot_available(NULL));
            uint8_t bytes[P4_DEBUG_SNAPSHOT_REQUEST_BYTES];
            snapshot_packet(bytes,P4_DEBUG_SNAPSHOT_READ,301);
            (void)content_uart_consume(NULL,bytes,sizeof(bytes));
            REQUIRE(snapshot_captures==1 && snapshot_releases==1 && snapshot_allocation==NULL);
            REQUIRE(s_debug_snapshot.data==NULL && snapshot_replies==1);
            REQUIRE(file_calls==(kind?0U:1U) && content_calls==(kind?1U:0U) && clock_calls==0);
            const p4_debug_input_t input=p4_debug_control_sample(&s_debug_control,debug_now_ms());
            REQUIRE(!input.buttons && !input.touch_down);
        }
    }
}
static void test_snapshot_loading_display_lifetime(void) {
    /* Null and uninitialized launcher surfaces cannot be captured. */
    for (unsigned initialized=0;initialized<2;++initialized)
    for (unsigned pixels=0;pixels<2;++pixels) {
        reset_fixture(); debug_transition("loading");
        s_display_initialized=initialized!=0;
        s_pixels=pixels?&fixture_framebuffer:NULL;
        if (!initialized || !pixels) {
            snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,601,P4_DEBUG_SNAPSHOT_UNAVAILABLE);
            REQUIRE(snapshot_captures==0 && snapshot_releases==0 && snapshot_allocation==NULL);
        } else {
            snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,601,P4_DEBUG_SNAPSHOT_OK);
            snapshot_request(P4_DEBUG_SNAPSHOT_END,601,P4_DEBUG_SNAPSHOT_OK);
        }
    }
    /* Loss of either lifetime condition cancels an already owned capture. */
    for (unsigned condition=0;condition<2;++condition) {
        reset_fixture(); service_usb=false;
        s_display_initialized=true; s_pixels=&fixture_framebuffer;
        debug_transition("loading");
        snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,602,P4_DEBUG_SNAPSHOT_OK);
        if (condition) s_pixels=NULL; else s_display_initialized=false;
        console_os_debug_poll();
        REQUIRE(snapshot_captures==1 && snapshot_releases==1 && snapshot_allocation==NULL);
        REQUIRE(s_debug_snapshot.data==NULL);
        snapshot_request(P4_DEBUG_SNAPSHOT_READ,602,P4_DEBUG_SNAPSHOT_UNAVAILABLE);
        REQUIRE(snapshot_releases==1);
    }
}
static void test_snapshot_loading_preserves_admission(void) {
    reset_fixture(); s_display_initialized=true; s_pixels=&fixture_framebuffer;
    debug_transition("loading"); sync_usb_transfer_availability();
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,701,P4_DEBUG_SNAPSHOT_OK);
    for (unsigned kind=0;kind<2;++kind) {
        REQUIRE(!content_uart_consume(NULL,requests[kind],8));
        REQUIRE(!content_uart_consume(NULL,requests[kind],5));
        REQUIRE(!content_uart_consume(NULL,requests[kind]+5,3));
    }
    uint8_t clock_request[P4_CLOCK_REQUEST_BYTES]={0};
    memcpy(clock_request,"P4K1",4); clock_request[4]=1; clock_request[5]=2;
    put32(clock_request+8,17); put32(clock_request+12,UINT32_C(1760000000));
    put32(clock_request+16,crc32(clock_request,16));
    REQUIRE(!content_uart_consume(NULL,clock_request,sizeof(clock_request)));
    snapshot_request(P4_DEBUG_SNAPSHOT_READ,701,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(!strcmp(s_debug_runtime,"loading") && !file_available && !content_available);
    REQUIRE(file_calls==0 && content_calls==0 && clock_calls==0 && simulated_opens==0);
    snapshot_request(P4_DEBUG_SNAPSHOT_END,701,P4_DEBUG_SNAPSHOT_OK);
}
static void test_snapshot_loading_handoff(bool fail_validation) {
    reset_fixture(); s_display_initialized=true; s_pixels=&fixture_framebuffer;
    inject_snapshot=true;
    if (fail_validation) { storage_result=ESP_FAIL; storage_after=PLATFORM_GAME_STORAGE_APP_READY; }
    REQUIRE(run_launch()==!fail_validation);
    REQUIRE(!inject_snapshot && snapshot_captures==1 && snapshot_releases==1);
    REQUIRE(snapshot_allocation==NULL && s_debug_snapshot.data==NULL);
    if (fail_validation) {
        REQUIRE(!strcmp(s_debug_runtime,"shell") && !engine_calls && !s_doom_handoff_count);
        REQUIRE(home_calls==1 && handler_restores==1 && cleanup_calls==1);
        REQUIRE(s_display_initialized && s_pixels==&fixture_framebuffer);
        REQUIRE(file_available && content_available);
    } else verify_success();
}
static void test_native_identity(void) {
    reset_fixture();
    char id[P4_GAME_ID_MAX_BYTES];
    memset(id,'x',sizeof(id)-1); id[sizeof(id)-1]=0;
    debug_native_begin(id);
    REQUIRE(strcmp(s_debug_runtime,"native")==0);
    require_neutral();
    id[0]='z'; /* Debug identity owns its bytes. */
    debug_request(P4_DEBUG_STATUS,0,true);
    REQUIRE(strstr((const char *)last_reply+32,"mode=native id=")==((const char *)last_reply+32));
    REQUIRE(strlen((const char *)last_reply+32)==strlen("mode=native id=")+sizeof(id)-1);
    REQUIRE(last_reply[32+strlen("mode=native id=")]=='x');
    debug_transition("shell");
    REQUIRE(s_debug_game_id[0]==0);
    debug_request(P4_DEBUG_STATUS,0,true);
    REQUIRE(strstr((const char *)last_reply+32,"mode=shell")!=NULL);
    REQUIRE(strstr((const char *)last_reply+32," id=")==NULL);
    debug_native_begin("org.p4console.checkers");
    debug_request(P4_DEBUG_STATUS,0,true);
    REQUIRE(strcmp((const char *)last_reply+32,"mode=native id=org.p4console.checkers")==0);
    debug_transition("loading"); REQUIRE(s_debug_game_id[0]==0);
}
static void test_snapshot_idle_poll(void) {
    reset_fixture(); service_usb=false;
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,401,P4_DEBUG_SNAPSHOT_OK);
    fixture_now_ms+=P4_DEBUG_SNAPSHOT_IDLE_MS-1;
    console_os_debug_poll();
    REQUIRE(snapshot_releases==0 && snapshot_allocation!=NULL && usb_polls==1);
    ++fixture_now_ms;
    console_os_debug_poll();
    REQUIRE(snapshot_releases==1 && snapshot_allocation==NULL && usb_polls==2);
    REQUIRE(snapshot_replies==1); /* Expiry works with no incoming USB request. */
    console_os_debug_poll(); REQUIRE(snapshot_releases==1);
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,401,P4_DEBUG_SNAPSHOT_EXPIRED);
    snapshot_request(P4_DEBUG_SNAPSHOT_BEGIN,402,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_captures==2);
    snapshot_request(P4_DEBUG_SNAPSHOT_END,402,P4_DEBUG_SNAPSHOT_OK);
    REQUIRE(snapshot_releases==2 && snapshot_allocation==NULL);
}
int main(int argc,char **argv) {
    REQUIRE(argc==2);
    if (!strcmp(argv[1],"fresh-file")) test_request(0,false);
#ifdef P4_HAS_DOOM_LOADING_PROGRESS
    else if (!strcmp(argv[1],"loading-progress")) test_loading_progress();
#endif
    else if (!strcmp(argv[1],"fresh-content")) test_request(1,false);
    else if (!strcmp(argv[1],"partial-file")) test_request(0,true);
    else if (!strcmp(argv[1],"partial-content")) test_request(1,true);
    else if (!strcmp(argv[1],"shell-control")) test_shell_control();
    else if (!strcmp(argv[1],"prepare-failure")) test_failure(true);
    else if (!strcmp(argv[1],"storage-failure")) test_failure(false);
    else if (!strcmp(argv[1],"nonidle")) test_nonidle();
    else if (!strcmp(argv[1],"terminal-gate")) test_terminal_gate();
    else if (!strcmp(argv[1],"solo")) test_solo_handoff();
    else if (!strcmp(argv[1],"solo-gates")) test_solo_handoff_gates();
    else if (!strcmp(argv[1],"solo-failed")) test_solo_failed_handoff_recovers();
    else if (!strcmp(argv[1],"guest-failed")) test_guest_failed_handoff_recovers();
    else if (!strcmp(argv[1],"availability")) test_availability();
    else if (!strcmp(argv[1],"snapshot-shell")) test_snapshot_foreground("shell");
    else if (!strcmp(argv[1],"snapshot-loading")) test_snapshot_foreground("loading");
    else if (!strcmp(argv[1],"snapshot-loading-lifetime")) test_snapshot_loading_display_lifetime();
    else if (!strcmp(argv[1],"snapshot-loading-admission")) test_snapshot_loading_preserves_admission();
    else if (!strcmp(argv[1],"snapshot-loading-handoff")) test_snapshot_loading_handoff(false);
    else if (!strcmp(argv[1],"snapshot-loading-failure")) test_snapshot_loading_handoff(true);
    else if (!strcmp(argv[1],"snapshot-doom")) test_snapshot_foreground("doom");
    else if (!strcmp(argv[1],"snapshot-native")) test_snapshot_foreground("native");
    else if (!strcmp(argv[1],"native-identity")) test_native_identity();
    else if (!strcmp(argv[1],"snapshot-transition")) test_snapshot_transition();
    else if (!strcmp(argv[1],"snapshot-transfer-priority")) test_snapshot_transfer_priority();
    else if (!strcmp(argv[1],"snapshot-idle-poll")) test_snapshot_idle_poll();
    else REQUIRE(false);
    return 0;
}
'''


class DoomLoadingBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = SOURCE.read_text()
        cls.temporary = tempfile.TemporaryDirectory(prefix="p4-doom-loading-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        names = ["storage_app_owned", "debug_now_ms", "debug_usb_status",
                 "console_os_debug_poll", "debug_transition", "content_uart_consume",
                 "poll_doom_multiplayer_loading", "launch_doom_exclusive", "multiplayer_members"]
        cls.has_loading_progress = "} doom_loading_context_t;" in source
        context_type = ""
        if cls.has_loading_progress:
            names.insert(6, "present_doom_file_loading")
            context_end = source.index("} doom_loading_context_t;") + len("} doom_loading_context_t;")
            context_type = source[source.rfind("typedef struct", 0, context_end):context_end]
            context_type += "\n#define P4_HAS_DOOM_LOADING_PROGRESS 1\n"
            if "uint64_t paint_us, render_us, submit_us;" in context_type:
                context_type += "\n#define P4_HAS_DOOM_LOADING_REGION_COST 1\n"
        units = [function(source, name) for name in names]
        try:
            units.append(function(source, "debug_native_begin"))
        except ValueError:
            units.append('static void debug_native_begin(const char *id) { debug_transition("native"); }')
        try:
            units.append(function(source, "debug_snapshot_available"))
        except ValueError:
            # Pre-snapshot baseline has no endpoint availability at all.
            units.append("static bool debug_snapshot_available(void *context) { return false; }")
        try:
            availability = function(source, "sync_usb_transfer_availability")
        except ValueError:
            original = function(source, "poll_multiplayer_link")
            block = original[original.index("{") + 1:original.index("    p4_content_transfer_poll();")]
            availability = "static void sync_usb_transfer_availability(void) {\n" + block + "\n}"
        units.append(availability)
        # app_main has mutually exclusive #if branches that each open an if;
        # its raw (unpreprocessed) braces therefore need not balance.
        main = source[source.index("void app_main(void)"):]
        begin = main.index("        poll_multiplayer_link(shell);") + len("        poll_multiplayer_link(shell);")
        end = main.index("        if (s_multiplayer_launch_due) {", begin)
        # Keep the real preprocessor branches, condition, wait and continue.
        gate = """
static bool run_launcher_gate(unsigned iterations) {
    int console_scheduler=0, last_wake=0;
    for (unsigned i=0;i<iterations;++i) {
        p4_content_transfer_poll(); p4_file_transfer_poll();
""" + main[begin:end] + """
        if (s_multiplayer_launch_due) { s_multiplayer_launch_due=false; return true; }
    }
    return false;
}
"""
        declarations = "\n".join(unit[:unit.index("{")].strip() + ";" for unit in units)
        (directory / "loading.c").write_text(
            PRELUDE + context_type + declarations + FIXTURES + "\n".join(units) + gate + CASES)
        (directory / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_STATE 1\n#define ESP_ERR_INVALID_ARG 2\n")
        includes = ["console_shell", "p4_game_api", "p4_desktop", "p4_multiplayer",
                    "doom_multiplayer", "p4_usb_content_transfer", "platform_game_storage"]
        cls.executable = directory / "loading"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
                   "-DCONFIG_P4_BOARD_M5STACK_TAB5=1", "-I", str(directory),
                   "-I", str(ROOT / "apps/console_os/main"), "-I", str(BASE / "apps/console_os/main")]
        for component in includes:
            command += ["-I", str(ROOT / "components" / component / "include"),
                        "-I", str(BASE / "components" / component / "include")]
        command += [str(directory / "loading.c"),
                    str(source_path("components/p4_usb_content_transfer/src/debug_control.c")),
                    str(source_path("components/p4_usb_content_transfer/src/debug_snapshot.c")),
                    str(source_path("components/doom_multiplayer/src/doom_multiplayer.c")),
                    "-o", str(cls.executable)]
        build = subprocess.run(command, capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)

    def run_case(self, name):
        result = subprocess.run([self.executable, name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fresh_file_request_cannot_enter_loading(self):
        self.run_case("fresh-file")

    def test_real_progress_throttles_and_preserves_validation_boundary(self):
        if not self.has_loading_progress:
            self.skipTest("Saved source predates Arena loading progress")
        self.run_case("loading-progress")

    def test_fresh_content_request_cannot_enter_loading(self):
        self.run_case("fresh-content")

    def test_partial_file_request_cannot_resume_during_loading(self):
        self.run_case("partial-file")

    def test_partial_content_request_cannot_resume_during_loading(self):
        self.run_case("partial-content")

    def test_same_requests_reach_parsers_in_shell(self):
        self.run_case("shell-control")

    def test_prepare_failure_restores_shell_and_storage_availability(self):
        self.run_case("prepare-failure")

    def test_validation_failure_restores_shell_and_storage_availability(self):
        self.run_case("storage-failure")

    def test_all_nonidle_transfers_reject_launch_before_prepare(self):
        self.run_case("nonidle")

    def test_solo_host_reaches_actual_doom_handoff(self):
        self.run_case("solo")

    def test_solo_handoff_rejects_invalid_roles_identity_transport_or_roster(self):
        self.run_case("solo-gates")

    def test_solo_failed_handoff_restores_admission(self):
        self.run_case("solo-failed")

    def test_replaying_guest_failed_handoff_restores_admission(self):
        self.run_case("guest-failed")

    def test_terminal_cleanup_preserves_pending_multiplayer_launch(self):
        self.run_case("terminal-gate")

    def test_availability_preserves_shell_storage_arbitration(self):
        self.run_case("availability")

    def test_snapshot_shell_begin_read_and_release(self):
        self.run_case("snapshot-shell")

    def test_snapshot_loading_begin_read_and_release(self):
        self.run_case("snapshot-loading")

    def test_snapshot_loading_requires_live_display_and_cancels_on_loss(self):
        self.run_case("snapshot-loading-lifetime")

    def test_snapshot_loading_keeps_file_content_and_clock_admission_closed(self):
        self.run_case("snapshot-loading-admission")

    def test_snapshot_during_validation_releases_before_display_handoff(self):
        self.run_case("snapshot-loading-handoff")

    def test_snapshot_during_failed_validation_releases_and_restores_shell(self):
        self.run_case("snapshot-loading-failure")

    def test_snapshot_doom_begin_read_and_release(self):
        self.run_case("snapshot-doom")

    def test_snapshot_native_begin_read_and_release(self):
        self.run_case("snapshot-native")

    def test_native_identity_and_capture_cancel_bound_real_game_lifetime(self):
        source = function(SOURCE.read_text(), "run_stored_game")
        before, after = source.split("platform_game_loader_run(game, &host)", 1)
        self.assertIn("debug_native_begin(game->package.id);", before)
        self.assertIn('debug_transition("shell");', after.split("native_multiplayer_end();", 1)[0])

    def test_native_identity_is_owned_bounded_and_cleared_on_exit(self):
        self.run_case("native-identity")

    def test_snapshot_loading_transition_releases_each_owned_capture(self):
        self.run_case("snapshot-transition")

    def test_snapshot_active_and_terminal_transfers_take_priority(self):
        self.run_case("snapshot-transfer-priority")

    def test_snapshot_expires_through_debug_poll_without_usb_input(self):
        self.run_case("snapshot-idle-poll")


if __name__ == "__main__":
    unittest.main()
