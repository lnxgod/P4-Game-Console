#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Cross-check committed Doom D0 provenance and host-test metadata."""

from __future__ import annotations

import json
import pathlib
import re
import sys


def load_object(path: pathlib.Path) -> dict[str, object]:
    with path.open(encoding="utf-8") as source_file:
        value = json.load(source_file)
    if not isinstance(value, dict):
        raise SystemExit(f"expected a JSON object: {path}")
    return value


def require_sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise SystemExit(f"invalid SHA-256 for {label}")
    return value


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-metadata.py PROJECT_ROOT")

    root = pathlib.Path(sys.argv[1]).resolve()
    source_lock = load_object(root / "third_party" / "source-lock.json")
    game_manifest = load_object(root / "third_party" / "game-data.json")
    evidence = load_object(root / "test-runs" / "2026-08-12-doom-d0-host.json")

    sources = source_lock.get("sources")
    if not isinstance(sources, dict) or not isinstance(sources.get("doomgeneric"), dict):
        raise SystemExit("source-lock.json has no doomgeneric source object")
    doomgeneric = sources["doomgeneric"]

    game_data = game_manifest.get("game_data")
    if not isinstance(game_data, list):
        raise SystemExit("game-data.json game_data must be an array")
    shareware_entries = [
        item for item in game_data
        if isinstance(item, dict) and item.get("id") == "doom-shareware-1.9"
    ]
    if len(shareware_entries) != 1:
        raise SystemExit("game-data.json must contain exactly one Doom shareware v1.9 entry")
    shareware = shareware_entries[0]

    policy = game_manifest.get("policy")
    if not isinstance(policy, dict) or any(
        policy.get(key) is not False
        for key in ("bytes_in_git", "bytes_in_firmware", "redistribution_by_project")
    ):
        raise SystemExit("game-data policy must forbid committed, embedded, and redistributed bytes")

    engine_evidence = evidence.get("engine")
    wad_evidence = evidence.get("game_data")
    if not isinstance(engine_evidence, dict) or not isinstance(wad_evidence, dict):
        raise SystemExit("D0 evidence engine/game_data must be objects")

    if engine_evidence.get("commit") != doomgeneric.get("commit"):
        raise SystemExit("D0 evidence engine commit differs from source lock")
    if engine_evidence.get("tree") != doomgeneric.get("tree"):
        raise SystemExit("D0 evidence engine tree differs from source lock")
    if wad_evidence.get("id") != shareware.get("id"):
        raise SystemExit("D0 evidence game-data ID differs from manifest")
    if wad_evidence.get("size_bytes") != shareware.get("size_bytes"):
        raise SystemExit("D0 evidence game-data size differs from manifest")
    if require_sha256(wad_evidence.get("sha256"), "D0 evidence") != require_sha256(
        shareware.get("sha256"), "shareware manifest"
    ):
        raise SystemExit("D0 evidence game-data SHA-256 differs from manifest")
    if evidence.get("classification") != "host-tested" or evidence.get("result") != "pass":
        raise SystemExit("D0 evidence must be a passing host-tested result")

    print("P4_DOOM_D0 METADATA PASS")


if __name__ == "__main__":
    main()
