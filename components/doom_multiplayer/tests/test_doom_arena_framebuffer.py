#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run real Arena UI, D_Display ordering, border copies and pixel primitives.

Only the 3D scene, normal HUD widgets, display device and WAD lookup are fixtures.
A synthetic Doom patch font drives the production glyph rasterizer. No WAD,
physical display or firmware build is needed. Every retained framebuffer byte is
compared after score, break, vote and menu transitions at several view sizes.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
VENDOR = Path(os.environ.get("P4_DOOM_FRAMEBUFFER_VENDOR", ROOT / "third_party/doomgeneric/doomgeneric"))

def function(source, name):
    match = re.search(r"^(?:static\s+)?[\w\s*]+?\b" + re.escape(name)
                      + r"\s*\([^;{}]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError(f"No definition for {name}")
    brace = match.end() - 1
    depth = 0
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[brace:]):
        if token.group() == "{": depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0: return source[match.start():brace + token.end()]
    raise ValueError(f"Unclosed definition for {name}")

with tempfile.TemporaryDirectory(prefix="p4-arena-framebuffer-") as temp:
    temp = Path(temp)
    definitions = []
    for name, functions in {
        "v_video.c": ["V_DrawFilledBox", "V_DrawPatch"],
        "hu_lib.c": ["HUlib_clearTextLine", "HUlib_initTextLine", "HUlib_addCharToTextLine", "HUlib_drawTextLine"],
        "hu_stuff.c": ["HU_Drawer", "HU_Erase"],
        "r_draw.c": ["R_VideoErase", "R_DrawViewBorder"],
        "d_main.c": ["D_Display"],
    }.items():
        source = (VENDOR / name).read_text()
        definitions.extend(function(source, value) for value in functions)
    (temp / "render_functions.h").write_text("\n".join(definitions))
    command = [os.environ.get("CC", "cc"), "-std=c11", "-g", "-O1", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", "-DRANGECHECK", "-I" + str(temp),
               "-I" + str(ROOT / "apps/doom_audio_probe/components/doom_engine_audio"),
               "-I" + str(ROOT / "components/doom_multiplayer/include"),
               "-I" + str(ROOT / "components/p4_multiplayer/include"),
               "-isystem", str(ROOT / "third_party/doomgeneric/doomgeneric"),
               str(ROOT / "components/doom_multiplayer/tests/test_doom_arena_framebuffer.c"),
               str(ROOT / "components/doom_multiplayer/src/doom_arena.c"),
               "-o", str(temp / "framebuffer")]
    subprocess.run(command, check=True)
    subprocess.run([str(temp / "framebuffer")], check=True)
