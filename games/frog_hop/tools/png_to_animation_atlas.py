#!/usr/bin/env python3
"""Convert Frog Hop's transparent 4x4 ImageGen animation sheet to RGB565."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ATLAS_WIDTH = 160
ATLAS_HEIGHT = 100


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.png OUTPUT.inc", file=sys.stderr)
        return 2
    raw = subprocess.run(
        ["ffmpeg", "-loglevel", "error", "-i", sys.argv[1],
         "-vf", f"scale={ATLAS_WIDTH}:{ATLAS_HEIGHT}:flags=neighbor",
         "-pix_fmt", "rgba", "-f", "rawvideo", "pipe:1"],
        check=True, stdout=subprocess.PIPE,
    ).stdout
    expected_bytes = ATLAS_WIDTH * ATLAS_HEIGHT * 4
    if len(raw) != expected_bytes:
        raise RuntimeError(
            f"ffmpeg emitted {len(raw)} bytes; expected {expected_bytes}")
    values: list[int] = []
    for offset in range(0, len(raw), 4):
        red, green, blue, alpha = raw[offset : offset + 4]
        values.append(0 if alpha < 128 else rgb565(red, green, blue))
    lines = [
        "/* Generated from assets/frog_hop_animation_atlas_v1.png; do not edit. */",
        "static const uint16_t s_frog_hop_animation_pixels[160U * 100U] = {",
    ]
    for offset in range(0, len(values), 12):
        row = values[offset : offset + 12]
        lines.append("    " + ", ".join(
            f"UINT16_C(0x{value:04x})" for value in row) + ",")
    lines.append("};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
