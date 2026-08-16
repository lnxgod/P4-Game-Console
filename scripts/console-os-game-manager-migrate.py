#!/usr/bin/env python3

"""Exact-unit, no-new-backup migration to dual-OTA Game Manager Console OS.

This one-time J1 transaction changes only the bootloader, partition table,
OTA selection data, and OTA0. The existing 0x710000 P4 GAMES partition is
never a write target. The installed predecessor is retained only in memory so
an ordinary in-process failure can restore it; a durable ledger can instead
resume the committed target after a host/process interruption.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import signal
import stat
import struct
import subprocess
import sys
import types
from typing import Any, Mapping, NamedTuple


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
ROOT = SCRIPT_PATH.parent.parent.resolve(strict=True)
FROZEN_UPDATER_PATH = ROOT / "scripts/console-os-program-manager-usb-install.py"
CAPTURE_PATH = ROOT / "scripts/capture-console-os-game-manager-runtime.py"
VERIFY_PATH = ROOT / "scripts/verify-console-os.py"
BACKUP_MANIFEST_PATH = ROOT / "hardware/backups/manifest.json"
AUTHORIZATION_PATH = (
    ROOT / "hardware/evidence/"
    "console-os-game-manager-dual-ota-exact-unit-authorization.json"
)

EXPECTED_FROZEN_UPDATER_SHA256 = (
    "603a97b5297ae1afe9cb43717ead019408cad465e54fcc80bc37797c668ce39a"
)
EXPECTED_BACKUP_MANIFEST_SHA256 = (
    "a4ffc0ed1ff2bd2717b15b53fc21c8c8792a9869309d7294f9c6de4d83fa2b3f"
)
EXPECTED_AUTHORIZATION_SHA256 = (
    "0c2d5e91d5b665743685427f34eb0f6f8b740e08b65078de68f54cb71cd9eed9"
)
EXPECTED_EXISTING_BACKUP_SHA256 = (
    "c567d0a4d960156b59216e4c424d78856e7b8dbe52e05810d47a69a3e58f4217"
)
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)

EXPECTED_PREDECESSOR_TABLE_SPAN_SHA256 = (
    "a10833df35f52d59601ef03d7250330c59b65a348597aeb603085b6e2d94b88d"
)
EXPECTED_PREDECESSOR_APP_BYTES = 5_070_112
EXPECTED_PREDECESSOR_APP_SHA256 = (
    "ac187a9906109c8b3a47cff91a222e33fb2e36285abeae7f15aa465535604693"
)
EXPECTED_PREDECESSOR_APP_SPAN_SHA256 = (
    "59a158418e3bb26a44e0c1126f371b73c186d2fcfaba9623b4ed04fd47291861"
)

FLASH_BYTES = 16 * 1024 * 1024
BOOTLOADER_OFFSET = 0x2000
BOOTLOADER_SPAN_BYTES = 0x6000
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_PAYLOAD_BYTES = 0xC00
PARTITION_TABLE_SPAN_BYTES = 0x1000
NVS_OFFSET = 0x9000
NVS_PHY_SPAN_BYTES = 0x7000
OTA_DATA_OFFSET = 0x10000
OTA_DATA_SPAN_BYTES = 0x2000
OTA0_OFFSET = 0x20000
OTA0_SPAN_BYTES = 0x370000
OTA1_OFFSET = 0x390000
OTA1_SPAN_BYTES = 0x380000
PREDECESSOR_APP_OFFSET = 0x10000
PREDECESSOR_APP_SPAN_BYTES = 0x700000
GAME_DATA_OFFSET = 0x710000
GAME_DATA_SPAN_BYTES = 0x8F0000
SECTOR_BYTES = 0x1000
WRITE_BLOCK_BYTES = 0x4000
READBACK_CHUNK_BYTES = 512 * 1024
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024
LEDGER_NAME = "game-manager-dual-ota-migration-ledger.json"
CAPTURE_RAW_NAME = "game-manager-dual-ota-startup.raw"
CAPTURE_SUMMARY_NAME = "game-manager-dual-ota-startup.json"


class MigrationError(RuntimeError):
    """The exact-unit dual-OTA migration contract was not met."""


class Target(NamedTuple):
    name: str
    path: pathlib.Path
    offset: int
    payload: bytes
    span: bytes
    block_bytes: int


class Predecessor(NamedTuple):
    bootloader: bytes
    partition_table: bytes
    application: bytes
    nvs_phy: bytes
    game_data_header: bytes
    ota1_first_sector: bytes
    ota1_last_sector: bytes


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_regular(
    path: pathlib.Path, label: str, *, maximum: int,
    required_mode: int | None = None,
) -> bytes:
    try:
        before = path.lstat()
    except OSError as error:
        raise MigrationError(f"cannot stat {label}: {error}") from error
    mode = stat.S_IMODE(before.st_mode)
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or before.st_size <= 0
        or before.st_size > maximum
        or (required_mode is not None and mode != required_mode)
        or (required_mode is None and mode & 0o022)
    ):
        raise MigrationError(f"{label} metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (before.st_dev, before.st_ino, before.st_size)
            or stat.S_IMODE(opened.st_mode) != mode
        ):
            raise MigrationError(f"{label} changed while opening")
        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise MigrationError(f"{label} was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise MigrationError(f"{label} grew while reading")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size)
            != (opened.st_dev, opened.st_ino, opened.st_size)
        ):
            raise MigrationError(f"{label} changed while reading")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _load_module_bytes(path: pathlib.Path, payload: bytes, name: str) -> Any:
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    try:
        exec(compile(payload, str(path), "exec"), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


def _load_transport() -> Any:
    payload = _read_regular(
        FROZEN_UPDATER_PATH, "frozen app updater",
        maximum=MAX_SOURCE_BYTES, required_mode=0o755,
    )
    if _sha256(payload) != EXPECTED_FROZEN_UPDATER_SHA256:
        raise MigrationError("frozen app updater bytes changed")
    updater = _load_module_bytes(
        FROZEN_UPDATER_PATH, payload,
        "console_game_manager_frozen_transport",
    )
    return updater.TRANSPORT


def _git(*args: str) -> bytes:
    result = subprocess.run(
        ["git", *args], cwd=ROOT, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace")[:512].strip()
        raise MigrationError(f"git {' '.join(args)} failed: {detail}")
    return result.stdout


def _source_commit() -> str:
    if _git("status", "--porcelain", "--untracked-files=no").strip():
        raise MigrationError("tracked source must be clean before migration")
    commit = _git("rev-parse", "HEAD").decode("ascii", "strict").strip()
    if len(commit) != 40:
        raise MigrationError("source commit has an invalid shape")
    return commit


def _load_committed_capture(commit: str) -> tuple[Any, str]:
    payload = _read_regular(
        CAPTURE_PATH, "Game Manager capture analyzer",
        maximum=MAX_SOURCE_BYTES,
    )
    relative = str(CAPTURE_PATH.relative_to(ROOT))
    committed = _git("show", f"{commit}:{relative}")
    if payload != committed:
        raise MigrationError("capture analyzer differs from committed source")
    return (
        _load_module_bytes(
            CAPTURE_PATH, payload, "console_game_manager_runtime_capture"
        ),
        _sha256(payload),
    )


def _validate_authorization() -> str:
    payload = _read_regular(
        AUTHORIZATION_PATH, "dual-OTA exact-unit authorization",
        maximum=MAX_JSON_BYTES,
    )
    digest = _sha256(payload)
    if digest != EXPECTED_AUTHORIZATION_SHA256:
        raise MigrationError("dual-OTA authorization changed")
    try:
        authorization = json.loads(payload.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise MigrationError("dual-OTA authorization is invalid") from error
    device = authorization.get("device_binding", {})
    contract = authorization.get("execution_contract", {})
    boundaries = authorization.get("authorization_boundaries", {})
    if not (
        authorization.get("schema") == 1
        and authorization.get("active") is True
        and authorization.get("scope")
            == "exact-unit-console-os-game-manager-dual-ota-migration-v1"
        and device.get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and device.get("chip") == "ESP32-P4"
        and device.get("chip_revision") == "v1.3"
        and device.get("flash_bytes") == FLASH_BYTES
        and contract.get("new_backup_created") is False
        and contract.get("never_write_range")
            == "game_data 0x710000..0xffffff"
        and contract.get("write_ranges") == [
            "0x2000..0x7fff",
            "0x8000..0x8fff",
            "0x10000..0x11fff",
            "0x20000..0x38ffff",
        ]
        and boundaries.get("exact_one_time_migration_only") is True
        and boundaries.get("game_data_firmware_seed_write_authorized") is False
        and boundaries.get("new_flash_backup_authorized") is False
    ):
        raise MigrationError("dual-OTA authorization scope differs")
    return digest


def _validate_existing_backup() -> dict[str, Any]:
    payload = _read_regular(
        BACKUP_MANIFEST_PATH, "factory backup manifest",
        maximum=MAX_JSON_BYTES,
    )
    if _sha256(payload) != EXPECTED_BACKUP_MANIFEST_SHA256:
        raise MigrationError("factory backup manifest changed")
    try:
        manifest = json.loads(payload.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise MigrationError("factory backup manifest is invalid") from error
    identity = manifest.get("device", {}).get("identity", {})
    backup = manifest.get("pre_e6_live_backup", {})
    if not (
        manifest.get("device", {}).get("chip") == "ESP32-P4"
        and manifest.get("device", {}).get("revision") == "v1.3"
        and manifest.get("device", {}).get("flash_bytes") == FLASH_BYTES
        and identity.get("sha256") == EXPECTED_DEVICE_SHA256
        and backup.get("bytes") == FLASH_BYTES
        and backup.get("sha256") == EXPECTED_EXISTING_BACKUP_SHA256
        and backup.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and backup.get("mode") == "0600"
    ):
        raise MigrationError("pre-E6 backup binding differs")
    relative = pathlib.Path(str(backup.get("file")))
    candidates = [ROOT / relative]
    worktree_lines = _git("worktree", "list", "--porcelain").decode(
        "utf-8", "strict"
    ).splitlines()
    candidates.extend(
        pathlib.Path(line.removeprefix("worktree ")) / relative
        for line in worktree_lines if line.startswith("worktree ")
    )
    backup_path = None
    for candidate in candidates:
        try:
            candidate_info = candidate.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISREG(candidate_info.st_mode) and not stat.S_ISLNK(
            candidate_info.st_mode
        ):
            backup_path = candidate.resolve(strict=True)
            break
    if backup_path is None:
        raise MigrationError("existing complete pre-E6 backup is unavailable")
    info = backup_path.lstat()
    if (
        not stat.S_ISREG(info.st_mode)
        or stat.S_ISLNK(info.st_mode)
        or info.st_uid != os.getuid()
        or info.st_size != FLASH_BYTES
        or stat.S_IMODE(info.st_mode) != 0o600
        or _sha256_file(backup_path) != EXPECTED_EXISTING_BACKUP_SHA256
    ):
        raise MigrationError("existing complete pre-E6 backup differs")
    return {
        "manifest_sha256": EXPECTED_BACKUP_MANIFEST_SHA256,
        "backup_bytes": FLASH_BYTES,
        "backup_sha256": EXPECTED_EXISTING_BACKUP_SHA256,
        "new_backup_created": False,
    }


def _run_build_verifier() -> None:
    result = subprocess.run(
        [sys.executable, str(VERIFY_PATH)], cwd=ROOT, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if result.returncode != 0:
        detail = result.stdout.decode("utf-8", "replace")[-2000:]
        raise MigrationError(f"Console OS verifier failed:\n{detail}")


def _target(
    name: str, relative: str, offset: int, span_bytes: int,
    block_bytes: int,
) -> Target:
    path = (ROOT / relative).resolve(strict=True)
    payload = _read_regular(path, name, maximum=FLASH_BYTES)
    if len(payload) > span_bytes:
        raise MigrationError(f"{name} exceeds its exact target span")
    if span_bytes % block_bytes != 0 or offset % block_bytes != 0:
        raise MigrationError(f"{name} target geometry is not exact")
    return Target(
        name, path, offset, payload,
        payload + b"\xff" * (span_bytes - len(payload)), block_bytes,
    )


def _load_targets(commit: str) -> tuple[Target, ...]:
    _run_build_verifier()
    bootloader = _target(
        "bootloader", "apps/console_os/build/bootloader/bootloader.bin",
        BOOTLOADER_OFFSET, BOOTLOADER_SPAN_BYTES, SECTOR_BYTES,
    )
    application = _target(
        "ota_0", "apps/console_os/build/p4_console_os.bin",
        OTA0_OFFSET, OTA0_SPAN_BYTES, WRITE_BLOCK_BYTES,
    )
    table = _target(
        "partition_table",
        "apps/console_os/build/partition_table/partition-table.bin",
        PARTITION_TABLE_OFFSET, PARTITION_TABLE_SPAN_BYTES, SECTOR_BYTES,
    )
    ota_data = _target(
        "ota_data", "apps/console_os/build/ota_data_initial.bin",
        OTA_DATA_OFFSET, OTA_DATA_SPAN_BYTES, SECTOR_BYTES,
    )
    if (
        len(table.payload) != PARTITION_TABLE_PAYLOAD_BYTES
        or len(ota_data.payload) != OTA_DATA_SPAN_BYTES
        or not 0 < len(bootloader.payload) <= BOOTLOADER_SPAN_BYTES
        or not 0 < len(application.payload) <= OTA0_SPAN_BYTES
        or application.payload[0] != 0xE9
    ):
        raise MigrationError("target artifact geometry differs")

    package = _read_regular(
        ROOT / "apps/console_os/build/P4UPDATE.P4U",
        "P4UPDATE package", maximum=OTA0_SPAN_BYTES + 256,
    )
    if len(package) < 256 or package[:8] != b"P4OSUP1\0":
        raise MigrationError("P4UPDATE header differs")
    values = struct.unpack_from("<6I", package, 8)
    header_bytes, package_bytes, payload_offset, payload_bytes, version, flags = values
    digest = package[32:64]
    build = package[96:160].split(b"\0", 1)[0]
    target = package[160:176].split(b"\0", 1)[0]
    if not (
        header_bytes == 256
        and package_bytes == len(package)
        and payload_offset == 256
        and payload_bytes == len(application.payload)
        and version == 1
        and flags == 0
        and package[256:] == application.payload
        and digest == hashlib.sha256(application.payload).digest()
        and build == commit[:12].encode("ascii")
        and target == b"esp32p4"
    ):
        raise MigrationError("P4UPDATE is not bound to this committed app")

    targets = (bootloader, application, table, ota_data)
    previous_end = 0
    for entry in sorted(targets, key=lambda item: item.offset):
        if entry.offset < previous_end or entry.offset + len(entry.span) > GAME_DATA_OFFSET:
            raise MigrationError("target ranges overlap or reach P4 GAMES")
        previous_end = entry.offset + len(entry.span)
    return targets


def _target_records(targets: tuple[Target, ...]) -> list[dict[str, Any]]:
    return [{
        "name": target.name,
        "path": str(target.path.relative_to(ROOT)),
        "offset": f"0x{target.offset:x}",
        "payload_bytes": len(target.payload),
        "payload_sha256": _sha256(target.payload),
        "span_bytes": len(target.span),
        "span_sha256": _sha256(target.span),
        "block_bytes": target.block_bytes,
    } for target in targets]


def _read_predecessor(
    transport: Any, stub: Any, device: Any,
    handle: tuple[int, int, int, int, int],
) -> Predecessor:
    table = transport._read_exact(
        stub, device, handle, PARTITION_TABLE_OFFSET,
        PARTITION_TABLE_SPAN_BYTES, READBACK_CHUNK_BYTES,
    )
    if _sha256(table) != EXPECTED_PREDECESSOR_TABLE_SPAN_SHA256:
        raise MigrationError("live predecessor partition table differs")
    payload = transport._read_exact(
        stub, device, handle, PREDECESSOR_APP_OFFSET,
        EXPECTED_PREDECESSOR_APP_BYTES, READBACK_CHUNK_BYTES,
    )
    if _sha256(payload) != EXPECTED_PREDECESSOR_APP_SHA256:
        raise MigrationError("live predecessor application differs")
    application = payload + b"\xff" * (
        PREDECESSOR_APP_SPAN_BYTES - len(payload)
    )
    if _sha256(application) != EXPECTED_PREDECESSOR_APP_SPAN_SHA256:
        raise MigrationError("reconstructed predecessor app span differs")
    tail = transport._read_exact(
        stub, device, handle, GAME_DATA_OFFSET - SECTOR_BYTES,
        SECTOR_BYTES, SECTOR_BYTES,
    )
    if tail != b"\xff" * SECTOR_BYTES:
        raise MigrationError("live predecessor padded app tail differs")
    return Predecessor(
        transport._read_exact(
            stub, device, handle, BOOTLOADER_OFFSET,
            BOOTLOADER_SPAN_BYTES, READBACK_CHUNK_BYTES,
        ),
        table,
        application,
        transport._read_exact(
            stub, device, handle, NVS_OFFSET,
            NVS_PHY_SPAN_BYTES, READBACK_CHUNK_BYTES,
        ),
        transport._read_exact(
            stub, device, handle, GAME_DATA_OFFSET,
            SECTOR_BYTES, SECTOR_BYTES,
        ),
        transport._read_exact(
            stub, device, handle, OTA1_OFFSET,
            SECTOR_BYTES, SECTOR_BYTES,
        ),
        transport._read_exact(
            stub, device, handle,
            OTA1_OFFSET + OTA1_SPAN_BYTES - SECTOR_BYTES,
            SECTOR_BYTES, SECTOR_BYTES,
        ),
    )


def _validate_live_device(
    transport: Any, stub: Any, device: Any,
    handle: tuple[int, int, int, int, int], stage: str,
) -> None:
    transport.RESTORE._validate_live_rom(
        stub, device, EXPECTED_DEVICE_SHA256
    )
    transport.E5._fresh_exact_flash_id(stub, stage)
    stub.flash_set_parameters(FLASH_BYTES)
    transport.E5._assert_same_handle(device, handle, stub)


def _validate_preserved(
    transport: Any, stub: Any, device: Any,
    handle: tuple[int, int, int, int, int], predecessor: Predecessor,
) -> None:
    checks = (
        ("NVS/PHY", NVS_OFFSET, predecessor.nvs_phy),
        ("P4 GAMES header", GAME_DATA_OFFSET, predecessor.game_data_header),
        ("OTA1 first sector", OTA1_OFFSET, predecessor.ota1_first_sector),
        (
            "OTA1 last sector",
            OTA1_OFFSET + OTA1_SPAN_BYTES - SECTOR_BYTES,
            predecessor.ota1_last_sector,
        ),
    )
    for label, offset, expected in checks:
        actual = transport._read_exact(
            stub, device, handle, offset, len(expected),
            READBACK_CHUNK_BYTES,
        )
        if actual != expected:
            raise MigrationError(f"{label} changed outside migration scope")


def _write_targets(
    transport: Any, stub: Any, device: Any,
    handle: tuple[int, int, int, int, int],
    targets: tuple[Target, ...], ledger: pathlib.Path,
    state: dict[str, Any], phase_prefix: str,
) -> dict[str, str]:
    verified: dict[str, str] = {}
    for target in targets:
        transport._write_span(
            stub, device, handle, target.offset,
            target.span, target.block_bytes,
        )
        readback = transport._read_exact(
            stub, device, handle, target.offset,
            len(target.span), READBACK_CHUNK_BYTES,
        )
        if readback != target.span:
            raise MigrationError(f"exact readback differs for {target.name}")
        verified[target.name] = _sha256(readback)
        transport._transition(
            ledger, state, f"{phase_prefix}-{target.name}-verified",
            restore_required=True, verified_segments=dict(verified),
        )
    return verified


def _persist_capture(
    transport: Any, directory: pathlib.Path,
    raw: bytes, summary: Mapping[str, Any], attempt: int,
) -> dict[str, Any]:
    if attempt <= 0:
        raise MigrationError("capture attempt must be positive")
    if attempt == 1:
        raw_name = CAPTURE_RAW_NAME
        summary_name = CAPTURE_SUMMARY_NAME
    else:
        raw_name = f"game-manager-dual-ota-startup-attempt{attempt}.raw"
        summary_name = f"game-manager-dual-ota-startup-attempt{attempt}.json"
    return {
        "raw": transport._write_new_private(
            directory / raw_name, raw
        ),
        "summary": transport._write_new_private(
            directory / summary_name,
            (json.dumps(dict(summary), indent=2, sort_keys=True) + "\n").encode(),
        ),
    }


def _launch_and_capture(
    transport: Any, capture: Any, device: Any, runtime: Any, stub: Any,
    handle: tuple[int, int, int, int, int], directory: pathlib.Path,
    seconds: float, attempt: int,
) -> tuple[dict[str, Any], Mapping[str, Any]]:
    transport.E5._set_controls_false(device)
    transport._restore_app_uart_baud(device, handle, stub)
    runtime.launch_reset_class(device, uses_usb=False).reset()
    transport.E5._set_controls_false(device)
    transport.E5._assert_same_handle(device, handle)
    try:
        raw, summary = capture.capture_open_handle(
            device, seconds, min_stats=2
        )
    except BaseException as error:
        raw = getattr(error, "partial_raw", b"")
        summary = getattr(error, "failure_summary", {
            "schema": 1, "result": "fail",
            "capture_error_reason": str(error)[:512],
        })
        if not isinstance(raw, bytes):
            raw = b""
        if not isinstance(summary, Mapping):
            summary = {"schema": 1, "result": "fail"}
        _persist_capture(transport, directory, raw, summary, attempt)
        raise
    binding = _persist_capture(
        transport, directory, raw, summary, attempt
    )
    if summary.get("result") != "pass":
        raise MigrationError("retained UART startup acceptance failed")
    return binding, summary


def _restore_predecessor(
    transport: Any, device: Any, runtime: Any,
    handle: tuple[int, int, int, int, int], predecessor: Predecessor,
    ledger: pathlib.Path, state: dict[str, Any],
) -> None:
    stub = transport._load_stub(device, handle, runtime)
    _validate_live_device(
        transport, stub, device, handle, "game-manager-migration-rollback"
    )
    restore_targets = (
        ("bootloader", BOOTLOADER_OFFSET, predecessor.bootloader, SECTOR_BYTES),
        (
            "application", PREDECESSOR_APP_OFFSET,
            predecessor.application, WRITE_BLOCK_BYTES,
        ),
        (
            "partition_table", PARTITION_TABLE_OFFSET,
            predecessor.partition_table, SECTOR_BYTES,
        ),
    )
    verified: dict[str, str] = {}
    for name, offset, payload, block_bytes in restore_targets:
        transport._write_span(
            stub, device, handle, offset, payload, block_bytes
        )
        readback = transport._read_exact(
            stub, device, handle, offset, len(payload), READBACK_CHUNK_BYTES
        )
        if readback != payload:
            raise MigrationError(f"predecessor restore differs for {name}")
        verified[name] = _sha256(readback)
    _validate_preserved(
        transport, stub, device, handle, predecessor
    )
    transport.E5._set_controls_false(device)
    transport._restore_app_uart_baud(device, handle, stub)
    runtime.launch_reset_class(device, uses_usb=False).reset()
    transport.E5._set_controls_false(device)
    transport.E5._assert_same_handle(device, handle)
    transport._transition(
        ledger, state, "predecessor-restored-and-launched",
        restore_required=False, rollback_verified=verified,
    )


def _validate_resume(
    transport: Any, ledger: pathlib.Path, state: dict[str, Any],
    commit: str, targets: tuple[Target, ...], authorization_sha256: str,
) -> None:
    transport._discard_uncommitted_ledger_replacement(ledger, state)
    if not (
        state.get("schema") == 1
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and state.get("source_commit") == commit
        and state.get("authorization_sha256") == authorization_sha256
        and state.get("restore_required") is True
        and state.get("new_backup_created") is False
        and state.get("targets") == _target_records(targets)
    ):
        raise MigrationError("resume ledger differs from the committed target")


def _migrate_same_handle(
    *, transport: Any, capture: Any, device: Any, runtime: Any,
    directory: pathlib.Path, commit: str, targets: tuple[Target, ...],
    backup: Mapping[str, Any], capture_sha256: str,
    authorization_sha256: str, capture_seconds: float, resume: bool,
) -> dict[str, Any]:
    transport._owned_directory(directory, "migration transaction directory")
    ledger = directory / LEDGER_NAME
    handle = transport.E5._handle_binding(device)
    predecessor: Predecessor | None = None
    write_marked = False
    if resume:
        state = transport._load_ledger(ledger)
        _validate_resume(
            transport, ledger, state, commit, targets,
            authorization_sha256,
        )
    else:
        for name in (LEDGER_NAME, CAPTURE_RAW_NAME, CAPTURE_SUMMARY_NAME):
            path = directory / name
            if path.exists() or path.is_symlink():
                raise MigrationError(f"transaction output already exists: {name}")
        state = {
            "schema": 1,
            "phase": "reserved",
            "transition_count": 0,
            "restore_required": False,
            "device_identity_sha256": EXPECTED_DEVICE_SHA256,
            "authorization_sha256": authorization_sha256,
            "source_commit": commit,
            "capture_analyzer_sha256": capture_sha256,
            "new_backup_created": False,
            "write_scope": [
                "0x2000..0x7fff",
                "0x8000..0x8fff",
                "0x10000..0x11fff",
                "0x20000..0x38ffff",
            ],
            "never_write": "game_data 0x710000..0xffffff",
            "targets": _target_records(targets),
            "existing_backup": dict(backup),
            "application_launch_count": 0,
        }
        transport._atomic_ledger(ledger, state)

    try:
        stub = transport._load_stub(device, handle, runtime)
        _validate_live_device(
            transport, stub, device, handle,
            "game-manager-migration-prewrite",
        )
        if not resume:
            predecessor = _read_predecessor(
                transport, stub, device, handle
            )
            transport._transition(
                ledger, state, "live-predecessor-validated",
                restore_required=False,
                predecessor_table_span_sha256=(
                    EXPECTED_PREDECESSOR_TABLE_SPAN_SHA256
                ),
                predecessor_app_payload_sha256=(
                    EXPECTED_PREDECESSOR_APP_SHA256
                ),
                predecessor_app_span_sha256=(
                    EXPECTED_PREDECESSOR_APP_SPAN_SHA256
                ),
                preserved_nvs_phy_sha256=_sha256(predecessor.nvs_phy),
                preserved_game_data_header_sha256=_sha256(
                    predecessor.game_data_header
                ),
            )
        transport._transition(
            ledger, state,
            "resume-write-attempted" if resume else "migration-write-attempted",
            restore_required=True,
        )
        write_marked = True
        verified = _write_targets(
            transport, stub, device, handle, targets, ledger, state,
            "resume" if resume else "migration",
        )
        if predecessor is not None:
            _validate_preserved(
                transport, stub, device, handle, predecessor
            )
        _validate_live_device(
            transport, stub, device, handle,
            "game-manager-migration-prelaunch",
        )
        capture_attempt = int(state.get("capture_attempt_count", 0)) + 1
        transport._transition(
            ledger, state, "application-launch-attempted",
            restore_required=True,
            application_launch_count=capture_attempt,
            capture_attempt_count=capture_attempt,
        )
        capture_binding, summary = _launch_and_capture(
            transport, capture, device, runtime, stub, handle,
            directory, capture_seconds, capture_attempt,
        )
        result = {
            "classification": "game-manager-dual-ota-migration-accepted",
            "source_commit": commit,
            "new_backup_created": False,
            "write_scope_excludes_game_data": True,
            "game_data_offset": f"0x{GAME_DATA_OFFSET:x}",
            "verified_segments": verified,
            "preserved_nvs_phy": predecessor is not None,
            "preserved_game_data_header": predecessor is not None,
            "preserved_ota1_samples": predecessor is not None,
            "same_uart_handle": True,
            "application_launch_count": capture_attempt,
            "registered_apps": summary.get("registered_apps"),
            "valid_package_count": summary.get("valid_package_count"),
            "startup_capture": capture_binding,
            "restore_required": False,
            "resumed_after_interruption": resume,
        }
        transport._transition(
            ledger, state, "runtime-accepted",
            restore_required=False, migration_result=result,
        )
        return result
    except BaseException as primary:
        if write_marked and predecessor is not None:
            try:
                transport._transition(
                    ledger, state, "rollback-requested",
                    restore_required=True,
                    primary_failure=transport._bounded_failure(primary),
                )
                _restore_predecessor(
                    transport, device, runtime, handle, predecessor,
                    ledger, state,
                )
            except BaseException as rollback_error:
                try:
                    transport._transition(
                        ledger, state, "rollback-failed",
                        restore_required=True,
                        rollback_failure=transport._bounded_failure(
                            rollback_error
                        ),
                    )
                except BaseException:
                    pass
                raise MigrationError(
                    f"migration failed ({primary}); rollback failed "
                    f"({rollback_error}); rerun with --resume"
                ) from rollback_error
            raise MigrationError(
                f"migration failed and predecessor was restored: {primary}"
            ) from primary
        if write_marked:
            try:
                transport._transition(
                    ledger, state, "resume-incomplete",
                    restore_required=True,
                    resume_failure=transport._bounded_failure(primary),
                )
            except BaseException:
                pass
        raise
    finally:
        transport.E5._set_controls_false(device)


def migrate(
    *, port: str, directory: pathlib.Path,
    capture_seconds: float, resume: bool,
) -> dict[str, Any]:
    commit = _source_commit()
    authorization_sha256 = _validate_authorization()
    backup = _validate_existing_backup()
    targets = _load_targets(commit)
    capture, capture_sha256 = _load_committed_capture(commit)
    transport = _load_transport()
    previous = transport._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = transport.E5._production_runtime()
        device = transport.E5.open_serial_once(port)
        result = _migrate_same_handle(
            transport=transport, capture=capture,
            device=device, runtime=runtime, directory=directory,
            commit=commit, targets=targets, backup=backup,
            capture_sha256=capture_sha256,
            authorization_sha256=authorization_sha256,
            capture_seconds=capture_seconds, resume=resume,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                transport.E5._set_controls_false(device)
                device.close()
                if getattr(device, "is_open", False):
                    raise MigrationError("exclusive UART did not close")
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise MigrationError("migration ended without a result")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument(
        "--transaction-directory", type=pathlib.Path, required=True
    )
    parser.add_argument("--capture-seconds", type=float, default=45.0)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    directory = args.transaction_directory.resolve(strict=True)
    result = migrate(
        port=args.port, directory=directory,
        capture_seconds=args.capture_seconds, resume=args.resume,
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
