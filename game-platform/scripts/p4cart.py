#!/usr/bin/env python3
"""Validate, pack, inspect, and unpack open P4 Cart v1 projects."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import uuid


MAGIC = b"P4CART1\0"
FORMAT_VERSION = 1
HEADER = struct.Struct("<8sHHIIIIHHI32s60s")
ENTRY = struct.Struct("<64sBBHII32s4s")
HEADER_HASH_OFFSET = 36
HEADER_HASH_SIZE = 32
MAX_MANIFEST_BYTES = 16 * 1024
MAX_FILES = 64
MAX_FILE_BYTES = 2 * 1024 * 1024
MAX_CART_BYTES = 8 * 1024 * 1024

KIND_TO_NUMBER = {
    "runtime-source": 1,
    "runtime-asset": 2,
    "source-asset": 3,
    "documentation": 4,
    "license": 5,
}
NUMBER_TO_KIND = {number: name for name, number in KIND_TO_NUMBER.items()}

CODE_LICENSES = {
    "0BSD", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause",
    "GPL-2.0-only", "GPL-2.0-or-later", "GPL-3.0-only",
    "GPL-3.0-or-later", "ISC", "LGPL-2.1-only",
    "LGPL-2.1-or-later", "LGPL-3.0-only", "LGPL-3.0-or-later",
    "MIT", "MPL-2.0", "Unlicense",
}
ASSET_LICENSES = CODE_LICENSES | {
    "CC0-1.0", "CC-BY-4.0", "CC-BY-SA-4.0", "OFL-1.1",
}
CATEGORIES = {"ACTION", "ADVENTURE", "ARCADE", "PLATFORM", "PUZZLE", "RACING"}
CAPABILITIES = {
    "draw", "input", "touch", "audio-tone", "save",
    "local-multiplayer", "lan-lockstep",
}
SLUG_RE = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$")
VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
PATH_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/-]{0,62}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class CartError(ValueError):
    pass


def _fail(message: str) -> None:
    raise CartError(message)


def _object(value: object, where: str, required: set[str], optional: set[str] = set()) -> dict:
    if type(value) is not dict:
        _fail(f"{where} must be an object")
    result = value
    missing = sorted(required - set(result))
    unknown = sorted(set(result) - required - optional)
    if missing:
        _fail(f"{where} missing: {', '.join(missing)}")
    if unknown:
        _fail(f"{where} unknown fields: {', '.join(unknown)}")
    return result


def _integer(value: object, where: str, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        _fail(f"{where} must be an integer in {minimum}..{maximum}")
    return value


def _text(value: object, where: str, minimum: int, maximum: int) -> str:
    if type(value) is not str:
        _fail(f"{where} must be text")
    size = len(value.encode("utf-8"))
    if not minimum <= size <= maximum:
        _fail(f"{where} must be {minimum}..{maximum} UTF-8 bytes")
    if any(ord(character) < 0x20 for character in value):
        _fail(f"{where} contains a control character")
    return value


def _path(value: object, where: str) -> str:
    path = _text(value, where, 1, 63)
    try:
        path.encode("ascii")
    except UnicodeEncodeError:
        _fail(f"{where} must be printable ASCII")
    if not PATH_RE.fullmatch(path) or "\\" in path:
        _fail(f"{where} is not a canonical P4 Cart path")
    parts = path.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        _fail(f"{where} contains an unsafe path segment")
    if path == "p4.json":
        _fail(f"{where} may not list the container manifest")
    return path


def _canonical_json(manifest: dict) -> bytes:
    return (json.dumps(
        manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
    ) + "\n").encode("utf-8")


def _validate_uuid(value: object, where: str) -> str:
    text = _text(value, where, 36, 36)
    try:
        parsed = uuid.UUID(text)
    except ValueError:
        _fail(f"{where} must be a canonical UUID")
    if str(parsed) != text or parsed.version not in {1, 2, 3, 4, 5}:
        _fail(f"{where} must be a lowercase canonical UUID")
    return text


def _validate_manifest(manifest: object) -> dict:
    root = _object(
        manifest, "manifest",
        {"format", "game", "runtime", "license", "remix", "files"},
    )
    if root["format"] != "p4-cart-source-v1":
        _fail("format must be p4-cart-source-v1")

    game = _object(
        root["game"], "game",
        {"id", "slug", "title", "version", "summary", "category", "players"},
    )
    _validate_uuid(game["id"], "game.id")
    slug = _text(game["slug"], "game.slug", 1, 31)
    if not SLUG_RE.fullmatch(slug):
        _fail("game.slug must be lowercase words separated by single hyphens")
    _text(game["title"], "game.title", 1, 32)
    version = _text(game["version"], "game.version", 5, 32)
    if not VERSION_RE.fullmatch(version):
        _fail("game.version must be MAJOR.MINOR.PATCH")
    _text(game["summary"], "game.summary", 1, 96)
    if game["category"] not in CATEGORIES:
        _fail("game.category is not supported")
    players = _object(game["players"], "game.players", {"min", "max"})
    player_min = _integer(players["min"], "game.players.min", 1, 4)
    player_max = _integer(players["max"], "game.players.max", 1, 4)
    if player_min > player_max:
        _fail("game.players.min may not exceed max")

    runtime = _object(
        root["runtime"], "runtime",
        {"id", "api", "entry", "logical_width", "logical_height",
         "update_hz", "present_hz", "heap_bytes", "save_bytes", "capabilities"},
    )
    expected = {
        "id": "p4-lua-5.4-v1", "api": 1, "logical_width": 768,
        "logical_height": 480, "update_hz": 60, "present_hz": 30,
    }
    for field, wanted in expected.items():
        if runtime[field] != wanted:
            _fail(f"runtime.{field} must be {wanted!r}")
    entry_path = _path(runtime["entry"], "runtime.entry")
    heap_bytes = _integer(runtime["heap_bytes"], "runtime.heap_bytes", 65536, 524288)
    if heap_bytes % 4096 != 0:
        _fail("runtime.heap_bytes must be a multiple of 4096")
    save_bytes = _integer(runtime["save_bytes"], "runtime.save_bytes", 0, 4096)
    capabilities = runtime["capabilities"]
    if type(capabilities) is not list or len(capabilities) > 8:
        _fail("runtime.capabilities must be an array of at most 8 entries")
    if any(type(item) is not str or item not in CAPABILITIES for item in capabilities):
        _fail("runtime.capabilities contains an unsupported value")
    if capabilities != sorted(set(capabilities)):
        _fail("runtime.capabilities must be unique and sorted")
    if not {"draw", "input"}.issubset(capabilities):
        _fail("runtime.capabilities must include draw and input")
    if save_bytes > 0 and "save" not in capabilities:
        _fail("a nonzero save_bytes requires the save capability")
    if player_max > 1 and "local-multiplayer" not in capabilities:
        _fail("more than one player requires local-multiplayer")
    if "lan-lockstep" in capabilities and (
        "local-multiplayer" not in capabilities or player_max < 2
    ):
        _fail("lan-lockstep requires local-multiplayer and at least two players")

    license_info = _object(root["license"], "license", {"code", "assets", "notice"})
    if license_info["code"] not in CODE_LICENSES:
        _fail("license.code must be an approved remixable SPDX identifier")
    if license_info["assets"] not in ASSET_LICENSES:
        _fail("license.assets must be an approved remixable SPDX identifier")
    notice_path = _path(license_info["notice"], "license.notice")

    remix = _object(root["remix"], "remix", {"source_included", "parent"})
    if remix["source_included"] is not True:
        _fail("remix.source_included must be true")
    if remix["parent"] is not None:
        parent = _object(remix["parent"], "remix.parent", {"id", "version", "sha256"})
        _validate_uuid(parent["id"], "remix.parent.id")
        parent_version = _text(parent["version"], "remix.parent.version", 5, 32)
        if not VERSION_RE.fullmatch(parent_version):
            _fail("remix.parent.version must be MAJOR.MINOR.PATCH")
        parent_hash = _text(parent["sha256"], "remix.parent.sha256", 64, 64)
        if not SHA256_RE.fullmatch(parent_hash):
            _fail("remix.parent.sha256 must be lowercase hexadecimal")
        if parent["id"] == game["id"]:
            _fail("a remix must have a new game.id")

    files = root["files"]
    if type(files) is not list or not 1 <= len(files) <= MAX_FILES:
        _fail(f"files must contain 1..{MAX_FILES} entries")
    paths: list[str] = []
    casefolded: set[str] = set()
    asset_ids: set[int] = set()
    kinds_by_path: dict[str, str] = {}
    for index, raw_file in enumerate(files):
        item = _object(raw_file, f"files[{index}]", {"path", "kind"}, {"asset_id"})
        path = _path(item["path"], f"files[{index}].path")
        folded = path.casefold()
        if folded in casefolded:
            _fail(f"duplicate or case-colliding path: {path}")
        casefolded.add(folded)
        kind = item["kind"]
        if kind not in KIND_TO_NUMBER:
            _fail(f"files[{index}].kind is not supported")
        if kind == "runtime-asset":
            if "asset_id" not in item:
                _fail(f"runtime asset {path} requires asset_id")
            asset_id = _integer(item["asset_id"], f"files[{index}].asset_id", 1, 4095)
            if asset_id in asset_ids:
                _fail(f"duplicate asset_id: {asset_id}")
            asset_ids.add(asset_id)
        elif "asset_id" in item:
            _fail(f"only runtime assets may declare asset_id: {path}")
        if kind == "runtime-source" and not path.endswith(".lua"):
            _fail(f"runtime source must end in .lua: {path}")
        paths.append(path)
        kinds_by_path[path] = kind

    if paths != sorted(paths):
        _fail("files must be sorted by path")
    if kinds_by_path.get(entry_path) != "runtime-source":
        _fail("runtime.entry must name a listed runtime-source file")
    if kinds_by_path.get(notice_path) != "license":
        _fail("license.notice must name a listed license file")
    if kinds_by_path.get("README.md") != "documentation":
        _fail("README.md must be listed as documentation")
    return root


def _load_source(source_directory: Path) -> tuple[dict, list[tuple[str, int, bytes]]]:
    root = source_directory.resolve()
    if not root.is_dir():
        _fail(f"source directory does not exist: {source_directory}")
    manifest_path = root / "p4.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        _fail("source project must contain a regular p4.json")
    try:
        raw_manifest = manifest_path.read_bytes()
    except OSError as error:
        _fail(f"cannot read p4.json: {error}")
    if len(raw_manifest) > MAX_MANIFEST_BYTES:
        _fail("p4.json exceeds the manifest limit")
    try:
        manifest = json.loads(raw_manifest.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"p4.json is not valid UTF-8 JSON: {error}")
    manifest = _validate_manifest(manifest)
    canonical = _canonical_json(manifest)
    if len(canonical) > MAX_MANIFEST_BYTES:
        _fail("canonical manifest exceeds the manifest limit")

    payloads: list[tuple[str, int, bytes]] = []
    for item in manifest["files"]:
        relative = item["path"]
        candidate = root / relative
        try:
            resolved = candidate.resolve(strict=True)
            resolved.relative_to(root)
        except (OSError, ValueError):
            _fail(f"listed file escapes or is missing: {relative}")
        current = candidate
        while current != root:
            if current.is_symlink():
                _fail(f"listed path may not traverse a symlink: {relative}")
            current = current.parent
        if not resolved.is_file():
            _fail(f"listed path is not a regular file: {relative}")
        data = resolved.read_bytes()
        if len(data) > MAX_FILE_BYTES:
            _fail(f"listed file exceeds 2 MiB: {relative}")
        kind = item["kind"]
        if kind == "runtime-source":
            if data.startswith(b"\x1bLua"):
                _fail(f"Lua bytecode is forbidden: {relative}")
            if b"\0" in data:
                _fail(f"Lua source contains NUL: {relative}")
            try:
                data.decode("utf-8")
            except UnicodeDecodeError:
                _fail(f"Lua source is not UTF-8: {relative}")
        payloads.append((relative, KIND_TO_NUMBER[kind], data))
    return manifest, payloads


def validate_source(source_directory: Path) -> tuple[dict, list[tuple[str, int, bytes]]]:
    manifest, payloads = _load_source(source_directory)
    estimated = HEADER.size + MAX_MANIFEST_BYTES + len(payloads) * ENTRY.size
    estimated += sum((len(data) + 3) & ~3 for _, _, data in payloads)
    if estimated > MAX_CART_BYTES:
        _fail("source project cannot fit the 8 MiB cart limit")
    return manifest, payloads


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _hash_with_zeroed_header_hash(data: bytes | bytearray) -> bytes:
    mutable = bytearray(data)
    mutable[HEADER_HASH_OFFSET:HEADER_HASH_OFFSET + HEADER_HASH_SIZE] = b"\0" * HEADER_HASH_SIZE
    return hashlib.sha256(mutable).digest()


def pack_source(source_directory: Path, output_path: Path) -> bytes:
    manifest, payloads = validate_source(source_directory)
    if output_path.suffix.lower() != ".p4cart":
        _fail("packed output must end in .p4cart")
    if output_path.exists():
        _fail(f"refusing to overwrite existing output: {output_path}")
    manifest_bytes = _canonical_json(manifest)
    table_offset = _align4(HEADER.size + len(manifest_bytes))
    payload_offset = _align4(table_offset + len(payloads) * ENTRY.size)
    cursor = payload_offset
    records: list[tuple[str, int, int, bytes]] = []
    for path, kind, data in payloads:
        cursor = _align4(cursor)
        records.append((path, kind, cursor, data))
        cursor += len(data)
    total_size = cursor
    if total_size > MAX_CART_BYTES:
        _fail("packed cart exceeds 8 MiB")

    blob = bytearray(total_size)
    HEADER.pack_into(
        blob, 0, MAGIC, HEADER.size, FORMAT_VERSION, 0, total_size,
        HEADER.size, len(manifest_bytes), len(records), ENTRY.size,
        payload_offset, b"\0" * 32, b"\0" * 60,
    )
    blob[HEADER.size:HEADER.size + len(manifest_bytes)] = manifest_bytes
    for index, (path, kind, offset, data) in enumerate(records):
        encoded_path = path.encode("ascii")
        ENTRY.pack_into(
            blob, table_offset + index * ENTRY.size,
            encoded_path, kind, 0, 0, offset, len(data),
            hashlib.sha256(data).digest(), b"\0" * 4,
        )
        blob[offset:offset + len(data)] = data
    digest = _hash_with_zeroed_header_hash(blob)
    blob[HEADER_HASH_OFFSET:HEADER_HASH_OFFSET + HEADER_HASH_SIZE] = digest

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name(f".{output_path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("xb") as stream:
            stream.write(blob)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output_path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return digest


def inspect_cart(cart_path: Path) -> tuple[dict, list[tuple[str, int, bytes]], bytes]:
    try:
        with cart_path.open("rb") as stream:
            blob = stream.read(MAX_CART_BYTES + 1)
    except OSError as error:
        _fail(f"cannot read cart: {error}")
    if len(blob) > MAX_CART_BYTES:
        _fail("cart exceeds 8 MiB")
    if len(blob) < HEADER.size:
        _fail("cart is shorter than its header")
    (magic, header_size, version, flags, total_size, manifest_offset,
     manifest_size, entry_count, entry_size, payload_offset, content_hash,
     reserved) = HEADER.unpack_from(blob)
    if magic != MAGIC or header_size != HEADER.size or version != FORMAT_VERSION:
        _fail("cart header identity is invalid")
    if flags != 0 or reserved != b"\0" * len(reserved):
        _fail("cart header has unsupported flags or reserved bytes")
    if total_size != len(blob):
        _fail("cart total_size does not match the file")
    if not 1 <= manifest_size <= MAX_MANIFEST_BYTES:
        _fail("cart manifest size is invalid")
    if not 1 <= entry_count <= MAX_FILES or entry_size != ENTRY.size:
        _fail("cart entry table geometry is invalid")
    if manifest_offset != HEADER.size:
        _fail("cart manifest offset is invalid")
    table_offset = _align4(manifest_offset + manifest_size)
    expected_payload_offset = _align4(table_offset + entry_count * ENTRY.size)
    if payload_offset != expected_payload_offset or payload_offset > len(blob):
        _fail("cart payload offset is invalid")
    if content_hash != _hash_with_zeroed_header_hash(blob):
        _fail("cart content SHA-256 does not match")
    if any(blob[manifest_offset + manifest_size:table_offset]):
        _fail("cart manifest padding is not zero")
    if any(blob[table_offset + entry_count * ENTRY.size:payload_offset]):
        _fail("cart table padding is not zero")

    manifest_bytes = blob[manifest_offset:manifest_offset + manifest_size]
    try:
        manifest = json.loads(manifest_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        _fail(f"cart manifest is invalid UTF-8 JSON: {error}")
    manifest = _validate_manifest(manifest)
    if manifest_bytes != _canonical_json(manifest):
        _fail("cart manifest is not canonical JSON")
    expected_files = manifest["files"]
    if len(expected_files) != entry_count:
        _fail("cart entry count does not match manifest")

    payloads: list[tuple[str, int, bytes]] = []
    previous_end = payload_offset
    for index, expected in enumerate(expected_files):
        (raw_path, kind, entry_flags, entry_reserved, offset, length,
         payload_hash, tail_reserved) = ENTRY.unpack_from(blob, table_offset + index * ENTRY.size)
        if entry_flags != 0 or entry_reserved != 0 or tail_reserved != b"\0" * 4:
            _fail(f"cart entry {index} has unsupported flags or reserved bytes")
        nul = raw_path.find(b"\0")
        if nul < 1 or any(raw_path[nul:]):
            _fail(f"cart entry {index} path encoding is invalid")
        try:
            path = raw_path[:nul].decode("ascii")
        except UnicodeDecodeError:
            _fail(f"cart entry {index} path is not ASCII")
        if path != expected["path"] or NUMBER_TO_KIND.get(kind) != expected["kind"]:
            _fail(f"cart entry {index} does not match manifest")
        if offset % 4 != 0 or offset < previous_end or length > MAX_FILE_BYTES:
            _fail(f"cart entry {path} range is invalid")
        end = offset + length
        if end > len(blob):
            _fail(f"cart entry {path} exceeds the file")
        if any(blob[previous_end:offset]):
            _fail(f"cart padding before {path} is not zero")
        data = blob[offset:end]
        if hashlib.sha256(data).digest() != payload_hash:
            _fail(f"cart payload hash mismatch: {path}")
        if kind == KIND_TO_NUMBER["runtime-source"]:
            if data.startswith(b"\x1bLua") or b"\0" in data:
                _fail(f"cart contains forbidden Lua bytecode or NUL: {path}")
            try:
                data.decode("utf-8")
            except UnicodeDecodeError:
                _fail(f"cart Lua source is not UTF-8: {path}")
        payloads.append((path, kind, data))
        previous_end = end
    if previous_end != len(blob):
        _fail("cart has unreferenced trailing bytes")
    return manifest, payloads, content_hash


def unpack_cart(cart_path: Path, destination: Path) -> bytes:
    manifest, payloads, digest = inspect_cart(cart_path)
    if destination.exists():
        _fail(f"refusing to overwrite existing destination: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=destination.parent))
    try:
        pretty_manifest = json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
        (temporary / "p4.json").write_text(pretty_manifest, encoding="utf-8")
        for path, _kind, data in payloads:
            output = temporary / path
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return digest


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate", help="validate a source directory")
    validate.add_argument("source", type=Path)
    pack = commands.add_parser("pack", help="pack a source directory")
    pack.add_argument("source", type=Path)
    pack.add_argument("output", type=Path)
    inspect = commands.add_parser("inspect", help="validate and describe a packed cart")
    inspect.add_argument("cart", type=Path)
    unpack = commands.add_parser("unpack", help="unpack a validated cart for remixing")
    unpack.add_argument("cart", type=Path)
    unpack.add_argument("destination", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.command == "validate":
            manifest, payloads = validate_source(arguments.source)
            print(f"P4_CART VALID id={manifest['game']['id']} files={len(payloads)}")
        elif arguments.command == "pack":
            digest = pack_source(arguments.source, arguments.output)
            print(f"P4_CART PACKED path={arguments.output} sha256={digest.hex()}")
        elif arguments.command == "inspect":
            manifest, payloads, digest = inspect_cart(arguments.cart)
            print(
                f"P4_CART VALID id={manifest['game']['id']} title={manifest['game']['title']!r} "
                f"files={len(payloads)} sha256={digest.hex()}"
            )
        elif arguments.command == "unpack":
            digest = unpack_cart(arguments.cart, arguments.destination)
            print(f"P4_CART UNPACKED path={arguments.destination} sha256={digest.hex()}")
        else:
            raise AssertionError(arguments.command)
    except CartError as error:
        print(f"P4_CART ERROR {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
