#!/usr/bin/env python3

"""Wrap one Console OS app image as a USB-deliverable .P4U update."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
import subprocess


HEADER_BYTES = 256
MAX_IMAGE_BYTES = 0x370000


def bounded(text: str, width: int, label: str) -> bytes:
    encoded = text.encode("ascii", "strict")
    if not encoded or len(encoded) >= width:
        raise ValueError(f"{label} must fit in {width - 1} ASCII bytes")
    return encoded + bytes(width - len(encoded))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=pathlib.Path, required=True)
    parser.add_argument("--version", required=True)
    build_source = parser.add_mutually_exclusive_group(required=True)
    build_source.add_argument("--build")
    build_source.add_argument("--source-root", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    build = args.build
    if args.source_root is not None:
        result = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=args.source_root.resolve(strict=True),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            detail = result.stderr.decode("utf-8", "replace")[:256].strip()
            raise ValueError(f"cannot resolve source build: {detail}")
        try:
            build = result.stdout.decode("ascii", "strict").strip()
        except UnicodeError as error:
            raise ValueError("source build is not ASCII") from error
    if build is None or len(build) != 12:
        raise ValueError("build must be exactly 12 ASCII hexadecimal digits")
    try:
        int(build, 16)
    except ValueError as error:
        raise ValueError(
            "build must be exactly 12 ASCII hexadecimal digits"
        ) from error
    image = args.image.read_bytes()
    if not image or len(image) > MAX_IMAGE_BYTES or image[0] != 0xE9:
        raise ValueError("input is not a bounded ESP-IDF app image")
    header = bytearray(HEADER_BYTES)
    header[0:8] = b"P4OSUP1\0"
    struct.pack_into(
        "<6I",
        header,
        8,
        HEADER_BYTES,
        HEADER_BYTES + len(image),
        HEADER_BYTES,
        len(image),
        1,
        0,
    )
    digest = hashlib.sha256(image).digest()
    header[32:64] = digest
    header[64:96] = bounded(args.version, 32, "version")
    header[96:160] = bounded(build, 64, "build")
    header[160:176] = bounded("esp32p4", 16, "target")
    package = bytes(header) + image
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_bytes(package)
    temporary.replace(args.output)
    print(json.dumps({
        "result": "p4-os-update-built",
        "bytes": len(package),
        "image_bytes": len(image),
        "image_sha256": digest.hex(),
        "package_sha256": hashlib.sha256(package).hexdigest(),
        "version": args.version,
        "build": build,
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
