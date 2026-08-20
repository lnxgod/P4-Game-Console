#!/usr/bin/env python3
"""Estimate, split, inspect, and reassemble QR transfers of P4 Cart files.

The QR layer is transport-only.  It compresses one already-valid `.p4cart`,
splits the resulting envelope into printable Base45 frames, and reconstructs
the exact original cartridge after validating every frame and both payload
hashes.  It never changes the game manifest or runtime capabilities.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import zlib


SCRIPT_DIRECTORY = Path(__file__).resolve().parent
P4CART_SCRIPT = SCRIPT_DIRECTORY / "p4cart.py"
P4CART_SPEC = importlib.util.spec_from_file_location("p4cart", P4CART_SCRIPT)
if P4CART_SPEC is None or P4CART_SPEC.loader is None:
    raise RuntimeError(f"cannot load P4 Cart tooling from {P4CART_SCRIPT}")
p4cart = importlib.util.module_from_spec(P4CART_SPEC)
P4CART_SPEC.loader.exec_module(p4cart)


ENVELOPE_MAGIC = b"P4QSET1\0"
ENVELOPE_VERSION = 1
ENVELOPE_FLAG_ZLIB = 1
ENVELOPE_HEADER = struct.Struct("<8sHHIII32s32s8s")
FRAME_PREFIX = "P4QR1"
BASE45_ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:"
BASE45_VALUES = {character: index for index, character in enumerate(BASE45_ALPHABET)}

MIN_CHUNK_BYTES = 128
MAX_CHUNK_BYTES = 1200
MAX_TRANSFER_FRAMES = 255
DEFAULT_PROFILE = "balanced"
QR_PROFILES = {
    "easy": 384,
    "balanced": 640,
    "dense": 900,
}

OBJECT_ID_RE = re.compile(r"^[0-9A-F]{16}$")
POSITION_RE = re.compile(r"^([1-9][0-9]*)/([1-9][0-9]*)$")
CRC_RE = re.compile(r"^[0-9A-F]{8}$")


class QrTransferError(ValueError):
    """A malformed, incomplete, or unsupported P4 QR transfer."""


def _fail(message: str) -> None:
    raise QrTransferError(message)


def base45_encode(data: bytes) -> str:
    """Encode bytes using the QR-alphanumeric-friendly Base45 alphabet."""
    output: list[str] = []
    index = 0
    while index + 1 < len(data):
        value = data[index] * 256 + data[index + 1]
        output.append(BASE45_ALPHABET[value % 45])
        output.append(BASE45_ALPHABET[(value // 45) % 45])
        output.append(BASE45_ALPHABET[value // (45 * 45)])
        index += 2
    if index < len(data):
        value = data[index]
        output.append(BASE45_ALPHABET[value % 45])
        output.append(BASE45_ALPHABET[value // 45])
    return "".join(output)


def base45_decode(text: str) -> bytes:
    """Decode strict Base45 text and reject impossible terminal groups."""
    if not text:
        _fail("QR frame payload is empty")
    if len(text) % 3 == 1:
        _fail("Base45 payload has an invalid length")
    output = bytearray()
    index = 0
    while index + 2 < len(text):
        try:
            first = BASE45_VALUES[text[index]]
            second = BASE45_VALUES[text[index + 1]]
            third = BASE45_VALUES[text[index + 2]]
        except KeyError as error:
            _fail(f"Base45 payload contains unsupported character {error.args[0]!r}")
        value = first + second * 45 + third * 45 * 45
        if value > 0xFFFF:
            _fail("Base45 payload contains an overflowing three-character group")
        output.extend((value // 256, value % 256))
        index += 3
    if index < len(text):
        try:
            first = BASE45_VALUES[text[index]]
            second = BASE45_VALUES[text[index + 1]]
        except KeyError as error:
            _fail(f"Base45 payload contains unsupported character {error.args[0]!r}")
        value = first + second * 45
        if value > 0xFF:
            _fail("Base45 payload contains an overflowing terminal group")
        output.append(value)
    return bytes(output)


def _read_valid_cart(cart_path: Path) -> tuple[bytes, dict, bytes]:
    manifest, _, cart_hash = p4cart.inspect_cart(cart_path)
    try:
        cart_bytes = cart_path.read_bytes()
    except OSError as error:
        _fail(f"cannot read cart: {error}")
    if len(cart_bytes) > p4cart.MAX_CART_BYTES:
        _fail("cart exceeds the P4 Cart maximum")
    return cart_bytes, manifest, cart_hash


def build_envelope(cart_bytes: bytes) -> bytes:
    """Build one bounded compressed envelope around exact P4CART bytes."""
    if not 1 <= len(cart_bytes) <= p4cart.MAX_CART_BYTES:
        _fail("cart size is outside the P4 Cart bounds")
    payload = zlib.compress(cart_bytes, level=9)
    header = ENVELOPE_HEADER.pack(
        ENVELOPE_MAGIC,
        ENVELOPE_HEADER.size,
        ENVELOPE_VERSION,
        ENVELOPE_FLAG_ZLIB,
        len(cart_bytes),
        len(payload),
        hashlib.sha256(cart_bytes).digest(),
        hashlib.sha256(payload).digest(),
        b"\0" * 8,
    )
    return header + payload


def unpack_envelope(envelope: bytes) -> bytes:
    """Validate and decompress one P4 QR envelope with a hard output bound."""
    if len(envelope) < ENVELOPE_HEADER.size:
        _fail("QR transfer is shorter than its envelope header")
    (magic, header_size, version, flags, cart_size, payload_size,
     cart_hash, payload_hash, reserved) = ENVELOPE_HEADER.unpack_from(envelope)
    if magic != ENVELOPE_MAGIC or header_size != ENVELOPE_HEADER.size:
        _fail("QR envelope identity is invalid")
    if version != ENVELOPE_VERSION or flags != ENVELOPE_FLAG_ZLIB:
        _fail("QR envelope version or compression is unsupported")
    if reserved != b"\0" * len(reserved):
        _fail("QR envelope reserved bytes are nonzero")
    if not 1 <= cart_size <= p4cart.MAX_CART_BYTES:
        _fail("QR envelope declares an invalid cart size")
    if payload_size == 0 or payload_size != len(envelope) - header_size:
        _fail("QR envelope payload size does not match")
    payload = envelope[header_size:]
    if hashlib.sha256(payload).digest() != payload_hash:
        _fail("QR envelope compressed payload SHA-256 does not match")

    decoder = zlib.decompressobj()
    try:
        cart_bytes = decoder.decompress(payload, cart_size + 1)
    except zlib.error as error:
        _fail(f"QR envelope compression stream is invalid: {error}")
    if len(cart_bytes) > cart_size or decoder.unconsumed_tail:
        _fail("QR envelope expands beyond its declared cart size")
    if (len(cart_bytes) != cart_size or not decoder.eof or
            decoder.unused_data):
        _fail("QR envelope did not produce exactly the declared cart bytes")
    if hashlib.sha256(cart_bytes).digest() != cart_hash:
        _fail("reconstructed cart SHA-256 does not match")
    return cart_bytes


def envelope_object_id(envelope: bytes) -> str:
    """Return the short transfer-group identity carried by every frame."""
    return hashlib.sha256(envelope).hexdigest()[:16].upper()


def _frame_text(
    object_id: str,
    index: int,
    total: int,
    chunk: bytes,
) -> str:
    crc = zlib.crc32(chunk) & 0xFFFFFFFF
    return (
        f"{FRAME_PREFIX}:{object_id}:{index}/{total}:{len(chunk)}:"
        f"{crc:08X}:{base45_encode(chunk)}"
    )


def split_envelope(
    envelope: bytes,
    chunk_bytes: int,
    *,
    enforce_frame_limit: bool = True,
) -> list[str]:
    if not MIN_CHUNK_BYTES <= chunk_bytes <= MAX_CHUNK_BYTES:
        _fail(f"chunk bytes must be in {MIN_CHUNK_BYTES}..{MAX_CHUNK_BYTES}")
    total = math.ceil(len(envelope) / chunk_bytes)
    if total == 0:
        _fail("cannot split an empty QR envelope")
    if enforce_frame_limit and total > MAX_TRANSFER_FRAMES:
        _fail(
            f"transfer needs {total} QR codes; the v1 receiver limit is "
            f"{MAX_TRANSFER_FRAMES}, so use denser frames, USB, or the BBS"
        )
    object_id = envelope_object_id(envelope)
    return [
        _frame_text(
            object_id,
            index + 1,
            total,
            envelope[index * chunk_bytes:(index + 1) * chunk_bytes],
        )
        for index in range(total)
    ]


def parse_frame(text: str) -> tuple[str, int, int, bytes]:
    """Parse and validate one independently scanned P4 QR frame."""
    fields = text.rstrip("\r\n").split(":", 5)
    if len(fields) != 6 or fields[0] != FRAME_PREFIX:
        _fail("QR frame prefix or field count is invalid")
    _, object_id, position, length_text, crc_text, encoded = fields
    if not OBJECT_ID_RE.fullmatch(object_id):
        _fail("QR frame object ID is invalid")
    match = POSITION_RE.fullmatch(position)
    if match is None:
        _fail("QR frame position is invalid")
    index = int(match.group(1))
    total = int(match.group(2))
    if total > MAX_TRANSFER_FRAMES or index > total:
        _fail("QR frame position exceeds the v1 transfer bounds")
    try:
        declared_length = int(length_text)
    except ValueError:
        _fail("QR frame byte length is invalid")
    if not 1 <= declared_length <= MAX_CHUNK_BYTES:
        _fail("QR frame byte length is outside the transfer bounds")
    if not CRC_RE.fullmatch(crc_text):
        _fail("QR frame CRC is invalid")
    chunk = base45_decode(encoded)
    if len(chunk) != declared_length:
        _fail("QR frame decoded byte length does not match")
    if (zlib.crc32(chunk) & 0xFFFFFFFF) != int(crc_text, 16):
        _fail("QR frame CRC-32 does not match")
    return object_id, index, total, chunk


def collect_frames(frame_texts: list[str]) -> tuple[str, int, dict[int, bytes]]:
    """Collect frames in any order while rejecting conflicting duplicates."""
    if not frame_texts:
        _fail("no QR frames were supplied")
    object_id: str | None = None
    total: int | None = None
    chunks: dict[int, bytes] = {}
    for text in frame_texts:
        current_id, index, current_total, chunk = parse_frame(text)
        if object_id is None:
            object_id = current_id
            total = current_total
        elif current_id != object_id or current_total != total:
            _fail("QR frames belong to different transfer sets")
        previous = chunks.get(index)
        if previous is not None and previous != chunk:
            _fail(f"QR frame {index} conflicts with an earlier scan")
        chunks[index] = chunk
    assert object_id is not None and total is not None
    return object_id, total, chunks


def assemble_frames(frame_texts: list[str]) -> bytes:
    """Reassemble a complete frame set and return validated P4CART bytes."""
    object_id, total, chunks = collect_frames(frame_texts)
    missing = [index for index in range(1, total + 1) if index not in chunks]
    if missing:
        preview = ", ".join(str(index) for index in missing[:12])
        suffix = "..." if len(missing) > 12 else ""
        _fail(f"QR transfer is incomplete; missing frame(s): {preview}{suffix}")
    envelope = b"".join(chunks[index] for index in range(1, total + 1))
    if envelope_object_id(envelope) != object_id:
        _fail("QR transfer object ID does not match the assembled envelope")
    return unpack_envelope(envelope)


def estimate_cart(cart_path: Path) -> dict:
    cart_bytes, manifest, cart_hash = _read_valid_cart(cart_path)
    envelope = build_envelope(cart_bytes)
    estimates = []
    for profile, chunk_bytes in QR_PROFILES.items():
        frames = split_envelope(
            envelope, chunk_bytes, enforce_frame_limit=False,
        )
        estimates.append({
            "profile": profile,
            "chunk_bytes": chunk_bytes,
            "qr_codes": len(frames),
            "largest_payload_characters": max(len(frame) for frame in frames),
            "within_v1_receiver_limit": len(frames) <= MAX_TRANSFER_FRAMES,
        })
    return {
        "game": {
            "id": manifest["game"]["id"],
            "title": manifest["game"]["title"],
            "version": manifest["game"]["version"],
        },
        "cart_bytes": len(cart_bytes),
        "compressed_payload_bytes": len(envelope) - ENVELOPE_HEADER.size,
        "envelope_bytes": len(envelope),
        "cart_sha256": cart_hash.hex(),
        "object_id": envelope_object_id(envelope),
        "profiles": estimates,
    }


def _atomic_write(path: Path, data: bytes) -> None:
    if path.exists():
        _fail(f"refusing to overwrite existing output: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def write_frame_set(cart_path: Path, output_directory: Path, chunk_bytes: int) -> dict:
    if output_directory.exists():
        _fail(f"refusing to overwrite existing output: {output_directory}")
    cart_bytes, manifest, cart_hash = _read_valid_cart(cart_path)
    envelope = build_envelope(cart_bytes)
    frames = split_envelope(envelope, chunk_bytes)
    object_id = envelope_object_id(envelope)
    summary = {
        "format": "p4-qr-transfer-v1",
        "object_id": object_id,
        "game": {
            "id": manifest["game"]["id"],
            "title": manifest["game"]["title"],
            "version": manifest["game"]["version"],
        },
        "cart_bytes": len(cart_bytes),
        "compressed_payload_bytes": len(envelope) - ENVELOPE_HEADER.size,
        "envelope_bytes": len(envelope),
        "cart_sha256": cart_hash.hex(),
        "chunk_bytes": chunk_bytes,
        "qr_codes": len(frames),
        "largest_payload_characters": max(len(frame) for frame in frames),
    }

    output_directory.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(
        prefix=f".{output_directory.name}.", dir=output_directory.parent,
    ))
    try:
        for index, frame in enumerate(frames, start=1):
            name = f"frame-{index:03d}-of-{len(frames):03d}.p4qr"
            (temporary / name).write_text(frame + "\n", encoding="ascii")
        (temporary / "transfer.json").write_text(
            json.dumps(summary, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, output_directory)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return summary


def read_frame_set(source: Path) -> list[str]:
    if source.is_dir():
        paths = sorted(source.glob("*.p4qr"))
        if not paths:
            _fail(f"no .p4qr frames found in {source}")
        texts = []
        for path in paths:
            try:
                texts.append(path.read_text(encoding="ascii"))
            except (OSError, UnicodeDecodeError) as error:
                _fail(f"cannot read QR frame {path}: {error}")
        return texts
    if source.is_file():
        try:
            lines = source.read_text(encoding="ascii").splitlines()
        except (OSError, UnicodeDecodeError) as error:
            _fail(f"cannot read QR frame list: {error}")
        return [line for line in lines if line.strip()]
    _fail(f"QR frame source does not exist: {source}")


def join_frame_set(source: Path, output_path: Path) -> dict:
    if output_path.suffix.lower() != ".p4cart":
        _fail("joined output must end in .p4cart")
    if output_path.exists():
        _fail(f"refusing to overwrite existing output: {output_path}")
    texts = read_frame_set(source)
    object_id, total, chunks = collect_frames(texts)
    cart_bytes = assemble_frames(texts)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name(f".{output_path.name}.{os.getpid()}.tmp")
    try:
        with temporary.open("xb") as stream:
            stream.write(cart_bytes)
            stream.flush()
            os.fsync(stream.fileno())
        manifest, _, cart_hash = p4cart.inspect_cart(temporary)
        os.replace(temporary, output_path)
    finally:
        if temporary.exists():
            temporary.unlink()
    return {
        "object_id": object_id,
        "frames_received": len(chunks),
        "qr_codes": total,
        "cart_bytes": len(cart_bytes),
        "cart_sha256": cart_hash.hex(),
        "game": {
            "id": manifest["game"]["id"],
            "title": manifest["game"]["title"],
            "version": manifest["game"]["version"],
        },
    }


def inspect_frame_set(source: Path) -> dict:
    texts = read_frame_set(source)
    object_id, total, chunks = collect_frames(texts)
    missing = [index for index in range(1, total + 1) if index not in chunks]
    result = {
        "object_id": object_id,
        "frames_received": len(chunks),
        "qr_codes": total,
        "complete": not missing,
        "missing": missing,
    }
    if not missing:
        cart_bytes = assemble_frames(texts)
        result["cart_bytes"] = len(cart_bytes)
        result["cart_sha256"] = hashlib.sha256(cart_bytes).hexdigest()
    return result


def _chunk_bytes(arguments: argparse.Namespace) -> int:
    if arguments.chunk_bytes is not None:
        return arguments.chunk_bytes
    return QR_PROFILES[arguments.profile]


def _print_estimate(summary: dict) -> None:
    game = summary["game"]
    print(f"{game['title']} {game['version']}")
    print(f"P4CART bytes: {summary['cart_bytes']}")
    print(f"Compressed payload bytes: {summary['compressed_payload_bytes']}")
    print(f"QR envelope bytes: {summary['envelope_bytes']}")
    print(f"Cart SHA-256: {summary['cart_sha256']}")
    for profile in summary["profiles"]:
        marker = "" if profile["within_v1_receiver_limit"] else " (use USB/BBS)"
        print(
            f"{profile['profile']}: {profile['qr_codes']} QR code(s), "
            f"{profile['largest_payload_characters']} max characters{marker}"
        )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="QR transport tooling for validated P4 Cart games",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    estimate = subparsers.add_parser(
        "estimate", help="show the QR-code cost of a P4 Cart",
    )
    estimate.add_argument("cart", type=Path)
    estimate.add_argument("--json", action="store_true")

    split = subparsers.add_parser(
        "split", help="write a complete multi-QR frame set",
    )
    split.add_argument("cart", type=Path)
    split.add_argument("output_directory", type=Path)
    split_size = split.add_mutually_exclusive_group()
    split_size.add_argument("--profile", choices=sorted(QR_PROFILES), default=DEFAULT_PROFILE)
    split_size.add_argument("--chunk-bytes", type=int)

    inspect = subparsers.add_parser(
        "inspect", help="inspect a directory or line-separated frame file",
    )
    inspect.add_argument("source", type=Path)

    join = subparsers.add_parser(
        "join", help="reassemble and validate the original P4 Cart",
    )
    join.add_argument("source", type=Path)
    join.add_argument("output", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "estimate":
            summary = estimate_cart(arguments.cart)
            if arguments.json:
                print(json.dumps(summary, indent=2, sort_keys=True))
            else:
                _print_estimate(summary)
        elif arguments.command == "split":
            summary = write_frame_set(
                arguments.cart,
                arguments.output_directory,
                _chunk_bytes(arguments),
            )
            print(json.dumps(summary, indent=2, sort_keys=True))
        elif arguments.command == "inspect":
            print(json.dumps(inspect_frame_set(arguments.source), indent=2, sort_keys=True))
        elif arguments.command == "join":
            print(json.dumps(
                join_frame_set(arguments.source, arguments.output),
                indent=2,
                sort_keys=True,
            ))
        else:
            parser.error("unknown command")
    except (QrTransferError, p4cart.CartError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
