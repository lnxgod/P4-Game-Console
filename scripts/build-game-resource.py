#!/usr/bin/env python3

"""Wrap one deterministic game-owned resource payload as a bounded .P4R."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import tempfile
from typing import Any


MAGIC = b"P4RES01\0"
HEADER_BYTES = 128
FORMAT_VERSION = 1
MAXIMUM_BYTES = 8 * 1024 * 1024
ID_RE = re.compile(r"[a-z][a-z0-9.-]{2,47}\Z")
RESOURCE_RE = re.compile(r"[A-Z0-9][A-Z0-9_-]{0,31}\.P4R\Z")


class ResourceError(RuntimeError):
    pass


def load_manifest(path: pathlib.Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ResourceError(f"cannot read manifest: {error}") from error
    if not isinstance(value, dict) or value.get("schema") != 1:
        raise ResourceError("manifest schema must be 1")
    game_id = value.get("id")
    resource_file = value.get("resource_file")
    if not isinstance(game_id, str) or not ID_RE.fullmatch(game_id):
        raise ResourceError("manifest game id is invalid")
    if not isinstance(resource_file, str) or not RESOURCE_RE.fullmatch(resource_file):
        raise ResourceError("manifest resource_file is invalid")
    return value


def build_resource(game_id: str, payload: bytes) -> bytes:
    if not payload or HEADER_BYTES + len(payload) > MAXIMUM_BYTES:
        raise ResourceError("resource payload is outside the 8 MiB package bound")
    header = bytearray(HEADER_BYTES)
    header[:8] = MAGIC
    struct.pack_into(
        "<6I",
        header,
        8,
        HEADER_BYTES,
        HEADER_BYTES + len(payload),
        HEADER_BYTES,
        len(payload),
        FORMAT_VERSION,
        0,
    )
    header[32:64] = hashlib.sha256(payload).digest()
    encoded_id = game_id.encode("ascii", "strict")
    header[64 : 64 + len(encoded_id)] = encoded_id
    return bytes(header) + payload


def atomic_write(path: pathlib.Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=path.name + ".", dir=path.parent
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--payload", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    manifest = load_manifest(args.manifest.resolve())
    try:
        payload = args.payload.resolve().read_bytes()
    except OSError as error:
        raise ResourceError(f"cannot read resource payload: {error}") from error
    if args.output.name != manifest["resource_file"]:
        raise ResourceError("output basename differs from manifest resource_file")
    package = build_resource(manifest["id"], payload)
    atomic_write(args.output.resolve(), package)
    print(json.dumps({
        "bytes": len(package),
        "file": args.output.name,
        "game_id": manifest["id"],
        "payload_bytes": len(payload),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "sha256": hashlib.sha256(package).hexdigest(),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ResourceError as error:
        raise SystemExit(f"game resource build failed: {error}") from error
