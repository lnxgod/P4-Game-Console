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
GRID_COLUMNS = 4
GRID_ROWS = 4
RUNTIME_SHEET_WIDTH = 256
RUNTIME_SHEET_HEIGHT = 256
MAXIMUM_SOURCE_SHEET_DIMENSION = 8192
FRAME_WIDTH = 64
FRAME_HEIGHT = 64
FRAMES_PER_SHEET = 16
PALETTE_ENTRIES = 16
PACKED_FRAME_BYTES = FRAME_WIDTH * FRAME_HEIGHT // 2
SIGNAL_GENOME_SHEET_INDEX = 24
SIGNAL_CITY_PROPS_SHEET_INDEX = 25
SIGNAL_LINEAGE_BADGES_SHEET_INDEX = 27
LARGE_BACKGROUND_COMPONENT_PIXELS = 64
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
ENCLOSED_BACKGROUND_SHEET_INDEXES = frozenset({
    SIGNAL_GENOME_SHEET_INDEX,
    SIGNAL_CITY_PROPS_SHEET_INDEX,
    SIGNAL_LINEAGE_BADGES_SHEET_INDEX,
})
EXPECTED_SHEETS = (
    "byte_buddy_signal_counter_fx_imagegen_v11.png",
    "byte_buddy_signal_outcome_fx_imagegen_v12.png",
    "byte_buddy_signal_passive_fx_imagegen_v13.png",
    "byte_buddy_signal_scan_fx_imagegen_v14.png",
    "byte_buddy_evolution_fx_imagegen_v15.png",
    "byte_buddy_dragon_rare_variants_pixellab_v1.png",
    "byte_buddy_need_fx_imagegen_v16.png",
    "byte_buddy_activity_fx_imagegen_v17.png",
    "byte_buddy_dragon_baby_reactions_imagegen_v1.png",
    "byte_buddy_dragon_flight_cycles_imagegen_v1.png",
    "byte_buddy_dragon_elemental_breath_imagegen_v1.png",
    "byte_buddy_dragon_egg_morph_ambient_imagegen_v2.png",
    "byte_buddy_dragon_egg_element_ambient_imagegen_v2.png",
    "byte_buddy_dragon_baby_idle_cycles_imagegen_v2.png",
    "byte_buddy_dragon_baby_care_cycles_imagegen_v2.png",
    "byte_buddy_dragon_winged_idle_cycles_imagegen_v2.png",
    "byte_buddy_dragon_winged_care_cycles_imagegen_v2.png",
    "byte_buddy_dragon_flight_aerobatics_imagegen_v2.png",
    "byte_buddy_dragon_elemental_mastery_imagegen_v2.png",
    "byte_buddy_dragon_elemental_impacts_imagegen_v2.png",
    "byte_buddy_dragon_hatch_transitions_imagegen_v3.png",
    "byte_buddy_dragon_signal_genetics_imagegen_v3.png",
    "byte_buddy_star_catcher_rewards_imagegen_v4.png",
    "byte_buddy_items_components_imagegen_v5.png",
    "byte_buddy_signal_genome_layers_imagegen_v6.png",
    "byte_buddy_signal_city_props_imagegen_v7.png",
    "byte_buddy_reaction_fx_imagegen_v8.png",
    "byte_buddy_signal_lineage_badges_imagegen_v9.png",
    "byte_buddy_signal_attack_cycles_imagegen_v10.png",
)


def source_png_dimensions(path: Path) -> tuple[int, int]:
    try:
        with path.open("rb") as source:
            header = source.read(24)
    except OSError as error:
        raise RuntimeError(f"{path}: cannot read source PNG: {error}") from error
    if (len(header) != 24 or header[:8] != PNG_SIGNATURE or
            header[8:12] != struct.pack(">I", 13) or
            header[12:16] != b"IHDR"):
        raise RuntimeError(f"{path}: source is not a bounded PNG with IHDR")
    return struct.unpack(">II", header[16:24])


def validate_sources(sources: list[Path]) -> None:
    if (RUNTIME_SHEET_WIDTH != GRID_COLUMNS * FRAME_WIDTH or
            RUNTIME_SHEET_HEIGHT != GRID_ROWS * FRAME_HEIGHT):
        raise AssertionError("runtime sheet geometry does not form a 4x4 grid")
    seen_paths: set[Path] = set()
    required_sheets = len(EXPECTED_SHEETS)
    if len(sources) != required_sheets:
        raise RuntimeError(
            f"full Byte Buddy art bank requires exactly "
            f"{required_sheets} sheets, got {len(sources)}"
        )
    for index, source in enumerate(sources):
        resolved = source.resolve()
        if resolved in seen_paths:
            raise RuntimeError(f"{source}: duplicate atlas source")
        seen_paths.add(resolved)
        width, height = source_png_dimensions(source)
        if width != height:
            raise RuntimeError(
                f"{source}: source grid must be square, got {width}x{height}"
            )
        if width < RUNTIME_SHEET_WIDTH:
            raise RuntimeError(
                f"{source}: source grid must be at least "
                f"{RUNTIME_SHEET_WIDTH}x{RUNTIME_SHEET_HEIGHT}, got "
                f"{width}x{height}"
            )
        if width > MAXIMUM_SOURCE_SHEET_DIMENSION:
            raise RuntimeError(
                f"{source}: source grid exceeds the "
                f"{MAXIMUM_SOURCE_SHEET_DIMENSION}px bound"
            )
        # Existing ImageGen masters are 1254px squares, so validate the
        # square grid but let nearest-neighbor scaling map it to exact 64px
        # runtime cells instead of requiring source dimensions divisible by 4.
        expected_name = EXPECTED_SHEETS[index]
        if source.name != expected_name:
            raise RuntimeError(
                f"{source}: sheet index {index} must be {expected_name}"
            )


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


def frame_rgba(raw: bytes, frame: int) -> bytearray:
    left = frame % 4 * FRAME_WIDTH
    top = frame // 4 * FRAME_HEIGHT
    output = bytearray(FRAME_WIDTH * FRAME_HEIGHT * 4)
    for y in range(FRAME_HEIGHT):
        source = ((top + y) * RUNTIME_SHEET_WIDTH + left) * 4
        destination = y * FRAME_WIDTH * 4
        output[destination : destination + FRAME_WIDTH * 4] = raw[
            source : source + FRAME_WIDTH * 4
        ]
    return output


def neutral_background(pixel: bytes) -> bool:
    red, green, blue, _ = pixel
    return min(red, green, blue) >= 180 and max(red, green, blue) - min(
        red, green, blue
    ) <= 22


def clear_baked_checkerboard(
    frame: bytearray, clear_enclosed_background: bool = False
) -> None:
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
    if not clear_enclosed_background:
        return
    for origin_y in range(FRAME_HEIGHT):
        for origin_x in range(FRAME_WIDTH):
            origin = origin_y * FRAME_WIDTH + origin_x
            origin_offset = origin * 4
            if (visited[origin] or not neutral_background(
                    frame[origin_offset : origin_offset + 4])):
                continue
            component: list[tuple[int, int]] = []
            visited[origin] = 1
            queue.append((origin_x, origin_y))
            while queue:
                x, y = queue.popleft()
                component.append((x, y))
                for next_x, next_y in ((x - 1, y), (x + 1, y),
                                       (x, y - 1), (x, y + 1)):
                    if not (0 <= next_x < FRAME_WIDTH and
                            0 <= next_y < FRAME_HEIGHT):
                        continue
                    index = next_y * FRAME_WIDTH + next_x
                    offset = index * 4
                    if (not visited[index] and neutral_background(
                            frame[offset : offset + 4])):
                        visited[index] = 1
                        queue.append((next_x, next_y))
            if len(component) >= LARGE_BACKGROUND_COMPONENT_PIXELS:
                for x, y in component:
                    frame[(y * FRAME_WIDTH + x) * 4 + 3] = 0


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


def encode_frame(
    frame: bytearray, clear_enclosed_background: bool = False
) -> tuple[bytes, bytes]:
    clear_baked_checkerboard(frame, clear_enclosed_background)
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
                frame_rgba(raw, frame),
                clear_enclosed_background=(
                    sheet_index in ENCLOSED_BACKGROUND_SHEET_INDEXES
                ),
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
    validate_sources(args.sources)
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
