#!/usr/bin/env python3

"""Capture and gate one exact Waveshare Console OS retained UART build."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import sys
import time
from datetime import datetime, timezone


MAX_BYTES = 1024 * 1024
CAPTURE_VERSION = os.environ.get("P4_CAPTURE_VERSION", "0.4.84")
APPLICATION_SHA256 = os.environ.get(
    "P4_CAPTURE_APPLICATION_SHA256",
    "8d542f4b5badfe83ff1955c318853a031bcc5a6dd48e0f25124550313599371b",
)
if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", CAPTURE_VERSION) is None:
    raise RuntimeError("invalid configured Console OS version")
if re.fullmatch(r"[0-9a-f]{64}", APPLICATION_SHA256) is None:
    raise RuntimeError("invalid configured application SHA-256")
REQUIRED = {
    "version": f"App version:      {CAPTURE_VERSION}".encode("ascii"),
    "board": (
        b"P4_CONSOLE_OS BOARD_ID vendor=Waveshare "
        b"product=ESP32-P4-WIFI6-Touch-LCD-4.3"
    ),
    "start": b"P4_CONSOLE_OS START shell=freertos-native",
    "catalog": b"P4_CONSOLE_OS GAME_CATALOG ",
    "multiplayer_registry": b"P4_CONSOLE_OS MULTIPLAYER_REGISTRY_READY ",
    "ready": (
        b"P4_CONSOLE_OS READY page=home display=mipi-dsi "
        b"storage=microsd-read-only"
    ),
    "h2_role": b"h2_role=usb-host-external-vbus",
}
REJECT = (
    b"P4_CONSOLE_OS HALT",
    b"Guru Meditation Error",
    b"panic'ed",
    b"abort() was called",
    b"assert failed:",
    b"Task watchdog got triggered",
    b"Interrupt wdt timeout",
    b"Brownout detector was triggered",
)


class CaptureFailure(RuntimeError):
    """Retained-UART failure carrying every bounded byte already received."""

    def __init__(self, reason: str, partial_raw: bytes) -> None:
        self.reason = reason[:512]
        self.partial_raw = bytes(partial_raw[:MAX_BYTES])
        super().__init__(self.reason)


def _stats_lines(payload: bytes) -> list[bytes]:
    return [
        line[line.find(b"P4_CONSOLE_OS STATS ") :]
        for line in payload.splitlines()
        if b"P4_CONSOLE_OS STATS " in line
    ]


def analyze(payload: bytes) -> dict[str, object]:
    required = {name: marker in payload for name, marker in REQUIRED.items()}
    rejected = [
        token.decode("ascii", "replace") for token in REJECT if token in payload
    ]
    stats = _stats_lines(payload)
    selected = stats[-2:]
    stats_valid = len(selected) == 2 and all(
        b"usb_input_ready=1" in line
        and b"display_failures=0" in line
        and b"audio=es8311-ready" in line
        and b"storage=app-ready" in line
        and b"p4cart_rejected=0" in line
        for line in selected
    )
    passed = all(required.values()) and not rejected and stats_valid
    return {
        "schema": 1,
        "result": "pass" if passed else "fail",
        "classification": f"waveshare-console-os-{CAPTURE_VERSION}-retained-uart",
        "application_sha256": APPLICATION_SHA256,
        "required_markers": required,
        "rejected_markers": rejected,
        "stats_observed": len(stats),
        "last_two_stats_valid": stats_valid,
        "raw_bytes": len(payload),
        "raw_sha256": hashlib.sha256(payload).hexdigest(),
    }


def capture(
    port: str, seconds: float, reset_via_rts: bool
) -> tuple[bytes, dict[str, object]]:
    try:
        import serial
    except ImportError as error:
        raise RuntimeError("pyserial is required") from error

    device = serial.Serial()
    device.port = port
    device.baudrate = 115200
    device.timeout = 0.10
    device.write_timeout = 0.10
    device.exclusive = True
    device.dsrdtr = False
    device.rtscts = False
    device.dtr = False
    device.rts = False
    raw = bytearray()
    started = time.monotonic()
    try:
        device.open()
        if not device.is_open or device.exclusive is not True:
            raise RuntimeError("retained UART is not open and exclusive")
        if bool(device.dtr) or bool(device.rts):
            raise RuntimeError("retained UART reset controls are active")
        descriptor = device.fileno()
        if reset_via_rts:
            device.reset_input_buffer()
            device.rts = True
            time.sleep(0.1)
            device.rts = False
            if bool(device.dtr) or bool(device.rts):
                raise RuntimeError("retained UART reset controls did not return idle")
            print(
                "Capture launched the app with one RTS reset on the retained UART.",
                flush=True,
            )
        else:
            print(
                "Capture armed with DTR/RTS inactive; physically tap RESET once.",
                flush=True,
            )
        while time.monotonic() - started < seconds:
            if (
                not device.is_open
                or device.fileno() != descriptor
                or device.exclusive is not True
            ):
                raise RuntimeError("retained UART descriptor changed")
            if bool(device.dtr) or bool(device.rts):
                raise RuntimeError("retained UART reset controls changed")
            chunk = device.read(4096)
            if not chunk:
                continue
            available = MAX_BYTES - len(raw)
            raw.extend(chunk[:available])
            if len(chunk) > available:
                raise RuntimeError("serial transcript exceeded 1 MiB")
            result = analyze(bytes(raw))
            if result["result"] == "pass":
                return bytes(raw), result
    except BaseException as error:
        if isinstance(error, CaptureFailure):
            raise
        raise CaptureFailure(f"{type(error).__name__}: {error}", bytes(raw)) from error
    finally:
        if device.is_open:
            device.close()
    payload = bytes(raw)
    return payload, analyze(payload)


def _write_new(path: pathlib.Path, data: bytes) -> None:
    if path.exists() or path.is_symlink():
        raise RuntimeError(f"refusing to replace existing output: {path}")
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            f"receive-only Waveshare Console OS {CAPTURE_VERSION} startup capture; "
            "the operator performs the physical reset"
        )
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--raw", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument(
        "--reset-via-rts",
        action="store_true",
        help="launch once by toggling EN through RTS on the retained UART",
    )
    args = parser.parse_args()
    if not 15.0 <= args.seconds <= 120.0:
        parser.error("--seconds must be in [15, 120]")
    if args.raw.exists() or args.raw.is_symlink():
        parser.error("--raw output must not already exist")
    if args.summary.exists() or args.summary.is_symlink():
        parser.error("--summary output must not already exist")
    if args.raw.resolve() == args.summary.resolve():
        parser.error("--raw and --summary must be different paths")

    started = datetime.now(timezone.utc).isoformat()
    try:
        payload, result = capture(args.port, args.seconds, args.reset_via_rts)
    except CaptureFailure as error:
        payload = error.partial_raw
        result = analyze(payload)
        result["capture_error"] = error.reason
    except BaseException as error:
        payload = b""
        result = analyze(payload)
        result["capture_error"] = f"{type(error).__name__}: {error}"[:512]
    result["timestamp_utc"] = started
    result["port"] = args.port
    _write_new(args.raw, payload)
    _write_new(
        args.summary,
        (json.dumps(result, indent=2, sort_keys=True) + "\n").encode(),
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
