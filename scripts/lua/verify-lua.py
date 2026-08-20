#!/usr/bin/env python3
"""Verify the pinned, offline Lua source used by the P4 Cart sandbox."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / "third_party" / "source-lock.json"
EXPECTED_VERSION = "5.4.8"
EXPECTED_ARCHIVE_SHA256 = "4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae"
MANIFEST_LINE = re.compile(r"^([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._/-]*)$")


def fail(message: str) -> None:
    raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(64 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    source = lock.get("sources", {}).get("lua_5_4")
    if type(source) is not dict:
        fail("source-lock.json is missing sources.lua_5_4")
    expected = {
        "version": EXPECTED_VERSION,
        "archive": f"https://www.lua.org/ftp/lua-{EXPECTED_VERSION}.tar.gz",
        "archive_sha256": EXPECTED_ARCHIVE_SHA256,
        "vendor_path": f"third_party/lua-{EXPECTED_VERSION}",
        "vendor_manifest": f"third_party/lua-{EXPECTED_VERSION}.manifest",
        "license": "MIT",
    }
    for field, value in expected.items():
        if source.get(field) != value:
            fail(f"sources.lua_5_4.{field} must be {value!r}")

    vendor = ROOT / source["vendor_path"]
    manifest = ROOT / source["vendor_manifest"]
    if not vendor.is_dir() or not manifest.is_file():
        fail("vendored Lua source or manifest is missing")
    if sha256(manifest) != source.get("vendor_manifest_sha256"):
        fail("Lua vendor manifest SHA-256 does not match source lock")

    listed: dict[str, str] = {}
    for line_number, line in enumerate(
        manifest.read_text(encoding="ascii").splitlines(), start=1,
    ):
        match = MANIFEST_LINE.fullmatch(line)
        if match is None:
            fail(f"invalid Lua manifest line {line_number}")
        digest, relative = match.groups()
        if relative in listed:
            fail(f"duplicate Lua manifest path: {relative}")
        path = vendor / relative
        try:
            path.resolve(strict=True).relative_to(vendor.resolve())
        except (OSError, ValueError):
            fail(f"Lua manifest path is missing or unsafe: {relative}")
        if not path.is_file() or path.is_symlink():
            fail(f"Lua manifest path is not a regular file: {relative}")
        if sha256(path) != digest:
            fail(f"vendored Lua file hash mismatch: {relative}")
        listed[relative] = digest

    actual = {
        path.relative_to(vendor).as_posix()
        for path in vendor.rglob("*") if path.is_file()
    }
    if actual != set(listed):
        missing = sorted(set(listed) - actual)
        extra = sorted(actual - set(listed))
        fail(f"Lua vendor tree differs from manifest; missing={missing}, extra={extra}")
    for required in ("README", "doc/readme.html", "src/lua.h", "src/lauxlib.h"):
        if required not in listed:
            fail(f"Lua vendor manifest is missing {required}")

    print(
        f"P4_LUA_SOURCE_OK version={EXPECTED_VERSION} files={len(listed)} "
        f"manifest_sha256={sha256(manifest)}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
