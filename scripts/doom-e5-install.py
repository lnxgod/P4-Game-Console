#!/usr/bin/env python3

"""Exclusive same-UART installer for the authorized E5 touch-only image.

The transaction retains one exclusive programming-UART descriptor from the
authoritative live-device checks through the only write, full-span readback,
and one post-readback application-launch reset.  It never opens a second
serial connection and never calls esptool's configurable reset or run paths.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
import pathlib
import signal
import stat
import sys
import termios
import time
from typing import Any, Mapping, NamedTuple


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
ROOT = SCRIPT_DIR.parent.resolve(strict=True)
RESTORE_PATH = SCRIPT_DIR / "gamepad-diag-restore.py"
AUTH_PATH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e5-persistent-demo-authorization.json"
EVIDENCE_PATH = ROOT / "test-runs/2026-08-13-doom-embedded-touch-audio-e5-build.json"
EXPECTED_OFFSET = 0x10000
EXPECTED_BYTES = 4_898_400
EXPECTED_SHA256 = "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8"
EXPECTED_DEVICE_SHA256 = "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
WRITE_BLOCK_BYTES = 0x4000
EXPECTED_SPAN_BYTES = 4_898_816
MAX_READBACK_CHUNK_BYTES = 512 * 1024
EXPECTED_FLASH_JEDEC_LOW24 = 0x1840C8
ALLOWED_FLASH_JEDEC_HIGH8 = {0x00, 0xFF}
EXPECTED_RUNTIME_HASHES = {
    "loader": "6c5f0c4a9d2047adb1c9164aee66208018789a0f6531922204f6b280168c80e8",
    "reset": "410334df7cb09cafc01efa8d32acb1903bbc5ba6f03df3a0b39823c433b0edef",
    "esp32p4": "0319aa7ee6e2e45c3ceb2e3424fb98a2ae9734873a2206767085abf75e74bf37",
    "esp32": "32b5e7f8b3e971705f57bb9cdf54e847eefd96e2168baa6120d3643beb224bb5",
}
EXPECTED_RESTORE_SHA256 = "5be16e889116cc8a9e9009d730c2d0bcd3438ac07ead2d726ac1a346a5f0e244"
EXPECTED_SERIAL_RUNTIME = {
    "python": {
        "path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/bin/python3.14",
        "bytes": 52_448,
        "sha256": "5c3ea934d18a7979253ca08ff16151db78a896be29dbc37fd9bf7e7c549593b8",
    },
    "serial": {"bytes": 3_212, "sha256": "5dec897fdef45a0eaf63eacda1e01d62e1e75490302584ae0238f75376592344"},
    "serialposix": {"bytes": 35_127, "sha256": "5d56f98513391e1766a1847a04e2e1202ced4172a152803ba1c823a149a9b6f9"},
    "serialutil": {"bytes": 21_797, "sha256": "3c84f8c7c319f161a85d6f8db5bedc4f65255714589bdcbeee6f2daf65a17c0a"},
    "termios": {
        "path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/lib/python3.14/lib-dynload/termios.cpython-314-darwin.so",
        "bytes": 71_952,
        "sha256": "18c723dc5d61cc411f12c75bf1b3de2be917490eb9733a226bc8d0c7a18c028f",
    },
    "fcntl": {
        "path": "/opt/homebrew/Cellar/python@3.14/3.14.4/Frameworks/Python.framework/Versions/3.14/lib/python3.14/lib-dynload/fcntl.cpython-314-darwin.so",
        "bytes": 70_832,
        "sha256": "0a54b8fc819837d545ee95da563421303e0cfaae73c5a4881ef56d32da3d667d",
    },
}
EXPECTED_SERIAL_RUNTIME["serial"]["path"] = "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/__init__.py"
EXPECTED_SERIAL_RUNTIME["serialposix"]["path"] = "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/serialposix.py"
EXPECTED_SERIAL_RUNTIME["serialutil"]["path"] = "/Users/billh/.espressif/python_env/idf5.5_py3.14_env/lib/python3.14/site-packages/serial/serialutil.py"
EXPECTED_AUTH_RUNTIME_BINDING = {
    "esptool_version": "4.12.0",
    "loader_sha256": EXPECTED_RUNTIME_HASHES["loader"],
    "reset_sha256": EXPECTED_RUNTIME_HASHES["reset"],
    "esp32p4_sha256": EXPECTED_RUNTIME_HASHES["esp32p4"],
    "esp32_sha256": EXPECTED_RUNTIME_HASHES["esp32"],
    "legacy_rev1_stub_sha256": "3c0f27938055192977123cd4b503bf27d1676c59a7fd7e3f93de8e95b0cf63bf",
    "runtime_helper_sha256": EXPECTED_RESTORE_SHA256,
    "loader_entry_reset_class": "UnixTightReset",
    "application_launch_reset_class": "HardReset",
    "custom_reset_sequence_allowed": False,
    "python_executable_sha256": EXPECTED_SERIAL_RUNTIME["python"]["sha256"],
    "pyserial_init_sha256": EXPECTED_SERIAL_RUNTIME["serial"]["sha256"],
    "pyserial_serialposix_sha256": EXPECTED_SERIAL_RUNTIME["serialposix"]["sha256"],
    "pyserial_serialutil_sha256": EXPECTED_SERIAL_RUNTIME["serialutil"]["sha256"],
    "python_executable_path": EXPECTED_SERIAL_RUNTIME["python"]["path"],
    "pyserial_init_path": EXPECTED_SERIAL_RUNTIME["serial"]["path"],
    "pyserial_serialposix_path": EXPECTED_SERIAL_RUNTIME["serialposix"]["path"],
    "pyserial_serialutil_path": EXPECTED_SERIAL_RUNTIME["serialutil"]["path"],
    "termios_path": EXPECTED_SERIAL_RUNTIME["termios"]["path"],
    "termios_sha256": EXPECTED_SERIAL_RUNTIME["termios"]["sha256"],
    "fcntl_path": EXPECTED_SERIAL_RUNTIME["fcntl"]["path"],
    "fcntl_sha256": EXPECTED_SERIAL_RUNTIME["fcntl"]["sha256"],
    "flash_jedec_low24": "0x1840c8",
    "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
    "canonical_flash_id": "0x1840c8",
}
EXPECTED_AUTH_EXECUTION_CONTRACT = {
    "app_partition_only": True,
    "full_project_flash": False,
    "shell_live_probe_count": 0,
    "exclusive_uart_open_count": 1,
    "exclusive_uart_reopen_count": 0,
    "exclusive_transaction_loader_entry_reset_count": 1,
    "loader_connect_no_reset_attempts": 1,
    "write_transport": "same-handle-pinned-rev1-ram-stub",
    "write_span_bytes": EXPECTED_SPAN_BYTES,
    "flash_jedec_low24": "0x1840c8",
    "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
    "canonical_flash_id": "0x1840c8",
    "stub_flash_finish_sync_count": 1,
    "stub_flash_finish_sync_launch_count": 0,
    "readback_max_chunk_bytes": MAX_READBACK_CHUNK_BYTES,
    "readback_scope": "same-handle-full-padded-mutation-span",
    "ordered_aggregate_sha256_required": True,
    "application_launch_after_exact_readback": True,
    "application_launch_mechanism": "direct-hash-bound-HardReset-rts-only-dtr-false",
    "exclusive_transaction_application_launch_reset_count": 1,
    "application_launch_count": 1,
    "soft_reset_count": 0,
    "esptool_run_count": 0,
    "persistent_across_future_resets": True,
    "audio_runtime": False,
    "gpio30_access": False,
    "usb_runtime": False,
}
EXPECTED_PREWRITE_ATTEMPT = {
    "occurred_after_authorization_issuance": True,
    "recorded_at": "2026-08-13",
    "result": "failed-closed-before-mutation",
    "stage": "post-stub-prewrite-density-recheck",
    "initial_raw_flash_id": "0x001840c8",
    "later_raw_flash_id": "0xff1840c8",
    "canonical_flash_id": "0x1840c8",
    "flash_begin_called": False,
    "application_bytes_written": 0,
    "partition_modified": False,
    "application_launched": False,
    "device_end_state": "ram-stub-probable",
}


class InstallError(RuntimeError):
    """The E5 exclusive transaction failed closed."""


class Runtime(NamedTuple):
    rom_class: Any
    loader_reset_class: Any
    launch_reset_class: Any
    stub_factory: Any
    binding: Mapping[str, Any]


def _load_restore() -> Any:
    path = RESTORE_PATH.resolve(strict=True)
    spec = importlib.util.spec_from_file_location("doom_e5_install_restore", path)
    if spec is None or spec.loader is None:
        raise InstallError("cannot load exact flash-runtime helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


RESTORE = _load_restore()


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_json(path: pathlib.Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise InstallError(f"cannot read exact {label}: {error}") from error
    if not isinstance(value, dict):
        raise InstallError(f"exact {label} is not an object")
    return value


def _validate_authorization(path: pathlib.Path) -> dict[str, Any]:
    if path.resolve(strict=True) != AUTH_PATH:
        raise InstallError("authorization path is not the exact repository record")
    auth = _load_json(path, "authorization")
    artifact = auth.get("exact_artifact")
    device = auth.get("device_binding")
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "touch-only-persistent-demo"
        and auth.get("gates") == {"composite": 1, "touch": 1, "audio": 0}
        and isinstance(artifact, dict)
        and artifact.get("offset") == "0x10000"
        and artifact.get("app_binary_bytes") == EXPECTED_BYTES
        and artifact.get("app_binary_sha256") == EXPECTED_SHA256
        and device == {
            "identity_kind": "sha256-of-normalized-base-identity",
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "flash_bytes": 16_777_216,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
        }
        and auth.get("build_evidence") == {
            "path": str(EVIDENCE_PATH.relative_to(ROOT)),
            "sha256": _sha256_file(EVIDENCE_PATH),
        }
        and auth.get("install_runtime_binding") == EXPECTED_AUTH_RUNTIME_BINDING
        and auth.get("execution_contract") == EXPECTED_AUTH_EXECUTION_CONTRACT
        and auth.get("prewrite_attempt_history") == [EXPECTED_PREWRITE_ATTEMPT]
    ):
        raise InstallError("active E5 authorization or exact bindings differ")
    evidence = _load_json(EVIDENCE_PATH, "build evidence")
    inventory = evidence.get("source_inventory")
    required = {
        "scripts/doom-e5-install.py",
        "scripts/gamepad-diag-restore.py",
        "scripts/flash.sh",
        "scripts/verify-doom-embedded-touch-audio.py",
        "scripts/tests/test-doom-e5-install.py",
    }
    if not isinstance(inventory, dict) or not required <= set(inventory):
        raise InstallError("build evidence omits the exact install gate inventory")
    for relative, expected in inventory.items():
        if not isinstance(relative, str) or not isinstance(expected, str):
            raise InstallError("build evidence source inventory is malformed")
        candidate = (ROOT / relative).resolve(strict=True)
        if not candidate.is_relative_to(ROOT) or not candidate.is_file():
            raise InstallError("build evidence source inventory leaves the repository")
        if _sha256_file(candidate) != expected:
            raise InstallError(f"frozen source changed before UART open: {relative}")
    return auth


def _read_sealed_artifact(path: pathlib.Path) -> bytes:
    try:
        before = path.lstat()
    except OSError as error:
        raise InstallError(f"cannot stat sealed E5 artifact: {error}") from error
    if not (
        stat.S_ISREG(before.st_mode)
        and not stat.S_ISLNK(before.st_mode)
        and before.st_uid == os.getuid()
        and stat.S_IMODE(before.st_mode) == 0o400
        and before.st_size == EXPECTED_BYTES
    ):
        raise InstallError("sealed E5 artifact metadata is not exact")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino):
            raise InstallError("sealed E5 artifact changed while opening")
        chunks: list[bytes] = []
        remaining = EXPECTED_BYTES
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError("sealed E5 artifact was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError("sealed E5 artifact grew while reading")
    finally:
        os.close(descriptor)
    payload = b"".join(chunks)
    if _sha256_bytes(payload) != EXPECTED_SHA256:
        raise InstallError("sealed E5 artifact hash differs")
    return payload


def _handle_binding(device: Any) -> tuple[int, int, int, int, int]:
    if not getattr(device, "is_open", False) or getattr(device, "exclusive", None) is not True:
        raise InstallError("installer does not own an exclusive open UART")
    try:
        descriptor = device.fileno()
        info = os.fstat(descriptor)
    except (OSError, TypeError, ValueError) as error:
        raise InstallError(f"cannot bind exclusive UART descriptor: {error}") from error
    return (id(device), descriptor, info.st_dev, info.st_ino, info.st_rdev)


def _assert_same_handle(device: Any, binding: tuple[int, int, int, int, int], transport: Any | None = None) -> None:
    if _handle_binding(device) != binding:
        raise InstallError("exclusive UART handle changed during E5 transaction")
    if transport is not None and (
        getattr(transport, "_port", None) is not device or bool(transport.uses_usb_otg())
    ):
        raise InstallError("esptool transport left the exclusive UART handle")


def _set_controls_false(device: Any) -> None:
    device.dtr = False
    device.rts = False
    if bool(device.dtr) or bool(device.rts):
        raise InstallError("UART DTR/RTS did not return inactive")


def _validate_serial_runtime_paths(runtime_paths: Mapping[str, pathlib.Path]) -> None:
    if set(runtime_paths) != set(EXPECTED_SERIAL_RUNTIME):
        raise InstallError("serial runtime path set is incomplete")
    for name, path in runtime_paths.items():
        expected = EXPECTED_SERIAL_RUNTIME[name]
        resolved = path.resolve(strict=True)
        if str(resolved) != expected["path"]:
            raise InstallError(f"serial runtime path changed: {name}")
        if resolved.stat().st_size != expected["bytes"] or _sha256_file(resolved) != expected["sha256"]:
            raise InstallError(f"serial runtime changed: {name}")


def open_serial_once(port: str) -> Any:
    try:
        import serial
        import serial.serialposix
        import serial.serialutil
    except ImportError as error:
        raise InstallError("activate the pinned ESP-IDF/pyserial environment") from error
    if getattr(serial, "__version__", None) != "3.5":
        raise InstallError("pyserial version differs from pinned 3.5")
    runtime_paths = {
        "python": pathlib.Path(sys.executable).resolve(strict=True),
        "serial": pathlib.Path(serial.__file__).resolve(strict=True),
        "serialposix": pathlib.Path(serial.serialposix.__file__).resolve(strict=True),
        "serialutil": pathlib.Path(serial.serialutil.__file__).resolve(strict=True),
        "termios": pathlib.Path(termios.__file__).resolve(strict=True),
        "fcntl": pathlib.Path(fcntl.__file__).resolve(strict=True),
    }
    _validate_serial_runtime_paths(runtime_paths)
    device = serial.Serial(
        port=None, baudrate=115200, timeout=1.0, write_timeout=10.0,
        xonxoff=False, rtscts=False, dsrdtr=False, exclusive=True,
    )
    device.port = port
    device.dtr = False
    device.rts = False
    device.open()
    try:
        attributes = termios.tcgetattr(device.fileno())
        attributes[2] &= ~getattr(termios, "HUPCL", 0)
        termios.tcsetattr(device.fileno(), termios.TCSANOW, attributes)
        verify = termios.tcgetattr(device.fileno())
        if (getattr(termios, "HUPCL", 0) and verify[2] & termios.HUPCL) or (
            device.exclusive is not True or bool(device.dtr) or bool(device.rts)
        ):
            raise InstallError("exclusive/DTR/RTS/HUPCL UART contract failed")
        _handle_binding(device)
    except BaseException:
        _set_controls_false(device)
        device.close()
        raise
    return device


def _validate_runtime_binding(binding: Mapping[str, Any]) -> None:
    if _sha256_file(RESTORE_PATH) != EXPECTED_RESTORE_SHA256:
        raise InstallError("flash-runtime helper bytes differ from the frozen dependency")
    for name, expected in EXPECTED_RUNTIME_HASHES.items():
        value = binding.get(name)
        if not isinstance(value, dict) or value.get("sha256") != expected:
            raise InstallError(f"pinned esptool runtime changed: {name}")
    if binding.get("legacy_rev1_stub", {}).get("sha256") != RESTORE.LEGACY_REV1_STUB_SHA256:
        raise InstallError("pinned ESP32-P4 rev1 stub binding changed")


def _production_runtime() -> Runtime:
    try:
        import esptool.loader
        from esptool.reset import HardReset, UnixTightReset
        from esptool.targets.esp32p4 import ESP32P4ROM
    except ImportError as error:
        raise InstallError("activate the pinned ESP-IDF/esptool environment") from error
    binding = RESTORE.pinned_restore_runtime_binding()
    _validate_runtime_binding(binding)
    if esptool.loader.cfg.get("custom_hard_reset_sequence") is not None:
        raise InstallError("custom reset configuration is prohibited")
    return Runtime(ESP32P4ROM, UnixTightReset, HardReset, RESTORE._load_legacy_stub, binding)


def _read_flash_exact(
    transport: Any, device: Any, handle: tuple[int, int, int, int, int],
    offset: int, byte_count: int, chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> bytes:
    if byte_count <= 0 or not 1 <= chunk_bytes <= MAX_READBACK_CHUNK_BYTES:
        raise InstallError("readback geometry is outside the bounded contract")
    result: list[bytes] = []
    for relative in range(0, byte_count, chunk_bytes):
        count = min(chunk_bytes, byte_count - relative)
        _assert_same_handle(device, handle, transport)
        payload = transport.read_flash(offset + relative, count, progress_fn=None)
        _assert_same_handle(device, handle, transport)
        if not isinstance(payload, bytes) or len(payload) != count:
            raise InstallError("same-handle readback returned the wrong byte count")
        result.append(payload)
    return b"".join(result)


def _write_exact_span(
    stub: Any, device: Any, handle: tuple[int, int, int, int, int], payload: bytes
) -> None:
    if len(payload) != EXPECTED_SPAN_BYTES or len(payload) % WRITE_BLOCK_BYTES:
        raise InstallError("E5 write span is not exact 16 KiB geometry")
    if getattr(stub, "FLASH_WRITE_SIZE", None) != WRITE_BLOCK_BYTES:
        raise InstallError("pinned stub write size is not exact 16 KiB")
    blocks = stub.flash_begin(len(payload), EXPECTED_OFFSET, encrypted_write=False)
    expected_blocks = len(payload) // WRITE_BLOCK_BYTES
    if blocks != expected_blocks:
        raise InstallError("stub returned an unexpected E5 block count")
    for sequence in range(expected_blocks):
        _assert_same_handle(device, handle, stub)
        block = payload[sequence * WRITE_BLOCK_BYTES:(sequence + 1) * WRITE_BLOCK_BYTES]
        stub.flash_block(block, sequence, encrypted=False)
        _assert_same_handle(device, handle, stub)
    # On the pinned stub this is a write-completion synchronizer, not a reset
    # or launch.  The direct launch is the one HardReset after readback below.
    stub.flash_finish(reboot=False)
    _assert_same_handle(device, handle, stub)


def _canonical_exact_flash_id(value: Any, stage: str) -> int:
    """Validate the exact target JEDEC value and remove permitted sign extension.

    Pinned esptool's P4 stub may return the 24-bit RDID as ``0x001840c8`` or,
    after a security-info command, with the unused high byte padded as
    ``0xff1840c8``.  No other high byte or low-24-bit identity is accepted.
    """

    if (
        not isinstance(value, int) or isinstance(value, bool)
        or not 0 <= value <= 0xFFFFFFFF
        or (value & 0xFFFFFF) != EXPECTED_FLASH_JEDEC_LOW24
        or ((value >> 24) & 0xFF) not in ALLOWED_FLASH_JEDEC_HIGH8
    ):
        rendered = f"0x{value:08x}" if isinstance(value, int) else type(value).__name__
        raise InstallError(
            f"exact flash JEDEC check failed at {stage}: raw={rendered}"
        )
    canonical = value & 0xFFFFFF
    print(
        f"P4_DOOM_E5 FLASH_DENSITY stage={stage} raw=0x{value:08x} "
        f"canonical=0x{canonical:06x} size_id=0x{canonical >> 16:02x} "
        f"flash_bytes={RESTORE.FLASH_BYTES}",
        flush=True,
    )
    return canonical


def _fresh_exact_flash_id(esp: Any, stage: str) -> int:
    """Perform one physical RDID and return its exact canonical low 24 bits."""

    return _canonical_exact_flash_id(RESTORE._fresh_flash_id(esp), stage)


def _xmc_flash_id_strict(canonical_flash_id: int) -> bool:
    """Pinned esptool 4.12 XMC test over an already-validated RDID value."""

    rdid = ((canonical_flash_id & 0xFF) << 16) | (
        (canonical_flash_id >> 16) & 0xFF
    ) | (canonical_flash_id & 0xFF00)
    vendor_id = (rdid >> 16) & 0xFF
    mfid = (rdid >> 8) & 0xFF
    cpid = rdid & 0xFF
    return vendor_id == 0x20 and (
        (mfid == 0x40 and 0x13 <= cpid <= 0x20)
        or (mfid == 0x41 and 0x17 <= cpid <= 0x20)
        or (mfid == 0x50 and 0x15 <= cpid <= 0x16)
    )


def _prepare_exact_flash_after_stub(esp: Any) -> int:
    """E5-local pinned post-stub setup with an exact check on every RDID.

    The shared recovery helper intentionally accepts more 16 MiB flash IDs.
    E5 is narrower: its authorized unit has JEDEC low24 ``0x1840c8``.  The
    pinned P4 stub can expose that same RDID as either ``0x001840c8`` or the
    high-byte-padded ``0xff1840c8`` after a security command, so every physical
    read is validated and canonicalized before any policy decision.
    """

    initial_id = _fresh_exact_flash_id(esp, "post-stub-initial-rdid")
    if not _xmc_flash_id_strict(initial_id):
        mf_id = esp.read_spiflash_sfdp(0x10, 8)
        if not isinstance(mf_id, int) or isinstance(mf_id, bool):
            raise InstallError("flash SFDP manufacturer ID has invalid shape")
        if mf_id == 0x20:
            for command in (0xB9, 0x79, 0xFF):
                esp.run_spiflash_command(command)
            time.sleep(0.002)
            esp.run_spiflash_command(0xAB)
            time.sleep(0.00002)
            startup_id = _fresh_exact_flash_id(esp, "post-xmc-startup-rdid")
            if not _xmc_flash_id_strict(startup_id):
                raise InstallError("XMC flash startup flow did not produce a valid ID")

    esp.run_spiflash_command(0x66)
    esp.run_spiflash_command(0x99)
    time.sleep(0.001)
    final_id = _fresh_exact_flash_id(esp, "post-66-99-final-rdid")
    esp.flash_set_parameters(RESTORE.FLASH_BYTES)
    return final_id


def install_same_handle(
    *, device: Any, artifact_path: pathlib.Path, runtime: Runtime | None = None,
    readback_chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> dict[str, Any]:
    handle = _handle_binding(device)
    if bool(device.dtr) or bool(device.rts):
        raise InstallError("UART controls must start inactive")
    selected = runtime or _production_runtime()
    artifact = _read_sealed_artifact(artifact_path)
    install_span = artifact + b"\xff" * (EXPECTED_SPAN_BYTES - EXPECTED_BYTES)
    if len(install_span) != EXPECTED_SPAN_BYTES:
        raise InstallError("E5 padded mutation span is not exact")

    loader_entry_reset_count = 0
    application_launch_reset_count = 0
    try:
        selected.loader_reset_class(device).reset()
        loader_entry_reset_count = 1
        _set_controls_false(device)
        _assert_same_handle(device, handle)

        rom = selected.rom_class(device, 115200, False)
        rom.connect("no_reset", attempts=1, warnings=False)
        _assert_same_handle(device, handle, rom)
        RESTORE._validate_live_rom(rom, device, EXPECTED_DEVICE_SHA256)
        stub_path = pathlib.Path(str(selected.binding["legacy_rev1_stub"]["path"]))
        if RESTORE._sha256_file(stub_path) != RESTORE.LEGACY_REV1_STUB_SHA256:
            raise InstallError("pinned ESP32-P4 rev1 stub changed")
        stub = rom.run_stub(selected.stub_factory(rom, stub_path))
        _assert_same_handle(device, handle, stub)
        if not getattr(stub, "IS_STUB", False):
            raise InstallError("RAM stub did not remain active")
        initial_flash_id = _prepare_exact_flash_after_stub(stub)
        _assert_same_handle(device, handle, stub)

        table = _read_flash_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        )
        table_binding = RESTORE.parse_partition_table(
            table, mutation_offset=EXPECTED_OFFSET, mutation_bytes=EXPECTED_SPAN_BYTES
        )
        # Authoritative same-object target/security/density check immediately
        # before mutation; no shell probe or second process is trusted here.
        RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
        final_flash_id = _fresh_exact_flash_id(stub, "prewrite-policy")
        stub.flash_set_parameters(RESTORE.FLASH_BYTES)
        _assert_same_handle(device, handle, stub)
        if _read_flash_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        ) != table:
            raise InstallError("live partition table changed before mutation")
        if _read_sealed_artifact(artifact_path) != artifact:
            raise InstallError("sealed artifact changed before mutation")
        # The reads above are untrusted operations too. Rebind the target after
        # them so a swap/fault cannot cross the last-check-to-first-write seam.
        RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
        prewrite_flash_id = _fresh_exact_flash_id(stub, "write-boundary")
        stub.flash_set_parameters(RESTORE.FLASH_BYTES)
        _assert_same_handle(device, handle, stub)

        _write_exact_span(stub, device, handle, install_span)
        readback = _read_flash_exact(
            stub, device, handle, EXPECTED_OFFSET, EXPECTED_SPAN_BYTES,
            readback_chunk_bytes,
        )
        if readback != install_span or _sha256_bytes(readback) != _sha256_bytes(install_span):
            raise InstallError("same-handle full mutation-span readback differs")

        table_after = _read_flash_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        )
        if table_after != table:
            raise InstallError("live partition table changed during app-only write")
        if RESTORE.parse_partition_table(
            table_after, mutation_offset=EXPECTED_OFFSET,
            mutation_bytes=EXPECTED_SPAN_BYTES,
        ) != table_binding:
            raise InstallError("postwrite partition-table geometry changed")

        RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
        launch_flash_id = _fresh_exact_flash_id(stub, "prelaunch")
        stub.flash_set_parameters(RESTORE.FLASH_BYTES)
        _assert_same_handle(device, handle, stub)
        _set_controls_false(device)
        # Direct, hash-bound HardReset bypasses esptool's configurable reset
        # path. DTR remains false (IO0 high); this is the sole app launch.
        selected.launch_reset_class(device, uses_usb=False).reset()
        application_launch_reset_count = 1
        _set_controls_false(device)
        _assert_same_handle(device, handle)
    finally:
        _set_controls_false(device)

    chunks = (EXPECTED_SPAN_BYTES + readback_chunk_bytes - 1) // readback_chunk_bytes
    return {
        "offset": "0x10000",
        "artifact_bytes": EXPECTED_BYTES,
        "artifact_sha256": EXPECTED_SHA256,
        "mutation_span_bytes": EXPECTED_SPAN_BYTES,
        "mutation_span_sha256": _sha256_bytes(install_span),
        "readback_bytes": EXPECTED_SPAN_BYTES,
        "readback_sha256": _sha256_bytes(readback),
        "readback_chunks": chunks,
        "readback_max_chunk_bytes": min(readback_chunk_bytes, EXPECTED_SPAN_BYTES),
        "partition_table_sha256": table_binding["sha256"],
        "factory_partition": table_binding["factory"],
        "exclusive_uart": True,
        "same_uart_handle": True,
        "connect_no_reset_attempts": 1,
        "exclusive_transaction_loader_entry_reset_count": loader_entry_reset_count,
        "exclusive_transaction_application_launch_reset_count": application_launch_reset_count,
        "application_launch_count": 1,
        "stub_flash_finish_sync_count": 1,
        "soft_reset_count": 0,
        "esptool_run_count": 0,
        "uart_reopen_count": 0,
        "audio_runtime": False,
        "gpio30_access": False,
        "flash_jedec_low24": "0x1840c8",
        "allowed_raw_flash_ids": ["0x001840c8", "0xff1840c8"],
        "canonical_flash_id": "0x1840c8",
        "initial_flash_id": f"0x{initial_flash_id:06x}",
        "prewrite_flash_id": f"0x{final_flash_id:06x}",
        "write_boundary_flash_id": f"0x{prewrite_flash_id:06x}",
        "prelaunch_flash_id": f"0x{launch_flash_id:06x}",
    }


def _validate_result_path(path: pathlib.Path) -> pathlib.Path:
    parent = path.parent.resolve(strict=True)
    info = parent.lstat()
    if (
        not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode)
        or stat.S_IMODE(info.st_mode) != 0o700 or info.st_uid != os.getuid()
    ):
        raise InstallError("result parent must be an owned exact 0700 directory")
    if path.exists() or path.is_symlink():
        raise InstallError("result path must be new")
    return parent


def _write_result(path: pathlib.Path, result: Mapping[str, Any]) -> None:
    parent = _validate_result_path(path)
    payload = (json.dumps(dict(result), sort_keys=True, separators=(",", ":")) + "\n").encode()
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0), 0o600)
    try:
        written = 0
        while written < len(payload):
            count = os.write(descriptor, payload[written:])
            if count <= 0:
                raise InstallError("result write made no progress")
            written += count
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    directory = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def _arm_signals() -> dict[int, Any]:
    previous: dict[int, Any] = {}
    def interrupted(signum: int, _frame: Any) -> None:
        raise InstallError(f"E5 install interrupted by signal {signum}")
    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, interrupted)
    return previous


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--artifact", type=pathlib.Path, required=True)
    parser.add_argument("--authorization", type=pathlib.Path, required=True)
    parser.add_argument("--result", type=pathlib.Path, required=True)
    args = parser.parse_args()
    previous = _arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        _validate_authorization(args.authorization)
        _read_sealed_artifact(args.artifact)
        _validate_result_path(args.result)
        runtime = _production_runtime()
        device = open_serial_once(args.port)
        result = install_same_handle(device=device, artifact_path=args.artifact, runtime=runtime)
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                _set_controls_false(device)
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
    _write_result(args.result, result)
    print("P4_DOOM_E5 INSTALL PASS same_uart=1 gates=1/1/0 audio_calls=0 app_launches=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
