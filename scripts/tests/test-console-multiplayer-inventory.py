#!/usr/bin/env python3
"""Exercise the production cartridge registry and shell inventory publisher.

Fixtures come from release-eligible game.json files plus the Wacky manifest
that is installed on the two development Tab5s. The package builder validates
their metadata; this test starts at the validated catalog boundary and does
not claim to execute cartridge payloads or inspect currently connected units.
No firmware build, game download, or serial access is needed.
"""
import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get(
    "P4_CONSOLE_INVENTORY_SOURCE", ROOT / "apps/console_os/main/console_os_main.c"))
sys.path.insert(0, str(ROOT / "scripts"))


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def block(text, start):
    """Keep a C body unchanged, balancing outside comments and literals."""
    brace = text.index("{", start)
    depth = 0
    for token in re.finditer(
            r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
            text[brace:]):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return text[start:brace + token.end()]
    raise ValueError("Unclosed C body")


def function(text, name):
    match = re.search(r"^(?:static\s+)?[^;{}\n][^;{}]*?\b" + re.escape(name) +
                      r"\s*\([^;{}]*?\)\s*\{", text, re.M)
    if match is None:
        raise ValueError(f"Missing production function: {name}")
    return block(text, match.start())


def manifest_fixtures(directory):
    package = load_module("inventory_package", ROOT / "scripts/build-game-package.py")
    release = load_module("inventory_release", ROOT / "scripts/p4_game_release.py")
    manifests = []
    for path in sorted((ROOT / "games").glob("*/game.json")):
        if not release.development_only(json.loads(path.read_text())):
            manifests.append(package.load_manifest(path))
    # Read the packaging script's literal manifest without executing its build,
    # local copyrighted-data reads, or objcopy commands.
    tree = ast.parse((ROOT / "scripts/wacky/package_probe.py").read_text())
    wacky = None
    for node in tree.body:
        if not isinstance(node, ast.Assign) or len(node.targets) != 1:
            continue
        target = node.targets[0]
        if isinstance(target, ast.Name) and target.id == "manifest":
            wacky = ast.literal_eval(node.value)
        elif (isinstance(target, ast.Subscript) and
              isinstance(target.value, ast.Name) and target.value.id == "manifest" and
              isinstance(target.slice, ast.Constant) and target.slice.value == "multiplayer"):
            wacky["multiplayer"] = ast.literal_eval(node.value)
    if wacky is None:
        raise AssertionError("Wacky packaging manifest not found")
    path = directory / "wacky-manifest.json"
    path.write_text(json.dumps(wacky))
    manifests.append(package.load_manifest(path, allow_dev=True))
    return manifests


def catalog_initializer(manifest):
    fields = [f'.launcher_id={manifest["launcher_id"]}',
              f'.required_capabilities={manifest["_required_mask"]}',
              f'.optional_capabilities={manifest["_optional_mask"]}']
    for key in ("id", "title"):
        fields.append(f'.{key}={json.dumps(manifest[key])}')
    profile = manifest["_multiplayer_profile"]
    if profile:
        members = [f'.{key}={value}' for key, value in profile.items()
                   if key not in ("style", "style_code")]
        members.append(f'.style={profile["style_code"]}')
        fields.append('.multiplayer_profile={' + ','.join(members) + '}')
    return '{.valid=true,.package={' + ','.join(fields) + '}}'


PRELUDE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "console/shell.h"
#include "p4/doom_multiplayer.h"
#include "p4/multiplayer_registry.h"
#include "platform/game_catalog.h"
#include "platform/game_storage.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define P4_CONSOLE_WIFI_MULTIPLAYER 1
#define P4_CONSOLE_BLE_MULTIPLAYER 1
enum { CONSOLE_MP_TRANSPORT_WIRED=0, CONSOLE_MP_TRANSPORT_BLE=1,
       CONSOLE_MP_TRANSPORT_WIFI=2, P4_PROTECTED_GAME_REJECTED=2 };
static platform_game_catalog_t s_game_catalog;
static p4_mp_game_registry_t s_multiplayer_game_registry;
static size_t s_multiplayer_game_selection;
static platform_game_storage_status_t s_game_storage_status;
static int protected_game_lineage_check(const p4_game_package_info_t *package) {
    (void)package; return 0; /* No protected game is in this inventory fixture. */
}
bool platform_game_storage_arena_present(void) { return true; }
'''

CASES = r'''
static console_shell_runtime_info_t reset_fixture(void) {
    memset(&s_game_catalog, 0, sizeof(s_game_catalog));
    s_multiplayer_game_selection = 0U;
    s_multiplayer_lobby_candidate_count = 0U;
    memset(s_multiplayer_lobby_candidates, 0, sizeof(s_multiplayer_lobby_candidates));
    s_game_catalog.available = true;
    s_game_catalog.entry_count = sizeof(installed) / sizeof(installed[0]);
    memcpy(s_game_catalog.entries, installed, sizeof(installed));
    s_game_storage_status.state = PLATFORM_GAME_STORAGE_APP_READY;
    rebuild_multiplayer_game_registry();
    return inventory_runtime();
}
static void check_bindings(console_shell_runtime_info_t info, size_t expected) {
    assert(s_multiplayer_game_registry.game_count == expected);
    assert(info.multiplayer_game_count == P4_DOOM_MP_GAME_COUNT + expected);
    for (size_t i=0; i<expected; ++i) {
        const size_t selection = P4_DOOM_MP_GAME_COUNT + i;
        const p4_mp_registered_game_t *registration =
            p4_mp_game_registry_at(&s_multiplayer_game_registry, i);
        const platform_game_catalog_entry_t *game = multiplayer_native_game_for(selection);
        assert(game && game->valid && native_game_supports_multiplayer(game));
        assert(game->package.launcher_id == registration->launcher_id);
        assert(strcmp(game->package.id, registration->game_id) == 0);
        assert(strcmp(info.multiplayer_games[selection].title, game->package.title) == 0);
        assert(info.multiplayer_games[selection].available);
        assert(info.multiplayer_games[selection].transport_mask == 7U);
        s_multiplayer_game_selection = selection;
        assert(multiplayer_selected_native_game() == game);
        assert(strcmp(multiplayer_selected_game_title(), game->package.title) == 0);
    }
    assert(multiplayer_native_game_for(P4_DOOM_MP_GAME_COUNT + expected) == NULL);
}
static void registered(void) {
    check_bindings(reset_fixture(), EXPECTED_NATIVE);
    assert(EXPECTED_NATIVE == 9U); /* Eight release games plus installed Wacky. */
    for (size_t i=0; i<EXPECTED_NATIVE; ++i)
        assert(p4_mp_game_registry_find_launcher(&s_multiplayer_game_registry,
                                                expected_launchers[i]));
}
static void arena_title(void) {
    (void)reset_fixture();
    const char *expected = "Doom Arena by Game Changers";
    assert(P4_DOOM_MP_GAME_GAME_CHANGERS_AI == 2U);
    assert(strcmp(multiplayer_game_title_at(2U), expected) == 0);
    s_multiplayer_game_selection = 2U;
    assert(strcmp(multiplayer_selected_game_title(), expected) == 0);
    s_multiplayer_lobby_candidate_count = 1U;
    s_multiplayer_lobby_candidates[0] = (console_mp_lobby_candidate_t){
        .game_selection = 2U, .game_available = true,
    };
    console_shell_runtime_info_t info = inventory_runtime();
    assert(CONSOLE_MULTIPLAYER_GAME_TITLE_MAX_BYTES == 64U);
    assert(sizeof(info.multiplayer_games[2].title) == 64U);
    assert(sizeof(info.multiplayer_game_title) == sizeof(info.multiplayer_games[2].title));
    assert(sizeof(info.multiplayer_lobbies[0].game_title) == sizeof(info.multiplayer_games[2].title));
    assert(strcmp(info.multiplayer_games[2].title, expected) == 0);
    assert(strcmp(info.multiplayer_game_title, expected) == 0);
    assert(strcmp(info.multiplayer_lobbies[0].game_title, expected) == 0);
}
static void excluded(void) {
    (void)reset_fixture();
    size_t single_player = 0U;
    for (size_t i=0; i<s_game_catalog.entry_count; ++i) {
        platform_game_catalog_entry_t *game = &s_game_catalog.entries[i];
        if (!((game->package.required_capabilities | game->package.optional_capabilities) &
              P4_GAME_CAP_MULTIPLAYER_SESSION)) {
            ++single_player;
            assert(!p4_mp_game_registry_find_launcher(&s_multiplayer_game_registry,
                                                     game->package.launcher_id));
        }
        if (game->package.launcher_id == expected_launchers[0]) game->valid = false;
        if (game->package.launcher_id == expected_launchers[1])
            game->package.multiplayer_profile.protocol = 0U;
    }
    assert(single_player > 0U);
    rebuild_multiplayer_game_registry();
    check_bindings(inventory_runtime(), EXPECTED_NATIVE - 2U);
    for (size_t i=0; i<2; ++i)
        assert(!p4_mp_game_registry_find_launcher(&s_multiplayer_game_registry,
                                                 expected_launchers[i]));
}
static void removed(void) {
    (void)reset_fixture();
    const uint32_t removed_id = expected_launchers[0];
    for (size_t i=0; i<s_game_catalog.entry_count; ++i) {
        if (s_game_catalog.entries[i].package.launcher_id == removed_id) {
            memmove(&s_game_catalog.entries[i], &s_game_catalog.entries[i+1],
                    (s_game_catalog.entry_count-i-1U)*sizeof(s_game_catalog.entries[0]));
            --s_game_catalog.entry_count;
            break;
        }
    }
    rebuild_multiplayer_game_registry();
    check_bindings(inventory_runtime(), EXPECTED_NATIVE - 1U);
    assert(!p4_mp_game_registry_find_launcher(&s_multiplayer_game_registry, removed_id));
    s_game_catalog = (platform_game_catalog_t){0};
    rebuild_multiplayer_game_registry();
    check_bindings(inventory_runtime(), 0U);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (strcmp(argv[1], "registered") == 0) registered();
    else if (strcmp(argv[1], "arena_title") == 0) arena_title();
    else if (strcmp(argv[1], "excluded") == 0) excluded();
    else if (strcmp(argv[1], "removed") == 0) removed();
    else return 2;
    return 0;
}
'''


class InventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="p4-console-inventory-")
        cls.addClassCleanup(cls.temp.cleanup)
        directory = Path(cls.temp.name)
        manifests = manifest_fixtures(directory)
        natives = [m for m in manifests if m["_multiplayer_profile"]]
        fixture = '\nstatic const platform_game_catalog_entry_t installed[] = {\n' + \
            ',\n'.join(catalog_initializer(m) for m in manifests) + '\n};\n'
        fixture += f'enum {{ EXPECTED_NATIVE={len(natives)} }};\n'
        fixture += 'static const uint32_t expected_launchers[]={' + \
            ','.join(str(m["launcher_id"]) for m in natives) + '};\n'
        source = SOURCE.read_text()
        capacity = re.search(r"CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS\s*=\s*(\d+)", source)
        prelude = PRELUDE + f'\nenum {{ CONSOLE_NATIVE_MULTIPLAYER_RUNTIME_PLAYERS={capacity[1]} }};\n'
        lobby_type = re.search(r"typedef struct\s*\{[^{}]*\}\s*console_mp_lobby_candidate_t;",
                               source).group()
        prelude += lobby_type + '\nstatic console_mp_lobby_candidate_t ' \
            's_multiplayer_lobby_candidates[CONSOLE_MULTIPLAYER_LOBBY_LIST_MAX];\n' \
            'static size_t s_multiplayer_lobby_candidate_count;\n'
        names = ["native_game_supports_multiplayer", "rebuild_multiplayer_game_registry",
                 "multiplayer_game_count", "multiplayer_native_game_at", "multiplayer_native_game_for",
                 "multiplayer_selected_native_game", "multiplayer_game_title_at",
                 "multiplayer_selected_game_title",
                 "multiplayer_transport_mask_for", "multiplayer_game_data_ready_for"]
        units = [function(source, name) for name in names]
        catalog = (ROOT / "components/platform_game_catalog/src/platform_game_catalog.c").read_text()
        units.insert(0, function(catalog, "platform_game_catalog_find_launcher"))
        runtime = function(source, "runtime_info")
        count = re.search(r"\.multiplayer_game_count\s*=[\s\S]*?,", runtime).group()
        publisher = block(runtime, runtime.index("for (size_t i = 0U; i < selectable_game_count"))
        selected_title = re.search(
            r"\(void\)snprintf\(\s*info\.multiplayer_game_title\s*,[^;]*;", runtime).group()
        lobby_count = re.search(r"const size_t lobby_display_count\s*=[^;]*;", runtime).group()
        lobby_publisher = block(runtime, runtime.index("for (size_t index = 0U; index < lobby_display_count"))
        units.append('static console_shell_runtime_info_t inventory_runtime(void) {\n'
                     'const size_t selectable_game_count = multiplayer_game_count();\n'
                     'console_shell_runtime_info_t info = {' + count + '};\n' +
                     '\n'.join([selected_title, publisher, lobby_count, lobby_publisher]) +
                     '\nreturn info;\n}')
        (directory / "inventory.c").write_text(prelude + '\n'.join(units) + fixture + CASES)
        (directory / "esp_err.h").write_text("typedef int esp_err_t;\n")
        cls.executable = directory / "inventory"
        includes = ["console_shell", "p4_desktop", "doom_multiplayer", "p4_multiplayer",
                    "p4_multiplayer_registry", "p4_game_api", "p4_game_package",
                    "platform_game_catalog", "platform_game_storage"]
        command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(directory)]
        for name in includes:
            command += ["-I", str(ROOT / "components" / name / "include")]
        command += [str(directory / "inventory.c"),
                    str(ROOT / "components/p4_multiplayer_registry/src/multiplayer_registry.c"),
                    str(ROOT / "components/p4_game_api/src/game_runtime.c"), "-o", str(cls.executable)]
        built = subprocess.run(command, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, case):
        result = subprocess.run([self.executable, case], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_installed_manifest_inventory_reaches_runtime_with_selection_indices(self):
        self.run_case("registered")

    def test_exact_arena_title_survives_game_selected_and_lobby_buffers(self):
        self.run_case("arena_title")

    def test_invalid_packages_profiles_and_single_player_titles_are_excluded(self):
        self.run_case("excluded")

    def test_removed_games_do_not_remain_in_registry_or_runtime(self):
        self.run_case("removed")


if __name__ == "__main__":
    unittest.main()
