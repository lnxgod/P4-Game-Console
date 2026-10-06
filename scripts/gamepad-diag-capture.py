#!/usr/bin/env python3

"""Issue, consume, capture, and restore a native gamepad diagnostic run.

The active protocol is deliberately fail closed.  A private per-artifact arm
token is sent exactly once only after the firmware publishes WAIT_ARM with USB
still disabled.  After the bounded diagnostic terminates, the same UART owner
restores and verifies the install-transport-block-rounded pre-install bytes
using bounded 4 KiB writes before any PASS result can be committed or
published.  The committed repository authorization
is inactive, so none of these hardware operations are currently issuable.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import re
import secrets
import signal
import stat
import sys
import termios
import tempfile
import time
import uuid
from typing import Any

# The sibling filename is hyphenated for CLI consistency; load it without
# allowing an unrelated module on PYTHONPATH to satisfy the restoration seam.
RESTORE_TOOL_PATH = pathlib.Path(__file__).resolve().with_name(
    "gamepad-diag-restore.py"
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
STATE_TOOL_PATH = ROOT / "scripts/gamepad-diag-one-shot-state.py"
IDF_COMMIT = "2c211b236707889e8400c4dc5644dd5c4ee071e0"
ESPTOOL_VERSION = "4.12.0"
PYSERIAL_VERSION = "3.5"
RECEIPT_MAX_AGE_SECONDS = 15 * 60
MAX_SERIAL_BYTES = 1024 * 1024
DESCRIPTOR_SHA256 = (
    "05a151c932362fee13503905962880ce75212bbf8c43c95701527b8290833162"
)
AUTH_ID = "gamepad-diag-d1-one-shot-authorization-2026-08-13"
ARM_FRAME_PREFIX = f"P4_GAMEPAD_D1_ARM {AUTH_ID} "
ARM_FRAME_BYTES = len(ARM_FRAME_PREFIX) + 64 + 1
IDF_LOG_PREFIX_RE = re.compile(r"^[IWE] \([0-9]+\) [A-Za-z0-9_.-]+: ")
HEX64 = r"[0-9a-f]{64}"
TRANSPORT_CONNECTED_RE = re.compile(
    r"GAMEPAD_CONNECTED session=(\d+) vid=0079 pid=0011 interface=(\d+) "
    r"protocol=0 descriptor_bytes=101 descriptor_sha256="
    + DESCRIPTOR_SHA256
    + r" profile=usb-gamepad-0079-0011 capabilities=0x00000003"
)
DIAG_CONNECTED_RE = re.compile(
    r"GAMEPAD_D1_CONNECTED session=(\d+) vid=0079 pid=0011 interface=(\d+) "
    r"transport=usb-hid descriptor_sha256="
    + DESCRIPTOR_SHA256
    + r" profile=usb-gamepad-0079-0011 capabilities=0x00000003 "
    r"reconnect_after_disconnect=(0|1)"
)
STATE_RE = re.compile(
    r"GAMEPAD_D1_STATE session=(\d+) sequence=(\d+) connected=(0|1) "
    r"buttons=([0-9a-f]{16}) dpad=(\d+) lx=(-?\d+) ly=(-?\d+) "
    r"rx=(-?\d+) ry=(-?\d+) lt=(\d+) rt=(\d+)"
)
ACTIVE_RE = re.compile(
    r"GAMEPAD_D1_ACTIVE_INPUT session=(\d+) sequence=(\d+) "
    r"buttons=([0-9a-f]{16}) dpad=(\d+)"
)
DISCONNECTED_RE = re.compile(
    r"GAMEPAD_D1_DISCONNECTED session=(\d+) sequence=(\d+) "
    r"source=(device-event|controlled-cleanup)"
)
NEUTRAL_RE = re.compile(
    r"GAMEPAD_D1_NEUTRAL session=(\d+) "
    r"source=(device-event|controlled-cleanup) neutral=(0|1) "
    r"held_input_before=(0|1)"
)
STATS_RE = re.compile(
    r"GAMEPAD_D1_STATS interfaces_seen=(\d+) interfaces_rejected=(\d+) "
    r"connections=(\d+) disconnections=(\d+) reports_committed=(\d+) "
    r"reports_dropped=(\d+) malformed_reports=(\d+) callback_faults=(\d+)"
)
PROGRESS_RE = re.compile(
    r"GAMEPAD_D1_PROGRESS elapsed_ms=(\d+) connected=(0|1) exact=(0|1) "
    r"reports=(\d+) disconnect=(0|1) reconnect=(0|1)"
)
WINDOW_COMPLETE_RE = re.compile(
    r"GAMEPAD_D1_WINDOW_COMPLETE reason=disconnect-reconnect-report"
)
QUIESCING_RE = re.compile(r"USB_HOST_QUIESCING root_port_enabled=0")
HOST_STOPPED_RE = re.compile(r"USB_HOST_STOPPED")
PLATFORM_DISCONNECTED_RE = re.compile(
    r"GAMEPAD_DISCONNECTED session=(\d+) "
    r"reason=(removed|transfer-error|queue-overflow|decode-error|shutdown|unknown) "
    r"neutral=1"
)
CLEANUP_RE = re.compile(
    r"GAMEPAD_D1_CLEANUP quiesce=ESP_OK gamepad_stop=ESP_OK "
    r"gamepad_stop_attempted=1 final_snapshot=ESP_OK final_neutral=1 "
    r"host_stop=ESP_OK host_stop_attempted=1 resources_retained=0"
)
RESULT_RE = re.compile(
    r"GAMEPAD_D1_RESULT result=PASS reason=none "
    r"exact_identity_profile=1 report_seen=1 active_input_seen=1 "
    r"initial_enumeration=1 physical_disconnect=(0|1) "
    r"disconnect_neutral=(0|1) held_disconnect_neutral=(0|1) "
    r"reconnect=(0|1) cleanup_neutral=1 cleanup_complete=1"
)
REJECT_MARKERS = (
    b"GAMEPAD_D1_FAIL",
    b"GAMEPAD_D1_HALT",
    b"GAMEPAD_D1_BLOCKED",
    b"GAMEPAD_D1_HOST_ROLLBACK",
    b"USB_HOST_BLOCKED",
    b"USB_DAEMON_ERROR",
    b"USB_HOST_FAULT",
    b"USB_HOST_ROOT_PORT_ENABLE_FAILED",
    b"USB_HOST_STOP_FAILED",
    b"GAMEPAD_USB_FAULT",
    b"GAMEPAD_INTERFACE_REJECT",
    b"GAMEPAD_STOP_WARN",
    b"Guru Meditation Error",
    b"panic'ed",
    b"Task watchdog got triggered",
    b"Brownout detector was triggered",
    b"waiting for download",
)
PREFIX_REJECT_MARKERS = (
    b"Guru Meditation Error",
    b"panic'ed",
    b"Task watchdog got triggered",
    b"Brownout detector was triggered",
    b"waiting for download",
    b"ets ",
)
BENIGN_RESET_CODES = {"0x1", "0x3", "0x16", "0x17"}
PROTECTED_TOKENS = (
    "GAMEPAD_",
    "USB_HOST_",
    "USB_DAEMON_",
    "P4_GAMEPAD_D1_ARM",
)
LIFECYCLE_PREFIXES = (
    "GAMEPAD_D1_SERIAL_ATTACH ",
    "GAMEPAD_D1_BOOT ",
    "GAMEPAD_D1_WAIT_ARM ",
    "GAMEPAD_D1_ARM_ACCEPTED ",
    "USB_HOST_READY ",
    "GAMEPAD_USB_READY ",
    "USB_HOST_ROOT_PORT_ENABLED ",
    "GAMEPAD_D1_READY ",
    "GAMEPAD_D1_ACTIVE_INPUT ",
    "GAMEPAD_D1_CLEANUP ",
    "GAMEPAD_D1_STATS ",
    "GAMEPAD_D1_RESULT result=PASS ",
    "GAMEPAD_D1_TERMINAL ",
)


class CaptureError(RuntimeError):
    pass


def arm_terminal_signal_handlers() -> dict[int, Any]:
    previous: dict[int, Any] = {}

    def interrupt(signum: int, _frame: Any) -> None:
        raise CaptureError(f"capture interrupted by signal {signum}")

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, interrupt)
    return previous


def restore_signal_handlers(previous: dict[int, Any]) -> None:
    for signum, handler in previous.items():
        signal.signal(signum, handler)


def ignore_terminal_signals() -> None:
    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, signal.SIG_IGN)


def load_state_tool():
    spec = importlib.util.spec_from_file_location("gamepad_diag_state", STATE_TOOL_PATH)
    if spec is None or spec.loader is None:
        raise CaptureError("cannot load gamepad one-shot state helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


STATE = load_state_tool()


def load_restore_tool():
    spec = importlib.util.spec_from_file_location("gamepad_diag_restore", RESTORE_TOOL_PATH)
    if spec is None or spec.loader is None:
        raise CaptureError("cannot load gamepad restore helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def utc_text(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def parse_utc(value: object) -> dt.datetime:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise CaptureError("receipt timestamp is not canonical UTC")
    try:
        parsed = dt.datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise CaptureError("receipt timestamp is invalid") from error
    if parsed.tzinfo != dt.timezone.utc:
        raise CaptureError("receipt timestamp is not UTC")
    return parsed


def fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def private_regular(path: pathlib.Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as error:
        raise CaptureError(f"cannot stat {label}: {error}") from error
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise CaptureError(f"{label} is not a regular non-symlink file")
    if info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o600:
        raise CaptureError(f"{label} must be owned by this user with exact mode 0600")
    return info


def atomic_write(path: pathlib.Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    parent_info = path.parent.lstat()
    if not stat.S_ISDIR(parent_info.st_mode) or stat.S_ISLNK(parent_info.st_mode):
        raise CaptureError("output parent is not a real directory")
    if path.parent != pathlib.Path("/private/tmp") and parent_info.st_mode & 0o077:
        raise CaptureError("output parent permissions are not private")
    temporary = path.parent / f".{path.name}.{uuid.uuid4().hex}.tmp"
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.close(descriptor)
        descriptor = -1
        try:
            os.link(temporary, path)
        except FileExistsError as error:
            raise CaptureError(f"refusing to overwrite output: {path}") from error
        os.unlink(temporary)
        fsync_directory(path.parent)
        private_regular(path, "created output")
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        temporary.unlink(missing_ok=True)


def canonical_json(value: dict[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def load_json(path: pathlib.Path, label: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise CaptureError(f"cannot read {label}: {error}") from error
    if not isinstance(value, dict):
        raise CaptureError(f"{label} must contain an object")
    return value


def validate_new_receipt_path(path: pathlib.Path) -> pathlib.Path:
    if path.name.startswith("gamepad-diag-receipt.") is False or re.fullmatch(
        r"gamepad-diag-receipt\.[A-Za-z0-9._-]+", path.name
    ) is None:
        raise CaptureError("receipt filename must use the exact gamepad-diag-receipt.* prefix")
    if path.exists() or path.is_symlink():
        raise CaptureError("receipt path must not already exist")
    try:
        parent_info = path.parent.lstat()
    except OSError as error:
        raise CaptureError(f"receipt parent is unavailable: {error}") from error
    if (
        not stat.S_ISDIR(parent_info.st_mode)
        or stat.S_ISLNK(parent_info.st_mode)
        or parent_info.st_uid != os.getuid()
        or stat.S_IMODE(parent_info.st_mode) != 0o700
    ):
        raise CaptureError("receipt parent must be an owned non-symlink 0700 directory")
    parent = path.parent.resolve(strict=True)
    allowed_roots = {
        pathlib.Path("/private/tmp").resolve(),
        pathlib.Path("/tmp").resolve(),
        pathlib.Path(tempfile.gettempdir()).resolve(),
    }
    if parent in allowed_roots or not any(parent.parent == root for root in allowed_roots):
        raise CaptureError("receipt parent must be a dedicated child of the system temporary root")
    return parent / path.name


def active_runtime_identity() -> dict[str, Any]:
    try:
        import esptool
        import esptool.loader
        import esptool.reset
        import esptool.targets.esp32p4
        import serial
        import serial.serialposix
    except ImportError as error:
        raise CaptureError("activate the pinned ESP-IDF environment first") from error
    if esptool.__version__ != ESPTOOL_VERSION or serial.__version__ != PYSERIAL_VERSION:
        raise CaptureError("esptool or pyserial version changed")
    if esptool.loader.cfg.get("custom_hard_reset_sequence") is not None:
        raise CaptureError("custom esptool hard-reset sequence is prohibited")
    def module_entry(module: Any) -> dict[str, str]:
        path = pathlib.Path(module.__file__).resolve(strict=True)
        return {"path": str(path), "sha256": sha256_file(path)}
    python_path = pathlib.Path(sys.executable).resolve(strict=True)
    identity = {
        "python_executable": str(python_path),
        "python_executable_sha256": sha256_file(python_path),
        "python_version": sys.version,
        "idf_commit": IDF_COMMIT,
        "esptool_version": esptool.__version__,
        "esptool_custom_hard_reset_sequence": None,
        "esptool_path": str(pathlib.Path(esptool.__file__).resolve()),
        "esptool_sha256": sha256_file(pathlib.Path(esptool.__file__).resolve()),
        "pyserial_version": serial.__version__,
        "pyserial_path": str(pathlib.Path(serial.__file__).resolve()),
        "pyserial_sha256": sha256_file(pathlib.Path(serial.__file__).resolve()),
        "esptool_modules": {
            "loader": module_entry(esptool.loader),
            "reset": module_entry(esptool.reset),
            "esp32p4": module_entry(esptool.targets.esp32p4),
        },
        "pyserial_modules": {
            "serialposix": module_entry(serial.serialposix),
        },
    }
    restore_binding = load_restore_tool().pinned_restore_runtime_binding()
    if restore_binding.get("legacy_rev1_stub", {}).get("sha256") != (
        "3c0f27938055192977123cd4b503bf27d1676c59a7fd7e3f93de8e95b0cf63bf"
    ):
        raise CaptureError("selected ESP32-P4 rev1 restore stub changed")
    identity["restore_runtime"] = restore_binding
    return identity


def launch_contract() -> dict[str, Any]:
    return {
        "single_open_uart_handle": True,
        "connect_before_hard_reset": "no_reset",
        "durable_hard_reset_attempt_before_reset": True,
        "launch_hard_reset_count": 1,
        "esptool_run_count": 0,
        "arm_frame_transmit_count": 1,
        "arm_frame_bytes": ARM_FRAME_BYTES,
        "post_arm_runtime_serial_receive_only": True,
        "receipt_and_arm_secret_unlinked_and_fsynced_before_reset": True,
        "restore_entry_durable_before_unix_tight_reset": True,
        "restore_write_after_action": "no_reset",
        "restore_readback_max_chunk_bytes": 524288,
        "restore_verified_before_pass": True,
        "post_restore_reset_count": 0,
        "pre_reset_dtr": False,
        "post_reset_dtr": False,
        "runtime_dtr": False,
        "pre_reset_rts": False,
        "expected_reset_rts_pulse": True,
        "post_reset_rts": False,
        "runtime_rts": False,
        "hupcl": False,
    }


def emit_receipt(args: argparse.Namespace) -> None:
    args.receipt = validate_new_receipt_path(args.receipt)
    owner_token = STATE.read_owner_token(args.owner_token_file)
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = STATE.validate_active_authorization(args.authorization, args.project_root)
    state = STATE.load_object(args.state, "one-shot state")
    if not (
        state.get("status") == "installed-verified"
        and state.get("restore_required") is True
        and state.get("arm_token_sha256") == auth["host_arm"]["token_sha256"]
        and state.get("install_readback_sha256") == STATE.EXPECTED_SHA256
        and state.get("install_span_readback_bytes")
        == STATE.mutation_span(STATE.EXPECTED_BYTES)
        and state.get("install_span_readback_sha256")
        == args.install_span_readback_sha256
    ):
        raise CaptureError("installed diagnostic has no exact bound restore state")
    if not (
        args.offset == STATE.EXPECTED_OFFSET
        and args.bytes == STATE.EXPECTED_BYTES
        and args.sha256 == STATE.EXPECTED_SHA256
        and args.readback_bytes == STATE.EXPECTED_BYTES
        and args.readback_sha256 == STATE.EXPECTED_SHA256
        and 1 <= args.readback_chunks
        and 1 <= args.readback_max_chunk_bytes <= 524288
        and args.mutation_span_bytes == STATE.mutation_span(STATE.EXPECTED_BYTES)
        and re.fullmatch(r"[0-9a-f]{64}", str(args.install_span_sha256))
        is not None
        and args.install_span_readback_sha256 == args.install_span_sha256
    ):
        raise CaptureError("write/readback identity differs from active exact artifact")
    runtime = active_runtime_identity()
    restore_tool = load_restore_tool()
    restore_tool.validate_partition_binding(
        state.get("live_partition_table", {}),
        expected_offset=int(STATE.EXPECTED_OFFSET, 0),
        expected_span=STATE.mutation_span(STATE.EXPECTED_BYTES),
    )
    created = utc_now()
    receipt = {
        "schema": 1,
        "kind": "gamepad-diag-host-arm-restore-capture-receipt",
        "authorization_id": STATE.AUTH_ID,
        "authorization_path": str(args.authorization.resolve()),
        "authorization_sha256": sha256_file(args.authorization),
        "project_root": str(ROOT),
        "fixture_evidence_path": str(fixture_path.resolve()),
        "fixture_evidence_sha256": fixture_sha256,
        "metadata_path": str(metadata_path.resolve()),
        "metadata_sha256": metadata_sha256,
        "board_profile_path": str(profile_path.resolve()),
        "board_profile_sha256": profile_sha256,
        "arm_token_sha256": auth["host_arm"]["token_sha256"],
        "arm_secret": state.get("arm_secret"),
        "live_partition_table": state.get("live_partition_table"),
        "restore_preimage": state.get("restore_preimage"),
        "fixture": {
            "id": auth["fixture_evidence"]["id"],
            "current_limit_ma": fixture["qualification"]["current_limit_ma"],
        },
        "state_path": str(args.state.resolve()),
        "owner_token": owner_token,
        "port": args.port,
        "device_identity_sha256": args.device_identity_sha256,
        "artifact": {
            "offset": args.offset,
            "bytes": args.bytes,
            "sha256": args.sha256,
        },
        "readback": {
            "bytes": args.readback_bytes,
            "sha256": args.readback_sha256,
            "chunks": args.readback_chunks,
            "max_chunk_bytes": args.readback_max_chunk_bytes,
        },
        "install_span": {
            "bytes": args.mutation_span_bytes,
            "sha256": args.install_span_sha256,
            "readback_sha256": args.install_span_readback_sha256,
        },
        "created_at_utc": utc_text(created),
        "expires_at_utc": utc_text(
            created + dt.timedelta(seconds=RECEIPT_MAX_AGE_SECONDS)
        ),
        "nonce": secrets.token_hex(32),
        "capture_tool": {
            "path": str(pathlib.Path(__file__).resolve()),
            "sha256": sha256_file(pathlib.Path(__file__).resolve()),
        },
        "state_tool": {
            "path": str(STATE_TOOL_PATH.resolve()),
            "sha256": sha256_file(STATE_TOOL_PATH),
        },
        "restore_tool": {
            "path": str(RESTORE_TOOL_PATH.resolve()),
            "sha256": sha256_file(RESTORE_TOOL_PATH),
        },
        "verifier": {
            "path": str(args.verifier.resolve()),
            "sha256": sha256_file(args.verifier),
        },
        "runtime_identity": runtime,
        "launch_contract": launch_contract(),
    }
    payload = canonical_json(receipt)
    receipt_sha256 = sha256_bytes(payload)
    # Bind the durable ledger first. An interruption before the receipt write
    # leaves an unusable pending state rather than an untracked launch permit.
    STATE.bind_receipt(
        args.state,
        args.authorization,
        args.project_root,
        owner_token,
        args.receipt,
        receipt_sha256,
    )
    atomic_write(args.receipt, payload)
    print(
        "Gamepad hard-reset receipt issued: "
        f"path={args.receipt} sha256={receipt_sha256} expires={receipt['expires_at_utc']}"
    )


def validate_bound_file(entry: object, label: str) -> None:
    if not isinstance(entry, dict):
        raise CaptureError(f"receipt {label} binding is missing")
    path = pathlib.Path(str(entry.get("path", ""))).resolve()
    if not path.is_file() or sha256_file(path) != entry.get("sha256"):
        raise CaptureError(f"receipt {label} binding changed")


def core_file_binding(entry: object) -> dict[str, object]:
    if not isinstance(entry, dict):
        raise CaptureError("private file binding is missing")
    keys = {"path", "device", "inode", "mode", "bytes", "sha256"}
    if not keys.issubset(entry):
        raise CaptureError("private file binding is incomplete")
    return {key: entry[key] for key in keys}


def validate_receipt(path: pathlib.Path, args: argparse.Namespace) -> dict[str, Any]:
    private_regular(path, "receipt")
    receipt = load_json(path, "receipt")
    if not (
        receipt.get("schema") == 1
        and receipt.get("kind") == "gamepad-diag-host-arm-restore-capture-receipt"
        and receipt.get("authorization_id") == STATE.AUTH_ID
        and receipt.get("port") == args.port
        and receipt.get("device_identity_sha256") == STATE.EXPECTED_DEVICE_SHA256
        and receipt.get("artifact")
        == {
            "offset": STATE.EXPECTED_OFFSET,
            "bytes": STATE.EXPECTED_BYTES,
            "sha256": STATE.EXPECTED_SHA256,
        }
        and receipt.get("readback", {}).get("bytes") == STATE.EXPECTED_BYTES
        and receipt.get("readback", {}).get("sha256") == STATE.EXPECTED_SHA256
        and receipt.get("readback", {}).get("chunks", 0) >= 1
        and 1 <= receipt.get("readback", {}).get("max_chunk_bytes", 0) <= 524288
        and receipt.get("readback", {}).get("chunks", 0)
        == math.ceil(
            STATE.EXPECTED_BYTES
            / receipt.get("readback", {}).get("max_chunk_bytes", 1)
        )
        and receipt.get("install_span", {}).get("bytes")
        == STATE.mutation_span(STATE.EXPECTED_BYTES)
        and re.fullmatch(
            r"[0-9a-f]{64}", str(receipt.get("install_span", {}).get("sha256", ""))
        )
        is not None
        and receipt.get("install_span", {}).get("readback_sha256")
        == receipt.get("install_span", {}).get("sha256")
        and re.fullmatch(r"[0-9a-f]{64}", str(receipt.get("owner_token", "")))
        and re.fullmatch(r"[0-9a-f]{64}", str(receipt.get("nonce", "")))
        and receipt.get("launch_contract") == launch_contract()
    ):
        raise CaptureError("receipt identity or execution scope changed")
    now = utc_now()
    created = parse_utc(receipt.get("created_at_utc"))
    expires = parse_utc(receipt.get("expires_at_utc"))
    if not created <= now <= expires or expires - created > dt.timedelta(
        seconds=RECEIPT_MAX_AGE_SECONDS
    ):
        raise CaptureError("receipt expired")
    auth_path = pathlib.Path(str(receipt.get("authorization_path", ""))).resolve()
    state_path = pathlib.Path(str(receipt.get("state_path", ""))).resolve()
    metadata_path = pathlib.Path(str(receipt.get("metadata_path", ""))).resolve()
    profile_path = pathlib.Path(str(receipt.get("board_profile_path", ""))).resolve()
    if auth_path != args.authorization.resolve() or state_path != args.state.resolve():
        raise CaptureError("receipt paths differ from capture arguments")
    if sha256_file(auth_path) != receipt.get("authorization_sha256"):
        raise CaptureError("authorization changed after receipt issuance")
    (
        auth,
        fixture,
        expected_fixture_path,
        fixture_sha256,
        expected_metadata_path,
        metadata_sha256,
        expected_profile_path,
        profile_sha256,
    ) = STATE.validate_active_authorization(auth_path, args.project_root)
    if not (
        receipt.get("project_root") == str(ROOT)
        and pathlib.Path(str(receipt.get("fixture_evidence_path", ""))).resolve()
        == expected_fixture_path
        and
        receipt.get("fixture_evidence_sha256") == fixture_sha256
        and metadata_path == expected_metadata_path
        and receipt.get("metadata_sha256") == metadata_sha256
        and sha256_file(metadata_path) == metadata_sha256
        and profile_path == expected_profile_path
        and receipt.get("board_profile_sha256") == profile_sha256
        and sha256_file(profile_path) == profile_sha256
        and receipt.get("arm_token_sha256") == auth["host_arm"]["token_sha256"]
        and receipt.get("fixture")
        == {
            "id": auth["fixture_evidence"]["id"],
            "current_limit_ma": fixture["qualification"]["current_limit_ma"],
        }
    ):
        raise CaptureError("authorization bindings changed after receipt issuance")
    state = STATE.load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "pending-capture"
        and state.get("terminal") is False
        and state.get("receipt_path") == str(path.resolve())
        and state.get("receipt_sha256") == sha256_file(path)
        and state.get("owner_token_sha256")
        == sha256_bytes(receipt["owner_token"].encode("ascii"))
        and state.get("metadata_path") == str(metadata_path)
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path)
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("arm_token_sha256") == receipt.get("arm_token_sha256")
        and state.get("arm_secret") == receipt.get("arm_secret")
        and state.get("restore_preimage") == receipt.get("restore_preimage")
        and state.get("live_partition_table") == receipt.get("live_partition_table")
        and state.get("install_span_readback_bytes")
        == receipt.get("install_span", {}).get("bytes")
        and state.get("install_span_readback_sha256")
        == receipt.get("install_span", {}).get("readback_sha256")
    ):
        raise CaptureError("durable ledger differs from pending receipt")
    for field in ("arm_secret", "restore_preimage", "live_partition_table"):
        binding = receipt.get(field)
        binding_path = pathlib.Path(str(binding.get("path", ""))) if isinstance(binding, dict) else pathlib.Path()
        if not STATE.binding_matches(binding_path, core_file_binding(binding)):
            raise CaptureError(f"bound {field} changed after receipt issuance")
    if not (
        receipt["arm_secret"].get("bytes") == 65
        and receipt["restore_preimage"].get("offset") == int(STATE.EXPECTED_OFFSET, 0)
        and receipt["restore_preimage"].get("sector_bytes") == STATE.SECTOR_BYTES
        and receipt["restore_preimage"].get("install_write_block_bytes")
        == STATE.INSTALL_WRITE_BLOCK_BYTES
        and receipt["restore_preimage"].get("mutation_span_bytes")
        == STATE.mutation_span(STATE.EXPECTED_BYTES)
        and receipt["live_partition_table"].get("offset")
        == STATE.PARTITION_TABLE_OFFSET
        and receipt["live_partition_table"].get("bytes")
        == STATE.PARTITION_TABLE_BYTES
        and receipt["live_partition_table"].get("validated_md5") is True
        and receipt["live_partition_table"].get("restore_runtime")
        == active_runtime_identity().get("restore_runtime")
        and receipt["live_partition_table"].get("restore_tool")
        == receipt.get("restore_tool")
    ):
        raise CaptureError("restore/preimage geometry changed")
    load_restore_tool().validate_partition_binding(
        receipt["live_partition_table"],
        expected_offset=int(STATE.EXPECTED_OFFSET, 0),
        expected_span=STATE.mutation_span(STATE.EXPECTED_BYTES),
    )
    validate_bound_file(receipt.get("capture_tool"), "capture tool")
    validate_bound_file(receipt.get("state_tool"), "state tool")
    validate_bound_file(receipt.get("restore_tool"), "restore tool")
    validate_bound_file(receipt.get("verifier"), "verifier")
    if receipt.get("runtime_identity") != active_runtime_identity():
        raise CaptureError("pinned host runtime identity changed")
    return receipt


def open_serial_once(port: str):
    import serial

    device = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=0.05,
        write_timeout=1.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
        exclusive=True,
    )
    device.port = port
    device.dtr = False
    device.rts = False
    device.open()
    attributes = termios.tcgetattr(device.fileno())
    attributes[2] &= ~getattr(termios, "HUPCL", 0)
    termios.tcsetattr(device.fileno(), termios.TCSANOW, attributes)
    verify = termios.tcgetattr(device.fileno())
    if (getattr(termios, "HUPCL", 0) and verify[2] & termios.HUPCL) or (
        not device.exclusive or device.dtr or device.rts
    ):
        device.close()
        raise CaptureError("exclusive/DTR/RTS/HUPCL serial contract failed")
    return device


def live_identity(esp: Any) -> str:
    mac = esp.read_mac()
    if not isinstance(mac, tuple) or len(mac) != 6:
        raise CaptureError("live base identity has invalid shape")
    normalized = "".join(f"{value:02x}" for value in mac)
    return sha256_bytes(normalized.encode("ascii"))


def launch_single_hard_reset(esp: Any, device: Any) -> None:
    try:
        esp.hard_reset()
    finally:
        device.dtr = False
        device.rts = False


def _obsolete_substring_summarize_never_use(payload: bytes) -> dict[str, Any]:
    """Historical parser kept temporarily for review comparison; never called."""
    lower = payload.lower()
    rejects = [
        marker.decode("ascii", "replace")
        for marker in REJECT_MARKERS
        if marker.lower() in lower
    ]
    marker_counts = {
        marker.decode("ascii"): payload.count(marker) for marker in REQUIRED_MARKERS
    }
    transport_connected = [
        match.groups() for match in TRANSPORT_CONNECTED_RE.finditer(payload)
    ]
    diag_connected = [match.groups() for match in DIAG_CONNECTED_RE.finditer(payload)]
    states = [match.groups() for match in STATE_RE.finditer(payload)]
    active_matches = [match.groups() for match in ACTIVE_RE.finditer(payload)]
    stats = [tuple(int(value) for value in match.groups()) for match in STATS_RE.finditer(payload)]
    state_sessions = {int(values[0]) for values in states}
    states_by_session: dict[int, list[int]] = {}
    for values in states:
        states_by_session.setdefault(int(values[0]), []).append(int(values[1]))
    state_monotonic = all(
        all(later > earlier for earlier, later in zip(sequences, sequences[1:]))
        for sequences in states_by_session.values()
    )
    transport_pairs = [(int(values[0]), int(values[1])) for values in transport_connected]
    diag_pairs = [(int(values[0]), int(values[1])) for values in diag_connected]
    transport_sessions = [session for session, _interface in transport_pairs]
    diag_sessions = [session for session, _interface in diag_pairs]
    session_identity_consistent = (
        bool(transport_pairs)
        and transport_pairs == diag_pairs
        and state_sessions == set(diag_sessions)
    )
    stats_exact = len(stats) == 1 and (
        stats[0][0] == len(transport_pairs)
        and stats[0][1] == 0
        and stats[0][2] == len(transport_pairs)
        and stats[0][3] == stats[0][2]
        and stats[0][4] >= 1
        and stats[0][5:] == (0, 0, 0)
    )
    cleanup_match = re.search(
        rb"GAMEPAD_D1_CLEANUP[^\r\n]*final_neutral=1[^\r\n]*"
        rb"resources_retained=0",
        payload,
    )
    result_match = re.search(
        rb"GAMEPAD_D1_RESULT result=PASS reason=none "
        rb"exact_identity_profile=1 report_seen=1 active_input_seen=1 "
        rb"initial_enumeration=1 "
        rb"physical_disconnect=(0|1) disconnect_neutral=(0|1) "
        rb"held_disconnect_neutral=(0|1) reconnect=(0|1) "
        rb"cleanup_neutral=1 cleanup_complete=1",
        payload,
    )
    active_state_exact = False
    if len(active_matches) == 1:
        active_session, active_sequence, active_buttons, active_dpad = active_matches[0]
        nonneutral = int(active_buttons, 16) != 0 or int(active_dpad) != 0
        active_state_exact = nonneutral and any(
            values[0] == active_session
            and values[1] == active_sequence
            and values[2] == b"1"
            and values[3].lower() == active_buttons.lower()
            and values[4] == active_dpad
            and (int(values[3], 16) != 0 or int(values[4]) != 0)
            for values in states
        )
    boot_position = payload.find(b"GAMEPAD_D1_BOOT")
    ready_position = payload.find(b"GAMEPAD_D1_READY", boot_position + 1)
    transport_position = payload.find(b"GAMEPAD_CONNECTED", boot_position + 1)
    diag_position = payload.find(b"GAMEPAD_D1_CONNECTED", boot_position + 1)
    state_position = payload.find(b"GAMEPAD_D1_STATE", diag_position + 1)
    active_position = payload.find(b"GAMEPAD_D1_ACTIVE_INPUT", state_position + 1)
    cleanup_position = payload.find(b"GAMEPAD_D1_CLEANUP", active_position + 1)
    stats_position = payload.find(b"GAMEPAD_D1_STATS", cleanup_position + 1)
    result_position = payload.find(b"GAMEPAD_D1_RESULT result=PASS", stats_position + 1)
    marker_order_valid = (
        0 <= boot_position < ready_position < diag_position
        and boot_position < transport_position < diag_position
        and diag_position < state_position < active_position < cleanup_position
        < stats_position < result_position
    )
    prefix = payload[:boot_position] if boot_position >= 0 else payload
    post_boot = payload[boot_position:] if boot_position >= 0 else b""
    reset_count = len(re.findall(rb"(?i)\brst:\s*0x[0-9a-f]+\b", prefix))
    no_post_boot_reset = not re.search(rb"(?i)(?:\brst:|ESP-ROM:)", post_boot)
    passed = (
        not rejects
        and session_identity_consistent
        and all(count == 1 for count in marker_counts.values())
        and len(states) >= 1
        and state_monotonic
        and stats_exact
        and cleanup_match is not None
        and result_match is not None
        and active_state_exact
        and marker_order_valid
        and reset_count == 1
        and no_post_boot_reset
    )
    return {
        "controller_identity_matches": session_identity_consistent,
        "controller_descriptor_sha256": DESCRIPTOR_SHA256,
        "transport_connected_sessions": transport_sessions,
        "diagnostic_connected_sessions": diag_sessions,
        "transport_connected_session_interfaces": transport_pairs,
        "diagnostic_connected_session_interfaces": diag_pairs,
        "marker_counts": marker_counts,
        "state_count": len(states),
        "state_sessions": sorted(state_sessions),
        "state_sequences_strictly_monotonic": state_monotonic,
        "stats_values": stats,
        "stats_exact_and_fault_free": stats_exact,
        "cleanup_complete_and_neutral": cleanup_match is not None,
        "smoke_result_exact": result_match is not None,
        "active_input_marker_count": len(active_matches),
        "active_input_matches_non_neutral_state": active_state_exact,
        "marker_order_valid": marker_order_valid,
        "pre_boot_reset_count": reset_count,
        "post_boot_reset_absent": no_post_boot_reset,
        "physical_disconnect_observed": (
            bool(result_match) and result_match.group(1) == b"1"
        ),
        "disconnect_neutral_observed": (
            bool(result_match) and result_match.group(2) == b"1"
        ),
        "held_disconnect_neutral_observed": (
            bool(result_match) and result_match.group(3) == b"1"
        ),
        "reconnect_observed": bool(result_match) and result_match.group(4) == b"1",
        "reject_markers": rejects,
        "result": "pass" if passed else "fail",
    }


def _runtime_lines(payload: bytes) -> tuple[list[str], list[str]]:
    """Return exact normalized runtime lines and structural rejection reasons."""

    lines: list[str] = []
    malformed: list[str] = []
    if payload and not payload.endswith(b"\n"):
        fragment = payload.rsplit(b"\n", 1)[-1]
        if any(token.encode("ascii") in fragment for token in PROTECTED_TOKENS):
            malformed.append("unterminated-final-protected-line")
    for number, raw_line in enumerate(payload.split(b"\n"), 1):
        if raw_line.endswith(b"\r"):
            raw_line = raw_line[:-1]
        if len(raw_line) > 1024:
            malformed.append(f"line-{number}-over-1024-bytes")
            continue
        try:
            decoded = raw_line.decode("ascii")
        except UnicodeDecodeError:
            if any(token.encode("ascii") in raw_line for token in PROTECTED_TOKENS):
                malformed.append(f"line-{number}-non-ascii-protected-event")
            continue
        normalized = IDF_LOG_PREFIX_RE.sub("", decoded, count=1)
        protected = any(token in normalized for token in PROTECTED_TOKENS)
        if protected and any(ord(char) < 0x20 for char in normalized):
            malformed.append(f"line-{number}-control-byte")
            continue
        if protected and not normalized.startswith(PROTECTED_TOKENS):
            malformed.append(f"line-{number}-leading-or-concatenated-marker")
            continue
        if normalized.startswith(PROTECTED_TOKENS):
            lines.append(normalized)
    return lines, malformed


def _exact_matches(pattern: re.Pattern[str], lines: list[str]) -> list[re.Match[str]]:
    return [match for line in lines if (match := pattern.fullmatch(line)) is not None]


U32_MAX = (1 << 32) - 1
U8_MAX = (1 << 8) - 1


def _bounded_unsigned(value: str, maximum: int) -> bool:
    """Return whether one canonical decimal capture fits its firmware type."""

    return int(value) <= maximum


def summarize(
    payload: bytes, expected: dict[str, Any] | None = None
) -> dict[str, Any]:
    """Validate one bounded D1 transcript using only full-line contracts."""

    expected = expected or {}
    fixture_id = str(expected.get("fixture_id", "fixture-test"))
    fixture_sha256 = str(expected.get("fixture_sha256", "1" * 64))
    current_limit_ma = int(expected.get("current_limit_ma", 250))
    arm_token_sha256 = str(expected.get("arm_token_sha256", "2" * 64))
    lines, malformed = _runtime_lines(payload)
    raw_lower = payload.lower()
    rejects = sorted(
        {
            marker.decode("ascii")
            for marker in REJECT_MARKERS
            if marker.lower() in raw_lower
        }
    )
    lifecycle_patterns = {
        "serial_attach": re.compile(
            r"GAMEPAD_D1_SERIAL_ATTACH wait_ms=2500 complete=1 "
            r"root_data_port_enabled=0"
        ),
        "boot": re.compile(
            r"GAMEPAD_D1_BOOT usb_component=1\.5\.0 hid_component=1\.2\.0 "
            r"controller=p4-hs serial_attach_delay_ms=2500 "
            r"root_data_port_enabled=0"
        ),
        "wait_arm": re.compile(
            r"GAMEPAD_D1_WAIT_ARM auth=" + re.escape(AUTH_ID)
            + r" timeout_ms=15000 root_data_port_enabled=0"
        ),
        "arm_accepted": re.compile(
            r"GAMEPAD_D1_ARM_ACCEPTED auth=" + re.escape(AUTH_ID)
            + r" token_sha256=" + re.escape(arm_token_sha256)
            + r" tx_count=1 root_data_port_enabled=0"
        ),
        "usb_host_ready": re.compile(
            r"USB_HOST_READY controller=p4-hs peripheral=0 root_port_enabled=0 "
            r"fixture=" + re.escape(fixture_id)
            + r" limit_ma=" + str(current_limit_ma)
        ),
        "gamepad_usb_ready": re.compile(
            r"GAMEPAD_USB_READY tier=generic-hid report_max=1024"
        ),
        "root_enabled": re.compile(
            r"USB_HOST_ROOT_PORT_ENABLED class_leases_present=1"
        ),
        "diag_ready": re.compile(
            r"GAMEPAD_D1_READY tier=1-generic-hid fixture="
            + re.escape(fixture_id)
            + r" evidence_sha256=" + re.escape(fixture_sha256)
            + r" external_vbus_fixture_owned=1 root_data_port_enabled=1 "
            r"window_ms=120000"
        ),
        "active": ACTIVE_RE,
        "quiescing": QUIESCING_RE,
        "host_stopped": HOST_STOPPED_RE,
        "cleanup": CLEANUP_RE,
        "stats": STATS_RE,
        "result": RESULT_RE,
        "terminal": re.compile(
            r"GAMEPAD_D1_TERMINAL state=halted root_data_port_enabled=0 "
            r"resources_retained=0 automatic_retry=0"
        ),
    }
    # The current firmware marker does not include the report baseline/current
    # counters needed to prove that a report arrived after reconnect.  Treat it
    # as non-acceptance evidence until a future artifact binds those counters.
    optional_success_patterns = (PROGRESS_RE,)
    accepted_record_patterns = (
        TRANSPORT_CONNECTED_RE,
        DIAG_CONNECTED_RE,
        STATE_RE,
        DISCONNECTED_RE,
        NEUTRAL_RE,
        PLATFORM_DISCONNECTED_RE,
    )
    rejection_prefixes = tuple(marker.decode("ascii") for marker in REJECT_MARKERS)
    for index, value in enumerate(lines, 1):
        if any(pattern.fullmatch(value) is not None for pattern in lifecycle_patterns.values()):
            continue
        if any(pattern.fullmatch(value) is not None for pattern in accepted_record_patterns):
            continue
        if any(pattern.fullmatch(value) is not None for pattern in optional_success_patterns):
            continue
        if value.startswith(rejection_prefixes):
            continue
        malformed.append(f"protected-line-{index}-has-no-exact-grammar")
    matches = {
        name: _exact_matches(pattern, lines)
        for name, pattern in lifecycle_patterns.items()
    }
    lifecycle_counts = {name: len(found) for name, found in matches.items()}
    lifecycle_exact = all(count == 1 for count in lifecycle_counts.values())

    transport_records = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := TRANSPORT_CONNECTED_RE.fullmatch(value)) is not None
    ]
    transport_matches = [match for _index, match in transport_records]
    diag_records = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := DIAG_CONNECTED_RE.fullmatch(value)) is not None
    ]
    diag_matches = [match for _index, match in diag_records]
    state_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := STATE_RE.fullmatch(value)) is not None
    ]
    disconnect_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := DISCONNECTED_RE.fullmatch(value)) is not None
    ]
    neutral_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := NEUTRAL_RE.fullmatch(value)) is not None
    ]
    platform_disconnect_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := PLATFORM_DISCONNECTED_RE.fullmatch(value)) is not None
    ]
    progress_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := PROGRESS_RE.fullmatch(value)) is not None
    ]
    window_complete_matches = [
        (index, match)
        for index, value in enumerate(lines)
        if (match := WINDOW_COMPLETE_RE.fullmatch(value)) is not None
    ]
    transport_pairs = [(int(m.group(1)), int(m.group(2))) for m in transport_matches]
    diag_pairs = [(int(m.group(1)), int(m.group(2))) for m in diag_matches]
    diag_reconnect = [int(m.group(3)) for m in diag_matches]
    states = [
        {
            "session": int(m.group(1)),
            "sequence": int(m.group(2)),
            "connected": int(m.group(3)),
            "buttons": int(m.group(4), 16),
            "dpad": int(m.group(5)),
            "axes": tuple(int(m.group(index)) for index in range(6, 10)),
            "triggers": (int(m.group(10)), int(m.group(11))),
            "line": m.group(0),
            "line_index": line_index,
        }
        for line_index, m in state_matches
    ]
    state_sessions = {state["session"] for state in states}
    sequences: dict[int, list[int]] = {}
    for state in states:
        sequences.setdefault(state["session"], []).append(state["sequence"])
    state_monotonic = all(
        all(later > earlier for earlier, later in zip(values, values[1:]))
        for values in sequences.values()
    )
    canonical_states = all(
        state["connected"] == 1
        and 0 <= state["buttons"] <= 0x3FF
        and state["dpad"] in {0, 1, 2, 3, 4, 6, 8, 9, 12}
        and state["axes"] == (0, 0, 0, 0)
        and state["triggers"] == (0, 0)
        for state in states
    )

    active_state_exact = False
    if len(matches["active"]) == 1:
        active = matches["active"][0]
        active_index = lines.index(active.group(0))
        session = int(active.group(1))
        sequence = int(active.group(2))
        buttons = int(active.group(3), 16)
        dpad = int(active.group(4))
        immediately_prior_state = next(
            (
                state
                for state in states
                if state["line_index"] == active_index - 1
            ),
            None,
        )
        active_state_exact = (
            _bounded_unsigned(active.group(1), U32_MAX)
            and _bounded_unsigned(active.group(2), U32_MAX)
            and 0 <= buttons <= 0x3FF
            and dpad in {0, 1, 2, 3, 4, 6, 8, 9, 12}
            and (buttons != 0 or dpad != 0)
            and immediately_prior_state is not None
            and immediately_prior_state["session"] == session
            and immediately_prior_state["sequence"] == sequence
            and immediately_prior_state["connected"] == 1
            and immediately_prior_state["buttons"] == buttons
            and immediately_prior_state["dpad"] == dpad
        )

    result_bits = (0, 0, 0, 0)
    if len(matches["result"]) == 1:
        result_bits = tuple(int(value) for value in matches["result"][0].groups())
    physical_bit, neutral_bit, held_bit, reconnect_bit = result_bits
    disconnected = [
        {
            "session": int(m.group(1)),
            "sequence": int(m.group(2)),
            "source": m.group(3),
            "line": m.group(0),
            "line_index": line_index,
        }
        for line_index, m in disconnect_matches
    ]
    neutrals = [
        {
            "session": int(m.group(1)),
            "source": m.group(2),
            "neutral": int(m.group(3)),
            "held": int(m.group(4)),
            "line": m.group(0),
            "line_index": line_index,
        }
        for line_index, m in neutral_matches
    ]
    physical_disconnects = [e for e in disconnected if e["source"] == "device-event"]
    physical_neutrals = [e for e in neutrals if e["source"] == "device-event"]
    cleanup_disconnects = [e for e in disconnected if e["source"] == "controlled-cleanup"]
    cleanup_neutrals = [e for e in neutrals if e["source"] == "controlled-cleanup"]
    platform_disconnects = [
        {
            "session": int(match.group(1)),
            "reason": match.group(2),
            "line_index": line_index,
        }
        for line_index, match in platform_disconnect_matches
    ]
    platform_removed = [
        event for event in platform_disconnects if event["reason"] == "removed"
    ]
    platform_shutdown = [
        event for event in platform_disconnects if event["reason"] == "shutdown"
    ]
    platform_fault_disconnects = [
        event
        for event in platform_disconnects
        if event["reason"] not in {"removed", "shutdown"}
    ]

    def state_before(session: int, line_index: int) -> dict[str, Any] | None:
        candidates = [
            state for state in states
            if state["session"] == session and state["line_index"] < line_index
        ]
        return max(candidates, key=lambda state: state["line_index"], default=None)

    def paired_events(left: list[dict[str, Any]], right: list[dict[str, Any]]) -> bool:
        return len(left) == len(right) and all(
            first["session"] == second["session"]
            and second["neutral"] == 1
            and first["line_index"] < second["line_index"]
            and second["line_index"] == first["line_index"] + 1
            and (prior := state_before(first["session"], first["line_index"]))
            is not None
            and first["sequence"] > prior["sequence"]
            and (
                first["sequence"] > prior["sequence"] + 1
                or second["held"]
                == int(prior["buttons"] != 0 or prior["dpad"] != 0)
            )
            for first, second in zip(left, right)
        )

    disconnect_sequences_valid = all(
        (prior := state_before(event["session"], event["line_index"])) is not None
        and event["sequence"] > prior["sequence"]
        for event in disconnected
    )
    ordered_state_disconnect_sequences = sorted(
        [
            (state["line_index"], state["sequence"])
            for state in states
        ]
        + [
            (event["line_index"], event["sequence"])
            for event in disconnected
        ]
    )
    global_sequence_monotonic = all(
        later_sequence > earlier_sequence
        for (_earlier_index, earlier_sequence), (_later_index, later_sequence)
        in zip(ordered_state_disconnect_sequences, ordered_state_disconnect_sequences[1:])
    )
    held_observed = any(event["held"] == 1 for event in physical_neutrals)
    physical_truth = bool(physical_disconnects)
    reconnect_truth = any(
        flag == 1
        and index > 0
        and index < len(transport_records)
        and diag_pairs[index][0] != diag_pairs[index - 1][0]
        and any(
            event["session"] == diag_pairs[index - 1][0]
            and event["line_index"] < transport_records[index][0]
            and any(
                neutral["session"] == event["session"]
                and event["line_index"] < neutral["line_index"]
                < transport_records[index][0]
                for neutral in physical_neutrals
            )
            for event in physical_disconnects
        )
        and transport_records[index][0] < diag_records[index][0]
        for index, flag in enumerate(diag_reconnect)
    )
    cleanup_index = (
        lines.index(matches["cleanup"][0].group(0))
        if len(matches["cleanup"]) == 1 else -1
    )
    stats_index = (
        lines.index(matches["stats"][0].group(0))
        if len(matches["stats"]) == 1 else -1
    )
    quiesce_index = (
        lines.index(matches["quiescing"][0].group(0))
        if len(matches["quiescing"]) == 1 else -1
    )
    host_stopped_index = (
        lines.index(matches["host_stopped"][0].group(0))
        if len(matches["host_stopped"]) == 1 else -1
    )
    session_ids = [session for session, _interface in diag_pairs]
    physical_platform_correlation = (
        len(platform_removed) == len(physical_disconnects)
        and all(
            platform["session"] == diagnostic["session"]
            and platform["line_index"] + 1 == diagnostic["line_index"]
            for platform, diagnostic in zip(
                platform_removed, physical_disconnects
            )
        )
    )
    final_session = session_ids[-1] if session_ids else 0
    final_session_physically_disconnected = any(
        event["session"] == final_session and event["line_index"] < quiesce_index
        for event in physical_disconnects
    )
    cleanup_platform_correlation = False
    if quiesce_index >= 0 and host_stopped_index >= 0 and cleanup_index >= 0:
        if final_session and not final_session_physically_disconnected:
            cleanup_platform_correlation = (
                len(platform_shutdown) == 1
                and platform_shutdown[0]["session"] == final_session
                and platform_shutdown[0]["line_index"] == quiesce_index + 1
                and host_stopped_index == platform_shutdown[0]["line_index"] + 1
                and cleanup_index == host_stopped_index + 1
                and len(cleanup_disconnects) == len(cleanup_neutrals) == 1
                and cleanup_disconnects[0]["session"] == final_session
                and cleanup_disconnects[0]["line_index"] == cleanup_index + 1
                and cleanup_neutrals[0]["line_index"] == cleanup_index + 2
            )
        else:
            cleanup_platform_correlation = (
                not platform_shutdown
                and host_stopped_index == quiesce_index + 1
                and cleanup_index == host_stopped_index + 1
                and not cleanup_disconnects
                and not cleanup_neutrals
            )
    exact_platform_disconnects = (
        not platform_fault_disconnects
        and physical_platform_correlation
        and cleanup_platform_correlation
        and len(platform_disconnects) == len(session_ids)
    )
    reconnect_predecessors_exact = bool(session_ids) and all(
        (
            index == 0
            and flag == 0
        )
        or (
            index > 0
            and index < len(transport_records)
            and flag == 1
            and any(
                event["session"] == session_ids[index - 1]
                and event["line_index"] < transport_records[index][0]
                and any(
                    neutral["session"] == event["session"]
                    and event["line_index"] < neutral["line_index"]
                    < transport_records[index][0]
                    < diag_records[index][0]
                    for neutral in physical_neutrals
                )
                for event in physical_disconnects
            )
        )
        for index, flag in enumerate(diag_reconnect)
    )
    exact_session_lifecycle = (
        session_ids == list(range(1, len(session_ids) + 1))
        and len(set(session_ids)) == len(session_ids)
        and len(transport_records) == len(diag_records) == len(session_ids)
        and all(
            transport_records[index][0] < diag_records[index][0]
            for index in range(len(session_ids))
        )
        and all(
            any(
                state["session"] == session
                and diag_records[index][0] < state["line_index"] < cleanup_index
                for state in states
            )
            for index, session in enumerate(session_ids)
        )
        and [event["session"] for event in disconnected] == session_ids
        and [event["session"] for event in neutrals] == session_ids
        and [event["session"] for event in platform_disconnects] == session_ids
        and reconnect_predecessors_exact
    )
    runtime_phase_order = (
        cleanup_index >= 0
        and stats_index > cleanup_index
        and 0 <= quiesce_index < host_stopped_index < cleanup_index
        and all(index < cleanup_index for index, _match in transport_records)
        and all(index < cleanup_index for index, _match in diag_records)
        and all(index < quiesce_index for index, _match in state_matches)
        and all(index < quiesce_index for index, _match in progress_matches)
        and all(index < quiesce_index for index, _match in window_complete_matches)
        and all(event["line_index"] < quiesce_index for event in physical_disconnects)
        and all(event["line_index"] < quiesce_index for event in physical_neutrals)
        and all(event["line_index"] < quiesce_index for event in platform_removed)
        and all(
            quiesce_index < event["line_index"] < host_stopped_index
            for event in platform_shutdown
        )
        and all(
            cleanup_index < event["line_index"] < stats_index
            for event in cleanup_disconnects + cleanup_neutrals
        )
    )
    hotplug_exact = (
        paired_events(physical_disconnects, physical_neutrals)
        and paired_events(cleanup_disconnects, cleanup_neutrals)
        and exact_platform_disconnects
        and disconnect_sequences_valid
        and global_sequence_monotonic
        and exact_session_lifecycle
        and runtime_phase_order
        and (diag_reconnect[0] == 0 if diag_reconnect else False)
        and physical_bit == int(physical_truth)
        and neutral_bit == int(physical_truth)
        and held_bit == int(held_observed)
        and reconnect_bit == int(reconnect_truth)
        and (held_bit == 0 or neutral_bit == physical_bit == 1)
        and (reconnect_bit == 0 or physical_bit == 1)
        and (any(flag == 1 for flag in diag_reconnect) == reconnect_truth)
        and all(
            event["session"] in {session for session, _interface in diag_pairs}
            and any(
                state["session"] == event["session"]
                and state["line_index"] < event["line_index"]
                for state in states
            )
            for event in physical_disconnects
        )
    )

    stats_values = [tuple(int(value) for value in m.groups()) for m in matches["stats"]]
    numeric_bounds_exact = (
        all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            and _bounded_unsigned(match.group(2), U8_MAX)
            for match in transport_matches
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            and _bounded_unsigned(match.group(2), U8_MAX)
            for match in diag_matches
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            and _bounded_unsigned(match.group(2), U32_MAX)
            for _index, match in state_matches
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            and _bounded_unsigned(match.group(2), U32_MAX)
            for match in matches["active"]
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            and _bounded_unsigned(match.group(2), U32_MAX)
            for _index, match in disconnect_matches
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            for _index, match in neutral_matches
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and 0 < int(match.group(1))
            for _index, match in platform_disconnect_matches
        )
        and all(
            _bounded_unsigned(value, U32_MAX)
            for match in matches["stats"]
            for value in match.groups()
        )
        and all(
            _bounded_unsigned(match.group(1), U32_MAX)
            and _bounded_unsigned(match.group(4), U32_MAX)
            for _index, match in progress_matches
        )
    )
    stats_exact = len(stats_values) == 1 and (
        numeric_bounds_exact
        and stats_values[0][0] == len(diag_pairs)
        and stats_values[0][1] == 0
        and stats_values[0][2] == len(diag_pairs)
        and stats_values[0][3] == stats_values[0][2]
        and stats_values[0][4] > 0
        and stats_values[0][5:] == (0, 0, 0)
        and len(disconnected) == len(diag_pairs)
        and disconnected[-1]["sequence"]
        == stats_values[0][2] + stats_values[0][4] + stats_values[0][3]
    )

    progress = [
        {
            "line_index": line_index,
            "elapsed_ms": int(match.group(1)),
            "connected": int(match.group(2)),
            "exact": int(match.group(3)),
            "reports": int(match.group(4)),
            "disconnect": int(match.group(5)),
            "reconnect": int(match.group(6)),
        }
        for line_index, match in progress_matches
    ]

    def progress_flags_are_true(record: dict[str, int]) -> bool:
        prior_diag = [
            (index, pair, flag)
            for (index, _match), pair, flag in zip(
                diag_records, diag_pairs, diag_reconnect
            )
            if index < record["line_index"]
        ]
        prior_physical = [
            event
            for event in physical_disconnects
            if event["line_index"] < record["line_index"]
        ]
        latest_session = prior_diag[-1][1][0] if prior_diag else 0
        latest_disconnected = bool(
            latest_session
            and any(event["session"] == latest_session for event in prior_physical)
        )
        expected_connected = int(bool(prior_diag) and not latest_disconnected)
        expected_exact = int(bool(prior_diag))
        expected_disconnect = int(bool(prior_physical))
        expected_reconnect = int(any(flag == 1 for _index, _pair, flag in prior_diag))
        active_before = (
            len(matches["active"]) == 1
            and lines.index(matches["active"][0].group(0)) < record["line_index"]
        )
        return (
            record["connected"] == expected_connected
            and record["exact"] == expected_exact
            and record["disconnect"] == expected_disconnect
            and record["reconnect"] == expected_reconnect
            and (bool(prior_diag) or record["reports"] == 0)
            and (not active_before or record["reports"] > 0)
        )

    final_report_count = stats_values[0][4] if len(stats_values) == 1 else -1
    diag_ready_index = (
        lines.index(matches["diag_ready"][0].group(0))
        if len(matches["diag_ready"]) == 1 else -1
    )
    progress_truth_exact = (
        numeric_bounds_exact
        and all(
            diag_ready_index < record["line_index"]
            for record in progress
        )
        and all(0 < record["elapsed_ms"] <= 120000 for record in progress)
        and all(
            later["elapsed_ms"] > earlier["elapsed_ms"]
            and later["reports"] >= earlier["reports"]
            for earlier, later in zip(progress, progress[1:])
        )
        and all(
            record["reports"] <= final_report_count
            and progress_flags_are_true(record)
            for record in progress
        )
    )

    window_complete_exact = len(window_complete_matches) == 0
    identity_exact = (
        bool(diag_pairs)
        and transport_pairs == diag_pairs
        and session_ids == list(range(1, len(session_ids) + 1))
        and state_sessions == {session for session, _interface in diag_pairs}
    )

    def one_position(name: str) -> int:
        return lines.index(matches[name][0].group(0)) if len(matches[name]) == 1 else -1

    order = {name: one_position(name) for name in lifecycle_patterns}
    first_transport = lines.index(transport_matches[0].group(0)) if transport_matches else -1
    first_diag = lines.index(diag_matches[0].group(0)) if diag_matches else -1
    first_state = state_matches[0][0] if state_matches else -1
    marker_order_valid = lifecycle_exact and (
        order["serial_attach"] < order["boot"] < order["wait_arm"]
        < order["arm_accepted"] < order["usb_host_ready"]
        < order["gamepad_usb_ready"] < order["root_enabled"]
        and order["root_enabled"] < order["diag_ready"] < first_diag
        and order["root_enabled"] < first_transport < first_diag
        and first_diag < first_state <= order["active"] < order["quiescing"]
        < order["host_stopped"] < order["cleanup"]
        < order["stats"] < order["result"] < order["terminal"]
    )
    attach_offset = payload.find(b"GAMEPAD_D1_SERIAL_ATTACH")
    post_attach = payload[attach_offset:] if attach_offset >= 0 else payload
    no_post_attach_reset = not re.search(rb"(?i)(?:\brst:|ESP-ROM:)", post_attach)
    passed = (
        not malformed
        and not rejects
        and lifecycle_exact
        and identity_exact
        and numeric_bounds_exact
        and canonical_states
        and state_monotonic
        and active_state_exact
        and hotplug_exact
        and progress_truth_exact
        and window_complete_exact
        and runtime_phase_order
        and stats_exact
        and marker_order_valid
        and no_post_attach_reset
    )
    return {
        "controller_identity_matches": identity_exact,
        "controller_descriptor_sha256": DESCRIPTOR_SHA256,
        "transport_connected_session_interfaces": transport_pairs,
        "diagnostic_connected_session_interfaces": diag_pairs,
        "lifecycle_counts": lifecycle_counts,
        "malformed_contract_lines": malformed,
        "state_count": len(states),
        "state_sessions": sorted(state_sessions),
        "state_sequences_strictly_monotonic": state_monotonic,
        "numeric_field_bounds_exact": numeric_bounds_exact,
        "canonical_dpad_values": canonical_states,
        "progress_records": progress,
        "progress_truth_exact": progress_truth_exact,
        "window_complete_count": len(window_complete_matches),
        "window_complete_truth_exact": window_complete_exact,
        "window_complete_claim_accepted": False,
        "stats_values": stats_values,
        "stats_exact_and_fault_free": stats_exact,
        "cleanup_complete_and_neutral": len(matches["cleanup"]) == 1,
        "smoke_result_exact": len(matches["result"]) == 1,
        "active_input_marker_count": len(matches["active"]),
        "active_input_matches_non_neutral_state": active_state_exact,
        "hotplug_evidence_exact": hotplug_exact,
        "platform_disconnects_exact": exact_platform_disconnects,
        "physical_disconnect_observed": physical_truth,
        "disconnect_neutral_observed": bool(physical_neutrals),
        "held_disconnect_neutral_observed": held_observed,
        "reconnect_observed": reconnect_truth,
        "marker_order_valid": marker_order_valid,
        "post_serial_attach_reset_absent": no_post_attach_reset,
        "reject_markers": rejects,
        "result": "pass" if passed else "fail",
    }


def validate_initial_prefix(prefix: bytes) -> dict[str, Any]:
    if len(prefix) > 32 * 1024:
        raise CaptureError("pre-BOOT prefix exceeds 32 KiB")
    decoded = prefix.decode("utf-8", "replace")
    lower = decoded.lower()
    if any(marker.lower() in lower.encode("utf-8") for marker in PREFIX_REJECT_MARKERS):
        raise CaptureError("rejected marker appears before GAMEPAD_D1_BOOT")
    reset_codes = re.findall(r"(?i)\brst:\s*(0x[0-9a-f]+)\b", decoded)
    if len(reset_codes) != 1 or any(
        code.lower() not in BENIGN_RESET_CODES for code in reset_codes
    ):
        raise CaptureError("initial reset reason is missing, duplicated, or unsafe")
    rom_lines = [line for line in decoded.splitlines() if "esp-rom:" in line.lower()]
    if len(rom_lines) != 1 or any(
        "esp32p4" not in line.lower() and "esp32-p4" not in line.lower()
        for line in rom_lines
    ):
        raise CaptureError("initial prefix must contain exactly one ESP32-P4 ROM banner")
    return {
        "bytes": len(prefix),
        "sha256": sha256_bytes(prefix),
        "reset_count": 1,
        "reset_code": reset_codes[0].lower(),
        "esp_rom_line_count": 1,
    }


def validate_pre_arm_transcript(payload: bytes) -> dict[str, Any]:
    lines, malformed = _runtime_lines(payload)
    if malformed:
        raise CaptureError("malformed protected line precedes WAIT_ARM")
    attach = "GAMEPAD_D1_SERIAL_ATTACH wait_ms=2500 complete=1 root_data_port_enabled=0"
    boot = (
        "GAMEPAD_D1_BOOT usb_component=1.5.0 hid_component=1.2.0 "
        "controller=p4-hs serial_attach_delay_ms=2500 root_data_port_enabled=0"
    )
    wait = (
        f"GAMEPAD_D1_WAIT_ARM auth={AUTH_ID} timeout_ms=15000 "
        "root_data_port_enabled=0"
    )
    if lines != [attach, boot, wait]:
        raise CaptureError("pre-ARM lifecycle is not exact SERIAL_ATTACH -> BOOT -> WAIT_ARM")
    attach_offset = payload.find(attach.encode("ascii"))
    if attach_offset < 0:
        raise CaptureError("serial attach boundary is missing")
    prefix = validate_initial_prefix(payload[:attach_offset])
    payload_lower = payload.lower()
    for marker in REJECT_MARKERS:
        if marker.lower() in payload_lower:
            raise CaptureError("reject marker precedes host ARM")
    return prefix


def consume_receipt_and_secret(
    receipt_path: pathlib.Path, secret_binding: dict[str, Any]
) -> bytearray:
    secret_path = pathlib.Path(str(secret_binding.get("path", "")))
    if not STATE.binding_matches(secret_path, core_file_binding(secret_binding)):
        raise CaptureError("private ARM secret changed before consumption")
    descriptor = os.open(secret_path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    token = bytearray(64)
    newline = bytearray(1)
    try:
        info = os.fstat(descriptor)
        if not (stat.S_ISREG(info.st_mode) and info.st_size == 65
                and info.st_dev == secret_binding.get("device")
                and info.st_ino == secret_binding.get("inode")):
            raise CaptureError("private ARM secret changed while opening")
        view = memoryview(token)
        read = 0
        while read < len(token):
            count = os.readv(descriptor, [view[read:]])
            if count <= 0:
                raise CaptureError("private ARM secret was truncated")
            read += count
        if os.readv(descriptor, [memoryview(newline)]) != 1 or newline[0] != 0x0A:
            raise CaptureError("private ARM secret has no exact final LF")
        if any(not (0x30 <= value <= 0x39 or 0x61 <= value <= 0x66) for value in token):
            raise CaptureError("private ARM secret is not lowercase hexadecimal")
        if hashlib.sha256(token).hexdigest() != secret_binding.get("token_sha256"):
            raise CaptureError("private ARM token digest changed")
    except BaseException:
        zeroize(token)
        raise
    finally:
        zeroize(newline)
        os.close(descriptor)
    try:
        receipt_path.unlink()
        fsync_directory(receipt_path.parent)
        secret_path.unlink()
        fsync_directory(secret_path.parent)
    except BaseException:
        zeroize(token)
        raise
    return token


def zeroize(value: bytearray) -> None:
    for index in range(len(value)):
        value[index] = 0


def destroy_recovery_arm_material(state: dict[str, Any]) -> None:
    """Remove stale ARM inputs without ever reading or transmitting them."""

    recovery = STATE.validate_recovery_directory(
        pathlib.Path(str(state.get("recovery_directory", "")))
    )
    secret = state.get("arm_secret", {})
    secret_path = pathlib.Path(str(secret.get("path", "")))
    if secret_path.exists():
        try:
            info = secret_path.lstat()
        except OSError as error:
            raise CaptureError(f"cannot stat stale ARM secret: {error}") from error
        if not (
            secret_path.resolve(strict=True).parent == recovery
            and stat.S_ISREG(info.st_mode)
            and not stat.S_ISLNK(info.st_mode)
            and info.st_uid == os.getuid()
            and stat.S_IMODE(info.st_mode) == 0o600
        ):
            raise CaptureError("stale ARM secret is not an owned recovery child")
        secret_path.unlink()
        fsync_directory(recovery)
    receipt_text = state.get("receipt_path")
    if isinstance(receipt_text, str) and receipt_text:
        receipt = pathlib.Path(receipt_text)
        if receipt.exists():
            private_regular(receipt, "stale receipt")
            parent = receipt.resolve(strict=True).parent
            if not (
                stat.S_IMODE(parent.lstat().st_mode) == 0o700
                and receipt.name.startswith("gamepad-diag-receipt.")
            ):
                raise CaptureError("stale receipt is outside its private receipt bundle")
            receipt.unlink()
            fsync_directory(parent)


def validate_recovery_gate_inventory(state: dict[str, Any]) -> None:
    """Revalidate frozen recovery orchestrator bytes without active auth files."""

    evidence_path = pathlib.Path(str(state.get("build_evidence_path", "")))
    if not (
        evidence_path.resolve(strict=True)
        == (ROOT / STATE.BUILD_EVIDENCE_REL).resolve(strict=True)
        and sha256_file(evidence_path) == state.get("build_evidence_sha256")
    ):
        raise CaptureError("recovery build evidence binding changed")
    evidence = load_json(evidence_path, "recovery build evidence")
    inventory = evidence.get("gate_inventory")
    required = {
        "scripts/gamepad-diag-capture.py": pathlib.Path(__file__).resolve(strict=True),
        "scripts/gamepad-diag-one-shot-state.py": STATE_TOOL_PATH.resolve(strict=True),
        "scripts/gamepad-diag-restore.py": RESTORE_TOOL_PATH.resolve(strict=True),
        "scripts/gamepad-diag-install.py": (ROOT / "scripts/gamepad-diag-install.py").resolve(strict=True),
        "scripts/tests/test-gamepad-diag-install.py": (
            ROOT / "scripts/tests/test-gamepad-diag-install.py"
        ).resolve(strict=True),
        "scripts/flash.sh": (ROOT / "scripts/flash.sh").resolve(strict=True),
    }
    if (
        not isinstance(inventory, dict)
        or any(
            re.fullmatch(r"[0-9a-f]{64}", str(inventory.get(relative))) is None
            or inventory.get(relative) != sha256_file(path)
            for relative, path in required.items()
        )
    ):
        raise CaptureError("recovery gate/orchestrator bytes changed")


def recover(args: argparse.Namespace) -> None:
    """Restore a nonterminal diagnostic install without accessing ARM material."""

    owner_token = STATE.read_owner_token(args.owner_token_file)
    state = STATE.load_object(args.state, "one-shot state")
    recovery_dir = STATE.validate_recovery_directory(
        pathlib.Path(str(state.get("recovery_directory", "")))
    )
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("terminal") is False
        and state.get("restore_required") is True
        and state.get("device_identity_sha256") == STATE.EXPECTED_DEVICE_SHA256
        and state.get("owner_token_sha256")
        == sha256_bytes(owner_token.encode("ascii"))
        and args.owner_token_file.resolve(strict=True).parent == recovery_dir
        and state.get("exact_artifact")
        == {
            "offset": STATE.EXPECTED_OFFSET,
            "bytes": STATE.EXPECTED_BYTES,
            "sha256": STATE.EXPECTED_SHA256,
            "sector_bytes": STATE.SECTOR_BYTES,
            "install_write_block_bytes": STATE.INSTALL_WRITE_BLOCK_BYTES,
            "mutation_span_bytes": STATE.mutation_span(STATE.EXPECTED_BYTES),
        }
        and isinstance(state.get("restore_preimage"), dict)
        and isinstance(state.get("live_partition_table"), dict)
    ):
        raise CaptureError("recovery has no exact owned restore-required state")
    validate_recovery_gate_inventory(state)
    destroy_recovery_arm_material(state)
    restore_tool = load_restore_tool()
    restore_tool.validate_partition_binding(
        state["live_partition_table"],
        expected_offset=int(STATE.EXPECTED_OFFSET, 0),
        expected_span=STATE.mutation_span(STATE.EXPECTED_BYTES),
    )
    previous_handlers = arm_terminal_signal_handlers()
    device = None
    primary_error: BaseException | None = None
    try:
        device = open_serial_once(args.port)
        STATE.mark_restore_entry(args.state, owner_token)
        result = restore_tool.restore_same_handle(
            device=device,
            preimage_binding=state["restore_preimage"],
            partition_table_binding=state["live_partition_table"],
            expected_device_sha256=STATE.EXPECTED_DEVICE_SHA256,
            mark_write_attempt=lambda: STATE.mark_restore_write_attempt(
                args.state, owner_token
            ),
        )
        STATE.mark_restore_verified(
            args.state, owner_token, result["readback_sha256"]
        )
        device.dtr = False
        device.rts = False
        device.close()
        if device.is_open:
            raise CaptureError("recovery UART did not close")
        device = None
        STATE.fail_if_reserved(args.state, owner_token, "recovery-restored-without-arm")
        print(
            "Gamepad diagnostic recovery PASS: exact pre-install span restored; "
            "ARM bytes transmitted=0; post-restore reset count=0"
        )
    except BaseException as error:
        primary_error = error
        ignore_terminal_signals()
        try:
            STATE.fail_if_reserved(args.state, owner_token, "recovery-failed-or-interrupted")
        except BaseException as ledger_error:
            print(f"Warning: recovery ledger update failed: {ledger_error}", file=sys.stderr)
        raise
    finally:
        try:
            if device is not None and device.is_open:
                device.dtr = False
                device.rts = False
                try:
                    device.close()
                except BaseException as close_error:
                    if primary_error is None:
                        raise CaptureError(f"recovery UART close failed: {close_error}")
        finally:
            restore_signal_handlers(previous_handlers)


def verify_result(
    state_path: pathlib.Path, raw_path: pathlib.Path, summary_path: pathlib.Path
) -> dict[str, Any]:
    state = STATE.load_object(state_path, "one-shot state")
    evidence = state.get("runtime_evidence")
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == STATE.AUTH_ID
        and state.get("status") == "completed"
        and state.get("terminal") is True
        and state.get("launch_hard_reset_count") == 1
        and state.get("hard_reset_attempt_count") == 1
        and state.get("hard_reset_outcome") == "hard-reset-succeeded"
        and state.get("arm_frame_tx_attempt_count") == 1
        and state.get("arm_frame_tx_outcome") == "accepted"
        and state.get("arm_frame", {}).get("count") == 1
        and state.get("arm_frame", {}).get("bytes") == ARM_FRAME_BYTES
        and state.get("arm_frame", {}).get("secret_logged") is False
        and state.get("restore_required") is False
        and state.get("restoration_verified_before_completion") is True
        and state.get("restore_write_outcome") == "verified"
        and state.get("restore_download_reset_attempt_count") == 1
        and state.get("restore_write_attempt_count") == 1
        and state.get("restore_readback_sha256")
        == state.get("restore_preimage", {}).get("sha256")
        and isinstance(evidence, dict)
        and evidence.get("raw_path") == str(raw_path.resolve())
        and evidence.get("summary_path") == str(summary_path.resolve())
        and evidence.get("publication_status")
        == "bound-before-exclusive-publication"
    ):
        raise CaptureError("durable ledger is not the exact completed one-shot result")
    private_regular(raw_path, "raw transcript")
    private_regular(summary_path, "summary")
    if not (
        sha256_file(raw_path) == evidence.get("raw_sha256")
        and sha256_file(summary_path) == evidence.get("summary_sha256")
    ):
        raise CaptureError("published result hashes differ from the durable ledger")
    summary = load_json(summary_path, "summary")
    raw_payload = raw_path.read_bytes()
    attach_offset = raw_payload.find(b"GAMEPAD_D1_SERIAL_ATTACH")
    if attach_offset < 0:
        raise CaptureError("published transcript has no serial-attach boundary")
    recomputed_prefix = validate_initial_prefix(raw_payload[:attach_offset])
    expected_runtime = {
        "fixture_id": state.get("fixture_id"),
        "fixture_sha256": state.get("fixture_evidence_sha256"),
        "current_limit_ma": state.get("fixture_current_limit_ma"),
        "arm_token_sha256": state.get("arm_token_sha256"),
    }
    recomputed = summarize(raw_payload, expected_runtime)
    expected_bindings = {
        "authorization_id": AUTH_ID,
        "authorization_sha256": state.get("authorization_sha256"),
        "device_identity_sha256": state.get("device_identity_sha256"),
        "artifact": {},
        "fixture_id": state.get("fixture_id"),
        "fixture_evidence_sha256": state.get("fixture_evidence_sha256"),
        "fixture_current_limit_ma": state.get("fixture_current_limit_ma"),
        "metadata_sha256": state.get("metadata_sha256"),
        "board_profile_sha256": state.get("board_profile_sha256"),
        "arm_token_sha256": state.get("arm_token_sha256"),
        "controller": {
            "vendor_id": "0079",
            "product_id": "0011",
            "profile": "usb-gamepad-0079-0011",
            "descriptor_sha256": DESCRIPTOR_SHA256,
        },
        "live_partition_table_sha256": state.get("live_partition_table", {}).get(
            "sha256"
        ),
        "restore_preimage_sha256": state.get("restore_preimage", {}).get("sha256"),
        "restore_tool_sha256": state.get("live_partition_table", {})
        .get("restore_tool", {})
        .get("sha256"),
    }
    # The published artifact binding intentionally omits ledger-only erase-span
    # fields; require its exact three public identity fields here.
    expected_bindings["artifact"] = {
        key: state.get("exact_artifact", {}).get(key)
        for key in ("offset", "bytes", "sha256")
    }
    parser_keys = set(recomputed)
    if not (
        summary.get("result") == "pass"
        and recomputed.get("result") == "pass"
        and all(summary.get(key) == recomputed.get(key) for key in parser_keys)
        and summary.get("initial_prefix") == recomputed_prefix
        and summary.get("bindings") == expected_bindings
        and summary.get("raw_sha256") == evidence.get("raw_sha256")
        and summary.get("single_hard_reset_succeeded") is True
        and summary.get("serial_closed_before_summary") is True
        and summary.get("arm_transmitted_frames") == 1
        and summary.get("arm_transmitted_bytes") == ARM_FRAME_BYTES
        and summary.get("arm_transmitted_sha256")
        == state.get("arm_frame", {}).get("sha256")
        and summary.get("arm_secret_logged") is False
        and summary.get("restore_verified_before_summary") is True
        and summary.get("restore", {}).get("readback_sha256")
        == state.get("restore_preimage", {}).get("sha256")
        and summary.get("restore", {}).get("offset") == int(STATE.EXPECTED_OFFSET, 0)
        and summary.get("restore", {}).get("bytes")
        == STATE.mutation_span(STATE.EXPECTED_BYTES)
        and summary.get("restore", {}).get("post_restore_reset_count") == 0
        and summary.get("restore", {}).get("download_reset_count") == 1
        and summary.get("restore", {}).get("chip_revision") == 103
        and summary.get("restore", {}).get("flash_bytes") == 16 * 1024 * 1024
        and summary.get("restore", {}).get("post_stub_flash_preparation")
        == "verified"
        and summary.get("restore", {}).get("restore_write_block_bytes")
        == STATE.SECTOR_BYTES
        and summary.get("restore", {}).get("arm_transmitted_bytes") == 0
        and summary.get("restore", {}).get("same_uart_handle") is True
        and summary.get("restore", {}).get("controls_inactive") is True
        and summary.get("restore_download_reset_attempt_count") == 1
        and summary.get("restore_write_attempt_count") == 1
    ):
        raise CaptureError("published summary is not an exact accepted PASS")
    restore_tool = load_restore_tool()
    restore_tool.validate_partition_binding(
        state.get("live_partition_table", {}),
        expected_offset=int(STATE.EXPECTED_OFFSET, 0),
        expected_span=STATE.mutation_span(STATE.EXPECTED_BYTES),
    )
    restore_tool.validate_binding(state.get("restore_preimage", {}), "restore preimage")
    return summary


def capture(args: argparse.Namespace) -> None:
    outputs = (args.raw, args.summary)
    if args.raw.resolve() == args.summary.resolve() or any(path.exists() for path in outputs):
        raise CaptureError("capture outputs must be distinct and new")
    if not 10 <= args.seconds <= 180:
        raise CaptureError("capture duration must be between 10 and 180 seconds")
    receipt = validate_receipt(args.receipt, args)
    owner_token = receipt["owner_token"]
    device = None
    raw = bytearray()
    arm_token = bytearray()
    arm_frame_sha256 = ""
    arm_tx_ns = 0
    hard_reset_succeeded = False
    opened_ns = 0
    hard_reset_ns = 0
    closed_ns = 0
    prefix: dict[str, Any] | None = None
    primary_error: BaseException | None = None
    previous_signal_handlers = arm_terminal_signal_handlers()
    try:
        device = open_serial_once(args.port)
        opened_ns = time.time_ns()
        from esptool.targets.esp32p4 import ESP32P4ROM

        esp = ESP32P4ROM(device, 115200, False)
        esp.connect("no_reset", attempts=1, warnings=False)
        if live_identity(esp) != STATE.EXPECTED_DEVICE_SHA256:
            raise CaptureError("live board differs from receipt binding")
        if esp._port is not device or esp.uses_usb_otg():
            raise CaptureError("hard reset is not using the reviewed UART handle")
        receipt = validate_receipt(args.receipt, args)
        esp.flush_input()
        if device.in_waiting != 0:
            raise CaptureError("unexpected serial bytes before hard reset")
        # Persist the conservative 'hard reset may launch' state before reset.
        STATE.mark_hard_reset_attempt(
            args.state,
            args.authorization,
            args.project_root,
            owner_token,
            args.receipt,
        )
        arm_token = consume_receipt_and_secret(args.receipt, receipt["arm_secret"])
        hard_reset_ns = time.time_ns()
        launch_single_hard_reset(esp, device)
        hard_reset_succeeded = True
        STATE.mark_hard_reset_succeeded(args.state, owner_token)
        device.baudrate = 115200
        device.timeout = 0.05
        deadline = time.monotonic() + args.seconds
        line_buffer = bytearray()
        arm_sent = False
        arm_accepted = False
        wait_arm_line = (
            f"GAMEPAD_D1_WAIT_ARM auth={AUTH_ID} timeout_ms=15000 "
            "root_data_port_enabled=0"
        )
        while time.monotonic() < deadline:
            chunk = device.read(4096)
            if chunk:
                raw.extend(chunk)
                line_buffer.extend(chunk)
                if len(raw) > MAX_SERIAL_BYTES:
                    raise CaptureError("serial transcript exceeded 1 MiB")
                while b"\n" in line_buffer:
                    serial_line, _separator, remainder = line_buffer.partition(b"\n")
                    line_buffer = bytearray(remainder)
                    normalized, malformed = _runtime_lines(serial_line + b"\n")
                    if malformed:
                        raise CaptureError("malformed protected line before ARM")
                    if normalized and normalized[0] == wait_arm_line:
                        if arm_sent:
                            raise CaptureError("duplicate WAIT_ARM marker")
                        prefix = validate_pre_arm_transcript(bytes(raw))
                        frame = bytearray(ARM_FRAME_PREFIX.encode("ascii"))
                        frame.extend(arm_token)
                        frame.append(0x0A)
                        if len(frame) != ARM_FRAME_BYTES:
                            raise CaptureError("ARM frame size changed")
                        STATE.mark_arm_frame_attempt(args.state, owner_token)
                        arm_tx_ns = time.time_ns()
                        arm_frame_sha256 = hashlib.sha256(frame).hexdigest()
                        try:
                            written = device.write(frame)
                            device.flush()
                        finally:
                            zeroize(frame)
                            zeroize(arm_token)
                        if written != ARM_FRAME_BYTES:
                            raise CaptureError("partial ARM frame transmission")
                        arm_sent = True
                    elif normalized and normalized[0].startswith(
                        "GAMEPAD_D1_ARM_ACCEPTED "
                    ):
                        exact_accept = (
                            f"GAMEPAD_D1_ARM_ACCEPTED auth={AUTH_ID} "
                            f"token_sha256={receipt['arm_token_sha256']} "
                            "tx_count=1 root_data_port_enabled=0"
                        )
                        if not arm_sent:
                            raise CaptureError("ARM_ACCEPTED preceded the one ARM frame")
                        if arm_accepted or normalized[0] != exact_accept:
                            raise CaptureError("ARM_ACCEPTED was duplicated or malformed")
                        STATE.mark_arm_accepted(
                            args.state, owner_token, ARM_FRAME_BYTES, arm_frame_sha256
                        )
                        arm_accepted = True
            if (
                b"GAMEPAD_D1_CLEANUP" in raw
                and b"GAMEPAD_D1_RESULT result=PASS" in raw
            ):
                # Keep a small bounded tail to catch a post-cleanup panic/reset.
                tail_deadline = min(deadline, time.monotonic() + 1.0)
                while time.monotonic() < tail_deadline:
                    chunk = device.read(4096)
                    if chunk:
                        raw.extend(chunk)
                break
        if not arm_sent or not arm_accepted or any(arm_token):
            raise CaptureError("exact one-shot ARM frame was not sent and zeroized")
        if prefix is None:
            raise CaptureError("validated reset/attach/BOOT/WAIT_ARM prefix was not captured")
        expected_runtime = {
            "fixture_id": receipt["fixture"]["id"],
            "fixture_sha256": receipt["fixture_evidence_sha256"],
            "current_limit_ma": receipt["fixture"]["current_limit_ma"],
            "arm_token_sha256": receipt["arm_token_sha256"],
        }
        summary = summarize(bytes(raw), expected_runtime)
        if summary["result"] != "pass":
            raise CaptureError("controller runtime acceptance markers did not pass")
        STATE.mark_restore_entry(args.state, owner_token)
        restore_tool = load_restore_tool()
        restore_result = restore_tool.restore_same_handle(
            device=device,
            preimage_binding=receipt["restore_preimage"],
            partition_table_binding=receipt["live_partition_table"],
            expected_device_sha256=STATE.EXPECTED_DEVICE_SHA256,
            mark_write_attempt=lambda: STATE.mark_restore_write_attempt(
                args.state, owner_token
            ),
        )
        STATE.mark_restore_verified(
            args.state, owner_token, restore_result["readback_sha256"]
        )
        device.dtr = False
        device.rts = False
        device.close()
        if device.is_open:
            raise CaptureError("serial handle remained open after close")
        closed_ns = time.time_ns()
        device = None
        completed_state = STATE.load_object(args.state, "one-shot state")
        summary.update(
            {
                "schema": 1,
                "capture": "gamepad-diag-d1-single-open-uart-hard-reset",
                "port": args.port,
                "baud": 115200,
                "serial_exclusive": True,
                "arm_transmitted_frames": 1,
                "arm_transmitted_bytes": ARM_FRAME_BYTES,
                "arm_transmitted_sha256": arm_frame_sha256,
                "arm_tx_wall_ns": arm_tx_ns,
                "post_arm_runtime_transmitted_bytes": 0,
                "pre_reset_dtr": False,
                "post_reset_dtr": False,
                "runtime_dtr": False,
                "pre_reset_rts": False,
                "expected_reset_rts_pulse": True,
                "post_reset_rts": False,
                "runtime_rts": False,
                "hupcl": False,
                "opened_wall_ns": opened_ns,
                "hard_reset_wall_ns": hard_reset_ns,
                "closed_wall_ns": closed_ns,
                "raw_bytes": len(raw),
                "raw_sha256": sha256_bytes(bytes(raw)),
                "receipt_and_arm_secret_unlinked_before_reset": True,
                "arm_secret_logged": False,
                "single_hard_reset_succeeded": hard_reset_succeeded,
                "restore": restore_result,
                "restore_download_reset_attempt_count": completed_state.get(
                    "restore_download_reset_attempt_count"
                ),
                "restore_write_attempt_count": completed_state.get(
                    "restore_write_attempt_count"
                ),
                "restore_verified_before_summary": True,
                "initial_prefix": prefix,
                "serial_closed_before_summary": True,
                "bindings": {
                    "authorization_id": AUTH_ID,
                    "authorization_sha256": receipt["authorization_sha256"],
                    "device_identity_sha256": receipt["device_identity_sha256"],
                    "artifact": receipt["artifact"],
                    "fixture_id": receipt["fixture"]["id"],
                    "fixture_evidence_sha256": receipt["fixture_evidence_sha256"],
                    "fixture_current_limit_ma": receipt["fixture"]["current_limit_ma"],
                    "metadata_sha256": receipt["metadata_sha256"],
                    "board_profile_sha256": receipt["board_profile_sha256"],
                    "arm_token_sha256": receipt["arm_token_sha256"],
                    "controller": {
                        "vendor_id": "0079",
                        "product_id": "0011",
                        "profile": "usb-gamepad-0079-0011",
                        "descriptor_sha256": DESCRIPTOR_SHA256,
                    },
                    "live_partition_table_sha256": receipt[
                        "live_partition_table"
                    ]["sha256"],
                    "restore_preimage_sha256": receipt["restore_preimage"]["sha256"],
                    "restore_tool_sha256": receipt["restore_tool"]["sha256"],
                },
            }
        )
        raw_payload = bytes(raw)
        summary_payload = canonical_json(summary)
        STATE.complete(
            args.state,
            args.authorization,
            args.project_root,
            owner_token,
            "single-hard-reset-succeeded-and-bounded-controller-markers-passed",
            args.raw,
            sha256_bytes(raw_payload),
            args.summary,
            sha256_bytes(summary_payload),
        )
        atomic_write(args.raw, raw_payload)
        atomic_write(args.summary, summary_payload)
        print(json.dumps(summary, separators=(",", ":")))
    except BaseException as error:
        primary_error = error
        # A first signal is converted into the normal terminal failure path.
        # Ignore repeats only while durably terminalizing and closing the UART.
        ignore_terminal_signals()
        try:
            state = STATE.load_object(args.state, "one-shot state")
            if (
                device is not None
                and device.is_open
                and state.get("restore_required") is True
                and state.get("status") in {
                    "installed-verified", "pending-capture",
                    "hard-reset-attempted", "hard-reset-succeeded",
                    "arm-frame-attempted", "arm-accepted",
                }
                and isinstance(receipt.get("restore_preimage"), dict)
            ):
                try:
                    STATE.mark_restore_entry(args.state, owner_token)
                    restore_tool = load_restore_tool()
                    recovered = restore_tool.restore_same_handle(
                        device=device,
                        preimage_binding=receipt["restore_preimage"],
                        partition_table_binding=receipt["live_partition_table"],
                        expected_device_sha256=STATE.EXPECTED_DEVICE_SHA256,
                        mark_write_attempt=lambda: STATE.mark_restore_write_attempt(
                            args.state, owner_token
                        ),
                    )
                    STATE.mark_restore_verified(
                        args.state, owner_token, recovered["readback_sha256"]
                    )
                except BaseException as restore_error:
                    print(
                        f"Warning: protected same-handle restore failed: {restore_error}",
                        file=sys.stderr,
                    )
            STATE.fail_if_reserved(
                args.state, owner_token, "capture-failed-or-interrupted"
            )
        except BaseException as terminal_error:
            print(f"Warning: terminal state update failed: {terminal_error}", file=sys.stderr)
        raise
    finally:
        try:
            zeroize(arm_token)
            if device is not None and device.is_open:
                device.dtr = False
                device.rts = False
                try:
                    device.close()
                    if device.is_open:
                        raise CaptureError("serial cleanup failed to close the handle")
                except BaseException as close_error:
                    try:
                        STATE.fail_if_reserved(
                            args.state, owner_token, "serial-close-failed"
                        )
                    except BaseException as terminal_error:
                        print(
                            f"Warning: close terminal update failed: {terminal_error}",
                            file=sys.stderr,
                        )
                    if primary_error is None:
                        raise CaptureError(
                            f"serial cleanup failed: {close_error}"
                        ) from close_error
        finally:
            restore_signal_handlers(previous_signal_handlers)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    check_path = commands.add_parser("check-receipt-path")
    check_path.add_argument("--receipt", type=pathlib.Path, required=True)

    verify = commands.add_parser("verify-result")
    verify.add_argument("--state", type=pathlib.Path, required=True)
    verify.add_argument("--raw", type=pathlib.Path, required=True)
    verify.add_argument("--summary", type=pathlib.Path, required=True)

    emit = commands.add_parser("emit-receipt")
    emit.add_argument("--receipt", type=pathlib.Path, required=True)
    emit.add_argument("--state", type=pathlib.Path, required=True)
    emit.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    emit.add_argument("--authorization", type=pathlib.Path, required=True)
    emit.add_argument("--project-root", type=pathlib.Path, required=True)
    emit.add_argument("--port", required=True)
    emit.add_argument("--device-identity-sha256", required=True)
    emit.add_argument("--offset", required=True)
    emit.add_argument("--bytes", type=int, required=True)
    emit.add_argument("--sha256", required=True)
    emit.add_argument("--readback-bytes", type=int, required=True)
    emit.add_argument("--readback-sha256", required=True)
    emit.add_argument("--readback-chunks", type=int, required=True)
    emit.add_argument("--readback-max-chunk-bytes", type=int, required=True)
    emit.add_argument("--mutation-span-bytes", type=int, required=True)
    emit.add_argument("--install-span-sha256", required=True)
    emit.add_argument("--install-span-readback-sha256", required=True)
    emit.add_argument("--verifier", type=pathlib.Path, required=True)

    run = commands.add_parser("capture")
    run.add_argument("--receipt", type=pathlib.Path, required=True)
    run.add_argument("--state", type=pathlib.Path, required=True)
    run.add_argument("--authorization", type=pathlib.Path, required=True)
    run.add_argument("--project-root", type=pathlib.Path, default=ROOT)
    run.add_argument("--port", required=True)
    run.add_argument("--raw", type=pathlib.Path, required=True)
    run.add_argument("--summary", type=pathlib.Path, required=True)
    run.add_argument("--seconds", type=float, default=150.0)

    recovery = commands.add_parser("recover")
    recovery.add_argument("--state", type=pathlib.Path, required=True)
    recovery.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    recovery.add_argument("--port", required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "check-receipt-path":
            validate_new_receipt_path(args.receipt)
            print(f"Gamepad receipt path preflight PASS: {args.receipt}")
        elif args.command == "verify-result":
            summary = verify_result(args.state, args.raw, args.summary)
            print(json.dumps(summary, separators=(",", ":")))
        elif args.command == "emit-receipt":
            emit_receipt(args)
        elif args.command == "recover":
            recover(args)
        else:
            capture(args)
    except (CaptureError, STATE.StateError) as error:
        raise SystemExit(f"gamepad diagnostic capture failed: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
