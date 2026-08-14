#!/usr/bin/env python3

"""Crash-recoverable exact-unit installer for USB-storage Console OS.

Installation is available only through the separately frozen issuance route.
The public CLI exposes recovery.  One exclusive J1 UART descriptor is retained
from live identity/security checks through a complete 16 MiB preimage capture,
the ordered writes, exact readback, the single launch, and startup acceptance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import signal
import stat
import sys
import types
from typing import Any, Mapping, NamedTuple


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
ROOT = SCRIPT_DIR.parent.resolve(strict=True)
TRANSPORT_PATH = SCRIPT_DIR / "doom-e6-install.py"
CAPTURE_PATH = SCRIPT_DIR / "capture-console-os-usb-runtime.py"
AUTH_PATH = (
    ROOT / "hardware/evidence/console-os-usb-storage-exact-unit-authorization.json"
)
BUILD_EVIDENCE_PATH = (
    ROOT / "test-runs/2026-08-14-console-os-usb-storage-build.json"
)
USB_EVIDENCE_PATH = (
    ROOT / "hardware/evidence/elecrow-10.1-usb-device-storage-path.json"
)
AUDIO_RELEASE_PATH = (
    ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-exact-unit-audio-release.json"
)
BACKUP_MANIFEST_PATH = ROOT / "hardware/backups/manifest.json"

EXPECTED_TRANSPORT_SHA256 = (
    "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
)
EXPECTED_CAPTURE_SHA256 = (
    "9ae5feda450d9ce6e050ae575a7863cb8b1887c9106faf4a06c415f9839446a4"
)
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
EXPECTED_PREDECESSOR_PARTITION_SHA256 = (
    "5b5bfa656e96706d5144b352bf9294ab455a7e1e49136ea5521cf15902a9e433"
)
EXPECTED_FACTORY_BACKUP_SHA256 = (
    "3e3f5f687f59938963d9040d6c8e9dc1a687baa512790680830422c22bd6ab3c"
)

FLASH_BYTES = 16 * 1024 * 1024
BOOTLOADER_OFFSET = 0x2000
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_PAYLOAD_BYTES = 0xC00
PARTITION_TABLE_SPAN_BYTES = 0x1000
NVS_OFFSET = 0x9000
APP_OFFSET = 0x10000
APP_SPAN_BYTES = 0x700000
GAME_DATA_OFFSET = 0x710000
GAME_DATA_SPAN_BYTES = 0x8F0000
READBACK_CHUNK_BYTES = 512 * 1024
INSTALL_BLOCK_BYTES = 0x4000
RESTORE_BLOCK_BYTES = 0x1000
APP_UART_BAUD = 115_200
TRANSFER_UART_BAUD = 460_800
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024

LEDGER_NAME = "console-usb-install-ledger.json"
FULL_PREIMAGE_NAME = "live-full-flash-before-usb-storage.bin"
CAPTURE_RAW_NAME = "console-usb-startup.raw"
CAPTURE_SUMMARY_NAME = "console-usb-startup.json"


class InstallError(RuntimeError):
    """The exact-unit USB-storage installation contract was not satisfied."""


class TargetSegment(NamedTuple):
    name: str
    path: pathlib.Path
    offset: int
    payload: bytes
    span: bytes
    block_bytes: int


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _digest(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise InstallError(f"{label} digest has invalid shape")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise InstallError(f"{label} digest has invalid shape") from error
    return value


def _read_regular(
    path: pathlib.Path, label: str, *, maximum: int,
    required_mode: int | None = None,
) -> bytes:
    try:
        before = path.lstat()
    except OSError as error:
        raise InstallError(f"cannot stat {label}: {error}") from error
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
        raise InstallError(f"{label} metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (before.st_dev, before.st_ino, before.st_size)
            or stat.S_IMODE(opened.st_mode) != mode
        ):
            raise InstallError(f"{label} changed while opening")
        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError(f"{label} was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError(f"{label} grew while reading")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size)
            != (opened.st_dev, opened.st_ino, opened.st_size)
        ):
            raise InstallError(f"{label} changed while reading")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _read_json(
    path: pathlib.Path, label: str, *, expected_sha256: str | None = None,
) -> dict[str, Any]:
    payload = _read_regular(path, label, maximum=MAX_JSON_BYTES)
    if expected_sha256 is not None and _sha256(payload) != _digest(
        expected_sha256, label
    ):
        raise InstallError(f"{label} changed")
    try:
        value = json.loads(payload.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise InstallError(f"{label} is not valid UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise InstallError(f"{label} is not a JSON object")
    return value


def _load_exact_module(
    path: pathlib.Path, expected_sha256: str, name: str,
) -> Any:
    payload = _read_regular(
        path.resolve(strict=True), name, maximum=MAX_SOURCE_BYTES,
        required_mode=0o755,
    )
    if _sha256(payload) != expected_sha256:
        raise InstallError(f"{name} bytes changed")
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


TRANSPORT = _load_exact_module(
    TRANSPORT_PATH, EXPECTED_TRANSPORT_SHA256,
    "console_usb_frozen_recovery_transport",
)
CAPTURE = _load_exact_module(
    CAPTURE_PATH, EXPECTED_CAPTURE_SHA256,
    "console_usb_frozen_startup_capture",
)


def _bound_json(
    binding: Mapping[str, Any], exact_path: pathlib.Path, label: str,
) -> dict[str, Any]:
    if not isinstance(binding, Mapping):
        raise InstallError(f"{label} binding is missing")
    relative = binding.get("path")
    expected = binding.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise InstallError(f"{label} binding is malformed")
    path = (ROOT / relative).resolve(strict=True)
    if path != exact_path.resolve(strict=True):
        raise InstallError(f"{label} path differs")
    return _read_json(path, label, expected_sha256=expected)


def _read_sealed_payload(
    path: pathlib.Path, label: str, expected_bytes: int, expected_sha256: str,
) -> bytes:
    payload = _read_regular(
        path.resolve(strict=True), label,
        maximum=FLASH_BYTES, required_mode=0o400,
    )
    if len(payload) != expected_bytes or _sha256(payload) != _digest(
        expected_sha256, label
    ):
        raise InstallError(f"{label} size or hash differs")
    return payload


def _segment_from_record(
    name: str, record: Mapping[str, Any], *, exact_path: pathlib.Path,
    offset: int, span_bytes: int, block_bytes: int,
) -> TargetSegment:
    if not isinstance(record, Mapping):
        raise InstallError(f"{name} artifact record is missing")
    relative = record.get("path")
    if not isinstance(relative, str) or (
        ROOT / relative
    ).resolve(strict=True) != exact_path.resolve(strict=True):
        raise InstallError(f"{name} artifact path differs")
    try:
        payload_bytes = int(record.get("payload_bytes"))
    except (TypeError, ValueError) as error:
        raise InstallError(f"{name} payload size is malformed") from error
    if (
        int(str(record.get("offset")), 0) != offset
        or int(record.get("span_bytes")) != span_bytes
        or payload_bytes <= 0
        or payload_bytes > span_bytes
        or span_bytes % block_bytes != 0
        or offset % block_bytes != 0
        or record.get("tail_byte") != "ff"
    ):
        raise InstallError(f"{name} geometry differs")
    payload = _read_sealed_payload(
        exact_path, name, payload_bytes, str(record.get("payload_sha256"))
    )
    span = payload + b"\xff" * (span_bytes - payload_bytes)
    if _sha256(span) != _digest(record.get("span_sha256"), f"{name} span"):
        raise InstallError(f"{name} padded span differs")
    return TargetSegment(name, exact_path, offset, payload, span, block_bytes)


def _load_targets(auth: Mapping[str, Any]) -> tuple[bytes, tuple[TargetSegment, ...]]:
    artifacts = auth.get("exact_artifacts")
    if not isinstance(artifacts, Mapping):
        raise InstallError("authorization omits exact artifacts")
    boot_record = artifacts.get("bootloader")
    if not isinstance(boot_record, Mapping):
        raise InstallError("bootloader artifact record is missing")
    boot_path = ROOT / "apps/console_os/build/bootloader/bootloader.bin"
    if (
        (ROOT / str(boot_record.get("path"))).resolve(strict=True)
        != boot_path.resolve(strict=True)
        or int(str(boot_record.get("offset")), 0) != BOOTLOADER_OFFSET
    ):
        raise InstallError("bootloader path or offset differs")
    bootloader = _read_sealed_payload(
        boot_path, "bootloader", int(boot_record.get("payload_bytes")),
        str(boot_record.get("payload_sha256")),
    )
    targets = (
        _segment_from_record(
            "game_data", artifacts.get("game_data", {}),
            exact_path=ROOT / "apps/console_os/build/game_data.bin",
            offset=GAME_DATA_OFFSET, span_bytes=GAME_DATA_SPAN_BYTES,
            block_bytes=INSTALL_BLOCK_BYTES,
        ),
        _segment_from_record(
            "application", artifacts.get("application", {}),
            exact_path=ROOT / "apps/console_os/build/p4_console_os.bin",
            offset=APP_OFFSET, span_bytes=APP_SPAN_BYTES,
            block_bytes=INSTALL_BLOCK_BYTES,
        ),
        _segment_from_record(
            "partition_table", artifacts.get("partition_table", {}),
            exact_path=ROOT / "apps/console_os/build/partition_table/partition-table.bin",
            offset=PARTITION_TABLE_OFFSET,
            span_bytes=PARTITION_TABLE_SPAN_BYTES,
            block_bytes=RESTORE_BLOCK_BYTES,
        ),
    )
    if len(targets[-1].payload) != PARTITION_TABLE_PAYLOAD_BYTES:
        raise InstallError("partition-table payload size differs")
    return bootloader, targets


def _contract_from_authorization(
    authorization: pathlib.Path, authorization_sha256: str,
) -> tuple[dict[str, Any], bytes, tuple[TargetSegment, ...]]:
    if authorization.resolve(strict=True) != AUTH_PATH.resolve(strict=True):
        raise InstallError("authorization path differs")
    auth = _read_json(
        authorization, "Console OS USB exact-unit authorization",
        expected_sha256=authorization_sha256,
    )
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-console-os-usb-storage-v1"
        and auth.get("operator_direction")
            == "merge it and push it to the device please"
        and auth.get("device_binding") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": FLASH_BYTES,
        }
        and auth.get("predecessor_partition_table_sha256")
            == EXPECTED_PREDECESSOR_PARTITION_SHA256
    ):
        raise InstallError("authorization scope or exact-unit binding differs")

    build = _bound_json(
        auth.get("build_evidence", {}), BUILD_EVIDENCE_PATH, "build evidence"
    )
    usb = _bound_json(
        auth.get("usb_device_evidence", {}), USB_EVIDENCE_PATH,
        "USB-device evidence",
    )
    audio = _bound_json(
        auth.get("exact_unit_audio_release", {}), AUDIO_RELEASE_PATH,
        "exact-unit audio release",
    )
    manifest = _bound_json(
        auth.get("factory_backup_manifest", {}), BACKUP_MANIFEST_PATH,
        "factory backup manifest",
    )
    TRANSPORT._validate_exact_unit_audio_release(audio)
    if not (
        usb.get("authorization", {}).get("authorized") is True
        and usb.get("scope") == "usb_device_only"
        and usb.get("runtime_status", {}).get("enumeration_tested") is False
        and manifest.get("backup", {}).get("bytes") == FLASH_BYTES
        and manifest.get("backup", {}).get("sha256")
            == EXPECTED_FACTORY_BACKUP_SHA256
        and build.get("result")
            == "console-os-usb-storage-exact-unit-build-verified"
        and build.get("exact_artifacts") == auth.get("exact_artifacts")
        and build.get("installer", {}).get("path")
            == "scripts/console-os-usb-install.py"
        and build.get("installer", {}).get("sha256")
            == _sha256_file(SCRIPT_PATH)
        and build.get("transport", {}).get("sha256")
            == EXPECTED_TRANSPORT_SHA256
        and build.get("startup_capture", {}).get("sha256")
            == EXPECTED_CAPTURE_SHA256
    ):
        raise InstallError("build, USB, backup, or installer evidence differs")
    inventory = build.get("source_inventory")
    if not isinstance(inventory, Mapping):
        raise InstallError("build evidence omits source inventory")
    required = {
        "apps/console_os/CMakeLists.txt",
        "apps/console_os/main/console_os_main.c",
        "apps/console_os/partitions.csv",
        "apps/console_os/dependencies.lock",
        "components/platform_game_storage/src/platform_game_storage.c",
        "scripts/console-os-usb-install.py",
        "scripts/capture-console-os-usb-runtime.py",
        "scripts/doom-e6-install.py",
        "scripts/verify-console-os.py",
        "toolchain.lock.json",
        "hardware/board-profile.json",
        "hardware/evidence/elecrow-10.1-usb-device-storage-path.json",
    }
    if not required <= set(inventory):
        raise InstallError("build inventory omits a critical source")
    TRANSPORT._validate_inventory(inventory)

    if auth.get("execution_contract") != {
        "same_uart_handle": True,
        "full_live_flash_preimage_required": True,
        "preserve_ranges": ["0x0000..0x7fff", "0x9000..0xffff"],
        "write_order": ["game_data", "application", "partition_table"],
        "partition_table_committed_last": True,
        "exact_readback_before_launch": True,
        "single_launch": True,
        "retained_launcher_capture_required": True,
        "automatic_restore_on_failure": True,
        "shell_audio_hardware_calls": 0,
        "doom_audio_deferred_until_user_handoff": True,
        "usb_device_role": "J16 MSC",
        "recovery_directory":
            "hardware/local-state/console-os-usb-storage-install-20260814",
    }:
        raise InstallError("execution contract differs")
    bootloader, targets = _load_targets(auth)
    return auth, bootloader, targets


def _validate_factory_backup(path: pathlib.Path) -> dict[str, Any]:
    payload = _read_regular(
        path.resolve(strict=True), "factory backup",
        maximum=FLASH_BYTES, required_mode=0o600,
    )
    if len(payload) != FLASH_BYTES or _sha256(payload) != EXPECTED_FACTORY_BACKUP_SHA256:
        raise InstallError("factory backup does not match the bound 16 MiB image")
    return {
        "path": str(path.resolve(strict=True)),
        "bytes": len(payload),
        "sha256": _sha256(payload),
        "mode": 0o600,
    }


def _target_records(targets: tuple[TargetSegment, ...]) -> list[dict[str, Any]]:
    return [
        {
            "name": target.name,
            "offset": target.offset,
            "payload_bytes": len(target.payload),
            "payload_sha256": _sha256(target.payload),
            "span_bytes": len(target.span),
            "span_sha256": _sha256(target.span),
            "block_bytes": target.block_bytes,
        }
        for target in targets
    ]


def _validate_preimage(
    state: Mapping[str, Any], targets: tuple[TargetSegment, ...], runtime: Any,
) -> bytes:
    if not (
        state.get("schema") == 1
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and state.get("flash_bytes") == FLASH_BYTES
        and state.get("targets") == _target_records(targets)
    ):
        raise InstallError("recovery ledger geometry differs")
    inventory = state.get("recovery_inventory")
    if not isinstance(inventory, Mapping):
        raise InstallError("recovery ledger omits helper inventory")
    TRANSPORT._validate_inventory(inventory)
    binding = state.get("full_preimage")
    if not isinstance(binding, Mapping):
        raise InstallError("recovery ledger omits the full live preimage")
    _path, payload = TRANSPORT.RESTORE._read_bound_payload(
        binding, "full live flash preimage"
    )
    if len(payload) != FLASH_BYTES:
        raise InstallError("full live preimage size differs")
    predecessor = payload[
        PARTITION_TABLE_OFFSET:
        PARTITION_TABLE_OFFSET + PARTITION_TABLE_PAYLOAD_BYTES
    ]
    if _sha256(predecessor) != EXPECTED_PREDECESSOR_PARTITION_SHA256:
        raise InstallError("full preimage has an unexpected predecessor table")
    if state.get("restore_runtime") != dict(runtime.binding):
        raise InstallError("recovery runtime binding differs")
    return payload


def _persist_capture(
    recovery_directory: pathlib.Path, raw: bytes, summary: Mapping[str, Any],
) -> dict[str, Any]:
    return {
        "raw": TRANSPORT._write_new_private(
            recovery_directory / CAPTURE_RAW_NAME, raw
        ),
        "summary": TRANSPORT._write_new_private(
            recovery_directory / CAPTURE_SUMMARY_NAME,
            (json.dumps(dict(summary), indent=2, sort_keys=True) + "\n").encode(),
        ),
    }


def _restore_same_handle(
    *, device: Any, runtime: Any, ledger_path: pathlib.Path,
    state: dict[str, Any], targets: tuple[TargetSegment, ...],
) -> dict[str, Any]:
    preimage = _validate_preimage(state, targets, runtime)
    handle = TRANSPORT.E5._handle_binding(device)
    TRANSPORT._transition(
        ledger_path, state, "restore-entered", restore_required=True,
        restore_attempt_count=int(state.get("restore_attempt_count", 0)) + 1,
    )
    stub = TRANSPORT._load_stub(device, handle, runtime)
    TRANSPORT.RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
    TRANSPORT.E5._fresh_exact_flash_id(stub, "console-usb-restore-boundary")
    stub.flash_set_parameters(FLASH_BYTES)
    TRANSPORT.E5._assert_same_handle(device, handle, stub)
    TRANSPORT._transition(
        ledger_path, state, "restore-write-attempted", restore_required=True,
        restore_write_attempt_count=int(
            state.get("restore_write_attempt_count", 0)
        ) + 1,
    )
    for target in targets:
        prior = preimage[target.offset:target.offset + len(target.span)]
        TRANSPORT._write_span(
            stub, device, handle, target.offset, prior, RESTORE_BLOCK_BYTES
        )
        restored = TRANSPORT._read_exact(
            stub, device, handle, target.offset, len(prior), READBACK_CHUNK_BYTES
        )
        if restored != prior:
            raise InstallError(f"restore readback differs for {target.name}")
    TRANSPORT.RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
    TRANSPORT.E5._fresh_exact_flash_id(stub, "console-usb-restore-prelaunch")
    TRANSPORT.E5._set_controls_false(device)
    TRANSPORT._restore_app_uart_baud(device, handle, stub)
    runtime.launch_reset_class(device, uses_usb=False).reset()
    TRANSPORT.E5._set_controls_false(device)
    TRANSPORT.E5._assert_same_handle(device, handle)
    result = {
        "full_preimage_bytes": len(preimage),
        "full_preimage_sha256": _sha256(preimage),
        "restored_segments": [target.name for target in targets],
        "same_uart_handle": True,
        "restore_write_count": 1,
        "predecessor_launch_count": 1,
    }
    TRANSPORT._transition(
        ledger_path, state, "predecessor-restored-and-launched",
        restore_required=False, restoration_verified=True,
        restore_result=result,
    )
    return result


def install_same_handle(
    *, device: Any, runtime: Any, bootloader: bytes,
    targets: tuple[TargetSegment, ...], recovery_directory: pathlib.Path,
    factory_backup_binding: Mapping[str, Any], authorization_sha256: str,
    capture_seconds: float,
) -> dict[str, Any]:
    TRANSPORT._owned_directory(recovery_directory, "recovery directory")
    ledger_path = recovery_directory / LEDGER_NAME
    for name in (
        LEDGER_NAME, FULL_PREIMAGE_NAME, CAPTURE_RAW_NAME, CAPTURE_SUMMARY_NAME,
    ):
        candidate = recovery_directory / name
        if candidate.exists() or candidate.is_symlink():
            raise InstallError(f"recovery child already exists: {name}")
    handle = TRANSPORT.E5._handle_binding(device)
    state: dict[str, Any] = {
        "schema": 1,
        "phase": "reserved",
        "transition_count": 0,
        "restore_required": False,
        "restoration_verified": False,
        "device_identity_sha256": EXPECTED_DEVICE_SHA256,
        "authorization_sha256": _digest(
            authorization_sha256, "authorization"
        ),
        "flash_bytes": FLASH_BYTES,
        "targets": _target_records(targets),
        "factory_backup": dict(factory_backup_binding),
        "install_write_attempt_count": 0,
        "restore_attempt_count": 0,
        "restore_write_attempt_count": 0,
        "application_launch_count": 0,
        "recovery_inventory": {
            str(path.relative_to(ROOT)): _sha256_file(path)
            for path in (
                SCRIPT_PATH, CAPTURE_PATH.resolve(strict=True),
                TRANSPORT_PATH.resolve(strict=True),
                TRANSPORT.E5_INSTALL_PATH.resolve(strict=True),
                TRANSPORT.E5.RESTORE_PATH.resolve(strict=True),
            )
        },
        "restore_runtime": dict(runtime.binding),
    }
    TRANSPORT._atomic_ledger(ledger_path, state)
    write_marked = False
    try:
        stub = TRANSPORT._load_stub(device, handle, runtime)
        live = TRANSPORT._read_exact(
            stub, device, handle, 0, FLASH_BYTES, READBACK_CHUNK_BYTES
        )
        if len(live) != FLASH_BYTES:
            raise InstallError("full live flash capture is incomplete")
        if live[BOOTLOADER_OFFSET:BOOTLOADER_OFFSET + len(bootloader)] != bootloader:
            raise InstallError("live bootloader differs from the pinned build")
        predecessor_table = live[
            PARTITION_TABLE_OFFSET:
            PARTITION_TABLE_OFFSET + PARTITION_TABLE_PAYLOAD_BYTES
        ]
        if _sha256(predecessor_table) != EXPECTED_PREDECESSOR_PARTITION_SHA256:
            raise InstallError("live predecessor partition table differs")
        full_binding = TRANSPORT._write_new_private(
            recovery_directory / FULL_PREIMAGE_NAME, live
        )
        TRANSPORT._transition(
            ledger_path, state, "full-preimage-sealed",
            full_preimage=full_binding, restore_required=False,
        )
        if _validate_preimage(state, targets, runtime) != live:
            raise InstallError("sealed full preimage differs before mutation")
        if TRANSPORT._read_exact(
            stub, device, handle, BOOTLOADER_OFFSET, len(bootloader)
        ) != bootloader:
            raise InstallError("live bootloader changed before mutation")
        if TRANSPORT._read_exact(
            stub, device, handle, PARTITION_TABLE_OFFSET,
            PARTITION_TABLE_PAYLOAD_BYTES,
        ) != predecessor_table:
            raise InstallError("live partition table changed before mutation")
        for target in targets:
            current = _read_sealed_payload(
                target.path, target.name, len(target.payload),
                _sha256(target.payload),
            )
            if current != target.payload:
                raise InstallError(f"sealed artifact changed: {target.name}")
        TRANSPORT.RESTORE._validate_live_rom(
            stub, device, EXPECTED_DEVICE_SHA256
        )
        TRANSPORT.E5._fresh_exact_flash_id(stub, "console-usb-write-boundary")
        stub.flash_set_parameters(FLASH_BYTES)
        TRANSPORT.E5._assert_same_handle(device, handle, stub)
        TRANSPORT._transition(
            ledger_path, state, "install-write-attempted",
            restore_required=True, install_write_attempt_count=1,
        )
        write_marked = True
        verified: dict[str, str] = {}
        for target in targets:
            TRANSPORT._write_span(
                stub, device, handle, target.offset, target.span,
                target.block_bytes,
            )
            readback = TRANSPORT._read_exact(
                stub, device, handle, target.offset, len(target.span),
                READBACK_CHUNK_BYTES,
            )
            if readback != target.span:
                raise InstallError(f"exact readback differs for {target.name}")
            verified[target.name] = _sha256(readback)
            TRANSPORT._transition(
                ledger_path, state, f"{target.name}-verified",
                restore_required=True, verified_segments=dict(verified),
            )

        if TRANSPORT._read_exact(
            stub, device, handle, 0, PARTITION_TABLE_OFFSET
        ) != live[:PARTITION_TABLE_OFFSET]:
            raise InstallError("bootloader or pre-table range changed")
        if TRANSPORT._read_exact(
            stub, device, handle, NVS_OFFSET, APP_OFFSET - NVS_OFFSET
        ) != live[NVS_OFFSET:APP_OFFSET]:
            raise InstallError("NVS or PHY range changed")
        TRANSPORT.RESTORE._validate_live_rom(
            stub, device, EXPECTED_DEVICE_SHA256
        )
        TRANSPORT.E5._fresh_exact_flash_id(stub, "console-usb-prelaunch")
        TRANSPORT.E5._set_controls_false(device)
        TRANSPORT._restore_app_uart_baud(device, handle, stub)
        TRANSPORT._transition(
            ledger_path, state, "application-launch-attempted",
            restore_required=True, application_launch_count=1,
        )
        runtime.launch_reset_class(device, uses_usb=False).reset()
        TRANSPORT.E5._set_controls_false(device)
        TRANSPORT.E5._assert_same_handle(device, handle)

        capture_error: BaseException | None = None
        try:
            raw, summary = CAPTURE.capture_open_handle(
                device, capture_seconds, min_stats=2
            )
        except BaseException as error:
            capture_error = error
            raw = getattr(error, "partial_raw", b"")
            summary = getattr(error, "failure_summary", {})
            if not isinstance(raw, bytes) or len(raw) > CAPTURE.MAX_TRANSCRIPT_BYTES:
                raw = b""
            if not isinstance(summary, Mapping):
                summary = {"schema": 1, "result": "fail"}
        capture_binding = _persist_capture(
            recovery_directory, raw, summary
        )
        capture_result = summary.get("result")
        TRANSPORT._transition(
            ledger_path, state, "startup-capture-recorded",
            restore_required=True, startup_capture=capture_binding,
            startup_capture_result=capture_result,
            startup_capture_error=(
                TRANSPORT._bounded_failure(capture_error)
                if capture_error is not None else None
            ),
        )
        if capture_error is not None:
            raise InstallError(
                "retained startup capture failed: "
                f"{TRANSPORT._bounded_failure(capture_error)}"
            ) from capture_error
        if capture_result != "pass":
            raise InstallError("retained startup acceptance failed")
        result = {
            "full_preimage_bytes": FLASH_BYTES,
            "full_preimage_sha256": _sha256(live),
            "verified_segments": verified,
            "same_uart_handle": True,
            "exclusive_uart_open_count": 1,
            "uart_reopen_count": 0,
            "application_launch_count": 1,
            "startup_capture": capture_binding,
            "restore_required": False,
            "shell_audio_hardware_calls": 0,
            "usb_device_driver_started": True,
            "transfer_baud": TRANSFER_UART_BAUD,
            "application_baud": APP_UART_BAUD,
        }
        TRANSPORT._transition(
            ledger_path, state, "runtime-accepted", restore_required=False,
            install_result=result,
        )
        return result
    except BaseException as primary:
        if write_marked or state.get("restore_required") is True:
            try:
                TRANSPORT._transition(
                    ledger_path, state, "rollback-requested",
                    restore_required=True,
                    primary_failure=TRANSPORT._bounded_failure(primary),
                )
            except BaseException:
                pass
            try:
                _restore_same_handle(
                    device=device, runtime=runtime, ledger_path=ledger_path,
                    state=state, targets=targets,
                )
            except BaseException as restore_error:
                try:
                    TRANSPORT._transition(
                        ledger_path, state, "restore-failed",
                        restore_required=True,
                        last_error=TRANSPORT._bounded_failure(restore_error),
                    )
                except BaseException:
                    pass
                raise InstallError(
                    f"install failed ({primary}); restoration also failed "
                    f"({restore_error}); recovery remains required"
                ) from restore_error
            raise InstallError(
                f"install acceptance failed and predecessor was restored: {primary}"
            ) from primary
        raise
    finally:
        TRANSPORT.E5._set_controls_false(device)


def install_from_trust_anchor(
    *, port: str, authorization: pathlib.Path, authorization_sha256: str,
    factory_backup: pathlib.Path, recovery_directory: pathlib.Path,
    capture_seconds: float = 30.0,
) -> dict[str, Any]:
    """Install only when called by the separately frozen issuance route."""

    _auth, bootloader, targets = _contract_from_authorization(
        authorization, authorization_sha256
    )
    factory_binding = _validate_factory_backup(factory_backup)
    expected_recovery = (
        ROOT / "hardware/local-state/console-os-usb-storage-install-20260814"
    ).resolve()
    if recovery_directory.resolve(strict=True) != expected_recovery:
        raise InstallError("recovery directory differs from authorization")
    TRANSPORT._owned_directory(recovery_directory, "recovery directory")

    previous = TRANSPORT._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = TRANSPORT.E5._production_runtime()
        device = TRANSPORT.E5.open_serial_once(port)
        result = install_same_handle(
            device=device, runtime=runtime, bootloader=bootloader,
            targets=targets, recovery_directory=recovery_directory,
            factory_backup_binding=factory_binding,
            authorization_sha256=authorization_sha256,
            capture_seconds=capture_seconds,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                TRANSPORT.E5._set_controls_false(device)
                device.close()
                if getattr(device, "is_open", False):
                    raise InstallError("exclusive UART did not close")
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise InstallError("transaction ended without a result")
    return {
        **result,
        "app": "console_os",
        "classification": "exact-unit-usb-storage-launcher-accepted",
    }


def recover(port: str, recovery_directory: pathlib.Path) -> dict[str, Any]:
    """Restore the complete changed ranges from the sealed full preimage."""

    TRANSPORT._owned_directory(recovery_directory, "recovery directory")
    ledger_path = recovery_directory / LEDGER_NAME
    state = TRANSPORT._load_ledger(ledger_path)
    TRANSPORT._discard_uncommitted_ledger_replacement(ledger_path, state)
    if state.get("restore_required") is not True:
        raise InstallError("ledger does not require restoration")
    authorization_sha256 = state.get("authorization_sha256")
    if not isinstance(authorization_sha256, str):
        raise InstallError("ledger omits authorization binding")
    _auth, _bootloader, targets = _contract_from_authorization(
        AUTH_PATH, authorization_sha256
    )
    previous = TRANSPORT._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = TRANSPORT.E5._production_runtime()
        device = TRANSPORT.E5.open_serial_once(port)
        result = _restore_same_handle(
            device=device, runtime=runtime, ledger_path=ledger_path,
            state=state, targets=targets,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                TRANSPORT.E5._set_controls_false(device)
                device.close()
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise InstallError("recovery ended without a result")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("recover",))
    parser.add_argument("--port", required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = recover(args.port, args.recovery_directory)
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
