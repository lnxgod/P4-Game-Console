#!/usr/bin/env python3
"""Convert the reviewed ImageGen atlas and official logo to RGB565 sprites."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

from PIL import Image, ImageDraw


SOURCE_SIZE = (1024, 1536)
SOURCE_SHA256 = "237823be2c22c401a56fe49254ced442ad32788c95b605b63247c2a82ade7ab5"
LOGO_SIZE = (112, 112)
LOGO_SHA256 = "48ee7b2a15a744547884ec6ea7f462277ab60e5dde4d0805bf39db9c0b2bd892"
CHROMA_KEY = 0x0410
SMALL_SIZE = (28, 42)
LARGE_SIZE = (42, 62)
SMALL_LOGO_SIZE = (15, 15)
LARGE_LOGO_SIZE = (24, 24)

# Hand-reviewed crop boxes in the generated 1024x1536 source. Each contains
# exactly one card and intentionally drops the opaque generation backdrop.
CROPS = (
    (8, 36, 333, 699),
    (345, 31, 680, 701),
    (684, 36, 1018, 701),
    (9, 742, 335, 1462),
    (347, 746, 681, 1465),
    (685, 735, 1019, 1462),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def rgb888(value: int) -> tuple[int, int, int]:
    red = ((value >> 11) & 0x1F) * 255 // 31
    green = ((value >> 5) & 0x3F) * 255 // 63
    blue = (value & 0x1F) * 255 // 31
    return red, green, blue


def rounded_frame(source: Image.Image, crop: tuple[int, int, int, int],
                  size: tuple[int, int]) -> tuple[list[int], Image.Image]:
    frame = source.crop(crop).resize(size, Image.Resampling.NEAREST).convert("RGBA")
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, size[0] - 1, size[1] - 1), radius=max(2, size[0] // 9), fill=255)
    frame.putalpha(mask)
    values: list[int] = []
    pixels = frame.load()
    for y in range(size[1]):
        for x in range(size[0]):
            red, green, blue, alpha = pixels[x, y]
            if alpha < 128:
                values.append(CHROMA_KEY)
                continue
            value = rgb565(red, green, blue)
            values.append(0 if value == CHROMA_KEY else value)
    return values, frame


def load_logo(path: Path) -> Image.Image:
    raw = path.read_bytes()
    expected = LOGO_SIZE[0] * LOGO_SIZE[1] * 2
    if len(raw) != expected:
        raise ValueError(f"official logo must be {expected} bytes")
    image = Image.new("RGBA", LOGO_SIZE, (0, 0, 0, 0))
    pixels = image.load()
    for index, (value,) in enumerate(struct.iter_unpack("<H", raw)):
        red, green, blue = rgb888(value)
        alpha = 0 if red + green + blue < 42 else 255
        pixels[index % LOGO_SIZE[0], index // LOGO_SIZE[0]] = (
            red, green, blue, alpha)
    return image


def logo_values(logo: Image.Image, size: tuple[int, int]) -> tuple[list[int], Image.Image]:
    sprite = logo.resize(size, Image.Resampling.NEAREST)
    values: list[int] = []
    pixels = sprite.load()
    for y in range(size[1]):
        for x in range(size[0]):
            red, green, blue, alpha = pixels[x, y]
            if alpha < 128:
                values.append(CHROMA_KEY)
                continue
            value = rgb565(red, green, blue)
            values.append(0 if value == CHROMA_KEY else value)
    return values, sprite


def emit_array(lines: list[str], name: str, values: list[list[int]],
               width: int, height: int) -> None:
    lines.extend((
        f"static const uint16_t {name}[{len(values)}]",
        f"    [{width}U * {height}U] = {{",
    ))
    for sprite in values:
        lines.append("    {")
        for offset in range(0, len(sprite), 12):
            chunk = sprite[offset:offset + 12]
            lines.append(
                "        " + ", ".join(
                    f"UINT16_C(0x{value:04x})" for value in chunk) + ",")
        lines.append("    },")
    lines.append("};")


def emit_logo(lines: list[str], name: str, values: list[int],
              width: int, height: int) -> None:
    lines.extend((
        f"static const uint16_t {name}[{width}U * {height}U] = {{",
    ))
    for offset in range(0, len(values), 12):
        chunk = values[offset:offset + 12]
        lines.append(
            "    " + ", ".join(
                f"UINT16_C(0x{value:04x})" for value in chunk) + ",")
    lines.append("};")


def convert(source_path: Path, logo_path: Path, output_path: Path,
            preview_path: Path) -> None:
    if sha256(source_path) != SOURCE_SHA256:
        raise ValueError("ImageGen source SHA-256 differs from reviewed atlas")
    if sha256(logo_path) != LOGO_SHA256:
        raise ValueError("official Game Changers AI logo SHA-256 differs")
    source = Image.open(source_path).convert("RGBA")
    if source.size != SOURCE_SIZE:
        raise ValueError(f"ImageGen source must be {SOURCE_SIZE}")
    small_values: list[list[int]] = []
    large_values: list[list[int]] = []
    large_previews: list[Image.Image] = []
    for crop in CROPS:
        small, _ = rounded_frame(source, crop, SMALL_SIZE)
        large, preview = rounded_frame(source, crop, LARGE_SIZE)
        small_values.append(small)
        large_values.append(large)
        large_previews.append(preview)

    logo = load_logo(logo_path)
    small_logo, _ = logo_values(logo, SMALL_LOGO_SIZE)
    large_logo, large_logo_preview = logo_values(logo, LARGE_LOGO_SIZE)

    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Generated by tools/png_to_card_frames.py; do not edit.",
        "",
        "enum {",
        f"    COLOR_CLASH_FRAME_SMALL_WIDTH = {SMALL_SIZE[0]},",
        f"    COLOR_CLASH_FRAME_SMALL_HEIGHT = {SMALL_SIZE[1]},",
        f"    COLOR_CLASH_FRAME_LARGE_WIDTH = {LARGE_SIZE[0]},",
        f"    COLOR_CLASH_FRAME_LARGE_HEIGHT = {LARGE_SIZE[1]},",
        f"    COLOR_CLASH_LOGO_SMALL_WIDTH = {SMALL_LOGO_SIZE[0]},",
        f"    COLOR_CLASH_LOGO_SMALL_HEIGHT = {SMALL_LOGO_SIZE[1]},",
        f"    COLOR_CLASH_LOGO_LARGE_WIDTH = {LARGE_LOGO_SIZE[0]},",
        f"    COLOR_CLASH_LOGO_LARGE_HEIGHT = {LARGE_LOGO_SIZE[1]},",
        "};",
        f"#define COLOR_CLASH_SPRITE_CHROMA UINT16_C(0x{CHROMA_KEY:04x})",
        "",
    ]
    emit_array(lines, "s_color_clash_frames_small", small_values,
               *SMALL_SIZE)
    lines.append("")
    emit_array(lines, "s_color_clash_frames_large", large_values,
               *LARGE_SIZE)
    lines.append("")
    emit_logo(lines, "s_color_clash_logo_small", small_logo,
              *SMALL_LOGO_SIZE)
    lines.append("")
    emit_logo(lines, "s_color_clash_logo_large", large_logo,
              *LARGE_LOGO_SIZE)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    gap = 8
    preview = Image.new(
        "RGBA", (3 * LARGE_SIZE[0] + 4 * gap,
                 2 * LARGE_SIZE[1] + 3 * gap), (7, 13, 24, 255))
    for index, frame in enumerate(large_previews):
        x = gap + (index % 3) * (LARGE_SIZE[0] + gap)
        y = gap + (index // 3) * (LARGE_SIZE[1] + gap)
        preview.alpha_composite(frame, (x, y))
        if index == 5:
            logo_x = x + (LARGE_SIZE[0] - LARGE_LOGO_SIZE[0]) // 2
            logo_y = y + 19
            preview.alpha_composite(large_logo_preview, (logo_x, logo_y))
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    preview.save(preview_path, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("logo", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("preview", type=Path)
    args = parser.parse_args()
    convert(args.source, args.logo, args.output, args.preview)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
