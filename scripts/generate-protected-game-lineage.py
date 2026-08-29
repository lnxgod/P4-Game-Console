#!/usr/bin/env python3
"""Generate the Console OS protected-game payload digest allowlist."""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import tempfile


HEADER_BYTES = 256
MAX_PACKAGE_BYTES = 512 * 1024
SAVE_CAPABILITY = 1 << 6


class LineageError(RuntimeError):
    pass


def _c_string(field: bytes, label: str) -> str:
    terminator = field.find(b"\0")
    if terminator <= 0 or any(field[terminator + 1 :]):
        raise LineageError(f"{label} is not canonical NUL-padded text")
    try:
        return field[:terminator].decode("ascii")
    except UnicodeDecodeError as error:
        raise LineageError(f"{label} is not ASCII") from error


def inspect_package(path: pathlib.Path, expected_id: str) -> bytes:
    data = path.read_bytes()
    if not HEADER_BYTES < len(data) <= MAX_PACKAGE_BYTES:
        raise LineageError("protected P4G size is invalid")
    if data[:8] != b"P4GAME1\0":
        raise LineageError("protected P4G magic is invalid")
    header_bytes, package_bytes, payload_offset, payload_bytes = \
        struct.unpack_from("<IIII", data, 8)
    format_version, api_version = struct.unpack_from("<II", data, 24)
    required_caps, optional_caps = struct.unpack_from("<II", data, 36)
    if (
        header_bytes != HEADER_BYTES
        or package_bytes != len(data)
        or payload_offset != HEADER_BYTES
        or payload_bytes != len(data) - HEADER_BYTES
        or format_version != 1
        or api_version != 1
    ):
        raise LineageError("protected P4G layout/version is invalid")
    game_id = _c_string(data[80:128], "protected P4G game ID")
    if game_id != expected_id:
        raise LineageError(
            f"protected P4G ID {game_id!r} differs from {expected_id!r}")
    if ((required_caps | optional_caps) & SAVE_CAPABILITY) == 0:
        raise LineageError("protected P4G does not declare save capability")
    embedded_digest = data[48:80]
    actual_digest = hashlib.sha256(data[payload_offset:]).digest()
    if embedded_digest != actual_digest:
        raise LineageError("protected P4G payload digest is invalid")
    return actual_digest


def render_header(game_id: str, digest: bytes) -> str:
    digest_values = ", ".join(f"UINT8_C(0x{value:02x})" for value in digest)
    return f"""// SPDX-License-Identifier: GPL-2.0-or-later
// Generated from the exact deterministic protected P4G. Do not edit.

#ifndef P4_PROTECTED_GAME_LINEAGE_GENERATED_H
#define P4_PROTECTED_GAME_LINEAGE_GENERATED_H

#include <stddef.h>
#include <stdint.h>

typedef struct {{
    const char *game_id;
    uint8_t payload_sha256[32];
}} p4_protected_game_lineage_t;

static const p4_protected_game_lineage_t s_protected_game_lineages[] = {{
    {{
        .game_id = \"{game_id}\",
        .payload_sha256 = {{{digest_values}}},
    }},
}};

enum {{
    P4_PROTECTED_GAME_LINEAGE_COUNT =
        sizeof(s_protected_game_lineages) /
        sizeof(s_protected_game_lineages[0]),
}};

#endif
"""


def write_if_changed(path: pathlib.Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, delete=False
    ) as temporary:
        temporary.write(text)
        temporary_path = pathlib.Path(temporary.name)
    temporary_path.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=pathlib.Path)
    parser.add_argument("--expected-id", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    try:
        digest = inspect_package(arguments.package, arguments.expected_id)
        write_if_changed(
            arguments.output, render_header(arguments.expected_id, digest))
    except (OSError, LineageError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
