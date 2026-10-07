#!/usr/bin/env python3
"""Reject native video compile-command drift without building or using devices."""
import copy
import importlib.util
from pathlib import Path
import shlex
import sys
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("tab5_native_verify", SCRIPTS / "verify-console-os-tab5.py")
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

DIMENSIONS = ["-DP4_DOOM_NATIVE_WIDTH=768", "-DP4_DOOM_NATIVE_HEIGHT=480",
              "-DDOOMGENERIC_RESX=768", "-DDOOMGENERIC_RESY=480"]
NATIVE = "-DDOOM_VIDEO_NATIVE_TAB5=1"
INDEXED = "-DP4_DOOM_INDEXED_PACKET_EXPERIMENT=1"
VENDOR = "third_party/doomgeneric/doomgeneric/"


def entry(source, definitions):
    return {"directory": "/fixture with spaces/build", "file": "../" + source,
            "arguments": ["riscv32-esp-elf-gcc", *definitions, "-c", "../" + source]}


def candidate():
    return [
        entry("apps/console_os/main/console_os_main.c", [*DIMENSIONS, NATIVE, INDEXED]),
        entry("apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c",
              [*DIMENSIONS, NATIVE, INDEXED]),
        entry("components/doom_video/src/doom_video_tab5_worker.c",
              [NATIVE, INDEXED, "-DP4_DOOM_TAB5_FUSED_PRESCALE=0"]),
        entry("components/doom_video/src/doom_video_indexed.c", [NATIVE, INDEXED]),
        *[entry(VENDOR + name, DIMENSIONS) for name in
          ("r_draw.c", "r_main.c", "r_plane.c", "v_video.c", "i_video.c", "doomgeneric.c", "r_things.c")],
    ]


class NativeVideoTests(unittest.TestCase):
    def test_native_candidate_arguments_and_shell_commands(self):
        commands = candidate()
        verifier.verify_native_video(commands)
        for command in commands:
            command["command"] = shlex.join(command.pop("arguments"))
        verifier.verify_native_video(commands)

    def test_split_macro_operands_and_bare_native_enable(self):
        commands = candidate()
        for command in commands:
            arguments = []
            for arg in command["arguments"]:
                if arg == NATIVE:
                    arguments.extend(("-D", "DOOM_VIDEO_NATIVE_TAB5"))
                elif arg.startswith("-D"):
                    arguments.extend(("-D", arg[2:]))
                else:
                    arguments.append(arg)
            command["arguments"] = arguments
        verifier.verify_native_video(commands)

    def test_each_required_source_is_required(self):
        commands = candidate()
        for index in range(len(commands) - 1):  # Extra r_things is not the minimum set.
            with self.subTest(source=commands[index]["file"]), self.assertRaisesRegex(ValueError, "missing native"):
                verifier.verify_native_video(commands[:index] + commands[index + 1:])

    def test_each_raster_dimension_missing_or_mismatched(self):
        for index, command in enumerate(candidate()):
            if DIMENSIONS[0] not in command["arguments"]:
                continue
            for definition in DIMENSIONS:
                for replacement in (None, definition.split("=")[0] + "=320"):
                    commands = candidate()
                    flags = commands[index]["arguments"]
                    if replacement is None:
                        flags.remove(definition)
                    else:
                        flags[flags.index(definition)] = replacement
                    with self.subTest(source=command["file"], definition=definition, replacement=replacement), self.assertRaises(ValueError):
                        verifier.verify_native_video(commands)

    def test_native_and_indexed_path_flags_missing_or_disabled(self):
        for index in range(4):
            for definition in (NATIVE, INDEXED) if index >= 2 else (NATIVE,):
                for replacement in (None, definition[:-1] + "0"):
                    commands = candidate()
                    flags = commands[index]["arguments"]
                    if replacement is None:
                        flags.remove(definition)
                    else:
                        flags[flags.index(definition)] = replacement
                    with self.subTest(index=index, definition=definition, replacement=replacement), self.assertRaises(ValueError):
                        verifier.verify_native_video(commands)

    def test_repeated_required_definitions_are_rejected_even_if_identical(self):
        for index, command in enumerate(candidate()):
            for flag in command["arguments"]:
                if flag not in (*DIMENSIONS, NATIVE, INDEXED):
                    continue
                for repeated in (flag, flag.split("=")[0] + "=0"):
                    commands = candidate()
                    commands[index]["arguments"].append(repeated)
                    with self.subTest(index=index, repeated=repeated), self.assertRaises(ValueError):
                        verifier.verify_native_video(commands)

    def test_undefinition_cannot_hide_before_or_after_a_valid_definition(self):
        for extra in (["-UDOOMGENERIC_RESX"], ["-U", "DOOMGENERIC_RESX"]):
            for before in (False, True):
                commands = candidate()
                flags = commands[0]["arguments"]
                flags[:] = extra + flags if before else flags + extra
                with self.subTest(extra=extra, before=before), self.assertRaises(ValueError):
                    verifier.verify_native_video(commands)

    def test_retired_flags_rejected_in_any_compiled_unit(self):
        for name in ("P4_CONSOLE_NATIVE_AIR_LOW_RES", "P4_CONSOLE_NATIVE_TIDE_BLAST_LOW_RES",
                     "P4_CONSOLE_NATIVE_CHECKERS_LOW_RES", "P4_DOOM_TAB5_FUSED_PRESCALE"):
            commands = candidate() + [entry("components/other/source.c", ["-D" + name + "=0"])]
            verifier.verify_native_video(commands)
            for flags in (["-D" + name], ["-D" + name + "=1"],
                          ["-D" + name + "=0", "-D" + name + "=1"]):
                commands[-1] = entry("components/other/source.c", flags)
                with self.subTest(name=name, flags=flags), self.assertRaisesRegex(ValueError, "retired Tab5"):
                    verifier.verify_native_video(commands)

    def test_legacy_video_adapter_is_rejected(self):
        commands = candidate() + [entry("components/doom_video/src/doom_video_espidf.c", [])]
        with self.assertRaisesRegex(ValueError, "legacy Doom video"):
            verifier.verify_native_video(commands)

    def test_explicit_video_worker_raster_override_is_rejected(self):
        commands = candidate()
        commands[2]["arguments"].append("-DDOOMGENERIC_RESX=320")
        with self.assertRaises(ValueError):
            verifier.verify_native_video(commands)

    def test_duplicate_compile_entry_with_conflicting_flags_is_rejected(self):
        commands = candidate()
        stale = copy.deepcopy(commands[0])
        stale["arguments"].remove(DIMENSIONS[0])
        stale["arguments"].append("-DP4_DOOM_NATIVE_WIDTH=320")
        with self.assertRaises(ValueError):
            verifier.verify_native_video(commands + [stale])

    def test_unrelated_compiler_definitions_are_not_constrained(self):
        commands = candidate() + [entry("components/other/source.c", ["-DFOO=1", "-DFOO=2"])]
        verifier.verify_native_video(commands)


if __name__ == "__main__":
    unittest.main()
