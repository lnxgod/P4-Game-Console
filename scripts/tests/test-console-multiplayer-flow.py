#!/usr/bin/env python3
"""Run Console OS room/configuration C code with hardware boundary fixtures.

The tested functions are extracted unchanged from console_os_main.c and linked
with the real lobby codec, Doom setup codec and compact game-token code. Radio
scan results, storage metadata, clocks and the UI refresh are controlled inputs.
A deterministic non-cryptographic digest replaces mbedTLS only in this host
harness; the tests check when identity changes, not cryptographic correctness.

Set P4_CONSOLE_FLOW_SOURCE to a saved console_os_main.c to reproduce regressions
against another revision without modifying the working checkout.
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
    p=ROOT/relative
    return p if p.exists() else BASE/relative
SOURCE = Path(os.environ.get("P4_CONSOLE_FLOW_SOURCE", ROOT / "apps/console_os/main/console_os_main.c"))


def function(source, name):
    """Extract a static C definition, balancing braces outside strings/comments."""
    match = re.search(r"^static\s+[^;{}]*?\b" + re.escape(name) + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"No definition for {name}")
    brace = match.end() - 1
    tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:])
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console/shell.h"
#include "p4/multiplayer.h"
#include "p4/multiplayer_ble.h"
#include "p4/doom_multiplayer.h"
#include "p4/doom_arena.h"
#include "p4/doom_resume.h"
#include "platform/game_catalog.h"
#include "platform/multiplayer_ble.h"
#include "platform/multiplayer_wifi.h"
#define P4_CONSOLE_WIFI_MULTIPLAYER 1
#define P4_CONSOLE_BLE_MULTIPLAYER 1
#define P4_CONSOLE_BLE_GAMEPAD 0
#define CONSOLE_MULTIPLAYER_PEER_TIMEOUT_MS 3000
#define CONSOLE_DOOM_MULTIPLAYER_PROTOCOL 1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static esp_err_t configure_multiplayer_local_offer(void);
static void refresh_multiplayer_lobby_candidates(void);
static const console_mp_lobby_candidate_t *selected_multiplayer_lobby(void);
'''

FIXTURES = r'''
static platform_game_catalog_entry_t fixture_game;
static platform_multiplayer_wifi_lobby_t wifi_rooms[PLATFORM_MULTIPLAYER_WIFI_MAX_LOBBIES];
static platform_multiplayer_ble_lobby_t ble_rooms[PLATFORM_MULTIPLAYER_BLE_MAX_LOBBIES];
static size_t wifi_count, ble_count;
static bool editable = true;
static bool s_arena_resume_valid;
static p4_doom_resume_record_t s_arena_resume;
static bool fixture_content_ready = true;
static char s_multiplayer_status[96];
static const uint8_t s_arena_content_sha256[P4_MP_SHA256_BYTES] = {1};
static const uint8_t s_chex_quest_sha256[P4_MP_SHA256_BYTES] = {2};
static const uint8_t s_doom_shareware_sha256[P4_MP_SHA256_BYTES] = {3};
static int64_t esp_timer_get_time(void) { return 1000000; }
static uint32_t random_nonzero(void) { return 42; }
static const char *esp_err_to_name(esp_err_t error) { (void)error; return "fixture error"; }
static int mbedtls_sha256(const unsigned char *input, size_t length, unsigned char output[32], int is224) {
    (void)is224;
    uint32_t h = 2166136261U;
    for (size_t i=0; i<length; ++i) h = (h ^ input[i]) * 16777619U;
    for (size_t i=0; i<32; ++i) { h = (h ^ (uint32_t)i) * 16777619U; output[i] = (unsigned char)(h >> 16); }
    return 0;
}
static size_t multiplayer_game_count(void) { return P4_DOOM_MP_GAME_COUNT + 1U; }
static const platform_game_catalog_entry_t *multiplayer_selected_native_game(void) {
    return s_multiplayer_game_selection == P4_DOOM_MP_GAME_COUNT ? &fixture_game : NULL;
}
static const platform_game_catalog_entry_t *multiplayer_native_game_for(size_t selection) {
    return selection == P4_DOOM_MP_GAME_COUNT ? &fixture_game : NULL;
}
static bool multiplayer_game_data_ready_for(size_t selection) {
    (void)selection; return fixture_content_ready;
}
static bool native_game_supports_multiplayer(const platform_game_catalog_entry_t *game) {
    return game != NULL && game->valid;
}
static bool multiplayer_dice_available(void) { return false; }
static bool p4_mp_game_dice_settings_encode(const p4_game_package_info_t *package,
    bool enabled, uint8_t settings[P4_MP_GAME_SETTINGS_BYTES]) {
    (void)package; (void)enabled; memset(settings, 0, P4_MP_GAME_SETTINGS_BYTES); return true;
}
static bool multiplayer_settings_editable(void) { return editable; }
static void reset_multiplayer_lobby(const char *reason) {
    (void)reason;
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_BROWSING;
    s_multiplayer_lobby_selection = 0;
    s_multiplayer_lobby_selected_id = 0;
    s_multiplayer_lobby_selected_session_id = 0;
}
static console_shell_runtime_info_t runtime_info(void) { return (console_shell_runtime_info_t){0}; }
void console_shell_set_runtime_info(console_shell_t *shell, const console_shell_runtime_info_t *info) {
    shell->runtime = *info;
}
static esp_err_t select_multiplayer_transport(console_mp_transport_kind_t transport) {
    s_multiplayer_transport = transport; return ESP_OK;
}
static esp_err_t request_ble_multiplayer_transport(void) {
    s_multiplayer_transport = CONSOLE_MP_TRANSPORT_BLE; return ESP_OK;
}
static esp_err_t release_multiplayer_ble_transport(const char *reason) {
    (void)reason; s_multiplayer_transport = CONSOLE_MP_TRANSPORT_WIRED; return ESP_OK;
}
static bool set_multiplayer_lobby_selection(console_shell_t *shell, size_t selection, bool host) {
    (void)shell; (void)selection; (void)host; return false;
}
size_t platform_multiplayer_wifi_list_lobbies(platform_multiplayer_wifi_lobby_t *out, size_t capacity) {
    size_t count = wifi_count < capacity ? wifi_count : capacity;
    memcpy(out, wifi_rooms, count * sizeof(*out)); return count;
}
size_t platform_multiplayer_ble_list_lobbies(platform_multiplayer_ble_lobby_t *out, size_t capacity) {
    size_t count = ble_count < capacity ? ble_count : capacity;
    memcpy(out, ble_rooms, count * sizeof(*out)); return count;
}
'''

CASES = r'''
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; } } while (0)
static void reset_fixture(void) {
    memset(&fixture_game, 0, sizeof(fixture_game));
    fixture_game.valid = true;
    strcpy(fixture_game.package.id, "org.p4console.fixture");
    memset(fixture_game.package.payload_sha256, 7, 32);
    fixture_game.package.multiplayer_profile = (p4_game_multiplayer_profile_t){
        .schema=1, .style=P4_GAME_MULTIPLAYER_STYLE_REALTIME, .min_players=2,
        .max_players=4, .tick_rate_hz=30, .input_delay_ticks=2, .message_bytes=32, .protocol=1};
    s_multiplayer_lobby_state = CONSOLE_MP_LOBBY_BROWSING;
    s_multiplayer_game_selection = P4_DOOM_MP_GAME_COUNT;
    s_multiplayer_transport = CONSOLE_MP_TRANSPORT_BLE;
    s_multiplayer_local_setup = (p4_doom_mp_setup_t){
        .game=P4_DOOM_MP_GAME_DOOM, .mode=P4_DOOM_MP_MODE_DEATHMATCH,
        .episode=1, .map=1, .skill=5, .no_monsters=true};
    assert(configure_multiplayer_local_offer() == ESP_OK);
}
static int arena_checkpoint_protocol_isolated(void) {
    _Static_assert(P4_DOOM_ARENA_CHECKPOINT_PROTOCOL == 8U, "checkpoint protocol must differ from legacy Arena 7");
    reset_fixture();
    s_multiplayer_transport = CONSOLE_MP_TRANSPORT_WIFI;
    s_multiplayer_game_selection = P4_DOOM_MP_GAME_GAME_CHANGERS_AI;
    CHECK(configure_multiplayer_local_offer() == ESP_OK);
    CHECK(s_multiplayer_local_offer.game_protocol == 8U);
    uint8_t wire[P4_MP_OFFER_PAYLOAD_BYTES];
    p4_mp_lobby_offer_t decoded;
    CHECK(p4_mp_lobby_offer_encode(&s_multiplayer_local_offer, wire) == P4_MP_OK);
    CHECK(p4_mp_lobby_offer_decode(wire, sizeof(wire), &decoded) == P4_MP_OK);
    CHECK(decoded.game_protocol == 8U);
    CHECK(p4_mp_lobby_offers_compatible(&s_multiplayer_local_offer, &decoded));
    /* Keep every field, including digest, identical to isolate the protocol
     * comparison that rejects legacy tic-zero replay peers. */
    decoded.game_protocol = 7U;
    CHECK(!p4_mp_lobby_offers_compatible(&s_multiplayer_local_offer, &decoded));
    CHECK(!p4_mp_lobby_offers_compatible(&decoded, &s_multiplayer_local_offer));
    for (size_t game = P4_DOOM_MP_GAME_DOOM; game <= P4_DOOM_MP_GAME_CHEX_QUEST; ++game) {
        s_multiplayer_game_selection = game;
        CHECK(configure_multiplayer_local_offer() == ESP_OK);
        CHECK(s_multiplayer_local_offer.game_protocol == 1U);
    }
    s_multiplayer_game_selection = P4_DOOM_MP_GAME_COUNT;
    fixture_game.package.multiplayer_profile.protocol = 23U;
    CHECK(configure_multiplayer_local_offer() == ESP_OK);
    CHECK(s_multiplayer_local_offer.game_protocol == 23U);
    return 0;
}
static int transport_rebuilds_four_player_identity(void) {
    reset_fixture();
    CHECK(s_multiplayer_local_offer.player_capacity == 2);
    uint8_t before[32]; memcpy(before, s_multiplayer_local_offer.compatibility_sha256, 32);
    uint16_t token_before = multiplayer_game_token();
    console_shell_t shell = {0};
    console_shell_action_t action = {.type=CONSOLE_ACTION_MULTIPLAYER_CONFIGURE,
        .multiplayer_option=CONSOLE_MULTIPLAYER_OPTION_TRANSPORT, .multiplayer_delta=1};
    handle_multiplayer_config_action(&shell, &action);
    CHECK(s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI);
    CHECK(s_multiplayer_local_offer.player_capacity == 4);
    CHECK(memcmp(before, s_multiplayer_local_offer.compatibility_sha256, 32) != 0);
    CHECK(multiplayer_game_token() != token_before);
    action.multiplayer_delta = -1;
    handle_multiplayer_config_action(&shell, &action);
    CHECK(s_multiplayer_local_offer.player_capacity == 2);
    CHECK(memcmp(before, s_multiplayer_local_offer.compatibility_sha256, 32) == 0);
    return 0;
}
static void prepare_room_fixtures(bool ble) {
    reset_fixture();
    s_multiplayer_transport = ble ? CONSOLE_MP_TRANSPORT_BLE : CONSOLE_MP_TRANSPORT_WIFI;
    assert(configure_multiplayer_local_offer() == ESP_OK);
    uint16_t native_token = multiplayer_game_token();
    s_multiplayer_game_selection = P4_DOOM_MP_GAME_DOOM;
    assert(configure_multiplayer_local_offer() == ESP_OK);
    uint16_t doom_token = multiplayer_game_token();
    s_multiplayer_game_selection = P4_DOOM_MP_GAME_COUNT;
    assert(configure_multiplayer_local_offer() == ESP_OK);
    const platform_multiplayer_wifi_lobby_t rooms[] = {
        {.lobby_id=101, .session_id=201, .game_token=doom_token, .players_present=1, .player_capacity=2},
        {.lobby_id=102, .session_id=202, .game_token=native_token, .players_present=4, .player_capacity=4},
        {.lobby_id=103, .session_id=203, .game_token=native_token, .players_present=1, .player_capacity=4},
        {.lobby_id=104, .session_id=204, .game_token=native_token, .players_present=1, .player_capacity=4},
    };
    memcpy(wifi_rooms, rooms, sizeof(rooms)); wifi_count = 4;
    for (size_t i=0; i<4; ++i) ble_rooms[i] = (platform_multiplayer_ble_lobby_t){
        .lobby_id=rooms[i].lobby_id, .session_id=rooms[i].session_id,
        .game_token=rooms[i].game_token, .players_present=i == 1 ? 2 : 1,
        .player_capacity=2};
    ble_count = 4;
}
static int room_filter(bool ble) {
    prepare_room_fixtures(ble);
    refresh_multiplayer_lobby_candidates();
    CHECK(s_multiplayer_lobby_candidate_count == 2);
    CHECK(s_multiplayer_lobby_candidates[0].lobby_id == 103);
    CHECK(s_multiplayer_lobby_candidates[1].lobby_id == 104);
    CHECK(s_multiplayer_game_selection == P4_DOOM_MP_GAME_COUNT);
    return 0;
}
static int full_arena_return(void) {
    prepare_room_fixtures(false);
    s_multiplayer_game_selection=P4_DOOM_MP_GAME_GAME_CHANGERS_AI;
    CHECK(configure_multiplayer_local_offer()==ESP_OK);
    for(size_t i=0;i<wifi_count;++i) {
        wifi_rooms[i].game_token=multiplayer_game_token();
        wifi_rooms[i].players_present=4; wifi_rooms[i].player_capacity=4;
    }
    refresh_multiplayer_lobby_candidates();
    CHECK(s_multiplayer_lobby_candidate_count==0);
    s_arena_resume_valid=true; s_arena_resume.session_id=202;
    refresh_multiplayer_lobby_candidates();
    CHECK(s_multiplayer_lobby_candidate_count==1);
    CHECK(s_multiplayer_lobby_candidates[0].session_id==202);
    wifi_rooms[0].players_present=1;
    refresh_multiplayer_lobby_candidates();
    CHECK(s_multiplayer_lobby_candidate_count==2);
    CHECK(s_multiplayer_lobby_candidates[0].session_id==201);
    CHECK(s_multiplayer_lobby_candidates[1].session_id==202);
    return 0;
}

static int delayed_selection(void) {
    prepare_room_fixtures(false);
    refresh_multiplayer_lobby_candidates();
    /* A rendered row can move before the event reaches the OS handler. */
    platform_multiplayer_wifi_lobby_t saved = wifi_rooms[2];
    wifi_rooms[2] = wifi_rooms[3]; wifi_rooms[3] = saved;
    console_shell_t shell = {0};
    CHECK(SELECT_ROOM(&shell, 1, 203));
    const console_mp_lobby_candidate_t *selected = selected_multiplayer_lobby();
    CHECK(selected != NULL && selected->lobby_id == 103 && selected->session_id == 203);
    wifi_rooms[3].session_id = 999;
    /* An old, still-valid choice must not survive a failed tap on another room. */
    CHECK(SELECT_ROOM(&shell, 1, 204));
    CHECK(selected_multiplayer_lobby()->session_id == 204);
    CHECK(!SELECT_ROOM(&shell, 1, 203));
    CHECK(selected_multiplayer_lobby() == NULL);
    return 0;
}
static int ambiguous_selection(void) {
    prepare_room_fixtures(false);
    console_shell_t shell = {0};
    CHECK(SELECT_ROOM(&shell, 2, 204));
    CHECK(selected_multiplayer_lobby()->session_id == 204);
    /* Two other routes reuse one advertised session; the old choice stays live. */
    wifi_rooms[1].session_id = 203;
    wifi_rooms[1].players_present = 1;
    CHECK(!SELECT_ROOM(&shell, 1, 203));
    CHECK(selected_multiplayer_lobby() == NULL);
    return 0;
}
#if HAS_DIRECT_SELECTION
static int direct_selection(void) {
    reset_fixture();
    console_shell_t shell = {.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_TRANSPORT};
    console_shell_action_t action = {.type=CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT,
        .multiplayer_game_selection=P4_DOOM_MP_GAME_GAME_CHANGERS_AI};
    handle_multiplayer_selection_action(&shell, &action);
    CHECK(s_multiplayer_game_selection == P4_DOOM_MP_GAME_GAME_CHANGERS_AI);
    CHECK(strcmp(s_multiplayer_local_offer.game_id, "org.p4console.gamechangersai") == 0);
    action.type = CONSOLE_ACTION_MULTIPLAYER_TRANSPORT_SELECT;
    action.multiplayer_transport_kind = CONSOLE_MP_TRANSPORT_WIFI;
    handle_multiplayer_selection_action(&shell, &action);
    CHECK(s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI);
    action.multiplayer_transport_kind = CONSOLE_MP_TRANSPORT_BLE;
    handle_multiplayer_selection_action(&shell, &action);
    CHECK(s_multiplayer_transport == CONSOLE_MP_TRANSPORT_WIFI);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_TRANSPORT);
    return 0;
}
static int unavailable_game(void) {
    reset_fixture();
    fixture_content_ready = false;
    console_shell_t shell = {.multiplayer_view=CONSOLE_MULTIPLAYER_VIEW_TRANSPORT};
    console_shell_action_t action = {.type=CONSOLE_ACTION_MULTIPLAYER_GAME_SELECT,
        .multiplayer_game_selection=P4_DOOM_MP_GAME_GAME_CHANGERS_AI};
    handle_multiplayer_selection_action(&shell, &action);
    CHECK(s_multiplayer_game_selection == P4_DOOM_MP_GAME_COUNT);
    CHECK(shell.multiplayer_view == CONSOLE_MULTIPLAYER_VIEW_GAME);
    return 0;
}
#endif
static int stable_selection(void) {
    prepare_room_fixtures(false);
    s_multiplayer_lobby_selected_id = 104;
    s_multiplayer_lobby_selected_session_id = 204;
    refresh_multiplayer_lobby_candidates();
    const console_mp_lobby_candidate_t *selected = selected_multiplayer_lobby();
    CHECK(selected != NULL && selected->lobby_id == 104 && selected->session_id == 204);
    platform_multiplayer_wifi_lobby_t saved = wifi_rooms[2];
    wifi_rooms[2] = wifi_rooms[3]; wifi_rooms[3] = saved;
    refresh_multiplayer_lobby_candidates();
    selected = selected_multiplayer_lobby();
    CHECK(selected != NULL && selected->lobby_id == 104 && selected->session_id == 204);
    wifi_rooms[2].session_id = 999;  /* Same radio address, restarted host. */
    refresh_multiplayer_lobby_candidates();
    CHECK(selected_multiplayer_lobby() == NULL);
    CHECK(s_multiplayer_lobby_selection == 0);
    return 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "checkpoint_protocol") == 0) return arena_checkpoint_protocol_isolated();
    if (strcmp(argv[1], "transport") == 0) return transport_rebuilds_four_player_identity();
    if (strcmp(argv[1], "full_return") == 0) return full_arena_return();
    if (strcmp(argv[1], "wifi_filter") == 0) return room_filter(false);
    if (strcmp(argv[1], "ble_filter") == 0) return room_filter(true);
    if (strcmp(argv[1], "stable_selection") == 0) return stable_selection();
    if (strcmp(argv[1], "delayed_selection") == 0) return delayed_selection();
    if (strcmp(argv[1], "ambiguous_selection") == 0) return ambiguous_selection();
#if HAS_DIRECT_SELECTION
    if (strcmp(argv[1], "direct_selection") == 0) return direct_selection();
    if (strcmp(argv[1], "unavailable_game") == 0) return unavailable_game();
#endif
    return 2;
}
'''


class MultiplayerFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-console-mp-flow-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        source = SOURCE.read_text()
        # Keep production-owned state/type definitions; no shadow copy of their layout.
        start = source.index("typedef enum {\n    CONSOLE_MP_LOBBY_IDLE")
        end = source.index("static int64_t s_multiplayer_peer_last_seen_us;")
        state = source[start:end + len("static int64_t s_multiplayer_peer_last_seen_us;")]
        # Candidate prototype needs the real candidate type defined first.
        prelude = PRELUDE.replace("static const console_mp_lobby_candidate_t *selected_multiplayer_lobby(void);", "")
        names = ["multiplayer_selected_game_is_doom", "multiplayer_selected_game_is_chex",
                 "multiplayer_selected_game_is_arena", "native_multiplayer_mode",
                 "native_multiplayer_content_identity", "configure_multiplayer_local_offer",
                 "multiplayer_game_token", "multiplayer_game_selection_for_token",
                 "multiplayer_full_arena_room_returnable", "refresh_multiplayer_lobby_candidates", "selected_multiplayer_lobby",
                 "multiplayer_cycle_range", "set_multiplayer_lobby_selection", "handle_multiplayer_config_action"]
        fixtures = FIXTURES.replace(function(FIXTURES, "set_multiplayer_lobby_selection"), "")
        if re.search(r"^static esp_err_t activate_multiplayer_transport\(", source, re.M):
            # Stub hardware activation below the real identity-rebuild boundary.
            fixtures = fixtures.replace(function(fixtures, "select_multiplayer_transport"),
                "static esp_err_t activate_multiplayer_transport(console_mp_transport_kind_t transport) { s_multiplayer_transport=transport; return ESP_OK; }")
            for name in ("request_ble_multiplayer_transport", "release_multiplayer_ble_transport"):
                fixtures = fixtures.replace(function(fixtures, name), "")
            names += ["select_multiplayer_transport", "request_ble_multiplayer_transport",
                      "release_multiplayer_ble_transport", "select_multiplayer_game",
                      "change_multiplayer_transport"]
        cls.direct_selection = "handle_multiplayer_selection_action(" in source
        if cls.direct_selection:
            names += ["handle_multiplayer_selection_action", "multiplayer_transport_mask_for"]
        selector = function(source, "set_multiplayer_lobby_selection")
        select_arguments = "shell, index, false, session" if "expected_session" in selector else "shell, index, false"
        prelude += f"\n#define SELECT_ROOM(shell, index, session) set_multiplayer_lobby_selection({select_arguments})\n"
        prelude += f"#define HAS_DIRECT_SELECTION {int(cls.direct_selection)}\n"
        units = [function(source, name) for name in names]
        declarations = "\n".join(unit[:unit.index("{")].strip() + ";" for unit in units)
        (directory / "flow.c").write_text(prelude + state + declarations + fixtures + "\n".join(units) + CASES)
        (directory / "esp_err.h").write_text("typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_STATE 1\n#define ESP_ERR_NOT_FOUND 2\n#define ESP_ERR_NOT_SUPPORTED 3\n#define ESP_ERR_NOT_FINISHED 4\n#define ESP_ERR_INVALID_ARG 5\n")
        cls.executable = directory / "flow"
        includes = ["console_shell", "p4_game_api", "p4_desktop", "p4_multiplayer",
                    "doom_multiplayer", "p4_game_package", "platform_game_catalog",
                    "platform_game_storage", "platform_multiplayer_ble", "platform_multiplayer_wifi"]
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter", "-DCONFIG_P4_BOARD_M5STACK_TAB5=1", "-I", str(directory)]
        for component in includes:
            command += ["-I", str(ROOT / "components" / component / "include"),
                        "-I", str(BASE / "components" / component / "include")]
        command += [str(directory / "flow.c")]
        command += [str(source_path(path)) for path in [
            "components/p4_multiplayer/src/lobby.c", "components/p4_multiplayer/src/ble_frame.c",
            "components/p4_multiplayer/src/packet.c", "components/doom_multiplayer/src/doom_multiplayer.c"]]
        command += ["-o", str(cls.executable)]
        build = subprocess.run(command, capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)

    def run_case(self, name):
        result = subprocess.run([self.executable, name], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_checkpoint_protocol_is_separate_from_legacy_arena_and_other_games(self):
        self.run_case("checkpoint_protocol")

    def test_transport_change_rebuilds_capacity_and_compatibility(self):
        self.run_case("transport")

    def test_full_arena_room_is_visible_only_for_retained_session(self): self.run_case("full_return")

    def test_wifi_only_lists_joinable_rooms_for_selected_game(self):
        self.run_case("wifi_filter")

    def test_ble_only_lists_joinable_rooms_for_selected_game(self):
        self.run_case("ble_filter")

    def test_delayed_room_tap_keeps_original_session_and_rejects_a_restart(self):
        self.run_case("delayed_selection")

    def test_ambiguous_room_session_is_not_joined(self):
        self.run_case("ambiguous_selection")

    def test_direct_game_and_transport_selection_respects_arena_transport(self):
        if not self.direct_selection:
            self.skipTest("This source predates direct selection actions")
        self.run_case("direct_selection")

    def test_unavailable_game_returns_to_game_selection(self):
        if not self.direct_selection:
            self.skipTest("This source predates direct selection actions")
        self.run_case("unavailable_game")

    def test_room_selection_survives_reordering_but_not_a_host_restart(self):
        self.run_case("stable_selection")


if __name__ == "__main__":
    unittest.main()
