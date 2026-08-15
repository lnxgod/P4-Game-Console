#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/console-os-game-manager-migrate.py"
SPEC = importlib.util.spec_from_file_location("console_game_manager_migrate", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> None:
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip()
    assert MODULE._validate_authorization() == (
        MODULE.EXPECTED_AUTHORIZATION_SHA256
    )
    targets = MODULE._load_targets(commit)
    assert [target.name for target in targets] == [
        "bootloader", "ota_0", "partition_table", "ota_data"
    ]
    assert [(target.offset, len(target.span)) for target in targets] == [
        (MODULE.BOOTLOADER_OFFSET, MODULE.BOOTLOADER_SPAN_BYTES),
        (MODULE.OTA0_OFFSET, MODULE.OTA0_SPAN_BYTES),
        (MODULE.PARTITION_TABLE_OFFSET, MODULE.PARTITION_TABLE_SPAN_BYTES),
        (MODULE.OTA_DATA_OFFSET, MODULE.OTA_DATA_SPAN_BYTES),
    ]
    assert all(
        target.offset + len(target.span) <= MODULE.GAME_DATA_OFFSET
        for target in targets
    )
    assert all("game_data" not in str(target.path) for target in targets)
    records = MODULE._target_records(targets)
    assert len({record["span_sha256"] for record in records}) == len(records)
    print("Console OS Game Manager migration artifact tests passed")


if __name__ == "__main__":
    main()
