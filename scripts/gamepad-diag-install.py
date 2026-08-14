#!/usr/bin/env python3

"""Exclusive same-UART installer for the one-shot gamepad diagnostic.

The shell route performs policy, artifact, and reservation work before calling
this helper.  This process opens the programming UART exactly once and retains
that exclusive descriptor while it identifies the target, seals its recovery
bytes, binds durable state, revalidates the same target and recovery bytes,
marks the write attempt, writes once, verifies exact readback, and issues the
deferred-launch receipt.  It never invokes the esptool CLI and never reconnects.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import signal
import stat
import sys
import termios
from typing import Any, Callable, Mapping


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
STATE_PATH = SCRIPT_DIR / "gamepad-diag-one-shot-state.py"
RESTORE_PATH = SCRIPT_DIR / "gamepad-diag-restore.py"
CAPTURE_PATH = SCRIPT_DIR / "gamepad-diag-capture.py"
MAX_READBACK_CHUNK_BYTES = 512 * 1024
INSTALL_WRITE_BLOCK_BYTES = 0x4000


class InstallError(RuntimeError):
    """The exclusive install transaction failed closed."""


def _load_exact(name: str, path: pathlib.Path) -> Any:
    resolved = path.resolve(strict=True)
    if resolved.parent != SCRIPT_DIR:
        raise InstallError(f"{name} helper leaves the exact script directory")
    spec = importlib.util.spec_from_file_location(name, resolved)
    if spec is None or spec.loader is None:
        raise InstallError(f"cannot load exact {name} helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STATE = _load_exact("gamepad_diag_install_state", STATE_PATH)
RESTORE = _load_exact("gamepad_diag_install_restore", RESTORE_PATH)


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _read_exact_private_artifact(
    path: pathlib.Path, expected_bytes: int, expected_sha256: str
) -> bytes:
    try:
        expected = path.lstat()
    except OSError as error:
        raise InstallError(f"cannot stat sealed application artifact: {error}") from error
    if not (
        stat.S_ISREG(expected.st_mode)
        and not stat.S_ISLNK(expected.st_mode)
        and expected.st_uid == os.getuid()
        and stat.S_IMODE(expected.st_mode) == 0o400
        and expected.st_size == expected_bytes
    ):
        raise InstallError("sealed application artifact metadata is not exact")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino) != (expected.st_dev, expected.st_ino):
            raise InstallError("sealed application artifact changed while opening")
        chunks: list[bytes] = []
        remaining = expected_bytes
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError("sealed application artifact was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError("sealed application artifact grew while reading")
        payload = b"".join(chunks)
    finally:
        os.close(descriptor)
    if _sha256(payload) != expected_sha256:
        raise InstallError("sealed application artifact hash changed")
    return payload


def _handle_binding(device: Any) -> tuple[int, int, int, int, int]:
    if not getattr(device, "is_open", False) or getattr(device, "exclusive", None) is not True:
        raise InstallError("installer does not own an exclusive open UART")
    try:
        descriptor = device.fileno()
        info = os.fstat(descriptor)
    except (OSError, TypeError, ValueError) as error:
        raise InstallError(f"cannot bind the exclusive UART descriptor: {error}") from error
    return (id(device), descriptor, info.st_dev, info.st_ino, info.st_rdev)


def _assert_same_handle(
    device: Any, binding: tuple[int, int, int, int, int], transport: Any | None = None
) -> None:
    if _handle_binding(device) != binding:
        raise InstallError("exclusive UART handle changed during install transaction")
    if transport is not None and (
        getattr(transport, "_port", None) is not device
        or bool(transport.uses_usb_otg())
    ):
        raise InstallError("esptool transport left the exclusive UART handle")


def _set_controls_false(device: Any) -> None:
    device.dtr = False
    device.rts = False
    if bool(device.dtr) or bool(device.rts):
        raise InstallError("UART DTR/RTS did not return to inactive")


def open_serial_once(port: str) -> Any:
    """Open one exclusive POSIX UART with inactive controls and no HUPCL."""

    try:
        import serial
    except ImportError as error:
        raise InstallError("activate the pinned ESP-IDF/pyserial environment") from error
    if getattr(serial, "__version__", None) != "3.5":
        raise InstallError("pyserial version differs from pinned 3.5")
    device = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=1.0,
        write_timeout=10.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
        exclusive=True,
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


def _read_flash_exact(
    transport: Any,
    device: Any,
    handle: tuple[int, int, int, int, int],
    offset: int,
    byte_count: int,
    *,
    chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> bytes:
    if byte_count <= 0 or not 1 <= chunk_bytes <= MAX_READBACK_CHUNK_BYTES:
        raise InstallError("flash read geometry is outside the bounded contract")
    chunks: list[bytes] = []
    for relative in range(0, byte_count, chunk_bytes):
        count = min(chunk_bytes, byte_count - relative)
        _assert_same_handle(device, handle, transport)
        payload = transport.read_flash(offset + relative, count, progress_fn=None)
        _assert_same_handle(device, handle, transport)
        if not isinstance(payload, bytes) or len(payload) != count:
            raise InstallError("same-handle flash read returned the wrong byte count")
        chunks.append(payload)
    return b"".join(chunks)


def _validate_flash_density(transport: Any) -> int:
    flash_id = RESTORE._fresh_flash_id(transport)
    if not isinstance(flash_id, int) or isinstance(flash_id, bool) or (
        flash_id >> 16
    ) not in {0x18, 0x38}:
        raise InstallError("live flash ID does not prove exact 16 MiB density")
    transport.flash_set_parameters(RESTORE.FLASH_BYTES)
    return flash_id


def _write_exact_install(
    transport: Any,
    device: Any,
    handle: tuple[int, int, int, int, int],
    offset: int,
    payload: bytes,
) -> None:
    if (
        offset != int(STATE.EXPECTED_OFFSET, 0)
        or len(payload) % INSTALL_WRITE_BLOCK_BYTES
        or getattr(transport, "FLASH_WRITE_SIZE", None) != INSTALL_WRITE_BLOCK_BYTES
    ):
        raise InstallError("install write is not exact 16 KiB transport-block geometry")
    _assert_same_handle(device, handle, transport)
    blocks = transport.flash_begin(len(payload), offset, encrypted_write=False)
    expected_blocks = len(payload) // INSTALL_WRITE_BLOCK_BYTES
    if blocks != expected_blocks:
        raise InstallError("stub returned an unexpected install block count")
    for sequence in range(expected_blocks):
        block = payload[
            sequence * INSTALL_WRITE_BLOCK_BYTES : (sequence + 1) * INSTALL_WRITE_BLOCK_BYTES
        ]
        _assert_same_handle(device, handle, transport)
        transport.flash_block(block, sequence, encrypted=False)
        _assert_same_handle(device, handle, transport)
    transport.flash_finish(reboot=False)
    _assert_same_handle(device, handle, transport)


def install_same_handle(
    *,
    device: Any,
    artifact_path: pathlib.Path,
    arm_token_source: pathlib.Path,
    recovery_dir: pathlib.Path,
    state_path: pathlib.Path,
    authorization_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token_file: pathlib.Path,
    runtime: Any | None = None,
    state_api: Any = STATE,
    readback_chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> dict[str, Any]:
    """Run the complete install transaction without releasing its UART handle."""

    handle = _handle_binding(device)
    if bool(device.dtr) or bool(device.rts):
        raise InstallError("UART controls must be inactive before install reset")
    selected = runtime or RESTORE._production_runtime()
    owner_token = state_api.read_owner_token(owner_token_file)
    artifact = _read_exact_private_artifact(
        artifact_path, state_api.EXPECTED_BYTES, state_api.EXPECTED_SHA256
    )
    span = state_api.mutation_span(state_api.EXPECTED_BYTES)
    if not (
        state_api.EXPECTED_OFFSET == STATE.EXPECTED_OFFSET
        and span % INSTALL_WRITE_BLOCK_BYTES == 0
        and len(artifact) < span
    ):
        raise InstallError("active artifact/install span geometry is inconsistent")

    partition_path = recovery_dir / "live-partition-table.bin"
    preimage_path = recovery_dir / "preinstall-mutation-span.bin"
    arm_secret_path = recovery_dir / "arm-token"
    result: dict[str, Any] | None = None
    try:
        # One exact download-mode reset occurs only after the durable reservation
        # exists, and it never releases this descriptor.
        reset = selected.reset_class(device)
        reset.reset()
        _set_controls_false(device)
        _assert_same_handle(device, handle)

        esp = selected.rom_class(device, 115200, False)
        esp.connect("no_reset", attempts=1, warnings=False)
        _assert_same_handle(device, handle, esp)
        RESTORE._validate_live_rom(esp, device, state_api.EXPECTED_DEVICE_SHA256)
        stub_path = pathlib.Path(str(selected.binding["legacy_rev1_stub"]["path"]))
        if RESTORE._sha256_file(stub_path) != RESTORE.LEGACY_REV1_STUB_SHA256:
            raise InstallError("pinned ESP32-P4 rev1 stub changed before upload")
        stub = esp.run_stub(selected.stub_factory(esp, stub_path))
        _assert_same_handle(device, handle, stub)
        if not getattr(stub, "IS_STUB", False):
            raise InstallError("RAM stub did not remain active on the exclusive UART")
        if getattr(stub, "FLASH_WRITE_SIZE", None) != INSTALL_WRITE_BLOCK_BYTES:
            raise InstallError("RAM stub install write size is not exact 16 KiB")
        flash_id = RESTORE._prepare_flash_after_stub(stub)
        _assert_same_handle(device, handle, stub)

        partition = _read_flash_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET, RESTORE.PARTITION_TABLE_BYTES
        )
        preimage = _read_flash_exact(
            stub, device, handle, int(state_api.EXPECTED_OFFSET, 0), span
        )
        RESTORE.parse_partition_table(
            partition,
            mutation_offset=int(state_api.EXPECTED_OFFSET, 0),
            mutation_bytes=span,
        )
        partition_binding = RESTORE.seal_snapshot(
            recovery_dir,
            partition_path.name,
            partition,
            expected_bytes=RESTORE.PARTITION_TABLE_BYTES,
            label="live partition table",
        )
        preimage_binding = RESTORE.seal_snapshot(
            recovery_dir,
            preimage_path.name,
            preimage,
            expected_bytes=span,
            label="restore preimage",
        )
        state_api.stage_arm_secret(
            arm_token_source, arm_secret_path, authorization_path, project_root
        )
        state_api.bind_preimage(
            state_path,
            authorization_path,
            project_root,
            owner_token,
            recovery_dir,
            partition_path,
            preimage_path,
            arm_secret_path,
        )

        # Revalidate the physical target and reread every recovery byte on this
        # same stub immediately before the durable attempt transition.
        _assert_same_handle(device, handle, stub)
        RESTORE._validate_live_rom(stub, device, state_api.EXPECTED_DEVICE_SHA256)
        final_flash_id = _validate_flash_density(stub)
        _assert_same_handle(device, handle, stub)
        partition_reread = _read_flash_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET, RESTORE.PARTITION_TABLE_BYTES
        )
        preimage_reread = _read_flash_exact(
            stub, device, handle, int(state_api.EXPECTED_OFFSET, 0), span
        )
        if not (
            partition_reread == partition
            and preimage_reread == preimage
            and _sha256(partition_reread) == partition_binding["sha256"]
            and _sha256(preimage_reread) == preimage_binding["sha256"]
        ):
            raise InstallError("same-handle live recovery bytes changed before install")
        artifact_reread = _read_exact_private_artifact(
            artifact_path, state_api.EXPECTED_BYTES, state_api.EXPECTED_SHA256
        )
        if artifact_reread != artifact:
            raise InstallError("sealed application artifact changed before install")

        state_api.mark_install_write_attempt(
            state_path, authorization_path, project_root, owner_token
        )
        install_payload = artifact + b"\xff" * (span - len(artifact))
        _write_exact_install(
            stub, device, handle, int(state_api.EXPECTED_OFFSET, 0), install_payload
        )
        installed_span = _read_flash_exact(
            stub,
            device,
            handle,
            int(state_api.EXPECTED_OFFSET, 0),
            span,
            chunk_bytes=readback_chunk_bytes,
        )
        span_readback_sha256 = _sha256(installed_span)
        readback = installed_span[: state_api.EXPECTED_BYTES]
        readback_sha256 = _sha256(readback)
        if not (
            installed_span == install_payload
            and span_readback_sha256 == _sha256(install_payload)
            and readback == artifact
            and readback_sha256 == state_api.EXPECTED_SHA256
        ):
            raise InstallError("same-handle installed full-span readback differs")
        readback_chunks = (
            span + readback_chunk_bytes - 1
        ) // readback_chunk_bytes
        state_api.mark_installed_verified(
            state_path, owner_token, readback_sha256, span_readback_sha256
        )
        result = {
            "offset": state_api.EXPECTED_OFFSET,
            "bytes": state_api.EXPECTED_BYTES,
            "sha256": state_api.EXPECTED_SHA256,
            "readback_bytes": state_api.EXPECTED_BYTES,
            "readback_sha256": readback_sha256,
            "readback_chunks": readback_chunks,
            "readback_max_chunk_bytes": min(
                readback_chunk_bytes, span
            ),
            "mutation_span_bytes": span,
            "install_span_sha256": _sha256(install_payload),
            "install_span_readback_sha256": span_readback_sha256,
            "partition_table_sha256": partition_binding["sha256"],
            "preimage_sha256": preimage_binding["sha256"],
            "same_uart_handle": True,
            "exclusive_uart": True,
            "download_reset_count": 1,
            "connect_no_reset_attempts": 1,
            "write_attempt_count": 1,
            "write_after_action": "no_reset",
            "readback_after_action": "no_reset",
            "post_install_reset_count": 0,
            "chip_revision": RESTORE.EXACT_CHIP_REVISION,
            "flash_bytes": RESTORE.FLASH_BYTES,
            "initial_flash_id": f"0x{flash_id:06x}",
            "final_flash_id": f"0x{final_flash_id:06x}",
        }
        _assert_same_handle(device, handle, stub)
    finally:
        _set_controls_false(device)
    if result is None:
        raise InstallError("install transaction ended without exact readback")
    return result


def _write_result(path: pathlib.Path, result: Mapping[str, Any]) -> None:
    parent = path.parent.resolve(strict=True)
    payload = (json.dumps(dict(result), sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
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


def _arm_signal_handlers() -> dict[int, Any]:
    previous: dict[int, Any] = {}

    def interrupted(signum: int, _frame: Any) -> None:
        raise InstallError(f"install interrupted by signal {signum}")

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, interrupted)
    return previous


def _restore_signal_handlers(previous: Mapping[int, Any]) -> None:
    for signum, handler in previous.items():
        signal.signal(signum, handler)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--artifact", type=pathlib.Path, required=True)
    parser.add_argument("--arm-token-file", type=pathlib.Path, required=True)
    parser.add_argument("--recovery-dir", type=pathlib.Path, required=True)
    parser.add_argument("--state", type=pathlib.Path, required=True)
    parser.add_argument("--authorization", type=pathlib.Path, required=True)
    parser.add_argument("--project-root", type=pathlib.Path, required=True)
    parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    parser.add_argument("--receipt", type=pathlib.Path, required=True)
    parser.add_argument("--verifier", type=pathlib.Path, required=True)
    parser.add_argument("--result", type=pathlib.Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    device = None
    primary_error: BaseException | None = None
    result: dict[str, Any] | None = None
    capture: Any | None = None
    root: pathlib.Path | None = None
    previous = _arm_signal_handlers()
    try:
        root = STATE.exact_project_root(args.project_root)
        recovery = STATE.validate_recovery_directory(args.recovery_dir)
        owner_token = STATE.read_owner_token(args.owner_token_file)
        STATE.check_reservation(
            args.state, args.authorization, root, owner_token
        )
        _read_exact_private_artifact(
            args.artifact, STATE.EXPECTED_BYTES, STATE.EXPECTED_SHA256
        )
        if args.result.exists():
            raise InstallError("install result path must be new")
        capture = _load_exact("gamepad_diag_install_capture", CAPTURE_PATH)
        capture.active_runtime_identity()
        selected = RESTORE._production_runtime()
        device = open_serial_once(args.port)

        result = install_same_handle(
            device=device,
            artifact_path=args.artifact,
            arm_token_source=args.arm_token_file,
            recovery_dir=recovery,
            state_path=args.state,
            authorization_path=args.authorization,
            project_root=root,
            owner_token_file=args.owner_token_file,
            runtime=selected,
            state_api=STATE,
        )
    except BaseException as error:
        primary_error = error
        print(f"Gamepad install transaction failed closed: {error}", file=sys.stderr)
    finally:
        try:
            if device is not None and getattr(device, "is_open", False):
                try:
                    _set_controls_false(device)
                finally:
                    device.close()
                if getattr(device, "is_open", False):
                    close_error = InstallError(
                        "exclusive UART remained open after transaction"
                    )
                    if primary_error is None:
                        primary_error = close_error
                    print(f"Gamepad install transaction failed closed: {close_error}", file=sys.stderr)
        finally:
            _restore_signal_handlers(previous)
    if primary_error is not None or result is None or capture is None or root is None:
        return 1
    # Receipt publication is deliberately outside the exclusive UART lifetime.
    # A close failure or any install uncertainty leaves installed-verified,
    # restore-required state with neither a receipt nor a published result.
    try:
        namespace = argparse.Namespace(
            receipt=args.receipt,
            state=args.state,
            owner_token_file=args.owner_token_file,
            authorization=args.authorization,
            project_root=root,
            port=args.port,
            device_identity_sha256=STATE.EXPECTED_DEVICE_SHA256,
            offset=STATE.EXPECTED_OFFSET,
            bytes=STATE.EXPECTED_BYTES,
            sha256=STATE.EXPECTED_SHA256,
            readback_bytes=int(result["readback_bytes"]),
            readback_sha256=str(result["readback_sha256"]),
            readback_chunks=int(result["readback_chunks"]),
            readback_max_chunk_bytes=int(result["readback_max_chunk_bytes"]),
            mutation_span_bytes=int(result["mutation_span_bytes"]),
            install_span_sha256=str(result["install_span_sha256"]),
            install_span_readback_sha256=str(
                result["install_span_readback_sha256"]
            ),
            verifier=args.verifier,
        )
        capture.emit_receipt(namespace)
        _write_result(args.result, result)
    except BaseException as error:
        print(f"Gamepad install receipt/result failed closed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
