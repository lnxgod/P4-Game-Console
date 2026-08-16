#!/usr/bin/env python3

"""Issued outer route for the exact-unit Space Invaders Console OS install."""

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
INSTALLER_PATH = SCRIPT_DIR / "console-os-space-invaders-install.py"
EXPECTED_INSTALLER_SHA256 = "2166d7997fe49003d0d17a389373e68818bd3abd4536c274488c7c6bf54cbc27"
ISSUED_AUTH_SHA256 = "52ae32b585a8464fae518b66a792706e3b9260d91c443c445ff2a5db8f5b4f22"
MAX_INSTALLER_BYTES = 2 * 1024 * 1024


class RouteError(RuntimeError):
    """The separately issued Space Invaders route is not exact."""


def _require_issued() -> str:
    value = ISSUED_AUTH_SHA256
    if not isinstance(value, str) or len(value) != 64 or value == "0" * 64:
        raise RouteError("Space Invaders exact-unit authorization is not issued")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise RouteError("issued authorization digest has invalid shape") from error
    return value


def _load_frozen_installer() -> Any:
    path = INSTALLER_PATH.resolve(strict=True)
    if path != SCRIPT_DIR / "console-os-space-invaders-install.py":
        raise RouteError("installer path differs from the exact sibling")
    before = path.lstat()
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or stat.S_IMODE(before.st_mode) != 0o755
        or not 0 < before.st_size <= MAX_INSTALLER_BYTES
    ):
        raise RouteError("installer metadata or bounded size differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (before.st_dev, before.st_ino, before.st_size)
            or stat.S_IMODE(opened.st_mode) != 0o755
        ):
            raise RouteError("installer changed while opening")
        payload = bytearray()
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise RouteError("installer was truncated while reading")
            payload.extend(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise RouteError("installer grew while reading")
        after = os.fstat(descriptor)
        if (after.st_dev, after.st_ino, after.st_size) != (
            opened.st_dev, opened.st_ino, opened.st_size
        ):
            raise RouteError("installer changed while reading")
    finally:
        os.close(descriptor)
    if hashlib.sha256(payload).hexdigest() != EXPECTED_INSTALLER_SHA256:
        raise RouteError("frozen Space Invaders installer bytes changed")
    name = "console_os_space_invaders_frozen_authorized_installer"
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
    parser.add_argument("--artifact", type=pathlib.Path, required=True)
    parser.add_argument("--authorization", type=pathlib.Path, required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    parser.add_argument("--capture-seconds", type=float, default=30.0)
    args = parser.parse_args()
    installer = _load_frozen_installer()
    result = installer.install_from_trust_anchor(
        port=args.port,
        artifact=args.artifact,
        authorization=args.authorization,
        authorization_sha256=_require_issued(),
        recovery_directory=args.recovery_directory,
        capture_seconds=args.capture_seconds,
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
