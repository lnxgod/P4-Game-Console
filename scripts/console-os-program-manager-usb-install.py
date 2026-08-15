#!/usr/bin/env python3

"""Exact-unit app-only update for Program Manager plus USB game storage.

The USB partition table and live game-data volume are already installed.  This
route writes only the 7 MiB application partition, verifies the complete padded
span, proves the partition table and game-data bytes did not change, and keeps
one exclusive J1 UART open through launch and startup acceptance.  It creates
no new flash backup.  Automatic in-process rollback uses the live predecessor
held in memory; crash recovery uses the already-preserved, owner-accepted
Program Manager application from the existing full preimage.
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
from typing import Any, Mapping


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
ROOT = SCRIPT_PATH.parent.parent.resolve(strict=True)
FROZEN_INSTALLER_PATH = ROOT / "scripts/console-os-usb-install.py"
APP_PATH = ROOT / "apps/console_os/build/p4_console_os.bin"
PRIOR_RECOVERY = (
    ROOT / "hardware/local-state/console-os-usb-storage-install-20260814"
)
PRIOR_PREIMAGE = PRIOR_RECOVERY / "live-full-flash-before-usb-storage.bin"

EXPECTED_FROZEN_INSTALLER_SHA256 = (
    "f911c52b63460ec1c972265a35662e7d900dc3eee54e8b452e0cac95ef125b70"
)
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
EXPECTED_PRIOR_PREIMAGE_SHA256 = (
    "d76cc27ef2ec023c488dedaba398accba2f43fb1bfc78da120fa1635245f527c"
)
EXPECTED_CURRENT_APP_SPAN_SHA256 = (
    "31574964e47513d1ddf06daaeb6ea035c702626d4cc70e944a169c1846d2c083"
)
EXPECTED_NEW_APP_BYTES = 5_062_208
EXPECTED_NEW_APP_SHA256 = (
    "bcf38ab8f3c0fbb56e7dd4eca6e31975cdb276fe42d426dec6d9ba5bed77f5d1"
)
EXPECTED_NEW_APP_SPAN_SHA256 = (
    "43544cbe4ce096f016dd9d78460c26accc1e4a542768e19912f53e59c2b57b42"
)
EXPECTED_PARTITION_TABLE_SPAN_SHA256 = (
    "a10833df35f52d59601ef03d7250330c59b65a348597aeb603085b6e2d94b88d"
)
EXPECTED_FALLBACK_APP_BYTES = 4_951_552
EXPECTED_FALLBACK_APP_SHA256 = (
    "636ee38221197c0c7b02c5d04ea19ddfac0a3c6dbd9b2719d8bb21dfb1658a7d"
)
EXPECTED_FALLBACK_SPAN_SHA256 = (
    "4239fc77b1a61165161172a24b460a3073e2a0a71ebf3a9bb13d6cf7a13abed2"
)

FLASH_BYTES = 16 * 1024 * 1024
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SPAN_BYTES = 0x1000
NVS_OFFSET = 0x9000
APP_OFFSET = 0x10000
APP_SPAN_BYTES = 0x700000
GAME_DATA_OFFSET = 0x710000
GAME_DATA_SPAN_BYTES = 0x8F0000
READBACK_CHUNK_BYTES = 512 * 1024
WRITE_BLOCK_BYTES = 0x1000
LEDGER_NAME = "program-manager-usb-update-ledger.json"
CAPTURE_RAW_NAME = "program-manager-usb-startup.raw"
CAPTURE_SUMMARY_NAME = "program-manager-usb-startup.json"
MAX_MODULE_BYTES = 2 * 1024 * 1024

PROGRAM_MANAGER_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=7 "
    b"surface=rgb565-320x200 touch=gt911 native_game_api=1 "
    b"native_format=p4-native-static-v1 game_storage=app-ready "
    b"execution=build-candidate"
)


class UpdateError(RuntimeError):
    """The exact-unit Program Manager app update contract was not met."""


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _load_frozen_installer() -> Any:
    path = FROZEN_INSTALLER_PATH.resolve(strict=True)
    before = path.lstat()
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or stat.S_IMODE(before.st_mode) != 0o755
        or not 0 < before.st_size <= MAX_MODULE_BYTES
    ):
        raise UpdateError("frozen USB installer metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino, opened.st_size) != (
            before.st_dev, before.st_ino, before.st_size
        ):
            raise UpdateError("frozen USB installer changed while opening")
        payload = b""
        while len(payload) < opened.st_size:
            chunk = os.read(descriptor, opened.st_size - len(payload))
            if not chunk:
                raise UpdateError("frozen USB installer was truncated")
            payload += chunk
        if os.read(descriptor, 1):
            raise UpdateError("frozen USB installer grew while reading")
    finally:
        os.close(descriptor)
    if _sha256(payload) != EXPECTED_FROZEN_INSTALLER_SHA256:
        raise UpdateError("frozen USB installer bytes changed")
    name = "console_os_program_manager_frozen_usb_installer"
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    exec(compile(payload, str(path), "exec"), module.__dict__)
    return module


INSTALLER = _load_frozen_installer()
TRANSPORT = INSTALLER.TRANSPORT
CAPTURE = INSTALLER.CAPTURE
CAPTURE.START = PROGRAM_MANAGER_START
CAPTURE.FIXED = (("start", PROGRAM_MANAGER_START), *CAPTURE.FIXED[1:])


def _read_new_app() -> bytes:
    return INSTALLER._read_sealed_payload(
        APP_PATH, "Program Manager USB application", EXPECTED_NEW_APP_BYTES,
        EXPECTED_NEW_APP_SHA256,
    )


def _fallback_span() -> bytes:
    payload = INSTALLER._read_regular(
        PRIOR_PREIMAGE, "existing full preimage", maximum=FLASH_BYTES,
        required_mode=0o600,
    )
    if len(payload) != FLASH_BYTES or _sha256(payload) != (
        EXPECTED_PRIOR_PREIMAGE_SHA256
    ):
        raise UpdateError("existing full preimage differs")
    app = payload[
        APP_OFFSET:APP_OFFSET + EXPECTED_FALLBACK_APP_BYTES
    ]
    if _sha256(app) != EXPECTED_FALLBACK_APP_SHA256:
        raise UpdateError("accepted Program Manager fallback differs")
    span = app + b"\xff" * (APP_SPAN_BYTES - len(app))
    if _sha256(span) != EXPECTED_FALLBACK_SPAN_SHA256:
        raise UpdateError("accepted Program Manager fallback span differs")
    return span


def _persist_capture(
    directory: pathlib.Path, raw: bytes, summary: Mapping[str, Any],
) -> dict[str, Any]:
    return {
        "raw": TRANSPORT._write_new_private(
            directory / CAPTURE_RAW_NAME, raw
        ),
        "summary": TRANSPORT._write_new_private(
            directory / CAPTURE_SUMMARY_NAME,
            (json.dumps(dict(summary), indent=2, sort_keys=True) + "\n").encode(),
        ),
    }


def _read_live_layout(
    stub: Any, device: Any, handle: tuple[int, int, int, int, int],
) -> tuple[bytes, bytes, bytes]:
    prefix = TRANSPORT._read_exact(
        stub, device, handle, 0, APP_OFFSET, READBACK_CHUNK_BYTES
    )
    table = prefix[
        PARTITION_TABLE_OFFSET:
        PARTITION_TABLE_OFFSET + PARTITION_TABLE_SPAN_BYTES
    ]
    if _sha256(table) != EXPECTED_PARTITION_TABLE_SPAN_SHA256:
        raise UpdateError("live USB partition table differs")
    app = TRANSPORT._read_exact(
        stub, device, handle, APP_OFFSET, APP_SPAN_BYTES,
        READBACK_CHUNK_BYTES,
    )
    game_data = TRANSPORT._read_exact(
        stub, device, handle, GAME_DATA_OFFSET, GAME_DATA_SPAN_BYTES,
        READBACK_CHUNK_BYTES,
    )
    return prefix, app, game_data


def _write_and_verify_app(
    *, stub: Any, device: Any, handle: tuple[int, int, int, int, int],
    span: bytes, boundary: str,
) -> str:
    TRANSPORT.RESTORE._validate_live_rom(
        stub, device, EXPECTED_DEVICE_SHA256
    )
    TRANSPORT.E5._fresh_exact_flash_id(stub, boundary)
    stub.flash_set_parameters(FLASH_BYTES)
    TRANSPORT.E5._assert_same_handle(device, handle, stub)
    TRANSPORT._write_span(
        stub, device, handle, APP_OFFSET, span, WRITE_BLOCK_BYTES
    )
    readback = TRANSPORT._read_exact(
        stub, device, handle, APP_OFFSET, APP_SPAN_BYTES,
        READBACK_CHUNK_BYTES,
    )
    if readback != span:
        raise UpdateError("complete application readback differs")
    return _sha256(readback)


def _launch_same_handle(
    device: Any, handle: tuple[int, int, int, int, int],
    runtime: Any, stub: Any,
) -> None:
    TRANSPORT.E5._set_controls_false(device)
    TRANSPORT._restore_app_uart_baud(device, handle, stub)
    runtime.launch_reset_class(device, uses_usb=False).reset()
    TRANSPORT.E5._set_controls_false(device)
    TRANSPORT.E5._assert_same_handle(device, handle)


def _verify_preserved(
    *, stub: Any, device: Any, handle: tuple[int, int, int, int, int],
    prefix: bytes, game_data_sha256: str,
) -> None:
    current_prefix = TRANSPORT._read_exact(
        stub, device, handle, 0, APP_OFFSET, READBACK_CHUNK_BYTES
    )
    if current_prefix != prefix:
        raise UpdateError("bootloader, partition table, NVS, or PHY changed")
    current_game_data = TRANSPORT._read_exact(
        stub, device, handle, GAME_DATA_OFFSET, GAME_DATA_SPAN_BYTES,
        READBACK_CHUNK_BYTES,
    )
    if _sha256(current_game_data) != game_data_sha256:
        raise UpdateError("USB game-data volume changed during app-only write")


def _restore_in_process(
    *, device: Any, runtime: Any, state: dict[str, Any],
    ledger: pathlib.Path, predecessor: bytes, prefix: bytes,
    game_data_sha256: str,
) -> None:
    handle = TRANSPORT.E5._handle_binding(device)
    stub = TRANSPORT._load_stub(device, handle, runtime)
    TRANSPORT._transition(
        ledger, state, "rollback-write-attempted", restore_required=True
    )
    restored = _write_and_verify_app(
        stub=stub, device=device, handle=handle, span=predecessor,
        boundary="program-manager-usb-rollback",
    )
    if restored != EXPECTED_CURRENT_APP_SPAN_SHA256:
        raise UpdateError("predecessor rollback hash differs")
    _verify_preserved(
        stub=stub, device=device, handle=handle, prefix=prefix,
        game_data_sha256=game_data_sha256,
    )
    _launch_same_handle(device, handle, runtime, stub)
    TRANSPORT._transition(
        ledger, state, "predecessor-restored-and-launched",
        restore_required=False, rollback_verified=True,
    )


def _capture_runtime(
    device: Any, directory: pathlib.Path, seconds: float,
) -> tuple[dict[str, Any], Mapping[str, Any]]:
    capture_error: BaseException | None = None
    try:
        raw, summary = CAPTURE.capture_open_handle(device, seconds, min_stats=2)
    except BaseException as error:
        capture_error = error
        raw = getattr(error, "partial_raw", b"")
        summary = getattr(error, "failure_summary", {})
        if not isinstance(raw, bytes) or len(raw) > CAPTURE.MAX_TRANSCRIPT_BYTES:
            raw = b""
        if not isinstance(summary, Mapping):
            summary = {"schema": 1, "result": "fail"}
    binding = _persist_capture(directory, raw, summary)
    if capture_error is not None:
        raise UpdateError(
            "retained startup capture failed: "
            f"{TRANSPORT._bounded_failure(capture_error)}"
        ) from capture_error
    if summary.get("result") != "pass":
        raise UpdateError("retained startup acceptance failed")
    return binding, summary


def _install_same_handle(
    *, device: Any, runtime: Any, directory: pathlib.Path,
    capture_seconds: float,
) -> dict[str, Any]:
    TRANSPORT._owned_directory(directory, "update recovery directory")
    ledger = directory / LEDGER_NAME
    for path in (ledger, directory / CAPTURE_RAW_NAME,
                 directory / CAPTURE_SUMMARY_NAME):
        if path.exists() or path.is_symlink():
            raise UpdateError(f"update output already exists: {path.name}")
    payload = _read_new_app()
    target = payload + b"\xff" * (APP_SPAN_BYTES - len(payload))
    if _sha256(target) != EXPECTED_NEW_APP_SPAN_SHA256:
        raise UpdateError("Program Manager USB padded span differs")
    fallback = _fallback_span()
    handle = TRANSPORT.E5._handle_binding(device)
    state: dict[str, Any] = {
        "schema": 1,
        "phase": "reserved",
        "transition_count": 0,
        "restore_required": False,
        "device_identity_sha256": EXPECTED_DEVICE_SHA256,
        "new_backup_created": False,
        "write_scope": "application-only-0x10000..0x70ffff",
        "target_payload_bytes": len(payload),
        "target_payload_sha256": _sha256(payload),
        "target_span_bytes": len(target),
        "target_span_sha256": _sha256(target),
        "fallback_span_sha256": _sha256(fallback),
        "application_launch_count": 0,
    }
    TRANSPORT._atomic_ledger(ledger, state)
    predecessor = b""
    prefix = b""
    game_data_sha256 = ""
    write_marked = False
    try:
        stub = TRANSPORT._load_stub(device, handle, runtime)
        prefix, predecessor, game_data = _read_live_layout(
            stub, device, handle
        )
        if _sha256(predecessor) != EXPECTED_CURRENT_APP_SPAN_SHA256:
            raise UpdateError("live USB predecessor application differs")
        game_data_sha256 = _sha256(game_data)
        del game_data
        TRANSPORT._transition(
            ledger, state, "live-layout-validated", restore_required=False,
            predecessor_span_sha256=_sha256(predecessor),
            live_game_data_sha256=game_data_sha256,
        )
        TRANSPORT._transition(
            ledger, state, "application-write-attempted",
            restore_required=True,
        )
        write_marked = True
        verified = _write_and_verify_app(
            stub=stub, device=device, handle=handle, span=target,
            boundary="program-manager-usb-write",
        )
        if verified != EXPECTED_NEW_APP_SPAN_SHA256:
            raise UpdateError("Program Manager USB app verification differs")
        _verify_preserved(
            stub=stub, device=device, handle=handle, prefix=prefix,
            game_data_sha256=game_data_sha256,
        )
        TRANSPORT._transition(
            ledger, state, "application-and-storage-verified",
            restore_required=True, verified_app_span_sha256=verified,
            preserved_game_data_sha256=game_data_sha256,
        )
        TRANSPORT.RESTORE._validate_live_rom(
            stub, device, EXPECTED_DEVICE_SHA256
        )
        TRANSPORT.E5._fresh_exact_flash_id(
            stub, "program-manager-usb-prelaunch"
        )
        TRANSPORT.E5._set_controls_false(device)
        TRANSPORT._restore_app_uart_baud(device, handle, stub)
        TRANSPORT._transition(
            ledger, state, "application-launch-attempted",
            restore_required=True, application_launch_count=1,
        )
        runtime.launch_reset_class(device, uses_usb=False).reset()
        TRANSPORT.E5._set_controls_false(device)
        TRANSPORT.E5._assert_same_handle(device, handle)
        capture, summary = _capture_runtime(
            device, directory, capture_seconds
        )
        result = {
            "classification": "program-manager-usb-app-only-accepted",
            "new_backup_created": False,
            "verified_app_span_sha256": verified,
            "preserved_game_data_sha256": game_data_sha256,
            "same_uart_handle": True,
            "application_launch_count": 1,
            "startup_capture": capture,
            "startup_stats_observed": summary.get("stats_observed"),
            "restore_required": False,
        }
        TRANSPORT._transition(
            ledger, state, "runtime-accepted", restore_required=False,
            install_result=result,
        )
        return result
    except BaseException as primary:
        if write_marked and predecessor and prefix and game_data_sha256:
            try:
                TRANSPORT._transition(
                    ledger, state, "rollback-requested", restore_required=True,
                    primary_failure=TRANSPORT._bounded_failure(primary),
                )
                _restore_in_process(
                    device=device, runtime=runtime, state=state, ledger=ledger,
                    predecessor=predecessor, prefix=prefix,
                    game_data_sha256=game_data_sha256,
                )
            except BaseException as restore_error:
                try:
                    TRANSPORT._transition(
                        ledger, state, "rollback-failed", restore_required=True,
                        rollback_failure=TRANSPORT._bounded_failure(restore_error),
                    )
                except BaseException:
                    pass
                raise UpdateError(
                    f"update failed ({primary}); rollback failed "
                    f"({restore_error}); run --recover"
                ) from restore_error
            raise UpdateError(
                f"update failed and live predecessor was restored: {primary}"
            ) from primary
        raise
    finally:
        TRANSPORT.E5._set_controls_false(device)


def install(
    *, port: str, directory: pathlib.Path, capture_seconds: float,
) -> dict[str, Any]:
    previous = TRANSPORT._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = TRANSPORT.E5._production_runtime()
        device = TRANSPORT.E5.open_serial_once(port)
        result = _install_same_handle(
            device=device, runtime=runtime, directory=directory,
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
                    raise UpdateError("exclusive UART did not close")
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise UpdateError("update ended without a result")
    return result


def _recover_same_handle(
    *, device: Any, runtime: Any, directory: pathlib.Path,
) -> dict[str, Any]:
    TRANSPORT._owned_directory(directory, "update recovery directory")
    ledger = directory / LEDGER_NAME
    state = TRANSPORT._load_ledger(ledger)
    TRANSPORT._discard_uncommitted_ledger_replacement(ledger, state)
    if not (
        state.get("schema") == 1
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and state.get("restore_required") is True
        and state.get("target_span_sha256") == EXPECTED_NEW_APP_SPAN_SHA256
        and state.get("new_backup_created") is False
    ):
        raise UpdateError("ledger does not require this exact recovery")
    expected_game_data = state.get("live_game_data_sha256")
    if not isinstance(expected_game_data, str) or len(expected_game_data) != 64:
        raise UpdateError("ledger has no bound game-data digest")
    fallback = _fallback_span()
    handle = TRANSPORT.E5._handle_binding(device)
    stub = TRANSPORT._load_stub(device, handle, runtime)
    prefix = TRANSPORT._read_exact(
        stub, device, handle, 0, APP_OFFSET, READBACK_CHUNK_BYTES
    )
    table = prefix[
        PARTITION_TABLE_OFFSET:
        PARTITION_TABLE_OFFSET + PARTITION_TABLE_SPAN_BYTES
    ]
    if _sha256(table) != EXPECTED_PARTITION_TABLE_SPAN_SHA256:
        raise UpdateError("live USB partition table differs during recovery")
    game_data = TRANSPORT._read_exact(
        stub, device, handle, GAME_DATA_OFFSET, GAME_DATA_SPAN_BYTES,
        READBACK_CHUNK_BYTES,
    )
    if _sha256(game_data) != expected_game_data:
        raise UpdateError("game-data volume changed before recovery")
    del game_data
    TRANSPORT._transition(
        ledger, state, "crash-recovery-write-attempted", restore_required=True
    )
    verified = _write_and_verify_app(
        stub=stub, device=device, handle=handle, span=fallback,
        boundary="program-manager-usb-crash-recovery",
    )
    if verified != EXPECTED_FALLBACK_SPAN_SHA256:
        raise UpdateError("accepted fallback readback differs")
    _verify_preserved(
        stub=stub, device=device, handle=handle, prefix=prefix,
        game_data_sha256=expected_game_data,
    )
    _launch_same_handle(device, handle, runtime, stub)
    result = {
        "classification": "accepted-program-manager-fallback-restored",
        "new_backup_created": False,
        "verified_fallback_span_sha256": verified,
        "preserved_game_data_sha256": expected_game_data,
        "same_uart_handle": True,
        "restore_required": False,
    }
    TRANSPORT._transition(
        ledger, state, "fallback-restored-and-launched",
        restore_required=False, recovery_result=result,
    )
    return result


def recover(*, port: str, directory: pathlib.Path) -> dict[str, Any]:
    previous = TRANSPORT._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = TRANSPORT.E5._production_runtime()
        device = TRANSPORT.E5.open_serial_once(port)
        result = _recover_same_handle(
            device=device, runtime=runtime, directory=directory
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
                    raise UpdateError("exclusive UART did not close")
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise UpdateError("recovery ended without a result")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    parser.add_argument("--capture-seconds", type=float, default=30.0)
    parser.add_argument("--recover", action="store_true")
    args = parser.parse_args()
    directory = args.recovery_directory.resolve(strict=True)
    if args.recover:
        result = recover(port=args.port, directory=directory)
    else:
        result = install(
            port=args.port, directory=directory,
            capture_seconds=args.capture_seconds,
        )
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
