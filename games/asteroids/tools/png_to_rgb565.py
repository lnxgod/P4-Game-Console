#!/usr/bin/env python3
"""Convert a source PNG into a bounded RGB565 C include for Asteroids."""

import struct
import subprocess
import sys
from pathlib import Path

WIDTH = 80
HEIGHT = 43


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.png OUTPUT.inc", file=sys.stderr)
        return 2
    try:
        from PIL import Image
    except ModuleNotFoundError:
        raw = subprocess.run(
            ["ffmpeg", "-loglevel", "error", "-i", sys.argv[1],
             "-vf", f"scale={WIDTH}:{HEIGHT}", "-pix_fmt", "rgb565le",
             "-f", "rawvideo", "pipe:1"],
            check=True, stdout=subprocess.PIPE,
        ).stdout
        values = [value[0] for value in struct.iter_unpack("<H", raw)]
    else:
        source = Image.open(sys.argv[1]).convert("RGB").resize(
            (WIDTH, HEIGHT), Image.Resampling.LANCZOS
        )
        values = [rgb565(*pixel) for pixel in source.getdata()]
    lines = [
        "/* Generated from assets/deep_space_v1.png; do not edit by hand. */",
        "static const uint16_t s_deep_space_pixels[80U * 43U] = {",
    ]
    for offset in range(0, len(values), 12):
        row = values[offset : offset + 12]
        lines.append("    " + ", ".join(f"UINT16_C(0x{value:04x})" for value in row) + ",")
    lines.append("};")
    Path(sys.argv[2]).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
