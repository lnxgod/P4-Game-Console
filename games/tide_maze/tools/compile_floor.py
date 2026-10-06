#!/usr/bin/env python3
"""Compile the original analytic tiled substrate used by the 3D water shader."""
from pathlib import Path

def rgb(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

colors = [rgb(211, 222, 212) if x % 16 == 0 or y % 16 == 0 else
          rgb(115, 168, 177) if (x // 16 + y // 16) % 2 else rgb(153, 190, 188)
          for y in range(32) for x in range(32)]
output = Path(__file__).resolve().parents[1] / "src/generated/floor.inc"
output.write_text("// SPDX-License-Identifier: MIT\n// Analytic 16-unit tiles, 32x32 repeating bed.\n" +
                  "".join(",".join(hex(c) for c in colors[y*32:(y+1)*32]) + ",\n" for y in range(32)))
