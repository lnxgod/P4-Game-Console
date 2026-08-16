#!/usr/bin/env python3
"""Create the ESP32-P4 Quake renderer with frame scratch off-stack."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


EXPECTED_R_MAIN_SHA256 = (
    "c5581acd4b3f721830dc219ddb67e3a67dddff60b21d234f3d0444592aa6a7d3"
)


def replace_once(source: str, old: str, new: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"expected one upstream match, found {count}: {old!r}"
        )
    return source.replace(old, new, 1)


def prepare(source_path: Path) -> bytes:
    source_bytes = source_path.read_bytes()
    actual_hash = hashlib.sha256(source_bytes).hexdigest()
    if actual_hash != EXPECTED_R_MAIN_SHA256:
        raise RuntimeError(
            "refusing to patch an unpinned quakegeneric r_main.c: "
            f"expected {EXPECTED_R_MAIN_SHA256}, got {actual_hash}"
        )
    source = source_bytes.decode("utf-8")
    source = replace_once(
        source,
        "qboolean\tr_fov_greater_than_90;\n",
        "qboolean\tr_fov_greater_than_90;\n\n"
        "/*\n"
        " * ESP32-P4: WinQuake puts about 128 KiB of renderer edge/surface\n"
        " * scratch in R_EdgeDrawing's automatic frame. This component's\n"
        " * BSS is linker-mapped to PSRAM, so keep the single-engine scratch\n"
        " * here instead of overflowing the bounded engine task stack.\n"
        " */\n"
        "static edge_t p4_quake_edge_scratch[NUMSTACKEDGES +\n"
        "\t\t((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];\n"
        "static surf_t p4_quake_surface_scratch[NUMSTACKSURFACES +\n"
        "\t\t((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];\n",
    )
    source = replace_once(
        source,
        "\tedge_t\tledges[NUMSTACKEDGES +\n"
        "\t\t\t\t((CACHE_SIZE - 1) / sizeof(edge_t)) + 1];\n"
        "\tsurf_t\tlsurfs[NUMSTACKSURFACES +\n"
        "\t\t\t\t((CACHE_SIZE - 1) / sizeof(surf_t)) + 1];\n",
        "\tedge_t\t*ledges = p4_quake_edge_scratch;\n"
        "\tsurf_t\t*lsurfs = p4_quake_surface_scratch;\n",
    )
    return source.encode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = prepare(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_bytes() != output:
        args.output.write_bytes(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
