#!/usr/bin/env python3
"""Verify bounded application-flash readback chunks.

This helper deliberately has no device access.  The flash wrapper gives it files
produced by esptool; it validates one chunk against the matching source range or
hashes the complete canonical chunk sequence in numeric order.
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import sys
from typing import NoReturn


def fail(message: str) -> NoReturn:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def hash_stream(path: pathlib.Path, *, offset: int = 0, length: int | None = None) -> tuple[int, str]:
    digest = hashlib.sha256()
    total = 0
    with path.open("rb") as stream:
        stream.seek(offset)
        remaining = length
        while remaining is None or remaining:
            request = 1024 * 1024 if remaining is None else min(1024 * 1024, remaining)
            block = stream.read(request)
            if not block:
                break
            digest.update(block)
            total += len(block)
            if remaining is not None:
                remaining -= len(block)
    return total, digest.hexdigest()


def verify_chunk(args: argparse.Namespace) -> None:
    source = args.source.resolve()
    readback = args.readback.resolve()
    if args.source_offset < 0 or args.bytes <= 0:
        fail("chunk source offset must be non-negative and byte count must be positive")
    if not source.is_file():
        fail(f"chunk source is not a regular file: {source}")
    if not readback.is_file():
        fail(f"chunk readback is not a regular file: {readback}")
    source_bytes = source.stat().st_size
    if args.source_offset + args.bytes > source_bytes:
        fail("chunk source range extends beyond the application binary")
    actual_bytes = readback.stat().st_size
    if actual_bytes != args.bytes:
        fail(
            "chunk byte-count mismatch: "
            f"expected={args.bytes} actual={actual_bytes}"
        )
    expected_count, expected_hash = hash_stream(
        source, offset=args.source_offset, length=args.bytes
    )
    actual_count, actual_hash = hash_stream(readback)
    if expected_count != args.bytes or actual_count != args.bytes:
        fail("chunk changed size while it was being verified")
    if actual_hash != expected_hash:
        fail(
            "chunk SHA-256 mismatch: "
            f"expected={expected_hash} actual={actual_hash}"
        )
    print(actual_hash)


def verify_aggregate(args: argparse.Namespace) -> None:
    directory = args.directory.resolve()
    if args.chunks <= 0 or args.bytes <= 0 or args.chunk_bytes <= 0:
        fail("aggregate counts and sizes must be positive")
    expected_chunks = (args.bytes + args.chunk_bytes - 1) // args.chunk_bytes
    if args.chunks != expected_chunks:
        fail(
            "aggregate chunk-count mismatch: "
            f"expected={expected_chunks} actual={args.chunks}"
        )
    if not directory.is_dir():
        fail(f"chunk directory is missing: {directory}")

    expected_names = {f"chunk-{index:06d}.bin" for index in range(args.chunks)}
    actual_names = {entry.name for entry in directory.iterdir()}
    if actual_names != expected_names:
        missing = sorted(expected_names - actual_names)
        extra = sorted(actual_names - expected_names)
        fail(f"canonical chunk set mismatch: missing={missing} extra={extra}")

    digest = hashlib.sha256()
    total = 0
    for index in range(args.chunks):
        path = directory / f"chunk-{index:06d}.bin"
        expected_bytes = min(args.chunk_bytes, args.bytes - total)
        actual_bytes = path.stat().st_size
        if actual_bytes != expected_bytes:
            fail(
                f"canonical chunk {index} byte-count mismatch: "
                f"expected={expected_bytes} actual={actual_bytes}"
            )
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
                total += len(block)

    if total != args.bytes:
        fail(f"ordered readback byte-count mismatch: expected={args.bytes} actual={total}")
    print(f"{total} {digest.hexdigest()}")


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser()
    commands = root.add_subparsers(dest="command", required=True)

    chunk = commands.add_parser("chunk")
    chunk.add_argument("--source", type=pathlib.Path, required=True)
    chunk.add_argument("--source-offset", type=int, required=True)
    chunk.add_argument("--bytes", type=int, required=True)
    chunk.add_argument("--readback", type=pathlib.Path, required=True)
    chunk.set_defaults(handler=verify_chunk)

    aggregate = commands.add_parser("aggregate")
    aggregate.add_argument("--directory", type=pathlib.Path, required=True)
    aggregate.add_argument("--chunks", type=int, required=True)
    aggregate.add_argument("--bytes", type=int, required=True)
    aggregate.add_argument("--chunk-bytes", type=int, required=True)
    aggregate.set_defaults(handler=verify_aggregate)
    return root


def main() -> None:
    args = parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
