#!/usr/bin/env python3

"""Exact-unit app-only File Manager update for the accepted Console OS.

The Program Manager USB partition table and live P4 GAMES volume are already
installed. This route reuses the frozen, reviewed app-only updater and changes
only its exact predecessor, target, startup, and output bindings. It writes the
7 MiB application span, verifies it completely, proves partition-table and
game-data preservation, and creates no new flash backup. In-process rollback
uses the live predecessor held in memory; crash recovery retains the existing
owner-accepted fallback from the previously preserved full preimage.
"""

from __future__ import annotations

import hashlib
import os
import pathlib
import stat
import sys
import types
from typing import Any


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
ROOT = SCRIPT_PATH.parent.parent.resolve(strict=True)
FROZEN_UPDATER_PATH = ROOT / "scripts/console-os-program-manager-usb-install.py"
EXPECTED_FROZEN_UPDATER_SHA256 = (
    "603a97b5297ae1afe9cb43717ead019408cad465e54fcc80bc37797c668ce39a"
)
MAX_MODULE_BYTES = 2 * 1024 * 1024

EXPECTED_PREDECESSOR_APP_SPAN_SHA256 = (
    "43544cbe4ce096f016dd9d78460c26accc1e4a542768e19912f53e59c2b57b42"
)
EXPECTED_FILE_MANAGER_APP_BYTES = 5_070_112
EXPECTED_FILE_MANAGER_APP_SHA256 = (
    "ac187a9906109c8b3a47cff91a222e33fb2e36285abeae7f15aa465535604693"
)
EXPECTED_FILE_MANAGER_APP_SPAN_SHA256 = (
    "59a158418e3bb26a44e0c1126f371b73c186d2fcfaba9623b4ed04fd47291861"
)

FILE_MANAGER_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=8 "
    b"surface=rgb565-320x200 touch=gt911 native_game_api=1 "
    b"native_format=p4-native-static-v1 game_storage=app-ready "
    b"execution=build-candidate"
)


class FileManagerInstallError(RuntimeError):
    """The pinned File Manager update contract could not be loaded."""


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _load_frozen_updater() -> Any:
    path = FROZEN_UPDATER_PATH.resolve(strict=True)
    before = path.lstat()
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or stat.S_IMODE(before.st_mode) != 0o755
        or not 0 < before.st_size <= MAX_MODULE_BYTES
    ):
        raise FileManagerInstallError("frozen app-only updater metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino, opened.st_size) != (
            before.st_dev, before.st_ino, before.st_size
        ):
            raise FileManagerInstallError(
                "frozen app-only updater changed while opening"
            )
        payload = b""
        while len(payload) < opened.st_size:
            chunk = os.read(descriptor, opened.st_size - len(payload))
            if not chunk:
                raise FileManagerInstallError(
                    "frozen app-only updater was truncated"
                )
            payload += chunk
        if os.read(descriptor, 1):
            raise FileManagerInstallError(
                "frozen app-only updater grew while reading"
            )
    finally:
        os.close(descriptor)
    if _sha256(payload) != EXPECTED_FROZEN_UPDATER_SHA256:
        raise FileManagerInstallError("frozen app-only updater bytes changed")

    name = "console_os_file_manager_frozen_app_updater"
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    exec(compile(payload, str(path), "exec"), module.__dict__)
    return module


UPDATER = _load_frozen_updater()

if (
    UPDATER.EXPECTED_NEW_APP_SPAN_SHA256
    != EXPECTED_PREDECESSOR_APP_SPAN_SHA256
    or b"apps=7 " not in UPDATER.PROGRAM_MANAGER_START
    or UPDATER.LEDGER_NAME != "program-manager-usb-update-ledger.json"
    or not UPDATER.CAPTURE.FIXED
    or UPDATER.CAPTURE.FIXED[0][0] != "start"
):
    raise FileManagerInstallError("frozen app-only updater contract differs")

UPDATER.EXPECTED_CURRENT_APP_SPAN_SHA256 = (
    EXPECTED_PREDECESSOR_APP_SPAN_SHA256
)
UPDATER.EXPECTED_NEW_APP_BYTES = EXPECTED_FILE_MANAGER_APP_BYTES
UPDATER.EXPECTED_NEW_APP_SHA256 = EXPECTED_FILE_MANAGER_APP_SHA256
UPDATER.EXPECTED_NEW_APP_SPAN_SHA256 = EXPECTED_FILE_MANAGER_APP_SPAN_SHA256
UPDATER.LEDGER_NAME = "file-manager-update-ledger.json"
UPDATER.CAPTURE_RAW_NAME = "file-manager-startup.raw"
UPDATER.CAPTURE_SUMMARY_NAME = "file-manager-startup.json"
UPDATER.PROGRAM_MANAGER_START = FILE_MANAGER_START
UPDATER.CAPTURE.START = FILE_MANAGER_START
UPDATER.CAPTURE.FIXED = (
    ("start", FILE_MANAGER_START), *UPDATER.CAPTURE.FIXED[1:]
)

# Preserve the reviewed updater API and transaction implementation. Its
# historical acceptance classification is retained in the ledger for backward
# compatibility; the target hashes and apps=8 startup line bind this successor.
UpdateError = UPDATER.UpdateError
install = UPDATER.install
recover = UPDATER.recover
main = UPDATER.main


if __name__ == "__main__":
    sys.exit(main())
