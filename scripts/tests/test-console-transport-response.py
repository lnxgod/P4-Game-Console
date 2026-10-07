#!/usr/bin/env python3
"""Host regressions for transport ownership and Console connection feedback.

Compile unchanged production function bodies with GAP, transport and UI boundary
fixtures. No radios, devices, ports or firmware builds are involved. Override
P4_BLE_TRANSPORT_SOURCE, P4_BLE_HOST_SOURCE and P4_CONSOLE_TRANSPORT_SOURCE to
run saved baselines.
For an older Console without the status helper, its original status assignment
is retained and the absent helper returns NULL; no new behavior is backported.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BLE_SOURCE = Path(os.environ.get("P4_BLE_TRANSPORT_SOURCE",
    ROOT / "components/platform_multiplayer_ble/src/platform_multiplayer_ble.c"))
CONSOLE_SOURCE = Path(os.environ.get("P4_CONSOLE_TRANSPORT_SOURCE",
    ROOT / "apps/console_os/main/console_os_main.c"))
HOST_SOURCE = Path(os.environ.get("P4_BLE_HOST_SOURCE",
    ROOT / "components/platform_ble_host/src/platform_ble_host.c"))


def function(source, name):
    match = re.search(r"^(?:static\s+)?[\w\s*]+?\b" + re.escape(name)
                      + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"No definition for {name}")
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
    raise ValueError(f"Unclosed definition for {name}")


COMMON = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/multiplayer_ble.h"
#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x); exit(1); } } while (0)
'''

BLE_FIXTURE = r'''
#define BLE_HS_CONN_HANDLE_NONE UINT16_MAX
#define BLE_ERR_REM_USER_CONN_TERM 0x13
static int s_lock, critical_depth;
#define portENTER_CRITICAL(lock) do { REQUIRE(critical_depth==0); ++critical_depth; } while (0)
#define portEXIT_CRITICAL(lock) do { REQUIRE(critical_depth==1); --critical_depth; } while (0)
static struct {
    platform_multiplayer_ble_status_t status;
    uint16_t conn_handle;
    uint64_t host_collision_deadline_ms;
    uint32_t host_collision_round;
    bool host_collision_scanning, selected_host_valid;
    uint8_t lobbies[PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES][16];
} s_ble;
static unsigned queue_resets, gap_calls;
static char gap_order[16];
static void clear_transport_queues(void) {
    REQUIRE(critical_depth==0); ++queue_resets;
}
static int ble_gap_disc_cancel(void) {
    REQUIRE(critical_depth==0); REQUIRE(!s_ble.status.enabled);
    gap_order[gap_calls++]='C'; return 0;
}
static int ble_gap_adv_stop(void) {
    REQUIRE(critical_depth==0); REQUIRE(!s_ble.status.enabled);
    gap_order[gap_calls++]='A'; return 0;
}
static int ble_gap_terminate(uint16_t handle,uint8_t reason) {
    REQUIRE(critical_depth==0 && handle==s_ble.conn_handle);
    REQUIRE(reason==BLE_ERR_REM_USER_CONN_TERM);
    gap_order[gap_calls++]='T'; return 0;
}
'''

BLE_CASES = r'''
static void setup(bool enabled,bool host_ready,bool connected) {
    memset(&s_ble,0,sizeof(s_ble));
    s_ble.status=(platform_multiplayer_ble_status_t){.enabled=enabled,.host_ready=host_ready,
        .ready=enabled,.state=enabled?PLATFORM_MULTIPLAYER_BLE_READY:PLATFORM_MULTIPLAYER_BLE_OFF,
        .lobby_mode=PLATFORM_MULTIPLAYER_BLE_LOBBY_HOST,.lobby_session_id=42,
        .lobby_game_token=7,.discovered_lobbies=3};
    s_ble.conn_handle=connected?23:BLE_HS_CONN_HANDLE_NONE;
    s_ble.host_collision_deadline_ms=1000; s_ble.host_collision_round=4;
    s_ble.host_collision_scanning=s_ble.selected_host_valid=true;
    memset(s_ble.lobbies,0xa5,sizeof(s_ble.lobbies));
    critical_depth=0; queue_resets=gap_calls=0; memset(gap_order,0,sizeof(gap_order));
}
static void disabled(void) {
    for (unsigned host=0;host<2;++host) for (unsigned connected=0;connected<2;++connected) {
        setup(false,host==0,connected!=0); /* Exercise an already-ready shared host first. */
        uint8_t before[sizeof(s_ble)]; memcpy(before,&s_ble,sizeof(before));
        platform_multiplayer_ble_disable();
        REQUIRE(gap_calls==0 && queue_resets==0 && critical_depth==0);
        REQUIRE(memcmp(before,&s_ble,sizeof(before))==0);
    }
}
static void enabled(bool host_ready) {
    for (unsigned connected=0;connected<2;++connected) {
        setup(true,host_ready,connected!=0);
        platform_multiplayer_ble_disable();
        REQUIRE(!s_ble.status.enabled && !s_ble.status.ready);
        REQUIRE(s_ble.status.state==PLATFORM_MULTIPLAYER_BLE_OFF);
        REQUIRE(s_ble.status.lobby_mode==PLATFORM_MULTIPLAYER_BLE_LOBBY_IDLE);
        REQUIRE(!s_ble.status.lobby_session_id && !s_ble.status.lobby_game_token);
        REQUIRE(!s_ble.status.discovered_lobbies && !s_ble.selected_host_valid);
        REQUIRE(!s_ble.host_collision_deadline_ms && !s_ble.host_collision_round && !s_ble.host_collision_scanning);
        for (size_t i=0;i<sizeof(s_ble.lobbies);++i) REQUIRE(((uint8_t *)s_ble.lobbies)[i]==0);
        REQUIRE(queue_resets==1 && critical_depth==0);
        REQUIRE(gap_calls==(host_ready?(connected?3U:2U):0U));
        REQUIRE(strcmp(gap_order,host_ready?(connected?"CAT":"CA"):"")==0);
        REQUIRE(s_ble.status.host_ready==host_ready); /* Shared host remains initialized. */
        const unsigned before=gap_calls;
        platform_multiplayer_ble_disable();
        REQUIRE(gap_calls==before && queue_resets==1); /* Repeated shutdown owns nothing. */
    }
}
int main(int argc,char **argv) {
    REQUIRE(argc==2);
    if (!strcmp(argv[1],"disabled")) disabled();
    else if (!strcmp(argv[1],"enabled")) enabled(true);
    else if (!strcmp(argv[1],"host-not-ready")) enabled(false);
    else REQUIRE(false);
    return 0;
}
'''

CONSOLE_FIXTURE = r'''
#include "console/shell.h"
#include "platform/multiplayer_wifi.h"
#include "p4/multiplayer_uart.h"
#define P4_CONSOLE_WIFI_MULTIPLAYER 1
#define P4_CONSOLE_BLE_MULTIPLAYER 1
static console_mp_transport_kind_t s_multiplayer_transport;
static platform_multiplayer_wifi_status_t wifi;
static platform_multiplayer_ble_status_t ble;
static p4_mp_uart_status_t uart;
static char s_multiplayer_status[96];
platform_multiplayer_wifi_status_t platform_multiplayer_wifi_status(void) { return wifi; }
platform_multiplayer_ble_status_t platform_multiplayer_ble_status(void) { return ble; }
p4_mp_uart_status_t p4_mp_uart_endpoint_status(void) { return uart; }
static bool s_multiplayer_transport_ready, s_multiplayer_uart_ready;
static unsigned wifi_enables, other_transport_calls;
static esp_err_t enable_result;
static void multiplayer_frame_received(void *context,uint64_t route,const uint8_t *data,size_t size) {}
esp_err_t platform_multiplayer_wifi_enable(platform_multiplayer_wifi_handler_t handler,void *context) {
    REQUIRE(handler==multiplayer_frame_received && context==NULL);
    ++wifi_enables; return enable_result;
}
void platform_multiplayer_wifi_disable(void) { ++other_transport_calls; }
void platform_multiplayer_ble_disable(void) { ++other_transport_calls; }
void p4_mp_uart_endpoint_reset_route(void) { ++other_transport_calls; }
esp_err_t platform_multiplayer_ble_enable(platform_multiplayer_ble_frame_handler_t handler,void *context) {
    ++other_transport_calls; return ESP_OK;
}
esp_err_t platform_multiplayer_ble_set_lobby_mode(platform_multiplayer_ble_lobby_mode_t mode,uint32_t session,uint16_t game) {
    ++other_transport_calls; return ESP_OK;
}
esp_err_t p4_mp_uart_endpoint_set_handler(p4_mp_uart_frame_handler_t handler,void *context) {
    ++other_transport_calls; return ESP_OK;
}
static unsigned presents, invalidations, arms, clears, reports;
static esp_err_t present_result;
#define ESP_LOGW(...) ((void)0)
static void arm_interactive_touch_timestamp_for_present(void) { ++arms; }
static void clear_interactive_touch_timestamp_after_present(void) { ++clears; }
static void report_scroll_timing(console_shell_t *shell,bool presented) {
    REQUIRE(presented); ++reports;
}
static esp_err_t present(console_shell_t *shell) {
    REQUIRE(arms==presents+1 && clears==presents);
    ++presents;
    if (shell) shell->dirty=false; /* Renderer consumed the dirty flag before submission. */
    return present_result;
}
void console_shell_invalidate_native_cache(console_shell_t *shell) {
    REQUIRE(shell!=NULL); ++invalidations;
}
'''

CONSOLE_CASES = r'''
static void starting(void) {
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    strcpy(s_multiplayer_status,"stale previous room message");
    wifi=(platform_multiplayer_wifi_status_t){.starting=true,.last_error=ESP_FAIL};
    const console_mp_transport_status_t status=multiplayer_transport_status();
    REQUIRE(status.starting && status.last_error==ESP_FAIL);
    const char *message=multiplayer_connection_status_text(&status);
    REQUIRE(message && message[0] && strstr(message,"Wi-Fi"));
    const console_shell_runtime_info_t info=runtime_info();
    REQUIRE(strcmp(info.multiplayer_status,message)==0);
    REQUIRE(strstr(info.multiplayer_status,"stale")==NULL);
}
static void failed(void) {
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    wifi=(platform_multiplayer_wifi_status_t){.last_error=ESP_FAIL};
    const console_mp_transport_status_t status=multiplayer_transport_status();
    REQUIRE(!status.available && !status.starting && status.last_error==ESP_FAIL);
    const char *message=multiplayer_connection_status_text(&status);
    REQUIRE(message && message[0] && strstr(message,"Wi-Fi"));
    wifi.starting=true;
    const console_mp_transport_status_t pending=multiplayer_transport_status();
    REQUIRE(strcmp(message,multiplayer_connection_status_text(&pending))!=0);
    wifi.starting=false;
    REQUIRE(strcmp(runtime_info().multiplayer_status,message)==0);
}
static void ready_and_idle(void) {
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    strcpy(s_multiplayer_status,"Room ready; waiting for players");
    for (unsigned available=0;available<2;++available) {
        wifi=(platform_multiplayer_wifi_status_t){.available=available!=0,.ready=available!=0,
            .route_id=123,.rx_frames=4,.tx_frames=5};
        const console_mp_transport_status_t status=multiplayer_transport_status();
        REQUIRE(status.available==(available!=0) && status.ready==(available!=0));
        REQUIRE(status.active_route_id==123 && status.rx_frames==4 && status.tx_frames==5);
        REQUIRE(multiplayer_connection_status_text(&status)==NULL);
        REQUIRE(strcmp(runtime_info().multiplayer_status,s_multiplayer_status)==0);
    }
    wifi.last_error=ESP_FAIL; /* A live route's previous error must not replace room state. */
    const console_mp_transport_status_t status=multiplayer_transport_status();
    REQUIRE(multiplayer_connection_status_text(&status)==NULL);
}
static void other_transports(void) {
    strcpy(s_multiplayer_status,"Existing transport message");
    wifi=(platform_multiplayer_wifi_status_t){.starting=true,.last_error=ESP_FAIL};
    for (unsigned kind=CONSOLE_MP_TRANSPORT_WIRED;kind<=CONSOLE_MP_TRANSPORT_BLE;++kind) {
        s_multiplayer_transport=(console_mp_transport_kind_t)kind;
        const console_mp_transport_status_t synthetic={.starting=true,.last_error=ESP_FAIL};
        REQUIRE(multiplayer_connection_status_text(&synthetic)==NULL);
        REQUIRE(strcmp(runtime_info().multiplayer_status,s_multiplayer_status)==0);
    }
}
static void ble_host_readiness(void) {
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_BLE;
    const platform_multiplayer_ble_state_t boot_states[]={
        PLATFORM_MULTIPLAYER_BLE_STARTING_RADIO, PLATFORM_MULTIPLAYER_BLE_STARTING_HOST};
    for (unsigned i=0;i<sizeof(boot_states)/sizeof(boot_states[0]);++i) {
        ble=(platform_multiplayer_ble_status_t){.enabled=true,.state=boot_states[i]};
        const console_mp_transport_status_t status=multiplayer_transport_status();
        REQUIRE(status.starting && !status.available && !status.ready);
    }
    const platform_multiplayer_ble_state_t browsing_states[]={
        PLATFORM_MULTIPLAYER_BLE_STANDBY, PLATFORM_MULTIPLAYER_BLE_DISCOVERING,
        PLATFORM_MULTIPLAYER_BLE_CONNECTING, PLATFORM_MULTIPLAYER_BLE_SECURING,
        PLATFORM_MULTIPLAYER_BLE_DISCOVERING_GATT, PLATFORM_MULTIPLAYER_BLE_READY};
    for (unsigned i=0;i<sizeof(browsing_states)/sizeof(browsing_states[0]);++i) {
        const bool peer_ready=browsing_states[i]==PLATFORM_MULTIPLAYER_BLE_READY;
        ble=(platform_multiplayer_ble_status_t){.enabled=true,.host_ready=true,
            .state=browsing_states[i],.connected=peer_ready,.ready=peer_ready,
            .encrypted=peer_ready,.route_id=123,.rx_frames=4,.tx_frames=5};
        const console_mp_transport_status_t status=multiplayer_transport_status();
        REQUIRE(!status.starting && status.available); /* Browsing does not require a peer. */
        REQUIRE(status.ready==peer_ready && status.encrypted==peer_ready);
        REQUIRE(status.active_route_id==123 && status.rx_frames==4 && status.tx_frames==5);
    }
    const platform_multiplayer_ble_status_t inactive[]={
        {.enabled=false,.state=PLATFORM_MULTIPLAYER_BLE_STARTING_RADIO},
        {.enabled=true,.state=PLATFORM_MULTIPLAYER_BLE_OFF},
        {.enabled=true,.state=PLATFORM_MULTIPLAYER_BLE_ERROR}};
    for (unsigned i=0;i<sizeof(inactive)/sizeof(inactive[0]);++i) {
        ble=inactive[i];
        const console_mp_transport_status_t status=multiplayer_transport_status();
        REQUIRE(!status.starting && !status.available && !status.ready);
    }
}
static void wifi_retry(bool failed) {
    s_multiplayer_transport=CONSOLE_MP_TRANSPORT_WIFI;
    s_multiplayer_transport_ready=true;
    if (failed) {
        wifi=(platform_multiplayer_wifi_status_t){.last_error=ESP_FAIL};
        for (unsigned failure=0;failure<2;++failure) {
            enable_result=failure?ESP_FAIL:ESP_OK;
            REQUIRE(activate_multiplayer_transport(CONSOLE_MP_TRANSPORT_WIFI)==enable_result);
            REQUIRE(wifi_enables==failure+1 && other_transport_calls==0);
            REQUIRE(s_multiplayer_transport==CONSOLE_MP_TRANSPORT_WIFI);
        }
    } else {
        for (unsigned available=0;available<2;++available)
            for (unsigned starting=0;starting<2;++starting)
                for (unsigned error=0;error<2;++error) {
                    if (!available && !starting && error) continue;
                    wifi=(platform_multiplayer_wifi_status_t){.available=available!=0,
                        .starting=starting!=0,.last_error=error?ESP_FAIL:ESP_OK};
                    REQUIRE(activate_multiplayer_transport(CONSOLE_MP_TRANSPORT_WIFI)==ESP_OK);
                    REQUIRE(wifi_enables==0 && other_transport_calls==0);
                    REQUIRE(s_multiplayer_transport_ready);
                }
    }
}
static void redraw(bool timeout) {
    console_shell_t shell={0}; shell.dirty=true;
    present_result=timeout?ESP_ERR_TIMEOUT:ESP_OK;
    REQUIRE(present_interactive(&shell)==ESP_OK);
    REQUIRE(shell.dirty==timeout && invalidations==(timeout?1U:0U));
    REQUIRE(presents==1 && arms==1 && clears==1 && reports==1);
    if (timeout) {
        present_result=ESP_OK;
        REQUIRE(present_interactive(&shell)==ESP_OK);
        REQUIRE(!shell.dirty && invalidations==1);
        REQUIRE(presents==2 && arms==2 && clears==2 && reports==2);
        present_result=ESP_ERR_TIMEOUT;
        REQUIRE(present_interactive(NULL)==ESP_OK && invalidations==1);
    }
    present_result=ESP_FAIL;
    REQUIRE(present_interactive(&shell)==ESP_FAIL); /* Other failures still propagate. */
}
int main(int argc,char **argv) {
    REQUIRE(argc==2);
    if (!strcmp(argv[1],"starting")) starting();
    else if (!strcmp(argv[1],"failed")) failed();
    else if (!strcmp(argv[1],"ready-idle")) ready_and_idle();
    else if (!strcmp(argv[1],"other-transports")) other_transports();
    else if (!strcmp(argv[1],"ble-host-readiness")) ble_host_readiness();
    else if (!strcmp(argv[1],"wifi-retry")) wifi_retry(true);
    else if (!strcmp(argv[1],"wifi-noop")) wifi_retry(false);
    else if (!strcmp(argv[1],"redraw")) redraw(true);
    else if (!strcmp(argv[1],"present-success")) redraw(false);
    else REQUIRE(false);
    return 0;
}
'''

HOST_FIXTURE = r'''
#include "platform/ble_host.h"
static int s_lock, critical_depth;
#define portENTER_CRITICAL(lock) do { REQUIRE(critical_depth==0); ++critical_depth; } while (0)
#define portEXIT_CRITICAL(lock) do { REQUIRE(critical_depth==1); --critical_depth; } while (0)
static struct { platform_ble_host_status_t status; bool start_in_progress; } s_host;
enum { PLATFORM_BLE_START_STACK_BYTES=8192, PLATFORM_BLE_START_PRIORITY=5, PLATFORM_BLE_START_CORE=1, pdPASS=1 };
static unsigned creates;
static int task_result=pdPASS;
static void start_task(void *arg) {}
static int xTaskCreatePinnedToCore(void (*task)(void *),const char *name,unsigned stack,
                                void *arg,unsigned priority,void *handle,int core) {
    REQUIRE(critical_depth==0 && s_host.start_in_progress);
    REQUIRE(s_host.status.state==PLATFORM_BLE_HOST_STARTING_RADIO);
    REQUIRE(s_host.status.last_error==0); /* Observed before the worker gets CPU time. */
    REQUIRE(task==start_task && arg==NULL && handle==NULL);
    ++creates; return task_result;
}
'''

HOST_CASES = r'''
int main(int argc,char **argv) {
    REQUIRE(argc==2);
    if (!strcmp(argv[1],"retry") || !strcmp(argv[1],"create-failure")) {
        const bool failure=!strcmp(argv[1],"create-failure");
        s_host.status.state=PLATFORM_BLE_HOST_ERROR; s_host.status.last_error=ESP_FAIL;
        task_result=failure?0:pdPASS;
        REQUIRE(platform_ble_host_start()==(failure?ESP_ERR_NO_MEM:ESP_OK));
        REQUIRE(creates==1 && critical_depth==0);
        REQUIRE(s_host.start_in_progress==!failure);
        REQUIRE(s_host.status.state==(failure?PLATFORM_BLE_HOST_ERROR:PLATFORM_BLE_HOST_STARTING_RADIO));
        REQUIRE(s_host.status.last_error==(failure?ESP_ERR_NO_MEM:0));
    } else if (!strcmp(argv[1],"noop")) {
        for (unsigned initialized=0;initialized<2;++initialized)
            for (unsigned starting=0;starting<2;++starting) {
                if (!initialized && !starting) continue;
                memset(&s_host,0,sizeof(s_host));
                s_host.status.initialized=initialized!=0; s_host.start_in_progress=starting!=0;
                s_host.status.state=initialized?PLATFORM_BLE_HOST_READY:PLATFORM_BLE_HOST_STARTING_RADIO;
                uint8_t before[sizeof(s_host)]; memcpy(before,&s_host,sizeof(before));
                REQUIRE(platform_ble_host_start()==ESP_OK);
                REQUIRE(!creates && !critical_depth && !memcmp(before,&s_host,sizeof(before)));
            }
    } else REQUIRE(false);
    return 0;
}
'''


class TransportResponseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="p4-transport-response-")
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        (directory / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_TIMEOUT -2\n#define ESP_ERR_INVALID_STATE -3\n"
            "#define ESP_ERR_NO_MEM -4\n#define ESP_ERR_NOT_SUPPORTED -5\n")
        ble_source = BLE_SOURCE.read_text()
        ble_units = "\n".join(function(ble_source, name) for name in
            ["clear_lobbies_locked", "platform_multiplayer_ble_disable"])
        cls.ble = cls.build(directory, "ble", COMMON + BLE_FIXTURE + ble_units + BLE_CASES)
        host_source = HOST_SOURCE.read_text()
        host_units = "\n".join(function(host_source, name) for name in
            ["set_state", "platform_ble_host_start"])
        cls.host = cls.build(directory, "host", COMMON + HOST_FIXTURE + host_units + HOST_CASES)

        source = CONSOLE_SOURCE.read_text()
        types = []
        for kind, name in [("enum", "console_mp_transport_kind_t"),
                           ("struct", "console_mp_transport_status_t")]:
            match = re.search(r"typedef " + kind + r"\s*\{[^{}]*\}\s*" + name + ";", source)
            if match is None:
                raise ValueError(f"No typedef for {name}")
            definition = match.group()
            if kind == "struct" and "last_error" not in definition:
                # Baseline ABI fixture: the old mapper leaves this new field zero.
                definition = definition.replace("}", "    esp_err_t last_error;\n}", 1)
            types.append(definition)
        status = function(source, "multiplayer_transport_status")
        try:
            helper = function(source, "multiplayer_connection_status_text")
        except ValueError:
            helper = "static const char *multiplayer_connection_status_text(const console_mp_transport_status_t *transport) { return NULL; }"
        runtime = function(source, "runtime_info")
        begin = runtime.find("    const char *const connection_status =")
        if begin < 0:
            begin = runtime.index("    (void)snprintf(info.multiplayer_status,")
        assignment = runtime[begin:runtime.index(";", runtime.index("(void)snprintf(info.multiplayer_status,", begin)) + 1]
        runtime_fixture = """
static console_shell_runtime_info_t runtime_info(void) {
    console_shell_runtime_info_t info={0};
    const console_mp_transport_status_t multiplayer=multiplayer_transport_status();
""" + assignment + "\nreturn info;\n}\n"
        cls.console = cls.build(directory, "console", COMMON + "\n".join(types)
            + CONSOLE_FIXTURE + status + helper + runtime_fixture
            + function(source, "activate_multiplayer_transport")
            + function(source, "present_interactive") + CONSOLE_CASES)

    @classmethod
    def build(cls, directory, name, source):
        path = directory / (name + ".c")
        path.write_text(source)
        executable = directory / name
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
                   "-DCONFIG_P4_BOARD_M5STACK_TAB5=1", "-I", str(directory)]
        for component in ["console_shell", "p4_game_api", "p4_desktop", "p4_multiplayer",
                          "platform_multiplayer_ble", "platform_multiplayer_wifi", "platform_ble_host"]:
            command += ["-I", str(ROOT / "components" / component / "include")]
        command += [str(path), "-o", str(executable)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return executable

    def run_case(self, executable, case):
        result = subprocess.run([executable, case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_disabled_multiplayer_does_not_touch_shared_host(self):
        self.run_case(self.ble, "disabled")

    def test_enabled_multiplayer_stops_owned_radio_activity_once(self):
        self.run_case(self.ble, "enabled")

    def test_enabled_before_host_ready_neutralizes_without_gap_calls(self):
        self.run_case(self.ble, "host-not-ready")

    def test_wifi_starting_overrides_stale_room_message(self):
        self.run_case(self.console, "starting")

    def test_wifi_failure_is_propagated_and_visible(self):
        self.run_case(self.console, "failed")

    def test_ready_or_idle_wifi_retains_room_message(self):
        self.run_case(self.console, "ready-idle")

    def test_ble_and_wired_keep_their_status_text(self):
        self.run_case(self.console, "other-transports")

    def test_bluetooth_host_readiness_does_not_require_a_connected_peer(self):
        self.run_case(self.console, "ble-host-readiness")

    def test_failed_wifi_selection_retries_and_propagates_result(self):
        self.run_case(self.console, "wifi-retry")

    def test_active_or_starting_wifi_selection_does_not_restart(self):
        self.run_case(self.console, "wifi-noop")

    def test_timed_out_frame_invalidates_cache_and_redraws_next_tick(self):
        self.run_case(self.console, "redraw")

    def test_successful_frame_stays_clean(self):
        self.run_case(self.console, "present-success")

    def test_ble_host_retry_publishes_starting_before_task_creation(self):
        self.run_case(self.host, "retry")

    def test_ble_host_task_creation_failure_restores_error(self):
        self.run_case(self.host, "create-failure")

    def test_ble_host_started_or_starting_does_not_create_another_task(self):
        self.run_case(self.host, "noop")


if __name__ == "__main__":
    unittest.main()
