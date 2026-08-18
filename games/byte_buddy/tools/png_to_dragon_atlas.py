#!/usr/bin/env python3
"""Build compact deterministic Byte Buddy 4bpp frame banks."""

from __future__ import annotations

import argparse
import collections
import struct
import subprocess
from pathlib import Path


MAGIC = b"BBDART2\0"
VERSION = 2
HEADER_BYTES = 64
RUNTIME_SHEET_WIDTH = 256
RUNTIME_SHEET_HEIGHT = 256
FRAME_WIDTH = 64
FRAME_HEIGHT = 64
FRAMES_PER_SHEET = 16
PALETTE_ENTRIES = 16
PACKED_FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT // 2
FLYING_SHEET_INDEX = 3
FLYING_CLEANUP_ROW = 54


def decode_sheet(path: Path) -> bytes:
    raw = subprocess.run(
        [
            "ffmpeg", "-loglevel", "error", "-i", str(path),
            "-vf",
            f"scale={RUNTIME_SHEET_WIDTH}:{RUNTIME_SHEET_HEIGHT}:flags=neighbor",
            "-pix_fmt", "rgba", "-f", "rawvideo", "pipe:1",
        ],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout
    expected = RUNTIME_SHEET_WIDTH * RUNTIME_SHEET_HEIGHT * 4
    if len(raw) != expected:
        raise RuntimeError(
            f"{path}: ffmpeg emitted {len(raw)} bytes; expected {expected}"
        )
    return raw


def frame_rgba(raw: bytes, frame: int, sheet_index: int) -> bytearray:
    left = frame % 4 * FRAME_WIDTH
    top = frame // 4 * FRAME_HEIGHT
    output = bytearray(FRAME_WIDTH * FRAME_HEIGHT * 4)
    for y in range(FRAME_HEIGHT):
        source = ((top + y) * RUNTIME_SHEET_WIDTH + left) * 4
        destination = y * FRAME_WIDTH * 4
        output[destination : destination + FRAME_WIDTH * 4] = raw[
            source : source + FRAME_WIDTH * 4
        ]
    if sheet_index == FLYING_SHEET_INDEX:
        for y in range(FLYING_CLEANUP_ROW, FRAME_HEIGHT):
            for x in range(FRAME_WIDTH):
                output[(y * FRAME_WIDTH + x) * 4 + 3] = 0
    return output


def neutral_background(pixel: bytes) -> bool:
    red, green, blue, _ = pixel
    return min(red, green, blue) >= 180 and max(red, green, blue) - min(
        red, green, blue
    ) <= 22


def clear_baked_checkerboard(frame: bytearray) -> None:
    if any(frame[offset + 3] < 128 for offset in range(0, len(frame), 4)):
        return
    queue: collections.deque[tuple[int, int]] = collections.deque()
    visited = bytearray(FRAME_WIDTH * FRAME_HEIGHT)

    def seed(x: int, y: int) -> None:
        index = y * FRAME_WIDTH + x
        offset = index * 4
        if not visited[index] and neutral_background(frame[offset : offset + 4]):
            visited[index] = 1
            queue.append((x, y))

    for x in range(FRAME_WIDTH):
        seed(x, 0)
        seed(x, FRAME_HEIGHT - 1)
    for y in range(FRAME_HEIGHT):
        seed(0, y)
        seed(FRAME_WIDTH - 1, y)
    while queue:
        x, y = queue.popleft()
        frame[(y * FRAME_WIDTH + x) * 4 + 3] = 0
        for next_x, next_y in ((x - 1, y), (x + 1, y),
                               (x, y - 1), (x, y + 1)):
            if not (0 <= next_x < FRAME_WIDTH and 0 <= next_y < FRAME_HEIGHT):
                continue
            index = next_y * FRAME_WIDTH + next_x
            offset = index * 4
            if (not visited[index] and
                    neutral_background(frame[offset : offset + 4])):
                visited[index] = 1
                queue.append((next_x, next_y))


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def components(value: int) -> tuple[int, int, int]:
    return ((value >> 11) & 31, (value >> 5) & 63, value & 31)


def nearest(value: int, palette: list[int]) -> int:
    red, green, blue = components(value)
    best_index = 1
    best_distance = 1 << 30
    for index, candidate in enumerate(palette[1:], 1):
        other_red, other_green, other_blue = components(candidate)
        distance = (
            (red - other_red) * (red - other_red) * 2
            + (green - other_green) * (green - other_green)
            + (blue - other_blue) * (blue - other_blue) * 2
        )
        if distance < best_distance:
            best_distance = distance
            best_index = index
    return best_index


def encode_frame(frame: bytearray) -> tuple[bytes, bytes]:
    clear_baked_checkerboard(frame)
    colors: list[int | None] = []
    histogram: collections.Counter[int] = collections.Counter()
    for offset in range(0, len(frame), 4):
        red, green, blue, alpha = frame[offset : offset + 4]
        if alpha < 128:
            colors.append(None)
        else:
            color = rgb565(red, green, blue)
            colors.append(color)
            histogram[color] += 1
    selected = sorted(
        histogram,
        key=lambda value: (-histogram[value], value),
    )[: PALETTE_ENTRIES - 1]
    if not selected:
        selected = [0]
    palette = [0, *selected]
    while len(palette) < PALETTE_ENTRIES:
        palette.append(palette[-1])
    lookup = {color: index + 1 for index, color in enumerate(selected)}
    indices = [
        0 if color is None else lookup.get(color, nearest(color, palette))
        for color in colors
    ]
    packed = bytearray(PACKED_FRAME_BYTES)
    for index in range(0, len(indices), 2):
        packed[index // 2] = (indices[index] << 4) | indices[index + 1]
    return struct.pack("<16H", *palette), bytes(packed)


def build_bank(sources: list[Path]) -> bytes:
    palettes = bytearray()
    pixels = bytearray()
    for sheet_index, source in enumerate(sources):
        raw = decode_sheet(source)
        for frame in range(FRAMES_PER_SHEET):
            palette, packed = encode_frame(
                frame_rgba(raw, frame, sheet_index)
            )
            palettes.extend(palette)
            pixels.extend(packed)
    frame_count = len(sources) * FRAMES_PER_SHEET
    palette_offset = HEADER_BYTES
    pixel_offset = palette_offset + len(palettes)
    total_bytes = pixel_offset + len(pixels)
    header = struct.pack(
        "<8s11I12x",
        MAGIC,
        VERSION,
        len(sources),
        FRAME_WIDTH,
        FRAME_HEIGHT,
        FRAMES_PER_SHEET,
        PALETTE_ENTRIES,
        frame_count,
        palette_offset,
        pixel_offset,
        total_bytes,
        0,
    )
    if len(header) != HEADER_BYTES:
        raise AssertionError(f"unexpected header bytes: {len(header)}")
    return header + palettes + pixels


def write_include(path: Path, data: bytes) -> None:
    lines = [
        "/* Generated Byte Buddy BBDART2 built-in frame bank; do not edit. */",
        f"static const uint8_t s_byte_buddy_builtin_art[{len(data)}U] = {{",
    ]
    for offset in range(0, len(data), 12):
        lines.append(
            "    " + ", ".join(
                f"UINT8_C(0x{value:02x})" for value in data[offset : offset + 12]
            ) + ","
        )
    lines.append("};")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sources", nargs="+", type=Path)
    parser.add_argument("--builtin-count", type=int, default=6)
    parser.add_argument("--include-output", required=True, type=Path)
    parser.add_argument("--resource-output", required=True, type=Path)
    args = parser.parse_args()
    if not (1 <= args.builtin_count <= len(args.sources)):
        parser.error("builtin count must select at least one source sheet")
    built_in = build_bank(args.sources[: args.builtin_count])
    resource = build_bank(args.sources)
    write_include(args.include_output, built_in)
    args.resource_output.parent.mkdir(parents=True, exist_ok=True)
    args.resource_output.write_bytes(resource)
    print(
        f"built_in_bytes={len(built_in)} resource_bytes={len(resource)} "
        f"sheets={len(args.sources)} frames={len(args.sources) * 16}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
