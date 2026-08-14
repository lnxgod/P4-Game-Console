"""Atomic receipt and one-open-port capture transport for D2.3.

This module deliberately uses the pinned esptool/pyserial environment and the
exact Homebrew PortAudio dylib.  It never shells out to esptool and never
closes/reopens the CH340 between leaving the ROM loader and reading app logs.
"""

from __future__ import annotations

import ctypes
import datetime as dt
import fcntl
import hashlib
import json
import os
import pathlib
import re
import stat
import sys
import termios
import threading
import time
import uuid
import wave
from typing import Any


SCHEMA = 1
APP = "audio_direct_diag"
AUTH_ID = "audio-direct-diag-d23-one-shot-authorization-2026-08-13"
ARM_NONCE_SEED = AUTH_ID + " host-arm v1"
ARM_NONCE = "0c47666f7da543e93831e0dfdcb7fecd6bb9887f0d0d0c72ee4422826b573562"
ARM_FRAME = "P4_AUDIO_D23_ARM " + AUTH_ID + " " + ARM_NONCE + "\n"
ARM_FRAME_BYTES = 138
ARM_FRAME_SHA256 = "cce24d670298f6d62755d240d14f928c212d82fea6246517eb8edaf48f0fade9"
IDF_COMMIT = "2c211b236707889e8400c4dc5644dd5c4ee071e0"
ESPTOOL_VERSION = "4.12.0"
PYSERIAL_VERSION = "3.5"
PORTAUDIO_PATH = pathlib.Path("/opt/homebrew/lib/libportaudio.2.dylib")
PORTAUDIO_SHA256 = "a45be7dde93ff3f0da2d3996823419d7c67354015e2948baf74c9a9ba5f202f1"
PORTAUDIO_VERSION = 0x00130700
PORTAUDIO_VERSION_TEXT = (
    "PortAudio V19.7.0-devel, revision "
    "147dd722548358763a8b649b3e4b41dfffbcfbb6"
)
CAPTURE_DEVICE_NAME = "MacBook Pro Microphone"
CAPTURE_HOST_API_NAME = "Core Audio"
CAPTURE_HOST_API_TYPE = 5
SAMPLE_RATE = 48_000
CHANNELS = 1
SAMPLE_WIDTH = 2
FRAMES_PER_BUFFER = 480
MAXIMUM_FRAMES = SAMPLE_RATE * 22
MAXIMUM_SERIAL_BYTES = 512 * 1024
RECEIPT_MAX_AGE_SECONDS = 15 * 60
EXPECTED_OFFSET = 0x10000
SERIAL_ATTACH_LINE = (
    "P4_AUDIO D2.3 SERIAL_ATTACH wait_ms=2000 complete=1 rails=off "
    "amp_shutdown=1"
)
WAIT_ARM_LINE = (
    "P4_AUDIO D2.3 WAIT_ARM auth=" + AUTH_ID
    + " timeout_ms=15000 rails=off amp_shutdown=1"
)
FIXED_MARKER_LINES = (
    "P4_AUDIO D2.3 START backend=platform_audio input=full-scale "
    "output_peak_cap=512 codec=none i2c=unused mclk=unused",
    "P4_AUDIO D2.3 SAFE gpio30=1 active_low=true",
    "P4_AUDIO D2.3 POWER_READY ldo3_mv=2500 ldo4_mv=3300 settle_ms=20",
    "P4_AUDIO D2.3 BACKEND_READY state=ready-muted i2s=1 lrclk=21 bclk=22 "
    "dout=23 write_frames=128 timeout_ms=100",
    "P4_AUDIO D2.3 CAPTURE_ARM wait_ms=5000 amp_shutdown=1",
    "P4_AUDIO D2.3 TONE_BEGIN hz=440 bounded_ms=400 input_peak=32767 "
    "output_peak_cap=512 chunks=50",
    "P4_AUDIO D2.3 POSTROLL frames=1536 chunks=12 ring_covered=1",
    "P4_AUDIO D2.3 PASS backend=platform_audio tone_count=1 bounded_ms=400 "
    "exact_writes=62 partial_writes=0 amp_shutdown=1 ldo3_released=1 "
    "ldo4_released=1",
    "P4_AUDIO D2.3 HEARTBEAT amp_shutdown=1 rails_released=1",
)
MARKER_NAMES = (
    "START", "SAFE", "POWER_READY", "BACKEND_READY", "CAPTURE_ARM",
    "TONE_BEGIN", "POSTROLL", "PASS", "HEARTBEAT",
)
REJECT_SUBSTRINGS = (
    "P4_AUDIO D2.3 HALT",
    "P4_AUDIO D2.3 CLEANUP_RETRY",
    "Guru Meditation Error",
    "Task watchdog got triggered",
    "assert failed",
    "brownout",
    "waiting for download",
    "glitch",
    "lockup",
)
RESET_SUBSTRINGS = ("rst:", "esp-rom:", "boot:", "ets ", "waiting for download")
BENIGN_RESET_CODES = {"0x1", "0x3", "0x16", "0x17"}
HEX64 = re.compile(r"^[0-9a-f]{64}$")

if hashlib.sha256(ARM_NONCE_SEED.encode("ascii")).hexdigest() != ARM_NONCE:
    raise RuntimeError("compiled D2.3 host-arm nonce derivation changed")
if len(ARM_FRAME.encode("ascii")) != ARM_FRAME_BYTES or \
        hashlib.sha256(ARM_FRAME.encode("ascii")).hexdigest() != ARM_FRAME_SHA256:
    raise RuntimeError("compiled D2.3 host-arm frame identity changed")


class CaptureError(RuntimeError):
    pass


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc)


def utc_text(value: dt.datetime) -> str:
    return value.astimezone(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def parse_utc(value: Any) -> dt.datetime:
    if not isinstance(value, str) or not value.endswith("Z"):
        raise CaptureError("timestamp is not canonical UTC")
    try:
        parsed = dt.datetime.fromisoformat(value[:-1] + "+00:00")
    except ValueError as error:
        raise CaptureError("timestamp is invalid") from error
    if parsed.tzinfo != dt.timezone.utc:
        raise CaptureError("timestamp is not UTC")
    return parsed


def fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def atomic_write_json(path: pathlib.Path, value: dict[str, Any], exclusive: bool) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    temporary = path.parent / f".{path.name}.{uuid.uuid4().hex}.tmp"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(temporary, flags, 0o600)
    try:
        payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.close(descriptor)
        descriptor = -1
        if exclusive:
            try:
                os.link(temporary, path)
            except FileExistsError as error:
                raise CaptureError(f"refusing to overwrite {path}") from error
            os.unlink(temporary)
        else:
            os.replace(temporary, path)
        fsync_directory(path.parent)
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def atomic_write_bytes(path: pathlib.Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.parent / f".{path.name}.{uuid.uuid4().hex}.tmp"
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.close(descriptor)
        descriptor = -1
        try:
            os.link(temporary, path)
        except FileExistsError as error:
            raise CaptureError(f"refusing to overwrite output: {path}") from error
        os.unlink(temporary)
        fsync_directory(path.parent)
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        temporary.unlink(missing_ok=True)


def pcm16_wav_bytes(pcm: bytes) -> bytes:
    import io
    output = io.BytesIO()
    with wave.open(output, "wb") as recording:
        recording.setnchannels(CHANNELS)
        recording.setsampwidth(SAMPLE_WIDTH)
        recording.setframerate(SAMPLE_RATE)
        recording.writeframes(pcm)
    return output.getvalue()


def load_private_json(path: pathlib.Path) -> dict[str, Any]:
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode):
        raise CaptureError(f"not a regular file: {path}")
    if stat.S_IMODE(info.st_mode) & 0o077:
        raise CaptureError(f"file must be mode 0600: {path}")
    value = json.loads(path.read_text())
    if not isinstance(value, dict):
        raise CaptureError(f"JSON object required: {path}")
    return value


class StateLock:
    """Cross-process lock adjacent to the durable one-shot ledger."""

    def __init__(self, state_path: pathlib.Path) -> None:
        self.path = state_path.with_name(state_path.name + ".lock")
        self.descriptor = -1

    def __enter__(self) -> "StateLock":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        os.chmod(self.path.parent, 0o700)
        self.descriptor = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o600)
        os.fchmod(self.descriptor, 0o600)
        fcntl.flock(self.descriptor, fcntl.LOCK_EX)
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        fcntl.flock(self.descriptor, fcntl.LOCK_UN)
        os.close(self.descriptor)
        self.descriptor = -1


def require_temp_receipt(path: pathlib.Path) -> pathlib.Path:
    resolved_parent = path.parent.resolve(strict=True)
    roots = {pathlib.Path("/private/tmp").resolve(strict=True)}
    environment_tmp = os.environ.get("TMPDIR")
    if environment_tmp:
        roots.add(pathlib.Path(environment_tmp).resolve(strict=True))
    if not any(root in resolved_parent.parents for root in roots):
        raise CaptureError(
            "receipt must be inside a dedicated child directory under "
            "/private/tmp or TMPDIR"
        )
    parent_info = resolved_parent.stat()
    if not stat.S_ISDIR(parent_info.st_mode) \
            or stat.S_IMODE(parent_info.st_mode) != 0o700:
        raise CaptureError("receipt parent must already be a mode-0700 directory")
    return resolved_parent / path.name


def regular_sha_entry(path: pathlib.Path) -> dict[str, str]:
    resolved = path.resolve(strict=True)
    if not resolved.is_file():
        raise CaptureError(f"missing bound file: {resolved}")
    return {"path": str(resolved), "sha256": sha256_file(resolved)}


def authorization_identity(path: pathlib.Path) -> tuple[str, str]:
    value = json.loads(path.read_text())
    identifier = value.get("id")
    if identifier != AUTH_ID:
        raise CaptureError("unexpected authorization ID")
    return identifier, sha256_file(path)


def authorized_artifact(path: pathlib.Path) -> tuple[int, int, str]:
    value = json.loads(path.read_text())
    exact = value.get("exact_artifact")
    if not isinstance(exact, dict) or set(exact) != {"offset", "bytes", "sha256"}:
        raise CaptureError("authorization lacks one exact artifact")
    try:
        offset = int(exact["offset"], 0)
    except (TypeError, ValueError) as error:
        raise CaptureError("authorization artifact offset is invalid") from error
    byte_count, digest = exact["bytes"], exact["sha256"]
    if (offset != EXPECTED_OFFSET or not isinstance(byte_count, int) or byte_count <= 0
            or not isinstance(digest, str) or not HEX64.fullmatch(digest)):
        raise CaptureError("authorization artifact identity is invalid")
    return offset, byte_count, digest


def check_issuable(state_path: pathlib.Path, authorization: pathlib.Path) -> None:
    with StateLock(state_path):
        identifier, authorization_hash = authorization_identity(authorization)
        if not state_path.exists():
            return
        state = load_private_json(state_path)
        prior_key = (state.get("authorization_id"), state.get("authorization_sha256"))
        if prior_key == (identifier, authorization_hash):
            raise CaptureError(
                "this authorization already has one pending or consumed launch state"
            )
        if state.get("status") not in {"completed", "failed"}:
            raise CaptureError("a prior one-shot receipt is still pending")


def reserve(args: Any) -> None:
    with StateLock(args.state):
        if args.state.exists():
            state = load_private_json(args.state)
            identifier, authorization_hash = authorization_identity(args.authorization)
            if (state.get("authorization_id"), state.get("authorization_sha256")) == \
                    (identifier, authorization_hash):
                raise CaptureError("this authorization was already reserved")
            if state.get("status") not in {"completed", "failed"}:
                raise CaptureError("a prior authorization is not terminal")
            raise CaptureError("use a fresh state path for a future authorization")
        identifier, authorization_hash = authorization_identity(args.authorization)
        offset, byte_count, digest = authorized_artifact(args.authorization)
        receipt_path = require_temp_receipt(args.receipt)
        if receipt_path.exists():
            raise CaptureError("launch receipt path already exists")
        if (args.offset, args.bytes, args.sha256) != (offset, byte_count, digest):
            raise CaptureError("reservation does not bind the authorized artifact")
        if not HEX64.fullmatch(args.device_identity_sha256):
            raise CaptureError("reservation device identity is invalid")
        state = {
            "schema": SCHEMA,
            "authorization_id": identifier,
            "authorization_sha256": authorization_hash,
            "arm_nonce": ARM_NONCE,
            "nonce_sha256": sha256_bytes(ARM_NONCE.encode("ascii")),
            "device_identity_sha256": args.device_identity_sha256,
            "offset": offset,
            "bytes": byte_count,
            "sha256": digest,
            "status": "preparing",
            "created_at_utc": utc_text(utc_now()),
            "receipt_path": str(receipt_path),
            "receipt_sha256": None,
            "launch_count": 0,
        }
        atomic_write_json(args.state, state, exclusive=True)
    print(f"D2.3 authorization reserved: {args.state}")


def fail_reservation(state_path: pathlib.Path, authorization: pathlib.Path,
                     reason: str) -> None:
    if not reason or len(reason) > 160 or any(ord(char) < 0x20 for char in reason):
        raise CaptureError("failure reason must be bounded printable text")
    identifier, authorization_hash = authorization_identity(authorization)
    with StateLock(state_path):
        state = load_private_json(state_path)
        if state.get("status") == "failed" and state.get("failure_reason") == reason:
            return
        if (state.get("status") != "preparing"
                or state.get("authorization_id") != identifier
                or state.get("authorization_sha256") != authorization_hash):
            raise CaptureError("only the exact preparing reservation can fail")
        state.update({"status": "failed", "failed_at_utc": utc_text(utc_now()),
                      "failure_reason": reason})
        atomic_write_json(state_path, state, exclusive=False)


def _runtime_identity() -> dict[str, Any]:
    try:
        import esptool
        import esptool.loader
        import esptool.reset
        import esptool.targets.esp32p4
        import serial
        import serial.serialposix
    except ImportError as error:
        raise CaptureError("activate the pinned ESP-IDF Python environment") from error
    if esptool.__version__ != ESPTOOL_VERSION or serial.__version__ != PYSERIAL_VERSION:
        raise CaptureError("esptool or pyserial version mismatch")
    custom_reset = esptool.loader.cfg.get("custom_hard_reset_sequence")
    if custom_reset is not None:
        raise CaptureError("custom esptool hard-reset sequence is prohibited")
    def module_entry(module: Any) -> dict[str, str]:
        path = pathlib.Path(module.__file__).resolve(strict=True)
        return {"path": str(path), "sha256": sha256_file(path)}
    esptool_path = pathlib.Path(esptool.__file__).resolve(strict=True)
    serial_path = pathlib.Path(serial.__file__).resolve(strict=True)
    python_path = pathlib.Path(sys.executable).resolve(strict=True)
    return {
        "python_executable": str(python_path),
        "python_executable_sha256": sha256_file(python_path),
        "python_version": sys.version,
        "esptool_version": esptool.__version__,
        "esptool_custom_hard_reset_sequence": None,
        "esptool_path": str(esptool_path),
        "esptool_sha256": sha256_file(esptool_path),
        "pyserial_version": serial.__version__,
        "pyserial_path": str(serial_path),
        "pyserial_sha256": sha256_file(serial_path),
        "esptool_modules": {
            "loader": module_entry(esptool.loader),
            "reset": module_entry(esptool.reset),
            "esp32p4": module_entry(esptool.targets.esp32p4),
        },
        "pyserial_modules": {"serialposix": module_entry(serial.serialposix)},
    }


def emit_receipt(args: Any) -> None:
    receipt_path = require_temp_receipt(args.receipt)
    if receipt_path.exists():
        raise CaptureError("receipt already exists")
    with StateLock(args.state):
        state = load_private_json(args.state)
        if state.get("status") != "preparing" or state.get("launch_count") != 0:
            raise CaptureError("one-shot authorization was not reserved")
        if state.get("receipt_path") != str(receipt_path):
            raise CaptureError("receipt path differs from the exact reservation")
        authorized_offset, authorized_bytes, authorized_hash = authorized_artifact(args.authorization)
        if (args.offset, args.bytes, args.sha256) != (
                authorized_offset, authorized_bytes, authorized_hash):
            raise CaptureError("artifact identity differs from authorization")
        if (args.readback_bytes != args.bytes
                or args.readback_sha256 != args.sha256
                or args.readback_chunks < 1
                or not 1 <= args.readback_max_chunk_bytes <= 524_288):
            raise CaptureError("readback does not exactly bind the authorized artifact")
        if not HEX64.fullmatch(args.device_identity_sha256):
            raise CaptureError("device identity hash must be lowercase SHA-256")
        authorization_id, authorization_hash = authorization_identity(args.authorization)
        reservation = {
            "authorization_id": authorization_id,
            "authorization_sha256": authorization_hash,
            "arm_nonce": ARM_NONCE,
            "nonce_sha256": sha256_bytes(ARM_NONCE.encode("ascii")),
            "device_identity_sha256": args.device_identity_sha256,
            "offset": args.offset,
            "bytes": args.bytes,
            "sha256": args.sha256,
        }
        if any(state.get(key) != value for key, value in reservation.items()):
            raise CaptureError("reservation binding differs from completed readback")
        created = utc_now()
        nonce = ARM_NONCE
        this_tool = pathlib.Path(__file__).with_name("capture-audio-direct-diag.py")
        receipt = {
        "schema": SCHEMA,
        "state": "ready",
        "nonce": nonce,
        "created_at_utc": utc_text(created),
        "expires_at_utc": utc_text(created + dt.timedelta(seconds=RECEIPT_MAX_AGE_SECONDS)),
        "app": APP,
        "port": args.port,
        "device_identity_sha256": args.device_identity_sha256,
        "offset": args.offset,
        "bytes": args.bytes,
        "sha256": args.sha256,
        "authorization_id": authorization_id,
        "authorization_path": str(args.authorization.resolve(strict=True)),
        "authorization_sha256": authorization_hash,
        "arm_nonce_derivation": "sha256-ascii-authorization-id-space-host-arm-space-v1",
        "arm_frame": {"bytes": ARM_FRAME_BYTES, "sha256": ARM_FRAME_SHA256,
                      "line_ending": "LF-only"},
        "verifier": regular_sha_entry(args.verifier),
        "capture_tool": regular_sha_entry(this_tool),
        "capture_transport": regular_sha_entry(pathlib.Path(__file__)),
        "analyzer": regular_sha_entry(args.analyzer),
        "esptool_version": ESPTOOL_VERSION,
        "idf_commit": IDF_COMMIT,
        "portaudio": {
            "path": str(PORTAUDIO_PATH),
            "sha256": PORTAUDIO_SHA256,
            "version": PORTAUDIO_VERSION,
            "version_text": PORTAUDIO_VERSION_TEXT,
        },
        "marker_lines": [SERIAL_ATTACH_LINE, WAIT_ARM_LINE,
                         "P4_AUDIO D2.3 ARM_ACCEPTED auth="
                         + AUTH_ID + " nonce=" + nonce + " tx_count=1", *FIXED_MARKER_LINES],
        "readback": {
            "bytes": args.readback_bytes,
            "sha256": args.readback_sha256,
            "chunks": args.readback_chunks,
            "max_chunk_bytes": args.readback_max_chunk_bytes,
        },
        "launch_count": 0,
        "runtime": _runtime_identity(),
        }
        state.update({"status": "pending", "receipt_sha256": None})
        # The reservation already prevents a second issuer.  Persist and fsync
        # the receipt, then bind it from that same locked durable state.
        atomic_write_json(receipt_path, receipt, exclusive=True)
        state["receipt_sha256"] = sha256_file(receipt_path)
        atomic_write_json(args.state, state, exclusive=False)
    print(f"D2.3 launch receipt ready: {receipt_path} nonce_sha256={state['nonce_sha256']}")


def _validate_bound_entry(value: Any, label: str) -> pathlib.Path:
    if not isinstance(value, dict) or set(value) != {"path", "sha256"}:
        raise CaptureError(f"invalid {label} binding")
    path = pathlib.Path(value["path"]).resolve(strict=True)
    if sha256_file(path) != value["sha256"]:
        raise CaptureError(f"{label} hash mismatch")
    return path


def validate_receipt(receipt_path: pathlib.Path, state_path: pathlib.Path, port: str) -> dict[str, Any]:
    receipt_path = require_temp_receipt(receipt_path)
    receipt = load_private_json(receipt_path)
    state = load_private_json(state_path)
    if set(state) != {
        "schema", "authorization_id", "authorization_sha256", "arm_nonce", "nonce_sha256",
        "device_identity_sha256", "offset", "bytes", "sha256", "status",
        "created_at_utc", "receipt_path", "receipt_sha256", "launch_count",
    }:
        raise CaptureError("one-shot ledger schema changed")
    if state["status"] != "pending" or state["launch_count"] != 0:
        raise CaptureError("one-shot authorization is not pending")
    if state["receipt_path"] != str(receipt_path) or state["receipt_sha256"] != sha256_file(receipt_path):
        raise CaptureError("ledger does not bind this receipt")
    required = {
        "schema", "state", "nonce", "created_at_utc", "expires_at_utc", "app", "port",
        "device_identity_sha256", "offset", "bytes", "sha256", "authorization_id",
        "authorization_path", "authorization_sha256", "verifier", "capture_tool",
        "capture_transport", "analyzer", "esptool_version", "idf_commit", "portaudio",
        "arm_nonce_derivation", "arm_frame", "marker_lines", "readback",
        "launch_count", "runtime",
    }
    if set(receipt) != required:
        raise CaptureError("launch receipt schema changed")
    nonce = receipt["nonce"]
    if not isinstance(nonce, str) or not HEX64.fullmatch(nonce):
        raise CaptureError("invalid receipt nonce")
    if state["nonce_sha256"] != sha256_bytes(nonce.encode("ascii")):
        raise CaptureError("ledger nonce binding mismatch")
    if nonce != ARM_NONCE or state["arm_nonce"] != ARM_NONCE:
        raise CaptureError("compiled host-arm nonce binding mismatch")
    if receipt["schema"] != SCHEMA or receipt["state"] != "ready" or receipt["app"] != APP:
        raise CaptureError("wrong receipt state or app")
    if receipt["port"] != port or receipt["launch_count"] != 0:
        raise CaptureError("receipt port or launch count mismatch")
    authorized_offset, authorized_bytes, authorized_hash = authorized_artifact(
        pathlib.Path(receipt["authorization_path"])
    )
    if (receipt["offset"], receipt["bytes"], receipt["sha256"]) != (
            authorized_offset, authorized_bytes, authorized_hash):
        raise CaptureError("receipt artifact identity mismatch")
    if receipt["readback"] != {
        "bytes": authorized_bytes,
        "sha256": authorized_hash,
        "chunks": receipt["readback"].get("chunks"),
        "max_chunk_bytes": receipt["readback"].get("max_chunk_bytes"),
    } or not isinstance(receipt["readback"]["chunks"], int) \
            or receipt["readback"]["chunks"] < 1 \
            or not 1 <= receipt["readback"]["max_chunk_bytes"] <= 524_288:
        raise CaptureError("receipt readback identity mismatch")
    if receipt["authorization_id"] != AUTH_ID:
        raise CaptureError("receipt authorization ID mismatch")
    if receipt["arm_nonce_derivation"] != \
            "sha256-ascii-authorization-id-space-host-arm-space-v1":
        raise CaptureError("host-arm nonce derivation mismatch")
    if receipt["arm_frame"] != {"bytes": ARM_FRAME_BYTES,
                                 "sha256": ARM_FRAME_SHA256,
                                 "line_ending": "LF-only"}:
        raise CaptureError("host-arm frame identity mismatch")
    authorization_path = pathlib.Path(receipt["authorization_path"]).resolve(strict=True)
    authorization_id, authorization_hash = authorization_identity(authorization_path)
    if (authorization_id, authorization_hash) != (
            receipt["authorization_id"], receipt["authorization_sha256"]):
        raise CaptureError("authorization changed since receipt issuance")
    if (state["authorization_id"], state["authorization_sha256"]) != (
            receipt["authorization_id"], receipt["authorization_sha256"]):
        raise CaptureError("ledger authorization binding mismatch")
    for field in ("device_identity_sha256", "offset", "bytes", "sha256"):
        if state[field] != receipt[field]:
            raise CaptureError(f"ledger {field} binding mismatch")
    now = utc_now()
    created, expires = parse_utc(receipt["created_at_utc"]), parse_utc(receipt["expires_at_utc"])
    if not created <= now <= expires or expires - created > dt.timedelta(seconds=RECEIPT_MAX_AGE_SECONDS):
        raise CaptureError("launch receipt is expired or not yet valid")
    if receipt["esptool_version"] != ESPTOOL_VERSION or receipt["idf_commit"] != IDF_COMMIT:
        raise CaptureError("receipt toolchain identity mismatch")
    runtime = _runtime_identity()
    if runtime != receipt["runtime"]:
        raise CaptureError("active Python/esptool/pyserial identity changed")
    this_tool = pathlib.Path(__file__).with_name("capture-audio-direct-diag.py").resolve(strict=True)
    transport = pathlib.Path(__file__).resolve(strict=True)
    if _validate_bound_entry(receipt["capture_tool"], "capture tool") != this_tool:
        raise CaptureError("wrong capture tool path")
    if _validate_bound_entry(receipt["capture_transport"], "capture transport") != transport:
        raise CaptureError("wrong transport path")
    _validate_bound_entry(receipt["verifier"], "verifier")
    _validate_bound_entry(receipt["analyzer"], "analyzer")
    expected_lines = [SERIAL_ATTACH_LINE, WAIT_ARM_LINE,
                      "P4_AUDIO D2.3 ARM_ACCEPTED auth=" + AUTH_ID
                      + " nonce=" + nonce + " tx_count=1", *FIXED_MARKER_LINES]
    if receipt["marker_lines"] != expected_lines:
        raise CaptureError("receipt marker contract mismatch")
    if receipt["portaudio"] != {
        "path": str(PORTAUDIO_PATH), "sha256": PORTAUDIO_SHA256,
        "version": PORTAUDIO_VERSION, "version_text": PORTAUDIO_VERSION_TEXT,
    }:
        raise CaptureError("receipt PortAudio identity mismatch")
    if sha256_file(PORTAUDIO_PATH) != PORTAUDIO_SHA256:
        raise CaptureError("PortAudio dylib hash mismatch")
    return receipt


class PaVersionInfo(ctypes.Structure):
    _fields_ = [("versionMajor", ctypes.c_int), ("versionMinor", ctypes.c_int),
                ("versionSubMinor", ctypes.c_int), ("versionControlRevision", ctypes.c_char_p),
                ("versionText", ctypes.c_char_p)]


class PaHostApiInfo(ctypes.Structure):
    _fields_ = [("structVersion", ctypes.c_int), ("type", ctypes.c_int),
                ("name", ctypes.c_char_p), ("deviceCount", ctypes.c_int),
                ("defaultInputDevice", ctypes.c_int), ("defaultOutputDevice", ctypes.c_int)]


class PaDeviceInfo(ctypes.Structure):
    _fields_ = [("structVersion", ctypes.c_int), ("name", ctypes.c_char_p),
                ("hostApi", ctypes.c_int), ("maxInputChannels", ctypes.c_int),
                ("maxOutputChannels", ctypes.c_int), ("defaultLowInputLatency", ctypes.c_double),
                ("defaultLowOutputLatency", ctypes.c_double),
                ("defaultHighInputLatency", ctypes.c_double),
                ("defaultHighOutputLatency", ctypes.c_double),
                ("defaultSampleRate", ctypes.c_double)]


class PaStreamParameters(ctypes.Structure):
    _fields_ = [("device", ctypes.c_int), ("channelCount", ctypes.c_int),
                ("sampleFormat", ctypes.c_ulong), ("suggestedLatency", ctypes.c_double),
                ("hostApiSpecificStreamInfo", ctypes.c_void_p)]


class PaCallbackTimeInfo(ctypes.Structure):
    _fields_ = [("inputBufferAdcTime", ctypes.c_double), ("currentTime", ctypes.c_double),
                ("outputBufferDacTime", ctypes.c_double)]


class PaStreamInfo(ctypes.Structure):
    _fields_ = [("structVersion", ctypes.c_int), ("inputLatency", ctypes.c_double),
                ("outputLatency", ctypes.c_double), ("sampleRate", ctypes.c_double)]


PaCallback = ctypes.CFUNCTYPE(
    ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong,
    ctypes.POINTER(PaCallbackTimeInfo), ctypes.c_ulong, ctypes.c_void_p,
)


class PortAudioCapture:
    def __init__(self) -> None:
        self.lib = ctypes.CDLL(str(PORTAUDIO_PATH))
        self._declare()
        self.initialized = False
        self.stream = ctypes.c_void_p()
        self.callback: Any = None
        self.lock = threading.Lock()
        self.chunks: list[bytes] = []
        self.callbacks: list[dict[str, Any]] = []
        self.callback_error: str | None = None
        self.accept_callbacks = False
        self.device: dict[str, Any] = {}
        self.stream_info: dict[str, Any] = {}
        self.clock_map: dict[str, Any] = {}
        self.started = False

    def _declare(self) -> None:
        library = self.lib
        library.Pa_GetVersion.restype = ctypes.c_int
        library.Pa_GetVersionInfo.restype = ctypes.POINTER(PaVersionInfo)
        library.Pa_GetErrorText.argtypes = [ctypes.c_int]
        library.Pa_GetErrorText.restype = ctypes.c_char_p
        for name in ("Pa_Initialize", "Pa_Terminate", "Pa_GetDeviceCount",
                     "Pa_StartStream", "Pa_StopStream", "Pa_CloseStream"):
            getattr(library, name).restype = ctypes.c_int
        library.Pa_GetDeviceInfo.argtypes = [ctypes.c_int]
        library.Pa_GetDeviceInfo.restype = ctypes.POINTER(PaDeviceInfo)
        library.Pa_GetHostApiInfo.argtypes = [ctypes.c_int]
        library.Pa_GetHostApiInfo.restype = ctypes.POINTER(PaHostApiInfo)
        library.Pa_IsFormatSupported.argtypes = [ctypes.POINTER(PaStreamParameters),
                                                  ctypes.c_void_p, ctypes.c_double]
        library.Pa_IsFormatSupported.restype = ctypes.c_int
        library.Pa_OpenStream.argtypes = [ctypes.POINTER(ctypes.c_void_p),
                                           ctypes.POINTER(PaStreamParameters), ctypes.c_void_p,
                                           ctypes.c_double, ctypes.c_ulong, ctypes.c_ulong,
                                           PaCallback, ctypes.c_void_p]
        library.Pa_OpenStream.restype = ctypes.c_int
        library.Pa_GetStreamInfo.argtypes = [ctypes.c_void_p]
        library.Pa_GetStreamInfo.restype = ctypes.POINTER(PaStreamInfo)
        library.Pa_GetStreamTime.argtypes = [ctypes.c_void_p]
        library.Pa_GetStreamTime.restype = ctypes.c_double

    def check(self, code: int, operation: str) -> None:
        if code != 0:
            text = self.lib.Pa_GetErrorText(code).decode("utf-8", "replace")
            raise CaptureError(f"{operation} failed: {code} {text}")

    def preflight_open(self) -> None:
        if sha256_file(PORTAUDIO_PATH) != PORTAUDIO_SHA256:
            raise CaptureError("PortAudio library changed")
        if self.lib.Pa_GetVersion() != PORTAUDIO_VERSION:
            raise CaptureError("PortAudio API version changed")
        version = self.lib.Pa_GetVersionInfo()
        if not version or version.contents.versionText.decode() != PORTAUDIO_VERSION_TEXT:
            raise CaptureError("PortAudio version text changed")
        self.check(self.lib.Pa_Initialize(), "Pa_Initialize")
        self.initialized = True
        count = self.lib.Pa_GetDeviceCount()
        if count < 0:
            self.check(count, "Pa_GetDeviceCount")
        matches: list[tuple[int, PaDeviceInfo, PaHostApiInfo]] = []
        for index in range(count):
            device_pointer = self.lib.Pa_GetDeviceInfo(index)
            if not device_pointer:
                raise CaptureError("Pa_GetDeviceInfo returned NULL")
            device = device_pointer.contents
            host_pointer = self.lib.Pa_GetHostApiInfo(device.hostApi)
            if not host_pointer:
                raise CaptureError("Pa_GetHostApiInfo returned NULL")
            host = host_pointer.contents
            if (device.name.decode("utf-8", "strict") == CAPTURE_DEVICE_NAME
                    and host.name.decode("utf-8", "strict") == CAPTURE_HOST_API_NAME
                    and host.type == CAPTURE_HOST_API_TYPE):
                matches.append((index, device, host))
        if len(matches) != 1:
            raise CaptureError("expected exactly one CoreAudio MacBook Pro Microphone")
        index, device, host = matches[0]
        if device.maxInputChannels < CHANNELS:
            raise CaptureError("microphone lacks one input channel")
        parameters = PaStreamParameters(index, CHANNELS, 0x8,
                                         device.defaultLowInputLatency, None)
        self.check(self.lib.Pa_IsFormatSupported(ctypes.byref(parameters), None,
                                                  float(SAMPLE_RATE)),
                   "Pa_IsFormatSupported")

        @PaCallback
        def callback(input_pointer: int, output_pointer: int, frame_count: int,
                     time_info: Any, status_flags: int, user_data: int) -> int:
            del output_pointer, user_data
            with self.lock:
                if not self.accept_callbacks:
                    self.callback_error = "callback outside live capture"
                    return 2
                if not input_pointer or not time_info:
                    self.callback_error = "PortAudio callback supplied NULL input/time"
                    return 2
                if status_flags != 0:
                    self.callback_error = f"PortAudio callback status {status_flags:#x}"
                    return 2
                if frame_count != FRAMES_PER_BUFFER:
                    self.callback_error = f"unexpected callback frames {frame_count}"
                    return 2
                if sum(len(chunk) for chunk in self.chunks) + frame_count * SAMPLE_WIDTH > \
                        MAXIMUM_FRAMES * SAMPLE_WIDTH:
                    self.callback_error = "audio capture exceeded bounded size"
                    return 2
                info = time_info.contents
                if self.callbacks:
                    expected = self.callbacks[-1]["adc_time"] + \
                        self.callbacks[-1]["frames"] / SAMPLE_RATE
                    delta = info.inputBufferAdcTime - expected
                    if abs(delta) > 0.0015:
                        self.callback_error = (
                            "ADC timestamps are discontinuous: "
                            f"expected={expected:.9f} actual="
                            f"{info.inputBufferAdcTime:.9f} delta={delta:.9f}"
                        )
                        return 2
                self.chunks.append(ctypes.string_at(input_pointer, frame_count * SAMPLE_WIDTH))
                self.callbacks.append({"frames": frame_count,
                                       "adc_time": info.inputBufferAdcTime,
                                       "current_time": info.currentTime,
                                       "status_flags": status_flags})
            return 0

        self.callback = callback
        self.check(self.lib.Pa_OpenStream(ctypes.byref(self.stream), ctypes.byref(parameters),
                                          None, float(SAMPLE_RATE), FRAMES_PER_BUFFER, 0,
                                          callback, None), "Pa_OpenStream")
        stream_info = self.lib.Pa_GetStreamInfo(self.stream)
        if not stream_info:
            raise CaptureError("Pa_GetStreamInfo returned NULL")
        if abs(stream_info.contents.sampleRate - SAMPLE_RATE) > 0.001:
            raise CaptureError("PortAudio actual sample rate differs")
        self.device = {"index": index, "name": CAPTURE_DEVICE_NAME,
                       "host_api_name": CAPTURE_HOST_API_NAME,
                       "host_api_type": host.type,
                       "max_input_channels": device.maxInputChannels}
        self.stream_info = {"input_latency_seconds": stream_info.contents.inputLatency,
                            "sample_rate_hz": stream_info.contents.sampleRate}

    def _clock_sample(self) -> dict[str, int | float]:
        mono_before = time.monotonic_ns()
        pa_time = self.lib.Pa_GetStreamTime(self.stream)
        mono_after = time.monotonic_ns()
        if pa_time <= 0.0:
            raise CaptureError("Pa_GetStreamTime failed")
        return {"pa_time": pa_time, "monotonic_before_ns": mono_before,
                "monotonic_after_ns": mono_after,
                "monotonic_mid_ns": (mono_before + mono_after) // 2,
                "uncertainty_ns": mono_after - mono_before}

    def start(self) -> None:
        with self.lock:
            self.accept_callbacks = True
        before = self._clock_sample()
        self.check(self.lib.Pa_StartStream(self.stream), "Pa_StartStream")
        self.started = True
        after = self._clock_sample()
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            with self.lock:
                if self.callbacks or self.callback_error:
                    break
            time.sleep(0.005)
        with self.lock:
            if self.callback_error:
                raise CaptureError(self.callback_error)
            if not self.callbacks:
                raise CaptureError("PortAudio callback did not start")
        best = min((before, after), key=lambda item: item["uncertainty_ns"])
        self.clock_map = {
            "start_samples": [before, after],
            "selected": best,
            "pa_to_monotonic_offset_ns": int(best["monotonic_mid_ns"]
                                                 - best["pa_time"] * 1_000_000_000),
            "uncertainty_ns": best["uncertainty_ns"],
        }

    def stop(self) -> None:
        end_samples = [self._clock_sample() for _ in range(3)]
        end_best = min(end_samples, key=lambda item: item["uncertainty_ns"])
        end_offset = int(end_best["monotonic_mid_ns"]
                         - end_best["pa_time"] * 1_000_000_000)
        offset_jump = abs(end_offset - self.clock_map["pa_to_monotonic_offset_ns"])
        self.clock_map.update({"end_samples": end_samples,
                               "end_selected": end_best,
                               "end_pa_to_monotonic_offset_ns": end_offset,
                               "offset_jump_ns": offset_jump})
        if (self.clock_map["uncertainty_ns"] > 2_000_000
                or end_best["uncertainty_ns"] > 2_000_000
                or offset_jump > 2_000_000):
            raise CaptureError("PortAudio/monotonic clock mapping jumped")
        self.check(self.lib.Pa_StopStream(self.stream), "Pa_StopStream")
        self.started = False
        with self.lock:
            self.accept_callbacks = False
        callbacks_at_stop = len(self.callbacks)
        time.sleep(0.02)
        with self.lock:
            if len(self.callbacks) != callbacks_at_stop:
                raise CaptureError("PortAudio callback occurred after stop")
            if self.callback_error:
                raise CaptureError(self.callback_error)

    def close(self) -> None:
        errors = []
        if self.started and self.stream.value:
            code = self.lib.Pa_StopStream(self.stream)
            if code != 0:
                errors.append(f"Pa_StopStream={code}")
            self.started = False
        if self.stream.value:
            code = self.lib.Pa_CloseStream(self.stream)
            if code != 0:
                errors.append(f"Pa_CloseStream={code}")
            self.stream = ctypes.c_void_p()
        if self.initialized:
            code = self.lib.Pa_Terminate()
            if code != 0:
                errors.append(f"Pa_Terminate={code}")
            self.initialized = False
        if errors:
            raise CaptureError("; ".join(errors))

    def result(self) -> tuple[bytes, list[dict[str, Any]]]:
        with self.lock:
            return b"".join(self.chunks), list(self.callbacks)


def preflight_host(port: str) -> None:
    _runtime_identity()
    audio = PortAudioCapture()
    device = None
    try:
        audio.preflight_open()
        audio.start()
        deadline = time.monotonic() + 1.10
        while time.monotonic() < deadline:
            time.sleep(0.01)
        audio.stop()
        payload, callbacks = audio.result()
        captured_frames = len(callbacks) * FRAMES_PER_BUFFER
        captured_seconds = (
            callbacks[-1]["adc_time"]
            + callbacks[-1]["frames"] / SAMPLE_RATE
            - callbacks[0]["adc_time"]
        ) if callbacks else 0.0
        if (captured_frames < SAMPLE_RATE or captured_seconds < 1.0
                or len(payload) != captured_frames * SAMPLE_WIDTH):
            raise CaptureError("PortAudio host preflight capture is incomplete")
        device = open_serial_once(port)
        if device.in_waiting != 0:
            raise CaptureError("serial preflight found unexpected pending bytes")
    finally:
        if device is not None:
            device.close()
        audio.close()
    print("D2.3 host capture preflight: PASS portaudio=19.7.0 coreaudio=1 "
          f"device=MacBook-Pro-Microphone rate=48000 mono_pcm16=1 port={port} "
          f"capture_seconds={captured_seconds:.3f} callbacks={len(callbacks)} "
          "serial_exclusive=1 dtr=0 rts=0 hupcl=0 tx_bytes=0")


def monotonic_wall_sample() -> dict[str, int]:
    samples = []
    for _ in range(9):
        mono_before = time.monotonic_ns()
        wall = time.time_ns()
        mono_after = time.monotonic_ns()
        samples.append((mono_after - mono_before, (mono_before + mono_after) // 2, wall))
    uncertainty, midpoint, wall = min(samples)
    return {"monotonic_mid_ns": midpoint, "wall_ns": wall,
            "wall_minus_monotonic_ns": wall - midpoint,
            "uncertainty_ns": uncertainty}


def open_serial_once(port: str) -> Any:
    import serial
    device = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                           write_timeout=1.0, xonxoff=False, rtscts=False,
                           dsrdtr=False, exclusive=True)
    device.port = port
    device.dtr = False
    device.rts = False
    device.open()
    attributes = termios.tcgetattr(device.fileno())
    attributes[2] &= ~getattr(termios, "HUPCL", 0)
    termios.tcsetattr(device.fileno(), termios.TCSANOW, attributes)
    verify = termios.tcgetattr(device.fileno())
    if getattr(termios, "HUPCL", 0) and verify[2] & termios.HUPCL:
        device.close()
        raise CaptureError("failed to clear HUPCL")
    if not device.exclusive or device.dtr or device.rts:
        device.close()
        raise CaptureError("serial exclusive/DTR/RTS preflight failed")
    return device


def atomic_consume(receipt_path: pathlib.Path, state_path: pathlib.Path,
                   receipt: dict[str, Any]) -> pathlib.Path:
    consumed = receipt_path.with_name(receipt_path.name + ".consumed-" + receipt["nonce"])
    with StateLock(state_path):
        state = load_private_json(state_path)
        if (state.get("status") != "pending" or state.get("launch_count") != 0
                or state.get("receipt_path") != str(receipt_path)
                or state.get("receipt_sha256") != sha256_file(receipt_path)
                or state.get("authorization_id") != receipt.get("authorization_id")
                or state.get("authorization_sha256") != receipt.get("authorization_sha256")
                or state.get("arm_nonce") != receipt.get("nonce")):
            raise CaptureError("ledger changed before receipt consumption")
        now = utc_now()
        created = parse_utc(receipt.get("created_at_utc"))
        expires = parse_utc(receipt.get("expires_at_utc"))
        if not created <= now <= expires \
                or expires - created > dt.timedelta(seconds=RECEIPT_MAX_AGE_SECONDS):
            raise CaptureError("receipt expired before atomic consumption")
        authorization_path = pathlib.Path(receipt["authorization_path"])
        if authorization_identity(authorization_path) != (
                receipt["authorization_id"], receipt["authorization_sha256"]):
            raise CaptureError("authorization changed before receipt consumption")
        _validate_bound_entry(receipt["capture_tool"], "capture tool")
        _validate_bound_entry(receipt["capture_transport"], "capture transport")
        _validate_bound_entry(receipt["verifier"], "verifier")
        _validate_bound_entry(receipt["analyzer"], "analyzer")
        try:
            os.link(receipt_path, consumed)
        except FileExistsError as error:
            raise CaptureError("consumed receipt path already exists") from error
        os.unlink(receipt_path)
        fsync_directory(receipt_path.parent)
        state.update({"status": "consumed", "consumed_at_utc": utc_text(utc_now()),
                      "consumed_receipt_path": str(consumed), "launch_count": 1})
        atomic_write_json(state_path, state, exclusive=False)
    return consumed


def parse_contract_lines(raw: bytes, previous_monotonic_ns: int = 0) -> list[dict[str, Any]]:
    """Parse complete lines with a strict, distinct host-monotonic order."""
    parsed: list[dict[str, Any]] = []
    end_offset = 0
    for raw_line in raw.splitlines(keepends=True):
        end_offset += len(raw_line)
        if not raw_line.endswith(b"\n"):
            continue
        decoded = raw_line[:-1].rstrip(b"\r").decode("utf-8", "replace")
        previous_monotonic_ns = max(time.monotonic_ns(), previous_monotonic_ns + 1)
        parsed.append({"raw_line": decoded,
                       "contract_line": strip_log_prefix(decoded),
                       "monotonic_ns": previous_monotonic_ns,
                       "raw_end_offset": end_offset})
    return parsed


def update_terminal_state(state_path: pathlib.Path, status: str,
                          outcome: str, timing_path: pathlib.Path | None = None) -> None:
    with StateLock(state_path):
        state = load_private_json(state_path)
        if state.get("status") != "consumed" or state.get("launch_count") != 1:
            raise CaptureError("only a consumed launch can become terminal")
        state["status"] = status
        state["outcome"] = outcome
        state[status + "_at_utc"] = utc_text(utc_now())
        if timing_path is not None and timing_path.exists():
            state["timing_path"] = str(timing_path.resolve())
            state["timing_sha256"] = sha256_file(timing_path)
        atomic_write_json(state_path, state, exclusive=False)


def strip_log_prefix(line: str) -> str:
    position = line.find("P4_AUDIO D2.3 ")
    return line[position:] if position >= 0 else line


def live_device_identity(esp: Any) -> str:
    mac = esp.read_mac()
    if not isinstance(mac, tuple) or len(mac) != 6 \
            or any(not isinstance(value, int) or not 0 <= value <= 255 for value in mac):
        raise CaptureError("live ESP32-P4 base identity is invalid")
    normalized = "".join(f"{value:02x}" for value in mac)
    return sha256_bytes(normalized.encode("ascii"))


def launch_cli_equivalent(esp: Any, device: Any) -> None:
    """Mimic pinned esptool's `run` command and default post-operation reset."""
    try:
        esp.run()
        esp.hard_reset()
    finally:
        try:
            device.dtr = False
        finally:
            device.rts = False


def validate_initial_prefix(prefix: bytes) -> dict[str, Any]:
    if len(prefix) > 32 * 1024:
        raise CaptureError("pre-SERIAL_ATTACH prefix exceeds 32 KiB")
    decoded = prefix.decode("utf-8", "replace")
    lower = decoded.lower()
    if any(marker.lower() in lower for marker in REJECT_SUBSTRINGS) or "ets " in lower:
        raise CaptureError("rejected marker appears before SERIAL_ATTACH")
    reset_codes = re.findall(r"(?i)\brst:\s*(0x[0-9a-f]+)\b", decoded)
    if len(reset_codes) != 1 or any(code.lower() not in BENIGN_RESET_CODES
                                   for code in reset_codes):
        raise CaptureError("initial reset reason is missing, duplicated, or unsafe")
    rom_lines = [line for line in decoded.splitlines() if "esp-rom:" in line.lower()]
    if any("esp32p4" not in line.lower() and "esp32-p4" not in line.lower()
           for line in rom_lines):
        raise CaptureError("initial ESP-ROM banner does not identify ESP32-P4")
    return {"bytes": len(prefix), "sha256": sha256_bytes(prefix),
            "reset_count": len(reset_codes),
            "reset_code": reset_codes[0].lower() if reset_codes else None,
            "esp_rom_line_count": len(rom_lines)}


def capture(args: Any) -> None:
    receipt_path = require_temp_receipt(args.receipt)
    outputs = (args.wav, args.serial_raw, args.timing_json)
    if len({path.resolve() for path in outputs}) != 3:
        raise CaptureError("capture outputs must be distinct")
    for path in outputs:
        if path.exists():
            raise CaptureError(f"refusing to overwrite output: {path}")
        path.parent.mkdir(parents=True, exist_ok=True)
    receipt = validate_receipt(receipt_path, args.state, args.port)
    audio = PortAudioCapture()
    device = None
    consumed: pathlib.Path | None = None
    raw = bytearray()
    line_buffer = bytearray()
    lines: list[dict[str, Any]] = []
    tx = b""
    monotonic_wall_start: dict[str, int] = {}
    serial_open_monotonic_ns = 0
    serial_close_monotonic_ns = 0
    launch_start_monotonic_ns = 0
    launch_end_monotonic_ns = 0
    arm_tx_monotonic_ns = 0
    outcome = "preflight-failed"
    try:
        audio.preflight_open()
        device = open_serial_once(args.port)
        serial_open_monotonic_ns = time.monotonic_ns()
        audio.start()
        monotonic_wall_start = monotonic_wall_sample()
        from esptool.targets.esp32p4 import ESP32P4ROM
        esp = ESP32P4ROM(device, 115200, False)
        esp.connect("no_reset", attempts=1, warnings=False)
        if live_device_identity(esp) != receipt["device_identity_sha256"]:
            raise CaptureError("live device identity differs from receipt")
        if esp._port is not device or esp.uses_usb_otg():
            raise CaptureError("launch transport is not the reviewed UART handle")
        # Revalidate after all potentially slow/reversible preflight, then
        # consume permanently immediately before the irreversible ROM run.
        receipt = validate_receipt(receipt_path, args.state, args.port)
        esp.flush_input()
        pre_run_rx_bytes = device.in_waiting
        if pre_run_rx_bytes != 0:
            raise CaptureError("serial RX was not empty immediately before run")
        consumed = atomic_consume(receipt_path, args.state, receipt)
        outcome = "post-consumption-failed-before-launch"
        launch_start_monotonic_ns = time.monotonic_ns()
        launch_cli_equivalent(esp, device)
        launch_end_monotonic_ns = time.monotonic_ns()
        outcome = "launched-capture-failed"
        device.baudrate = 115200
        device.timeout = 0.02
        device.write_timeout = 1.0
        deadline = time.monotonic() + 21.0
        arm_sent = False
        expected_arm_accepted = receipt["marker_lines"][2]
        heartbeat_seen = False
        heartbeat_monotonic = 0.0
        last_line_monotonic_ns = 0
        wait_arm_raw_offset: int | None = None
        wait_arm_monotonic_ns = 0
        serial_attach_raw_offset: int | None = None
        serial_attach_monotonic_ns = 0
        serial_attach_seen = False
        while time.monotonic() < deadline:
            chunk = device.read(max(1, min(device.in_waiting, 4096)))
            if not chunk:
                if heartbeat_seen and time.monotonic() - heartbeat_monotonic >= 1.0:
                    break
                continue
            if len(raw) + len(chunk) > MAXIMUM_SERIAL_BYTES:
                raise CaptureError("serial transcript exceeded bounded size")
            raw.extend(chunk)
            line_buffer.extend(chunk)
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            while b"\n" in line_buffer:
                line, _, remainder = line_buffer.partition(b"\n")
                line_buffer[:] = remainder
                decoded = line.rstrip(b"\r").decode("utf-8", "replace")
                contract_line = strip_log_prefix(decoded)
                receipt_mono = max(time.monotonic_ns(), last_line_monotonic_ns + 1)
                last_line_monotonic_ns = receipt_mono
                lines.append({"raw_line": decoded, "contract_line": contract_line,
                              "monotonic_ns": receipt_mono, "raw_end_offset": len(raw)})
                lower = contract_line.lower()
                if any(marker.lower() in lower for marker in REJECT_SUBSTRINGS):
                    raise CaptureError(f"rejected serial marker: {contract_line}")
                if serial_attach_seen and any(marker in lower for marker in RESET_SUBSTRINGS):
                    raise CaptureError("reset/ROM banner appeared after SERIAL_ATTACH")
                if contract_line == SERIAL_ATTACH_LINE:
                    if serial_attach_seen:
                        raise CaptureError("SERIAL_ATTACH occurred more than once")
                    serial_attach_raw_offset = raw.find(SERIAL_ATTACH_LINE.encode("ascii"))
                    serial_attach_monotonic_ns = receipt_mono
                    if serial_attach_raw_offset < 0 or serial_attach_raw_offset > 32 * 1024:
                        raise CaptureError("SERIAL_ATTACH prefix is missing or too large")
                    if serial_attach_monotonic_ns - launch_end_monotonic_ns > 8_000_000_000:
                        raise CaptureError("SERIAL_ATTACH arrived more than eight seconds after run")
                    serial_attach_seen = True
                elif contract_line == WAIT_ARM_LINE:
                    if arm_sent:
                        raise CaptureError("WAIT_ARM occurred more than once")
                    if not serial_attach_seen:
                        raise CaptureError("WAIT_ARM preceded SERIAL_ATTACH")
                    tx = ARM_FRAME.encode("ascii")
                    wait_arm_raw_offset = raw.find(WAIT_ARM_LINE.encode("ascii"))
                    wait_arm_monotonic_ns = receipt_mono
                    if wait_arm_raw_offset < 0 or wait_arm_raw_offset > 32 * 1024:
                        raise CaptureError("WAIT_ARM prefix is missing or too large")
                    if wait_arm_monotonic_ns - launch_end_monotonic_ns > 8_000_000_000:
                        raise CaptureError("WAIT_ARM arrived more than eight seconds after run")
                    written = device.write(tx)
                    device.flush()
                    arm_tx_monotonic_ns = time.monotonic_ns()
                    if written != len(tx):
                        raise CaptureError("partial arm frame transmission")
                    arm_sent = True
                elif contract_line == expected_arm_accepted and not arm_sent:
                    raise CaptureError("ARM_ACCEPTED preceded arm transmission")
                if contract_line == FIXED_MARKER_LINES[-1]:
                    heartbeat_seen = True
                    heartbeat_monotonic = time.monotonic()
        if not arm_sent:
            raise CaptureError("exact WAIT_ARM was not observed")
        expected = receipt["marker_lines"]
        positions: list[int] = []
        for exact in expected:
            matches = [index for index, line in enumerate(lines)
                       if line["contract_line"] == exact]
            if len(matches) != 1:
                raise CaptureError(f"exact marker count differs for {exact!r}")
            positions.append(matches[0])
        if positions != sorted(positions) or len(set(positions)) != len(positions):
            raise CaptureError("exact markers are out of transcript order")
        transcript_lower = raw.decode("utf-8", "replace").lower()
        if any(marker.lower() in transcript_lower for marker in REJECT_SUBSTRINGS):
            raise CaptureError("reject marker appears in transcript")
        attach_position = transcript_lower.find(SERIAL_ATTACH_LINE.lower())
        if attach_position < 0 or any(marker in transcript_lower[attach_position:]
                                    for marker in RESET_SUBSTRINGS):
            raise CaptureError("reset/ROM banner appears at or after SERIAL_ATTACH")
        if serial_attach_raw_offset is None or wait_arm_raw_offset is None:
            raise CaptureError("serial attach/arm raw offsets were not recorded")
        initial_prefix = validate_initial_prefix(bytes(raw[:serial_attach_raw_offset]))
        audio.stop()
        audio_bytes, callbacks = audio.result()
        if not callbacks or not audio_bytes:
            raise CaptureError("microphone capture is empty")
        atomic_write_bytes(args.serial_raw, bytes(raw))
        atomic_write_bytes(args.wav, pcm16_wav_bytes(audio_bytes))
        first_adc = callbacks[0]["adc_time"]
        first_sample_monotonic_ns = int(first_adc * 1_000_000_000
                                        + audio.clock_map["pa_to_monotonic_offset_ns"])
        marker_events = {name: lines[position]["monotonic_ns"]
                         for name, position in zip(("SERIAL_ATTACH", "WAIT_ARM",
                                                   "ARM_ACCEPTED", *MARKER_NAMES),
                                                   positions)}
        monotonic_wall_end = monotonic_wall_sample()
        wall_offset_jump_ns = abs(
            monotonic_wall_end["wall_minus_monotonic_ns"]
            - monotonic_wall_start["wall_minus_monotonic_ns"]
        )
        if wall_offset_jump_ns > 2_000_000:
            raise CaptureError("monotonic/wall clock mapping jumped")
        immutable_wall_offset_ns = monotonic_wall_start["wall_minus_monotonic_ns"]
        marker_wall = {name: value + immutable_wall_offset_ns
                       for name, value in marker_events.items()}
        last_sample_end_monotonic_ns = int(
            (callbacks[-1]["adc_time"] + callbacks[-1]["frames"] / SAMPLE_RATE)
            * 1_000_000_000 + audio.clock_map["pa_to_monotonic_offset_ns"]
        )
        if last_sample_end_monotonic_ns < marker_events["TONE_BEGIN"] + 1_350_000_000:
            raise CaptureError("microphone capture lacks the required post-tone baseline")
        if first_sample_monotonic_ns > marker_events["CAPTURE_ARM"]:
            raise CaptureError("microphone capture started after CAPTURE_ARM")
        if last_sample_end_monotonic_ns < marker_events["HEARTBEAT"]:
            raise CaptureError("microphone capture ended before HEARTBEAT")
        if audio.clock_map["uncertainty_ns"] > 2_000_000 \
                or audio.clock_map["offset_jump_ns"] > 2_000_000:
            raise CaptureError("PortAudio clock mapping uncertainty is excessive")
        if any(item["status_flags"] != 0 for item in callbacks):
            raise CaptureError("PortAudio callback reported a nonzero status")
        timing = {
            "schema": 1,
            "result": "pass",
            "receipt_path_consumed": str(consumed),
            "receipt_sha256": sha256_file(consumed),
            "receipt_nonce_sha256": sha256_bytes(receipt["nonce"].encode("ascii")),
            "capture_tool": receipt["capture_tool"],
            "capture_transport": receipt["capture_transport"],
            "analyzer": receipt["analyzer"],
            "authorization_id": receipt["authorization_id"],
            "authorization_sha256": receipt["authorization_sha256"],
            "runtime": receipt["runtime"],
            "portaudio": receipt["portaudio"],
            "capture_backend": "ctypes-portaudio-callback",
            "capture_device": audio.device,
            "capture_format_supported": True,
            "stream_info": audio.stream_info,
            "audio_sample_rate_hz": SAMPLE_RATE,
            "audio_channels": CHANNELS,
            "audio_sample_width_bytes": SAMPLE_WIDTH,
            "audio_frames_per_callback": FRAMES_PER_BUFFER,
            "audio_frames": len(audio_bytes) // SAMPLE_WIDTH,
            "audio_bytes": len(audio_bytes),
            "audio_sha256_pcm": sha256_bytes(audio_bytes),
            "audio_callback_count": len(callbacks),
            "audio_callback_status_flags": [item["status_flags"] for item in callbacks],
            "audio_callback_adc_first_seconds": first_adc,
            "audio_callback_adc_last_seconds": callbacks[-1]["adc_time"],
            "audio_last_sample_end_monotonic_ns": last_sample_end_monotonic_ns,
            "audio_spans_tone_begin_plus_1_35_seconds": True,
            "audio_first_sample_monotonic_ns": first_sample_monotonic_ns,
            "audio_first_sample_wall_ns": first_sample_monotonic_ns
            + immutable_wall_offset_ns,
            "portaudio_monotonic_mapping": audio.clock_map,
            "monotonic_wall_mapping": {
                "start": monotonic_wall_start,
                "end": monotonic_wall_end,
                "immutable_wall_minus_monotonic_ns": immutable_wall_offset_ns,
                "offset_jump_ns": wall_offset_jump_ns,
            },
            "serial_port": args.port,
            "serial_open_count": 1,
            "serial_close_count": 1,
            "serial_exclusive": True,
            "serial_hupcl": False,
            "serial_dtr_initial": False,
            "serial_rts_initial": False,
            "serial_open_monotonic_ns": serial_open_monotonic_ns,
            "serial_close_monotonic_ns": None,
            "serial_transmitted_frames": 1,
            "serial_transmitted_bytes": len(tx),
            "serial_transmitted_sha256": sha256_bytes(tx),
            "arm_tx_monotonic_ns": arm_tx_monotonic_ns,
            "launch_transport": (
                "esptool-4.12.0-same-open-pyserial-handle-"
                "run-then-cli-equivalent-hard-reset"
            ),
            "launch_hard_reset_count": 1,
            "launch_dtr_assertions": 0,
            "launch_rts_en_pulse_ms": 100,
            "launch_start_monotonic_ns": launch_start_monotonic_ns,
            "launch_end_monotonic_ns": launch_end_monotonic_ns,
            "pre_run_rx_bytes": pre_run_rx_bytes,
            "serial_attach_latency_ns": serial_attach_monotonic_ns
            - launch_end_monotonic_ns,
            "wait_arm_latency_ns": wait_arm_monotonic_ns - launch_end_monotonic_ns,
            "initial_boot_prefix": initial_prefix,
            "serial_bytes": len(raw),
            "serial_sha256": sha256_bytes(raw),
            "serial_line_count": len(lines),
            "serial_exact_marker_lines": receipt["marker_lines"],
            "serial_exact_marker_positions": positions,
            "serial_event_monotonic_ns": marker_events,
            "serial_event_wall_ns": marker_wall,
            "serial_markers_ordered": True,
            "serial_reject_markers_absent": True,
            "wav": str(args.wav.resolve()),
            "wav_bytes": args.wav.stat().st_size,
            "wav_sha256": sha256_file(args.wav),
            "serial_raw": str(args.serial_raw.resolve()),
        }
        atomic_write_json(args.timing_json, timing, exclusive=True)
        outcome = "completed"
    except Exception as error:
        if consumed is not None:
            outcome = f"{outcome}: {type(error).__name__}: {error}"
        raise
    finally:
        close_errors = []
        if device is not None:
            serial_close_monotonic_ns = time.monotonic_ns()
            try:
                device.close()
            except Exception as error:
                close_errors.append(f"serial close: {error}")
        try:
            audio.close()
        except Exception as error:
            close_errors.append(f"audio close: {error}")
        if args.timing_json.exists():
            try:
                timing = json.loads(args.timing_json.read_text())
                timing["serial_close_monotonic_ns"] = serial_close_monotonic_ns
                atomic_write_json(args.timing_json, timing, exclusive=False)
            except Exception as error:
                close_errors.append(f"timing finalize: {error}")
        if consumed is not None:
            terminal = "completed" if outcome == "completed" and not close_errors else "failed"
            detail = outcome if not close_errors else outcome + "; " + "; ".join(close_errors)
            update_terminal_state(args.state, terminal, detail,
                                  args.timing_json if args.timing_json.exists() else None)
        if close_errors and sys.exc_info()[0] is None:
            raise CaptureError("; ".join(close_errors))
