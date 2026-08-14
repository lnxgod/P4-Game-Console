#!/usr/bin/env python3
"""Make ESP-IDF's generated WL/FAT game-data image reproducible."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct


WL_SECTOR_BYTES = 4096
WL_STATE_HEADER_BYTES = 64
WL_STATE_RECORD_BYTES = 16
WL_STATE_COPIES = 2
FAT_VOLUME_ID = 0x60B22589
FAT_VOLUME_LABEL = b"P4 GAMES   "
WL_DEVICE_ID = 0x1D0844CC


def fail(message: str) -> None:
    raise SystemExit(f"game-storage normalization failed: {message}")


def normalized_image(source: bytes) -> bytes:
    if len(source) == 0 or len(source) % WL_SECTOR_BYTES != 0:
        fail("image size is not a nonzero multiple of the WL sector")

    image = bytearray(source)
    total_sectors = len(image) // WL_SECTOR_BYTES
    state_bytes = WL_STATE_HEADER_BYTES + WL_STATE_RECORD_BYTES * total_sectors
    state_sectors = (state_bytes + WL_SECTOR_BYTES - 1) // WL_SECTOR_BYTES
    metadata_sectors = 2 + WL_STATE_COPIES * state_sectors
    if total_sectors <= metadata_sectors:
        fail("image is too small for its WL metadata")
    plain_fat_sectors = total_sectors - metadata_sectors

    config_offset = len(image) - WL_SECTOR_BYTES
    config = struct.unpack_from("<8I", image, config_offset)
    if config[0] != 0 or config[1] != len(image) or config[2] != 4096 or \
            config[3] != 4096:
        fail("unexpected WL configuration geometry")

    boot_offset = WL_SECTOR_BYTES
    fat_sector_bytes = struct.unpack_from("<H", image, boot_offset + 11)[0]
    if fat_sector_bytes not in (512, 1024, 2048, 4096) or \
            image[boot_offset + 510:boot_offset + 512] != b"\x55\xaa":
        fail("invalid FAT boot sector")
    struct.pack_into("<I", image, boot_offset + 39, FAT_VOLUME_ID)
    image[boot_offset + 43:boot_offset + 54] = FAT_VOLUME_LABEL

    first_state = (1 + plain_fat_sectors) * WL_SECTOR_BYTES
    for copy in range(WL_STATE_COPIES):
        state_offset = first_state + copy * state_sectors * WL_SECTOR_BYTES
        state = struct.unpack_from("<8I", image, state_offset)
        if state[1] != plain_fat_sectors + 1 or \
                state[5] != WL_SECTOR_BYTES:
            fail("unexpected WL state geometry")
        struct.pack_into("<I", image, state_offset + 28, WL_DEVICE_ID)
        crc = binascii.crc32(
            image[state_offset:state_offset + 60], 0xFFFFFFFF) & 0xFFFFFFFF
        struct.pack_into("<I", image, state_offset + 60, crc)

    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        original = args.image.read_bytes()
    except OSError as error:
        fail(str(error))
    normalized = normalized_image(original)
    if args.check:
        if original != normalized:
            fail("image contains nondeterministic volume metadata")
        return
    try:
        args.image.write_bytes(normalized)
    except OSError as error:
        fail(str(error))


if __name__ == "__main__":
    main()
