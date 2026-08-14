#!/usr/bin/env python3

"""Bounded, fail-closed flash restoration for the gamepad diagnostic.

This module never opens a serial port and performs no work at import time.  A
caller must already exclusively own the programming UART.  The public restore
entry point performs exactly one download-mode reset on that same handle,
connects once without another reset, restores only the sealed install-block-
rounded application mutation span using exact 4 KiB packets, verifies it by
ordered bounded reads, and leaves the chip in the RAM stub/ROM loader.
Recovery uses this same path and has no ARM token API.
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import pathlib
import stat
import struct
import time
from typing import Any, Callable, Mapping, NamedTuple


PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_BYTES = 0xC00
APP_MUTATION_OFFSET = 0x10000
SECTOR_BYTES = 0x1000
MAX_READBACK_CHUNK_BYTES = 512 * 1024
FLASH_BYTES = 16 * 1024 * 1024
ESP32P4_IMAGE_CHIP_ID = 18
EXACT_CHIP_REVISION = 103
LEGACY_REV1_STUB_SHA256 = (
    "3c0f27938055192977123cd4b503bf27d1676c59a7fd7e3f93de8e95b0cf63bf"
)
ENTRY_MAGIC = b"\xaa\x50"
MD5_MAGIC_PREFIX = b"\xeb\xeb"
MD5_RECORD_PREFIX = MD5_MAGIC_PREFIX + b"\xff" * 14
ERASED_ENTRY = b"\xff" * 32


class RestoreError(RuntimeError):
    """The bounded restore contract was not satisfied."""


class RestoreRuntime(NamedTuple):
    """Dependency seam used by production and hermetic tests."""

    rom_class: Any
    reset_class: Any
    stub_factory: Callable[[Any, pathlib.Path], Any]
    binding: Mapping[str, Any]


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _regular_file_info(path: pathlib.Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as error:
        raise RestoreError(f"cannot stat {label}: {error}") from error
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise RestoreError(f"{label} is not a regular non-symlink file")
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o600:
        raise RestoreError(f"{label} must be owned by this user with exact mode 0600")
    return info


def _directory_info(path: pathlib.Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as error:
        raise RestoreError(f"cannot stat {label}: {error}") from error
    if not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise RestoreError(f"{label} is not a real directory")
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700:
        raise RestoreError(f"{label} must be owned by this user with exact mode 0700")
    return info


def _fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def sector_span(byte_count: int, sector_bytes: int = SECTOR_BYTES) -> int:
    """Return the exact erase-sector span covering ``byte_count`` bytes."""

    if (
        not isinstance(byte_count, int)
        or isinstance(byte_count, bool)
        or byte_count <= 0
        or not isinstance(sector_bytes, int)
        or isinstance(sector_bytes, bool)
        or sector_bytes <= 0
        or sector_bytes & (sector_bytes - 1)
    ):
        raise RestoreError("byte count and power-of-two sector size must be positive integers")
    return ((byte_count + sector_bytes - 1) // sector_bytes) * sector_bytes


def parse_partition_table(
    payload: bytes,
    mutation_offset: int = APP_MUTATION_OFFSET,
    mutation_bytes: int | None = None,
) -> dict[str, Any]:
    """Strictly parse a complete ESP-IDF 0xc00 partition table.

    The table must have one MD5 record immediately after its entries, followed
    only by erased bytes, and exactly one factory application partition.  When
    a mutation span is supplied, that factory partition must contain it.
    """

    if not isinstance(payload, bytes) or len(payload) != PARTITION_TABLE_BYTES:
        raise RestoreError("live partition table must contain exactly 0xc00 bytes")
    if (
        not isinstance(mutation_offset, int)
        or isinstance(mutation_offset, bool)
        or mutation_offset < 0
        or mutation_offset % SECTOR_BYTES
    ):
        raise RestoreError("mutation offset must be a non-negative sector boundary")
    if mutation_bytes is not None and (
        not isinstance(mutation_bytes, int)
        or isinstance(mutation_bytes, bool)
        or mutation_bytes <= 0
        or mutation_bytes % SECTOR_BYTES
    ):
        raise RestoreError("mutation span must be a positive sector multiple")

    entries: list[dict[str, Any]] = []
    md5_offset: int | None = None
    md5_digest = b""
    for offset in range(0, PARTITION_TABLE_BYTES, 32):
        record = payload[offset : offset + 32]
        if record == ERASED_ENTRY:
            raise RestoreError("partition table reached erased tail before its MD5 record")
        if record[:2] == MD5_MAGIC_PREFIX:
            if record[:16] != MD5_RECORD_PREFIX:
                raise RestoreError("partition table MD5 record has noncanonical reserved bytes")
            md5_offset = offset
            md5_digest = record[16:32]
            break
        if record[:2] != ENTRY_MAGIC:
            raise RestoreError(f"partition entry at 0x{offset:x} has invalid magic")
        magic, ptype, subtype, part_offset, part_size, raw_name, flags = struct.unpack(
            "<2sBBII16sI", record
        )
        if magic != ENTRY_MAGIC or part_size <= 0:
            raise RestoreError(f"partition entry at 0x{offset:x} is invalid")
        nul = raw_name.find(b"\x00")
        name_bytes = raw_name if nul < 0 else raw_name[:nul]
        if not name_bytes or (nul >= 0 and any(raw_name[nul + 1 :])):
            raise RestoreError(f"partition entry at 0x{offset:x} has a noncanonical name")
        try:
            name = name_bytes.decode("ascii")
        except UnicodeDecodeError as error:
            raise RestoreError(f"partition entry at 0x{offset:x} has a non-ASCII name") from error
        if any(ord(char) < 0x21 or ord(char) > 0x7E for char in name):
            raise RestoreError(f"partition entry at 0x{offset:x} has an unsafe name")
        if part_offset % SECTOR_BYTES or part_size % SECTOR_BYTES:
            raise RestoreError(f"partition {name!r} is not sector aligned")
        end = part_offset + part_size
        if end <= part_offset or end > FLASH_BYTES:
            raise RestoreError(f"partition {name!r} exceeds the bound 16 MiB flash")
        entries.append(
            {
                "name": name,
                "type": ptype,
                "subtype": subtype,
                "offset": part_offset,
                "size": part_size,
                "flags": flags,
            }
        )
    if md5_offset is None:
        raise RestoreError("partition table has no MD5 record")
    if md5_digest != hashlib.md5(payload[:md5_offset]).digest():
        raise RestoreError("partition table MD5 does not match its entries")
    if payload[md5_offset + 32 :] != b"\xff" * (
        PARTITION_TABLE_BYTES - md5_offset - 32
    ):
        raise RestoreError("partition table has data after its MD5 record")
    if not entries:
        raise RestoreError("partition table has no entries")
    if len({entry["name"] for entry in entries}) != len(entries):
        raise RestoreError("partition names are not unique")
    ordered = sorted(entries, key=lambda entry: entry["offset"])
    if any(
        previous["offset"] + previous["size"] > current["offset"]
        for previous, current in zip(ordered, ordered[1:])
    ):
        raise RestoreError("partition ranges overlap")
    factories = [
        entry for entry in entries if entry["type"] == 0x00 and entry["subtype"] == 0x00
    ]
    if len(factories) != 1:
        raise RestoreError("live table must contain exactly one factory application partition")
    factory = factories[0]
    if factory["offset"] != APP_MUTATION_OFFSET:
        raise RestoreError("factory application offset is not the exact 0x10000 boundary")
    if mutation_bytes is not None:
        mutation_end = mutation_offset + mutation_bytes
        if mutation_end <= mutation_offset or not (
            factory["offset"] <= mutation_offset
            and mutation_end <= factory["offset"] + factory["size"]
        ):
            raise RestoreError("factory application does not contain the mutation span")
    return {
        "offset": PARTITION_TABLE_OFFSET,
        "bytes": PARTITION_TABLE_BYTES,
        "sha256": _sha256_bytes(payload),
        "validated_md5": True,
        "md5_record_offset": md5_offset,
        "md5_sha256": _sha256_bytes(md5_digest),
        "entry_count": len(entries),
        "factory": dict(factory),
    }


def parse_partition_table_file(
    path: pathlib.Path,
    mutation_offset: int = APP_MUTATION_OFFSET,
    mutation_bytes: int | None = None,
) -> dict[str, Any]:
    """Parse a private table file through a no-follow descriptor."""

    path = pathlib.Path(path)
    expected = _regular_file_info(path, "live partition table")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            opened.st_dev != expected.st_dev
            or opened.st_ino != expected.st_ino
            or opened.st_size != PARTITION_TABLE_BYTES
        ):
            raise RestoreError("live partition table changed while opening")
        payload = b""
        while len(payload) < PARTITION_TABLE_BYTES:
            chunk = os.read(descriptor, PARTITION_TABLE_BYTES - len(payload))
            if not chunk:
                break
            payload += chunk
        if len(payload) != PARTITION_TABLE_BYTES or os.read(descriptor, 1):
            raise RestoreError("live partition table length changed while reading")
    finally:
        os.close(descriptor)
    return parse_partition_table(payload, mutation_offset, mutation_bytes)


def file_binding(path: pathlib.Path, expected_bytes: int, label: str) -> dict[str, Any]:
    """Return an identity-and-content binding for an exact private file."""

    path = pathlib.Path(path)
    info = _regular_file_info(path, label)
    if info.st_size != expected_bytes:
        raise RestoreError(f"{label} has the wrong byte count")
    return {
        "path": str(path.resolve(strict=True)),
        "device": info.st_dev,
        "inode": info.st_ino,
        "mode": stat.S_IMODE(info.st_mode),
        "bytes": info.st_size,
        "sha256": _sha256_file(path),
    }


def validate_binding(binding: Mapping[str, Any], label: str) -> pathlib.Path:
    """Revalidate every stored identity/content attribute of a file binding."""

    if not isinstance(binding, Mapping):
        raise RestoreError(f"{label} binding is missing")
    try:
        path = pathlib.Path(str(binding["path"]))
        expected_bytes = int(binding["bytes"])
    except (KeyError, TypeError, ValueError) as error:
        raise RestoreError(f"{label} binding is malformed") from error
    actual = file_binding(path, expected_bytes, label)
    expected = {
        key: binding.get(key)
        for key in ("path", "device", "inode", "mode", "bytes", "sha256")
    }
    if actual != expected:
        raise RestoreError(f"{label} no longer matches its sealed binding")
    return path


def seal_snapshot(
    directory: pathlib.Path,
    filename: str,
    payload: bytes,
    *,
    expected_bytes: int,
    label: str = "restore preimage",
) -> dict[str, Any]:
    """O_EXCL-create and durably seal a snapshot in an exact 0700 directory."""

    directory = pathlib.Path(directory)
    _directory_info(directory, "recovery directory")
    if pathlib.PurePath(filename).name != filename or filename in {"", ".", ".."}:
        raise RestoreError("snapshot filename must be one safe direct child name")
    if not isinstance(payload, bytes) or len(payload) != expected_bytes:
        raise RestoreError("snapshot payload has the wrong byte count")
    path = directory / filename
    try:
        descriptor = os.open(
            path,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
            0o600,
        )
    except FileExistsError as error:
        raise RestoreError("refusing to overwrite an existing snapshot") from error
    try:
        view = memoryview(payload)
        written = 0
        while written < len(view):
            count = os.write(descriptor, view[written:])
            if count <= 0:
                raise RestoreError("snapshot write made no progress")
            written += count
        os.fsync(descriptor)
        opened = os.fstat(descriptor)
        if stat.S_IMODE(opened.st_mode) != 0o600 or opened.st_size != expected_bytes:
            raise RestoreError("sealed snapshot metadata is not exact")
    except BaseException:
        try:
            os.close(descriptor)
        finally:
            path.unlink(missing_ok=True)
            _fsync_directory(directory)
        raise
    else:
        os.close(descriptor)
    _fsync_directory(directory)
    return file_binding(path, expected_bytes, label)


def _legacy_stub_path() -> pathlib.Path:
    try:
        import esptool.loader
    except ImportError as error:
        raise RestoreError("activate the pinned ESP-IDF/esptool environment") from error
    return (
        pathlib.Path(esptool.loader.__file__).resolve(strict=True).parent
        / "targets"
        / "stub_flasher"
        / "1"
        / "esp32p4-rev1.json"
    )


def pinned_restore_runtime_binding() -> dict[str, Any]:
    """Bind the exact esptool modules and selected P4-v1 RAM stub bytes."""

    try:
        import esptool
        import esptool.loader
        import esptool.reset
        import esptool.targets.esp32
        import esptool.targets.esp32p4
    except ImportError as error:
        raise RestoreError("activate the pinned ESP-IDF/esptool environment") from error
    if getattr(esptool, "__version__", None) != "4.12.0":
        raise RestoreError("esptool version differs from the pinned 4.12.0 runtime")
    if esptool.loader.cfg.get("custom_hard_reset_sequence") is not None:
        raise RestoreError("custom hard-reset sequences are prohibited")
    stub = _legacy_stub_path()
    if not stub.is_file() or _sha256_file(stub) != LEGACY_REV1_STUB_SHA256:
        raise RestoreError("legacy ESP32-P4 rev1 stub JSON differs from the pinned bytes")

    def entry(module: Any) -> dict[str, Any]:
        path = pathlib.Path(module.__file__).resolve(strict=True)
        return {"path": str(path), "sha256": _sha256_file(path)}

    return {
        "esptool_version": "4.12.0",
        "loader": entry(esptool.loader),
        "reset": entry(esptool.reset),
        "esp32p4": entry(esptool.targets.esp32p4),
        "esp32": entry(esptool.targets.esp32),
        "legacy_rev1_stub": {
            "path": str(stub),
            "sha256": LEGACY_REV1_STUB_SHA256,
        },
    }


def _load_legacy_stub(_rom: Any, path: pathlib.Path) -> Any:
    """Load only the already hash-verified legacy rev1 stub JSON."""

    class StubImage:
        pass

    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        stub = StubImage()
        stub.text = base64.b64decode(value["text"], validate=True)
        stub.text_start = int(value["text_start"])
        stub.entry = int(value["entry"])
        stub.data = base64.b64decode(value["data"], validate=True)
        stub.data_start = int(value["data_start"])
        stub.bss_start = int(value["bss_start"])
    except (OSError, UnicodeError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise RestoreError("cannot parse pinned legacy ESP32-P4 rev1 stub") from error
    return stub


def _production_runtime() -> RestoreRuntime:
    try:
        from esptool.reset import UnixTightReset
        from esptool.targets.esp32p4 import ESP32P4ROM
    except ImportError as error:
        raise RestoreError("activate the pinned ESP-IDF/esptool environment") from error
    binding = pinned_restore_runtime_binding()
    return RestoreRuntime(
        rom_class=ESP32P4ROM,
        reset_class=UnixTightReset,
        stub_factory=_load_legacy_stub,
        binding=binding,
    )


def validate_partition_binding(
    binding: Mapping[str, Any],
    *,
    expected_offset: int = APP_MUTATION_OFFSET,
    expected_span: int,
    runtime: RestoreRuntime | None = None,
) -> pathlib.Path:
    """Re-read/reparse a bound table and match every recorded parsed field."""

    path = validate_binding(binding, "live partition table")
    parsed = parse_partition_table_file(path, expected_offset, expected_span)
    for key in (
        "offset",
        "bytes",
        "sha256",
        "validated_md5",
        "md5_record_offset",
        "md5_sha256",
        "entry_count",
        "factory",
    ):
        if binding.get(key) != parsed[key]:
            raise RestoreError(f"live partition table parsed binding changed: {key}")
    selected = runtime or _production_runtime()
    if binding.get("restore_runtime") != dict(selected.binding):
        raise RestoreError("restore runtime/stub binding changed")
    current_tool = pathlib.Path(__file__).resolve(strict=True)
    if binding.get("restore_tool") != {
        "path": str(current_tool),
        "sha256": _sha256_file(current_tool),
    }:
        raise RestoreError("restore helper path or bytes changed")
    return path


def _read_bound_payload(binding: Mapping[str, Any], label: str) -> tuple[pathlib.Path, bytes]:
    path = validate_binding(binding, label)
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        info = os.fstat(descriptor)
        if info.st_dev != binding.get("device") or info.st_ino != binding.get("inode"):
            raise RestoreError(f"{label} changed while opening")
        expected = int(binding["bytes"])
        chunks: list[bytes] = []
        remaining = expected
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise RestoreError(f"{label} was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise RestoreError(f"{label} grew while reading")
        payload = b"".join(chunks)
    finally:
        os.close(descriptor)
    if _sha256_bytes(payload) != binding.get("sha256"):
        raise RestoreError(f"{label} content changed while reading")
    return path, payload


def _device_identity_sha256(esp: Any) -> str:
    mac = esp.read_mac()
    if not isinstance(mac, tuple) or len(mac) != 6 or any(
        not isinstance(byte, int) or isinstance(byte, bool) or not 0 <= byte <= 0xFF
        for byte in mac
    ):
        raise RestoreError("live base identity has invalid shape")
    normalized = "".join(f"{byte:02x}" for byte in mac).encode("ascii")
    return _sha256_bytes(normalized)


def _validate_live_rom(esp: Any, device: Any, expected_device_sha256: str) -> None:
    if esp._port is not device or esp.uses_usb_otg():
        raise RestoreError("restore did not retain the exclusive UART handle")
    if getattr(esp, "CHIP_NAME", None) != "ESP32-P4" or getattr(
        esp, "IMAGE_CHIP_ID", None
    ) != ESP32P4_IMAGE_CHIP_ID:
        raise RestoreError("restore target is not ESP32-P4")
    revision = esp.get_chip_revision()
    if revision != EXACT_CHIP_REVISION:
        raise RestoreError("live ESP32-P4 is not the exact authorized revision 103/v1.3")
    if _device_identity_sha256(esp) != expected_device_sha256:
        raise RestoreError("live board differs from the sealed device identity")
    security = esp.get_security_info(cache=False)
    flags = security.get("parsed_flags") if isinstance(security, dict) else None
    if not isinstance(flags, dict) or flags.get("SECURE_BOOT_EN") is not False:
        raise RestoreError("secure boot is enabled or its state is ambiguous")
    flash_count = security.get("flash_crypt_cnt")
    if not isinstance(flash_count, int) or bin(flash_count).count("1") & 1:
        raise RestoreError("flash encryption is enabled or its state is ambiguous")
    if esp.get_secure_boot_enabled() or esp.get_flash_encryption_enabled():
        raise RestoreError("live eFuse security state prohibits plaintext restoration")


def _set_controls_false(device: Any) -> None:
    device.dtr = False
    device.rts = False
    if bool(device.dtr) or bool(device.rts):
        raise RestoreError("UART DTR/RTS did not return to inactive")


def _xmc_flash_id_strict(esp: Any) -> bool:
    flash_id = esp.flash_id()
    if not isinstance(flash_id, int) or isinstance(flash_id, bool):
        raise RestoreError("flash ID has invalid shape")
    rdid = ((flash_id & 0xFF) << 16) | ((flash_id >> 16) & 0xFF) | (
        flash_id & 0xFF00
    )
    vendor_id = (rdid >> 16) & 0xFF
    mfid = (rdid >> 8) & 0xFF
    cpid = rdid & 0xFF
    return vendor_id == 0x20 and (
        (mfid == 0x40 and 0x13 <= cpid <= 0x20)
        or (mfid == 0x41 and 0x17 <= cpid <= 0x20)
        or (mfid == 0x50 and 0x15 <= cpid <= 0x16)
    )


def _fresh_flash_id(esp: Any) -> int:
    cache = getattr(esp, "cache", None)
    if not isinstance(cache, dict) or "flash_id" not in cache:
        raise RestoreError("esptool flash-ID cache shape changed")
    cache["flash_id"] = None
    value = esp.flash_id()
    if cache.get("flash_id") != value:
        raise RestoreError("esptool did not cache the freshly read flash ID")
    return value


def _prepare_flash_after_stub(esp: Any) -> int:
    """Fail-closed form of pinned esptool 4.12 post-stub flash setup."""

    flash_id = _fresh_flash_id(esp)
    if not isinstance(flash_id, int) or isinstance(flash_id, bool) or flash_id in {
        0xFFFFFF,
        0x000000,
        0xFFFF3F,
    }:
        raise RestoreError("unable to verify the live flash chip connection")

    if not _xmc_flash_id_strict(esp):
        mf_id = esp.read_spiflash_sfdp(0x10, 8)
        if not isinstance(mf_id, int) or isinstance(mf_id, bool):
            raise RestoreError("flash SFDP manufacturer ID has invalid shape")
        if mf_id == 0x20:
            for command in (0xB9, 0x79, 0xFF):
                esp.run_spiflash_command(command)
            time.sleep(0.002)
            esp.run_spiflash_command(0xAB)
            time.sleep(0.00002)
            # Unlike the CLI's warning-only path, restoration must fail closed.
            _fresh_flash_id(esp)
            if not _xmc_flash_id_strict(esp):
                raise RestoreError("XMC flash startup flow did not produce a valid ID")

    # Clear application-residual flash state exactly as pinned esptool does.
    esp.run_spiflash_command(0x66)
    esp.run_spiflash_command(0x99)
    time.sleep(0.001)
    # A fresh ID after reset proves communication and the exact 16 MiB density.
    final_id = _fresh_flash_id(esp)
    if not isinstance(final_id, int) or (final_id >> 16) not in {0x18, 0x38}:
        raise RestoreError("live flash ID does not prove the exact 16 MiB density")
    esp.flash_set_parameters(FLASH_BYTES)
    return final_id


def _write_exact_span(esp: Any, offset: int, payload: bytes) -> None:
    if offset % SECTOR_BYTES or len(payload) % SECTOR_BYTES:
        raise RestoreError("restore write is not sector aligned")
    write_size = getattr(esp, "FLASH_WRITE_SIZE", None)
    if write_size != SECTOR_BYTES:
        raise RestoreError("stub restore write size is not the exact 4 KiB sector")
    blocks = esp.flash_begin(len(payload), offset, encrypted_write=False)
    expected_blocks = (len(payload) + write_size - 1) // write_size
    if blocks != expected_blocks:
        raise RestoreError("stub returned an unexpected flash block count")
    for sequence in range(expected_blocks):
        block = payload[sequence * write_size : (sequence + 1) * write_size]
        if len(block) < write_size:
            block += b"\xff" * (write_size - len(block))
        esp.flash_block(block, sequence, encrypted=False)
    # reboot=False is the exact no-reset/no-run completion semantic.
    esp.flash_finish(reboot=False)


def _configure_exact_restore_write_size(stub: Any) -> None:
    """Use sector-sized stub packets so no padded block crosses the sealed span."""

    maximum = getattr(stub, "FLASH_WRITE_SIZE", None)
    if (
        not isinstance(maximum, int)
        or isinstance(maximum, bool)
        or maximum < SECTOR_BYTES
        or maximum % SECTOR_BYTES
    ):
        raise RestoreError("stub does not support exact sector-sized restore writes")
    stub.FLASH_WRITE_SIZE = SECTOR_BYTES
    if stub.FLASH_WRITE_SIZE != SECTOR_BYTES:
        raise RestoreError("stub refused the exact sector-sized restore write setting")


def _readback_exact(
    esp: Any, offset: int, byte_count: int, chunk_bytes: int
) -> tuple[str, int]:
    digest = hashlib.sha256()
    chunks = 0
    for relative in range(0, byte_count, chunk_bytes):
        count = min(chunk_bytes, byte_count - relative)
        payload = esp.read_flash(offset + relative, count, progress_fn=None)
        if not isinstance(payload, bytes) or len(payload) != count:
            raise RestoreError("ordered restore readback returned the wrong byte count")
        digest.update(payload)
        chunks += 1
    return digest.hexdigest(), chunks


def restore_same_handle(
    device: Any,
    preimage_binding: Mapping[str, Any],
    partition_table_binding: Mapping[str, Any],
    expected_device_sha256: str,
    *,
    runtime: RestoreRuntime | None = None,
    mark_write_attempt: Callable[[], None] | None = None,
    readback_chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> dict[str, Any]:
    """Restore/verify a sealed span through one already-owned UART handle.

    The caller must durably record restore entry before this call.  If supplied,
    ``mark_write_attempt`` is called exactly once after reset/connect and all
    immutable/live checks, but immediately before the first erase/write command.
    Recovery supplies the same callback and never supplies an ARM secret.
    """

    if not getattr(device, "is_open", False):
        raise RestoreError("same-handle restore requires an open UART")
    if bool(device.dtr) or bool(device.rts):
        raise RestoreError("UART controls must be inactive before restore reset")
    if not isinstance(expected_device_sha256, str) or len(expected_device_sha256) != 64:
        raise RestoreError("expected device identity digest is invalid")
    try:
        bytes.fromhex(expected_device_sha256)
    except ValueError as error:
        raise RestoreError("expected device identity digest is invalid") from error
    if not isinstance(readback_chunk_bytes, int) or isinstance(
        readback_chunk_bytes, bool
    ) or not 1 <= readback_chunk_bytes <= MAX_READBACK_CHUNK_BYTES:
        raise RestoreError("readback chunk size is outside 1..512 KiB")

    selected = runtime or _production_runtime()
    preimage_path, preimage = _read_bound_payload(preimage_binding, "restore preimage")
    try:
        offset = int(preimage_binding.get("offset"))
        byte_count = int(preimage_binding.get("bytes"))
        recorded_span = int(preimage_binding.get("mutation_span_bytes"))
        recorded_sector = int(preimage_binding.get("sector_bytes"))
        install_write_block = int(preimage_binding.get("install_write_block_bytes"))
    except (TypeError, ValueError) as error:
        raise RestoreError("restore preimage geometry is malformed") from error
    if not (
        offset == APP_MUTATION_OFFSET
        and byte_count == len(preimage)
        and recorded_span == byte_count
        and recorded_sector == SECTOR_BYTES
        and install_write_block == 0x4000
        and byte_count == sector_span(byte_count)
        and byte_count % install_write_block == 0
    ):
        raise RestoreError(
            "restore preimage is not the exact install-block-rounded mutation span"
        )
    validate_partition_binding(
        partition_table_binding,
        expected_offset=offset,
        expected_span=byte_count,
        runtime=selected,
    )

    # Call reset(), not __call__(): ResetStrategy.__call__ retries up to three
    # times, whereas this recovery contract allows one exact reset attempt.
    reset = selected.reset_class(device)
    result: dict[str, Any] | None = None
    try:
        reset.reset()
        _set_controls_false(device)

        esp = selected.rom_class(device, 115200, False)
        esp.connect("no_reset", attempts=1, warnings=False)
        _validate_live_rom(esp, device, expected_device_sha256)
        stub_path = pathlib.Path(str(selected.binding["legacy_rev1_stub"]["path"]))
        if _sha256_file(stub_path) != LEGACY_REV1_STUB_SHA256:
            raise RestoreError("pinned legacy rev1 stub changed before upload")
        stub = esp.run_stub(selected.stub_factory(esp, stub_path))
        if stub._port is not device or not getattr(stub, "IS_STUB", False):
            raise RestoreError("RAM stub did not retain the same UART handle")
        _configure_exact_restore_write_size(stub)
        flash_id = _prepare_flash_after_stub(stub)
        if mark_write_attempt is not None:
            mark_write_attempt()
        _write_exact_span(stub, offset, preimage)
        readback_sha256, readback_chunks = _readback_exact(
            stub, offset, byte_count, readback_chunk_bytes
        )
        if readback_sha256 != preimage_binding.get("sha256"):
            raise RestoreError("restore readback differs from the sealed preimage")
        if not getattr(device, "is_open", False):
            raise RestoreError("restore unexpectedly closed the caller-owned UART")
        # Keep the path named in the result only as a binding aid; never include
        # payload bytes or any ARM material.
        result = {
            "offset": offset,
            "bytes": byte_count,
            "readback_sha256": readback_sha256,
            "readback_chunks": readback_chunks,
            "readback_max_chunk_bytes": readback_chunk_bytes,
            "preimage_path": str(preimage_path),
            "same_uart_handle": True,
            "download_reset_count": 1,
            "connect_no_reset_attempts": 1,
            "write_after_action": "no_reset",
            "post_restore_reset_count": 0,
            "controls_inactive": True,
            "chip_revision": EXACT_CHIP_REVISION,
            "flash_bytes": FLASH_BYTES,
            "flash_id": f"0x{flash_id:06x}",
            "post_stub_flash_preparation": "verified",
            "restore_write_block_bytes": SECTOR_BYTES,
            "arm_transmitted_bytes": 0,
        }
    finally:
        _set_controls_false(device)
    if result is None:  # defensive: every non-exceptional path sets a result
        raise RestoreError("restore ended without a verified result")
    return result
