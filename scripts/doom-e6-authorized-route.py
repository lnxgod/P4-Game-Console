#!/usr/bin/env python3

"""Outer issuance trust root for the exact-unit E6 sound install.

This file is deliberately outside the authorization-bound build inventory.
After the installer and evidence graph are frozen, independent issuance changes
only ``ISSUED_AUTH_SHA256`` below.  The generic flash route never accepts an
authorization digest from its caller.  The installer itself exposes recovery,
but no standalone install CLI.
"""

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
INSTALLER_PATH = SCRIPT_DIR / "doom-e6-install.py"
EXPECTED_INSTALLER_SHA256 = (
    "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
)
ISSUED_AUTH_SHA256 = "558e5907cf6598e4dab5c578678ae5db5f651eb8a49b4e2ca135671e063c7c5b"
MAX_INSTALLER_BYTES = 2 * 1024 * 1024


class RouteError(RuntimeError):
    """The separately reviewed E6 issuance route is not exact."""


def _require_issued() -> str:
    value = ISSUED_AUTH_SHA256
    if value == "0" * 64:
        raise RouteError("E6 exact-unit authorization has not been independently issued")
    if not isinstance(value, str) or len(value) != 64:
        raise RouteError("issued authorization digest has invalid shape")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise RouteError("issued authorization digest has invalid shape") from error
    return value


def _load_frozen_installer() -> Any:
    path = INSTALLER_PATH.resolve(strict=True)
    if path != SCRIPT_DIR / "doom-e6-install.py":
        raise RouteError("installer path differs from the exact sibling")
    before = path.lstat()
    if (
        not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid() or stat.S_IMODE(before.st_mode) != 0o755
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
        payload = b""
        while len(payload) < opened.st_size:
            chunk = os.read(descriptor, opened.st_size - len(payload))
            if not chunk:
                raise RouteError("installer was truncated while reading")
            payload += chunk
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
        raise RouteError("frozen E6 installer bytes changed")
    name = "doom_e6_frozen_authorized_installer"
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
    parser.add_argument("--capture-seconds", type=float, default=45.0)
    args = parser.parse_args()

    issued_digest = _require_issued()
    installer = _load_frozen_installer()
    result = installer.install_from_trust_anchor(
        port=args.port,
        artifact=args.artifact,
        authorization=args.authorization,
        authorization_sha256=issued_digest,
        recovery_directory=args.recovery_directory,
        capture_seconds=args.capture_seconds,
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
