#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/console-os-file-manager-install.py"
SPEC = importlib.util.spec_from_file_location("console_file_manager_install", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def main() -> None:
    updater = MODULE.UPDATER
    assert updater.EXPECTED_CURRENT_APP_SPAN_SHA256 == (
        MODULE.EXPECTED_PREDECESSOR_APP_SPAN_SHA256
    )
    assert updater.EXPECTED_NEW_APP_BYTES == (
        MODULE.EXPECTED_FILE_MANAGER_APP_BYTES
    )
    assert updater.EXPECTED_NEW_APP_SHA256 == (
        MODULE.EXPECTED_FILE_MANAGER_APP_SHA256
    )
    assert updater.EXPECTED_NEW_APP_SPAN_SHA256 == (
        MODULE.EXPECTED_FILE_MANAGER_APP_SPAN_SHA256
    )
    assert updater.LEDGER_NAME == "file-manager-update-ledger.json"
    assert updater.CAPTURE_RAW_NAME == "file-manager-startup.raw"
    assert updater.CAPTURE_SUMMARY_NAME == "file-manager-startup.json"
    assert updater.CAPTURE.START == MODULE.FILE_MANAGER_START
    assert updater.CAPTURE.FIXED[0] == ("start", MODULE.FILE_MANAGER_START)
    assert b"apps=8 " in MODULE.FILE_MANAGER_START
    assert 0 < MODULE.EXPECTED_FILE_MANAGER_APP_BYTES < updater.APP_SPAN_BYTES
    for digest in (
        MODULE.EXPECTED_PREDECESSOR_APP_SPAN_SHA256,
        MODULE.EXPECTED_FILE_MANAGER_APP_SHA256,
        MODULE.EXPECTED_FILE_MANAGER_APP_SPAN_SHA256,
    ):
        assert len(digest) == 64
        int(digest, 16)
    print("Console OS File Manager exact app-only contract tests passed")


if __name__ == "__main__":
    main()
