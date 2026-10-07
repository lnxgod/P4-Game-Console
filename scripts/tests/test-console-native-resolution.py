#!/usr/bin/env python3
"""Exercise maintained native launch admission and retained legacy surfaces."""
from pathlib import Path
import json
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONSOLE = ROOT / "apps/console_os/main/console_os_main.c"
MAIN_CMAKE = ROOT / "apps/console_os/main/CMakeLists.txt"
APP_CMAKE = ROOT / "apps/console_os/CMakeLists.txt"
RETIRED_FLAGS = (
    "P4_CONSOLE_NATIVE_AIR_LOW_RES",
    "P4_CONSOLE_NATIVE_TIDE_BLAST_LOW_RES",
    "P4_CONSOLE_NATIVE_CHECKERS_LOW_RES",
)
BOARDS = {
    "m5stack-tab5": "CONFIG_P4_BOARD_M5STACK_TAB5",
    "waveshare-esp32-p4-wifi6-touch-lcd-4.3":
        "CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3",
    "elecrow-crowpanel-advanced-10": "CONFIG_P4_BOARD_ELECROW_10",
    "olimex-esp32-p4-pc": "CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC",
}


class NativeResolutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="p4-native-resolution-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        text = CONSOLE.read_text()
        start = text.index("static bool native_game_surface(")
        end = text.index("static bool native_game_video_supported(", start)
        selection = text[start:end]
        source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "p4/game.h"
#include "cartridge_video.h"
typedef struct {
    bool valid;
    struct {
        uint32_t required_capabilities, optional_capabilities;
        const char *id, *folder;
    } package;
} platform_game_catalog_entry_t;
''' + selection + r'''
int main(void)
{
    const struct { const char *id, *folder; } titles[] = {
        {"org.p4console.p4-air-hockey", "GAMES/ARCADE"},
        {"org.p4console.tide-maze", "GAMES/ARCADE"},
        {"org.p4console.blast-circuit", "GAMES/ARCADE"},
        {"org.p4console.checkers", "GAMES/BOARD"},
        {"org.p4console.new-title", "GAMES/ARCADE"},
        {"org.p4console.calculator", "SYSTEM/TOOLS"},
        {"org.p4console.input-test", "SYSTEM/TESTS"},
        {"org.p4console.av-test", "SYSTEM/TESTS"},
    };
    assert(!native_game_surface(NULL, NULL, NULL));
    const platform_game_catalog_entry_t invalid = {0};
    assert(!native_game_surface(&invalid, NULL, NULL));
    for (unsigned title = 0; title < sizeof(titles) / sizeof(titles[0]); ++title) {
        for (unsigned required = 0; required < 2; ++required) {
            for (unsigned optional = 0; optional < 2; ++optional) {
                platform_game_catalog_entry_t game = {.valid = true, .package = {
                    .required_capabilities = P4_GAME_CAP_VIDEO |
                        (required ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U),
                    .optional_capabilities = optional ? P4_GAME_CAP_VIDEO_HIGH_RES : 0U,
                    .id = titles[title].id, .folder = titles[title].folder,
                }};
                uint16_t width = 0, height = 0;
                const bool result = native_game_surface(&game, &width, &height);
#if CONFIG_P4_BOARD_M5STACK_TAB5
                assert(result == (required != 0));
                if (required) assert(width == 768 && height == 480);
                else assert(width == 0 && height == 0);
#elif CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3
                assert(result);
                assert(width == ((required || optional) ? 768 : 320));
                assert(height == ((required || optional) ? 480 : 200));
#else
                assert(result == (required == 0));
                if (!required) assert(width == 320 && height == 200);
                else assert(width == 0 && height == 0);
#endif
            }
        }
    }
    return 0;
}
'''
        path = cls.directory / "selection.c"
        path.write_text(source)
        cls.executables = {}
        for board, active_macro in BOARDS.items():
            executable = cls.directory / board
            command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra",
                       "-Werror", "-I", str(ROOT / "components/p4_game_api/include"),
                       "-I", str(CONSOLE.parent)]
            command += [f"-D{macro}={int(macro == active_macro)}"
                        for macro in BOARDS.values()]
            command += [str(path), "-o", str(executable)]
            compiled = subprocess.run(command, capture_output=True, text=True)
            if compiled.returncode:
                raise AssertionError(compiled.stdout + compiled.stderr)
            cls.executables[board] = executable

    def test_tab5_requires_native_resolution_for_every_title(self):
        subprocess.run([self.executables["m5stack-tab5"]], check=True)

    def test_explicit_legacy_boards_retain_surface_admission(self):
        for board in BOARDS:
            if board != "m5stack-tab5":
                with self.subTest(board=board):
                    subprocess.run([self.executables[board]], check=True)

    def test_stale_resolution_experiments_fail_closed_in_cmake(self):
        text = MAIN_CMAKE.read_text()
        prelude = text[:text.index("set(P4_DOOM_TOUCH_AUDIO_MAIN")]
        start = text.index('if(P4_ACTIVE_BOARD_PROFILE STREQUAL "m5stack-tab5")\n'
                           '    foreach(retired_override')
        end = text.index("target_compile_features(", start)
        retirement = text[start:end]
        script = self.directory / "retired-flags.cmake"
        for board in BOARDS:
            for flag in RETIRED_FLAGS:
                for cached in (False, True):
                    for value in (None, "OFF", "ON"):
                        with self.subTest(board=board, flag=flag, cached=cached,
                                          value=value):
                            prefix = ('function(target_compile_definitions)\n'
                                      'endfunction()\n'
                                      f'set(P4_ACTIVE_BOARD_PROFILE "{board}")\n')
                            if value is not None:
                                cache = ' CACHE BOOL "stale experiment"' if cached else ''
                                prefix += f'set({flag} {value}{cache})\n'
                            script.write_text(prefix + prelude + retirement)
                            result = subprocess.run(["cmake", "-P", script],
                                                    capture_output=True, text=True)
                            rejected = board == "m5stack-tab5" and value == "ON"
                            self.assertEqual(result.returncode != 0, rejected,
                                             result.stdout + result.stderr)
                            if rejected:
                                self.assertIn(f"{flag} is retired", result.stderr)

    def test_stale_resolution_defines_fail_closed_in_source(self):
        text = CONSOLE.read_text()
        start = text.index("/* Reject stale diagnostic flags")
        end = text.index("#ifndef P4_CONSOLE_SIGNAL_SCAN", start)
        path = self.directory / "retired-flags.c"
        path.write_text(text[start:end] + "int main(void) { return 0; }\n")
        for board, active_macro in BOARDS.items():
            for flag in RETIRED_FLAGS:
                for value in (0, 1):
                    with self.subTest(board=board, flag=flag, value=value):
                        command = [os.environ.get("CC", "cc"), "-std=c11", "-fsyntax-only"]
                        command += [f"-D{macro}={int(macro == active_macro)}"
                                    for macro in BOARDS.values()]
                        command += [f"-D{flag}={value}", str(path)]
                        result = subprocess.run(command, capture_output=True, text=True)
                        rejected = board == "m5stack-tab5" and value == 1
                        self.assertEqual(result.returncode != 0, rejected, result.stderr)
                        if rejected:
                            self.assertIn("low-resolution overrides are retired", result.stderr)

    def test_only_maintained_tab5_configure_enforces_native_registry(self):
        text = APP_CMAKE.read_text()
        start = text.index('set(P4_GENERATED_GAMES_CMAKE "')
        end = text.index('include("${P4_GENERATED_GAMES_CMAKE}")', start)
        registry = text[start:end]
        capture = self.directory / "registry-command.json"
        python_shim = self.directory / "capture-python"
        python_shim.write_text(
            '#!/usr/bin/env python3\nimport json, os, sys\n'
            'with open(os.environ["P4_TEST_REGISTRY_COMMAND"], "w") as output:\n'
            '    json.dump(sys.argv[1:], output)\n')
        python_shim.chmod(0o755)
        script = self.directory / "registry-gate.cmake"
        for board in BOARDS:
            with self.subTest(board=board):
                script.write_text(f'set(P4_BOARD_PROFILE "{board}")\n'
                                  f'set(P4_IDF_PYTHON "{python_shim}")\n' + registry)
                environment = dict(os.environ, P4_TEST_REGISTRY_COMMAND=str(capture))
                subprocess.run(["cmake", "-P", script], check=True,
                               capture_output=True, text=True, env=environment)
                arguments = json.loads(capture.read_text())
                self.assertEqual("--require-native-resolution" in arguments,
                                 board == "m5stack-tab5")


if __name__ == "__main__":
    unittest.main()
