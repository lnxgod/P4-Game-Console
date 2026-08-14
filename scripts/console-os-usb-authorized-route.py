#!/usr/bin/env python3

"""Frozen issuance route for the exact-unit USB-storage Console OS install."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import stat
import sys
import types
from typing import Any


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
INSTALLER_PATH = SCRIPT_DIR / "console-os-usb-install.py"
EXPECTED_INSTALLER_SHA256 = (
    "f911c52b63460ec1c972265a35662e7d900dc3eee54e8b452e0cac95ef125b70"
)
ISSUED_AUTH_SHA256 = (
    "7c38f4d8ead24e2e6b56f748d774f1d1425eb30262bed96ac8f1b80c2cdc119e"
)
MAX_INSTALLER_BYTES = 2 * 1024 * 1024


class RouteError(RuntimeError):
    """The separately issued USB-storage install route is not exact."""


def _issued_digest() -> str:
    value = ISSUED_AUTH_SHA256
    if value == "0" * 64:
        raise RouteError("USB-storage exact-unit authorization is not issued")
    if not isinstance(value, str) or len(value) != 64:
        raise RouteError("issued authorization digest has invalid shape")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise RouteError("issued authorization digest has invalid shape") from error
    return value


def _load_installer() -> Any:
    path = INSTALLER_PATH.resolve(strict=True)
    if path != SCRIPT_DIR / "console-os-usb-install.py":
        raise RouteError("installer path differs from the exact sibling")
    before = path.lstat()
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or stat.S_IMODE(before.st_mode) != 0o755
        or not 0 < before.st_size <= MAX_INSTALLER_BYTES
    ):
        raise RouteError("installer metadata or size differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (before.st_dev, before.st_ino, before.st_size)
            or stat.S_IMODE(opened.st_mode) != 0o755
        ):
            raise RouteError("installer changed while opening")
        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise RouteError("installer was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise RouteError("installer grew while reading")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size)
            != (opened.st_dev, opened.st_ino, opened.st_size)
        ):
            raise RouteError("installer changed while reading")
        payload = b"".join(chunks)
    finally:
        os.close(descriptor)
    if hashlib.sha256(payload).hexdigest() != EXPECTED_INSTALLER_SHA256:
        raise RouteError("frozen USB-storage installer bytes changed")
    name = "console_os_usb_frozen_authorized_installer"
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    try:
        exec(compile(payload, str(path), "exec"), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--authorization", type=pathlib.Path, required=True)
    parser.add_argument("--factory-backup", type=pathlib.Path, required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    args = parser.parse_args()

    installer = _load_installer()
    result = installer.install_from_trust_anchor(
        port=args.port,
        authorization=args.authorization,
        authorization_sha256=_issued_digest(),
        factory_backup=args.factory_backup,
        recovery_directory=args.recovery_directory,
        capture_seconds=30.0,
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
