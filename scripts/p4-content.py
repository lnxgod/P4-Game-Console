#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate and atomically install P4 game content onto an SD filesystem."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import stat
import sys
from typing import Callable


QUAKE_SHAREWARE_SIZE = 18_689_235
QUAKE_SHAREWARE_SHA256 = "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af"
COPY_CHUNK_BYTES = 128 * 1024


class ContentError(RuntimeError):
    """A validation or safe-install boundary was rejected."""


def _regular_file(path: Path) -> os.stat_result:
    try:
        metadata = path.lstat()
    except FileNotFoundError as error:
        raise ContentError(f"input does not exist: {path}") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISREG(metadata.st_mode):
        raise ContentError(f"input must be one regular file, not a link: {path}")
    return metadata


def _storage_root(path: Path) -> Path:
    try:
        metadata = path.lstat()
    except FileNotFoundError as error:
        raise ContentError(f"SD root does not exist: {path}") from error
    if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
        raise ContentError(f"SD root must be one real directory: {path}")
    return path.resolve()


def _sha256_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    total = 0
    with path.open("rb") as source:
        while True:
            block = source.read(COPY_CHUNK_BYTES)
            if not block:
                break
            total += len(block)
            digest.update(block)
    return total, digest.hexdigest()


def validate_quake_shareware(path: Path) -> str:
    metadata = _regular_file(path)
    if metadata.st_size != QUAKE_SHAREWARE_SIZE:
        raise ContentError(
            f"Quake shareware PAK has {metadata.st_size} bytes; "
            f"expected {QUAKE_SHAREWARE_SIZE}"
        )
    size, digest = _sha256_file(path)
    if size != QUAKE_SHAREWARE_SIZE or digest != QUAKE_SHAREWARE_SHA256:
        raise ContentError("Quake shareware PAK SHA-256 does not match the pinned v1.06 data")
    return digest


def _sync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _ensure_real_directory(root: Path, relative: Path) -> Path:
    current = root
    for part in relative.parts:
        current = current / part
        try:
            metadata = current.lstat()
        except FileNotFoundError:
            current.mkdir(mode=0o700)
            _sync_directory(current.parent)
            metadata = current.lstat()
        if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
            raise ContentError(f"SD directory path is not a real directory: {current}")
    return current


def _copy_staged(source: Path, stage: Path) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    descriptor = os.open(stage, flags, 0o600)
    try:
        with source.open("rb") as input_file, os.fdopen(descriptor, "wb", closefd=False) as output:
            shutil.copyfileobj(input_file, output, COPY_CHUNK_BYTES)
            output.flush()
            os.fsync(output.fileno())
    finally:
        os.close(descriptor)


def _install(
    source: Path,
    storage_root: Path,
    destination_relative: Path,
    stage_name: str,
    validator: Callable[[Path], str],
    replace: bool,
) -> tuple[Path, str]:
    expected_digest = validator(source)
    root = _storage_root(storage_root)
    inbox = _ensure_real_directory(root, Path("P4") / "INBOX")
    destination = root / destination_relative
    _ensure_real_directory(root, destination_relative.parent)
    try:
        destination_metadata = destination.lstat()
    except FileNotFoundError:
        destination_metadata = None
    if destination_metadata is not None:
        if stat.S_ISLNK(destination_metadata.st_mode) or not stat.S_ISREG(
            destination_metadata.st_mode
        ):
            raise ContentError(f"destination must be a regular file: {destination}")
        if not replace:
            raise ContentError(
                f"destination already exists (use --replace explicitly): {destination}"
            )

    stage = inbox / f".{stage_name}.stage-{os.getpid()}"
    if stage.exists() or stage.is_symlink():
        raise ContentError(f"staging path is unexpectedly occupied: {stage}")
    try:
        _copy_staged(source, stage)
        staged_digest = validator(stage)
        if staged_digest != expected_digest:
            raise ContentError("staged read-back digest differs from the validated input")
        _sync_directory(inbox)
        os.replace(stage, destination)
        _sync_directory(destination.parent)
    except Exception:
        try:
            stage.unlink(missing_ok=True)
        except OSError:
            pass
        raise
    return destination, expected_digest


def install_cart(
    source: Path,
    storage_root: Path,
    name: str | None = None,
    replace: bool = False,
) -> tuple[Path, str]:
    # Keep a clear failure for old callers, without validating, staging,
    # creating directories or replacing an existing source cartridge.
    raise ContentError("Lua .P4CART installation is retired; use native .P4G games")


def install_quake(
    source: Path,
    storage_root: Path,
    replace: bool = False,
) -> tuple[Path, str]:
    return _install(
        source,
        storage_root,
        Path("GAMES") / "QUAKE" / "ID1" / "PAK0.PAK",
        "QUAKE-PAK0.PAK",
        validate_quake_shareware,
        replace,
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate, stage, sync, and atomically install game content onto an SD root."
    )
    subparsers = parser.add_subparsers(dest="kind", required=True)
    cart = subparsers.add_parser("cart", help="retired Lua install route (always rejected)")
    cart.add_argument("input", type=Path)
    cart.add_argument("--sd-root", type=Path, required=True)
    cart.add_argument("--name")
    cart.add_argument("--replace", action="store_true")
    quake = subparsers.add_parser("quake", help="install the exact Quake v1.06 shareware PAK")
    quake.add_argument("input", type=Path)
    quake.add_argument("--sd-root", type=Path, required=True)
    quake.add_argument("--replace", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.kind == "cart":
            destination, digest = install_cart(
                args.input, args.sd_root, args.name, args.replace
            )
        else:
            destination, digest = install_quake(args.input, args.sd_root, args.replace)
    except ContentError as error:
        print(f"P4_CONTENT REJECTED reason={error}", file=sys.stderr)
        return 2
    print(f"P4_CONTENT INSTALLED path={destination} sha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
