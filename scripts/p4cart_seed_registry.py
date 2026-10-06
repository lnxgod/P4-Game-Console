#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compatibility guard only: the old cartridge generator has been removed."""
from __future__ import annotations
import argparse
import json
from pathlib import Path

DEFAULT_METADATA = Path(__file__).resolve().parents[1] / "apps/console_os/app-metadata.json"

class SeedRegistryError(RuntimeError):
    pass

def check_no_legacy_seeds(metadata_path: Path) -> None:
    try:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SeedRegistryError(f"cannot read metadata: {error}") from error
    if not isinstance(metadata, dict):
        raise SeedRegistryError("metadata must be an object")
    legacy = metadata.get("legacy_p4cart", {})
    if not isinstance(legacy, dict) or legacy.get("seed_carts", []) != [] or legacy.get("seed_cart") is not None:
        raise SeedRegistryError("Lua .P4CART seeding is retired; use native .P4G games")

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, default=DEFAULT_METADATA)
    parser.add_argument("--check", action="store_true")
    # Accept old spellings only to give callers a clear, non-mutating error.
    parser.add_argument("--templates-root", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--output-cmake", type=Path, help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if not arguments.check or arguments.templates_root is not None or arguments.output_cmake is not None:
        raise SeedRegistryError("Lua creator and seed generator removed; only --check is supported")
    check_no_legacy_seeds(arguments.metadata)
    print(json.dumps({"result": "native-only-seed-guard-pass", "seed_carts": []}, sort_keys=True))
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SeedRegistryError as error:
        raise SystemExit(f"Native-only seed guard rejected: {error}") from error
