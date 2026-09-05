#!/usr/bin/env python3

"""Capture and gate one exact Waveshare Console OS retained UART build."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import pathlib
import re
import stat
import sys
import time
from datetime import datetime, timezone


MAX_BYTES = 1024 * 1024
CAPTURE_VERSION = os.environ.get("P4_CAPTURE_VERSION", "0.4.84")
APPLICATION_SHA256 = os.environ.get(
    "P4_CAPTURE_APPLICATION_SHA256",
    "8d542f4b5badfe83ff1955c318853a031bcc5a6dd48e0f25124550313599371b",
)
APPLICATION_PATH = os.environ.get("P4_CAPTURE_APPLICATION_PATH", "")
APPLICATION_BYTES_TEXT = os.environ.get("P4_CAPTURE_APPLICATION_BYTES", "")
AUTHORIZATION_PATH = os.environ.get("P4_CAPTURE_AUTHORIZATION_PATH", "")
AUTHORIZATION_SHA256 = os.environ.get("P4_CAPTURE_AUTHORIZATION_SHA256", "")
REQUIRE_USB_TOUCH = os.environ.get("P4_CAPTURE_REQUIRE_USB_TOUCH", "0") == "1"
REQUIRE_SINGLE_START = os.environ.get("P4_CAPTURE_REQUIRE_SINGLE_START", "0") == "1"
REQUIRE_BATTERY = os.environ.get("P4_CAPTURE_REQUIRE_BATTERY", "0") == "1"
REQUIRE_H1_USB_DRIVE = (
    os.environ.get("P4_CAPTURE_REQUIRE_H1_USB_DRIVE", "0") == "1"
)
REQUIRE_PIPELINED_DISPLAY = (
    os.environ.get("P4_CAPTURE_REQUIRE_PIPELINED_DISPLAY", "0") == "1"
)
REQUIRE_NATIVE_CONTENT = (
    os.environ.get("P4_CAPTURE_REQUIRE_NATIVE_CONTENT", "0") == "1"
)
REQUIRE_PARTIAL_PRESENT = (
    os.environ.get("P4_CAPTURE_REQUIRE_PARTIAL_PRESENT", "0") == "1"
)
REQUIRE_GT911_CONFIG = (
    os.environ.get("P4_CAPTURE_REQUIRE_GT911_CONFIG", "0") == "1"
)
REQUIRE_GT911_TUNE = (
    os.environ.get("P4_CAPTURE_REQUIRE_GT911_TUNE", "0") == "1"
)
REQUIRE_GT911_RESTORE = (
    os.environ.get("P4_CAPTURE_REQUIRE_GT911_RESTORE", "0") == "1"
)
REQUIRE_RUNTIME_STATS = (
    os.environ.get("P4_CAPTURE_REQUIRE_RUNTIME_STATS", "1") == "1"
)
FORBID_RUNTIME_STATS = (
    os.environ.get("P4_CAPTURE_FORBID_RUNTIME_STATS", "0") == "1"
)
if REQUIRE_RUNTIME_STATS and FORBID_RUNTIME_STATS:
    raise RuntimeError("runtime stats require and forbid modes are incompatible")
try:
    MIN_SECONDS = float(os.environ.get("P4_CAPTURE_MIN_SECONDS", "0"))
except ValueError as error:
    raise RuntimeError("invalid configured minimum capture seconds") from error
if not math.isfinite(MIN_SECONDS) or MIN_SECONDS < 0:
    raise RuntimeError("invalid configured minimum capture seconds")
if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", CAPTURE_VERSION) is None:
    raise RuntimeError("invalid configured Console OS version")
if re.fullmatch(r"[0-9a-f]{64}", APPLICATION_SHA256) is None:
    raise RuntimeError("invalid configured application SHA-256")
if APPLICATION_BYTES_TEXT and not APPLICATION_BYTES_TEXT.isdigit():
    raise RuntimeError("invalid configured application byte count")
if bool(AUTHORIZATION_PATH) != bool(AUTHORIZATION_SHA256):
    raise RuntimeError("authorization path and SHA-256 must be configured together")
if AUTHORIZATION_SHA256 and re.fullmatch(r"[0-9a-f]{64}", AUTHORIZATION_SHA256) is None:
    raise RuntimeError("invalid configured authorization SHA-256")
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
if REQUIRE_USB_TOUCH:
    REQUIRED.update({
        "usb_input": b"P4_CONSOLE_OS USB_INPUT_READY ",
        "touch": b"P4_CONSOLE_OS TOUCH_READY ",
    })
if REQUIRE_BATTERY:
    REQUIRED.update({
        "battery_ready": b"P4_CONSOLE_OS BATTERY_READY ",
        "battery_sample": b"P4_CONSOLE_OS BATTERY_SAMPLE valid=1 ",
    })
if REQUIRE_H1_USB_DRIVE:
    REQUIRED["h1_usb_drive_control"] = (
        b"P4_CONSOLE_OS H1_USB_DRIVE_CONTROL_READY "
    )
if REQUIRE_PIPELINED_DISPLAY:
    REQUIRED["pipelined_display"] = b"pipelined_double_buffer=1"
if REQUIRE_NATIVE_CONTENT:
    REQUIRED["native_content"] = (
        b"P4_DISPLAY CONTENT_ACCELERATOR ready=1 engine=ppa-srm "
        b"source=768x480 target=480x768 scale=1:1 rotation_ccw=90 "
        b"shell_handoff=pipelined game_handoff=refresh-synchronous"
    )
if REQUIRE_PARTIAL_PRESENT:
    REQUIRED.update({
        "partial_present": b"partial_submits=",
        "partial_fallbacks": b"partial_full_fallbacks=",
    })
if REQUIRE_GT911_CONFIG:
    REQUIRED["gt911_config"] = (
        b"P4_CONSOLE_OS GT911_CONFIG mode=read-only product="
    )
if REQUIRE_GT911_TUNE:
    REQUIRED["gt911_tune"] = (
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=ready action="
    )
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
REBOOT_MARKERS = (
    b"rst:",
    b"SW_RESET",
    b"TG0WDT_SYS_RESET",
    b"TG1WDT_SYS_RESET",
    b"RTCWDT_RTC_RESET",
    b"RTCWDT_BROWN_OUT_RESET",
)
START_MARKER = b"P4_CONSOLE_OS START shell=freertos-native"
GT911_TUNE_MARKER = b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
GT911_TUNE_LINE = re.compile(
    rb"^P4_CONSOLE_OS GT911_CONFIG_TUNE "
    rb"target_normal_filter=([0-9]+) "
    rb"result=([^ ]+) action=([^ ]+) vendor=0x([0-9a-fA-F]{2}) "
    rb"before_filter=([0-9]+) before_checksum=0x([0-9a-fA-F]{2}) "
    rb"after_filter=([0-9]+) after_checksum=0x([0-9a-fA-F]{2}) "
    rb"changed=([01]) may_have_changed=([01]) restore_attempted=([01]) "
    rb"restored=([01]) restore_result=([^ ]+) apply_result=([^ ]+)$"
)
GT911_RESTORE_MARKER = b"P4_CONSOLE_OS GT911_CONFIG_RESTORE "
GT911_RESTORE_LINE = re.compile(
    rb"^P4_CONSOLE_OS GT911_CONFIG_RESTORE target_normal_filter=([0-9]+) "
    rb"result=([^ ]+) action=([^ ]+) vendor=0x([0-9a-fA-F]{2}) "
    rb"before_filter=([0-9]+) before_checksum=0x([0-9a-fA-F]{2}) "
    rb"after_filter=([0-9]+) after_checksum=0x([0-9a-fA-F]{2}) "
    rb"changed=([01]) already_original=([01]) may_have_changed=([01]) "
    rb"restore_result=([^ ]+)$"
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


def _stats_line_valid(line: bytes) -> bool:
    fields = {
        match.group(1): int(match.group(2))
        for match in re.finditer(rb"(?:^| )([a-z_]+)=([0-9]+)(?= |$)", line)
    }
    return (
        b"usb_input_ready=1" in line
        and b"audio=es8311-ready" in line
        and b"storage=app-ready" in line
        and b"p4cart_rejected=0" in line
        and fields.get(b"display_submits", 0) > 0
        and fields.get(b"display_completions") == fields.get(b"display_submits")
        and fields.get(b"display_timeouts") == 0
        and fields.get(b"display_failures") == 0
        and fields.get(b"display_accel_failures") == 0
        and (
            not REQUIRE_PIPELINED_DISPLAY
            or fields.get(b"display_accelerated", 0) > 0
        )
    )


def _gt911_tune_valid(post_start: bytes) -> tuple[bool, int]:
    lines = []
    for line in post_start.splitlines():
        marker_position = line.find(GT911_TUNE_MARKER)
        if marker_position >= 0:
            lines.append(line[marker_position:])
    if len(lines) != 1:
        return False, len(lines)

    match = GT911_TUNE_LINE.fullmatch(lines[0])
    if match is None:
        return False, len(lines)
    (
        target,
        result,
        action,
        _vendor,
        before_filter,
        before_checksum,
        after_filter,
        after_checksum,
        changed,
        may_have_changed,
        restore_attempted,
        restored,
        restore_result,
        apply_result,
    ) = match.groups()
    if (
        target != b"4"
        or result != b"ready"
        or action not in (b"applied", b"already-target")
        or after_filter != b"4"
        or apply_result != b"ESP_OK"
        or restore_attempted != b"0"
        or restored != b"0"
        or restore_result != b"ESP_ERR_INVALID_STATE"
        or int(before_checksum, 16) == 0
        or int(after_checksum, 16) == 0
    ):
        return False, len(lines)
    if action == b"applied":
        valid = (
            before_filter == b"8"
            and before_checksum == b"79"
            and after_checksum == b"7d"
            and changed == b"1"
            and may_have_changed == b"1"
        )
    else:
        valid = (
            before_filter == b"4"
            and before_checksum == b"7d"
            and after_checksum == b"7d"
            and changed == b"0"
            and may_have_changed == b"0"
        )
    return valid, len(lines)


def _gt911_restore_valid(post_start: bytes) -> tuple[bool, int]:
    lines = [
        line[line.find(GT911_RESTORE_MARKER):]
        for line in post_start.splitlines()
        if GT911_RESTORE_MARKER in line
    ]
    if len(lines) != 1:
        return False, len(lines)
    match = GT911_RESTORE_LINE.fullmatch(lines[0])
    if match is None:
        return False, len(lines)
    (target, result, action, _vendor, before_filter, before_checksum,
     after_filter, after_checksum, changed, already_original, may_have_changed,
     restore_result) = match.groups()
    if target != b"8" or result != b"ready" or restore_result != b"ESP_OK":
        return False, len(lines)
    restored = (action == b"restored" and before_filter == b"4" and
                before_checksum == b"7d" and after_filter == b"8" and
                after_checksum == b"79" and changed == b"1" and
                already_original == b"0" and may_have_changed == b"1")
    noop = (action == b"already-original" and before_filter == b"8" and
            before_checksum == b"79" and after_filter == b"8" and
            after_checksum == b"79" and changed == b"0" and
            already_original == b"1" and may_have_changed == b"0")
    return restored or noop, len(lines)


def analyze(payload: bytes) -> dict[str, object]:
    required = {name: marker in payload for name, marker in REQUIRED.items()}
    rejected = [
        token.decode("ascii", "replace") for token in REJECT if token in payload
    ]
    start_position = payload.find(START_MARKER)
    post_start = (
        payload[start_position + len(START_MARKER) :]
        if start_position >= 0
        else b""
    )
    gt911_tune_valid = True
    gt911_tune_lines = 0
    if REQUIRE_GT911_TUNE:
        gt911_tune_valid, gt911_tune_lines = _gt911_tune_valid(post_start)
        required["gt911_tune"] = gt911_tune_valid
    gt911_restore_valid = True
    gt911_restore_lines = 0
    if REQUIRE_GT911_RESTORE:
        gt911_restore_valid, gt911_restore_lines = _gt911_restore_valid(post_start)
        required["gt911_restore"] = gt911_restore_valid
    reboot_markers = [
        token.decode("ascii", "replace")
        for token in REBOOT_MARKERS
        if token in post_start
    ]
    start_count = payload.count(START_MARKER)
    stats = _stats_lines(payload)
    selected = stats[-2:]
    if FORBID_RUNTIME_STATS:
        stats_valid = len(stats) == 0
    elif REQUIRE_RUNTIME_STATS:
        stats_valid = len(selected) == 2 and all(
            _stats_line_valid(line) for line in selected
        )
    else:
        stats_valid = True
    single_start_valid = (
        not REQUIRE_SINGLE_START
        or (start_count == 1 and not reboot_markers)
    )
    passed = (
        all(required.values())
        and not rejected
        and stats_valid
        and single_start_valid
    )
    analysis = {
        "schema": 1,
        "result": "pass" if passed else "fail",
        "classification": f"waveshare-console-os-{CAPTURE_VERSION}-retained-uart",
        "application_sha256": APPLICATION_SHA256,
        "application_path": APPLICATION_PATH or None,
        "application_bytes": (
            int(APPLICATION_BYTES_TEXT) if APPLICATION_BYTES_TEXT else None
        ),
        "authorization_sha256": AUTHORIZATION_SHA256 or None,
        "required_markers": required,
        "require_gt911_restore": REQUIRE_GT911_RESTORE,
        "rejected_markers": rejected,
        "reboot_markers": reboot_markers,
        "start_count": start_count,
        "single_start_valid": single_start_valid,
        "stats_observed": len(stats),
        "last_two_stats_valid": stats_valid,
        "require_runtime_stats": REQUIRE_RUNTIME_STATS,
        "forbid_runtime_stats": FORBID_RUNTIME_STATS,
        "runtime_stats_mode": "forbid" if FORBID_RUNTIME_STATS else (
            "require" if REQUIRE_RUNTIME_STATS else "optional"
        ),
        "min_seconds": MIN_SECONDS,
        "raw_bytes": len(payload),
        "raw_sha256": hashlib.sha256(payload).hexdigest(),
    }
    if REQUIRE_GT911_TUNE:
        analysis["gt911_tune_valid"] = gt911_tune_valid
        analysis["gt911_tune_lines"] = gt911_tune_lines
    if REQUIRE_GT911_RESTORE:
        analysis["gt911_restore_valid"] = gt911_restore_valid
        analysis["gt911_restore_lines"] = gt911_restore_lines
    return analysis


def validate_exact_artifact() -> None:
    if not AUTHORIZATION_PATH:
        return
    if not APPLICATION_PATH or not APPLICATION_BYTES_TEXT:
        raise RuntimeError(
            "exact capture requires application path and byte count"
        )
    project_root = pathlib.Path(__file__).resolve().parent.parent
    authorization_path = project_root / AUTHORIZATION_PATH
    authorization_raw = authorization_path.read_bytes()
    if hashlib.sha256(authorization_raw).hexdigest() != AUTHORIZATION_SHA256:
        raise RuntimeError("exact-unit authorization digest changed")
    authorization = json.loads(authorization_raw)
    candidate = authorization["candidate"]["application"]
    if candidate["path"] != APPLICATION_PATH:
        raise RuntimeError("capture application path differs from authorization")
    if candidate["bytes"] != int(APPLICATION_BYTES_TEXT):
        raise RuntimeError("capture application byte count differs from authorization")
    if candidate["sha256"] != APPLICATION_SHA256:
        raise RuntimeError("capture application digest differs from authorization")
    application_path = project_root / APPLICATION_PATH
    application_raw = application_path.read_bytes()
    if len(application_raw) != int(APPLICATION_BYTES_TEXT):
        raise RuntimeError("sealed application byte count changed")
    if hashlib.sha256(application_raw).hexdigest() != APPLICATION_SHA256:
        raise RuntimeError("sealed application digest changed")
    if stat.S_IMODE(application_path.stat().st_mode) != 0o400:
        raise RuntimeError("sealed application mode is not 0400")


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
            if result["result"] == "pass" and time.monotonic() - started >= MIN_SECONDS:
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
    validate_exact_artifact()
    if not 15.0 <= args.seconds <= 120.0:
        parser.error("--seconds must be in [15, 120]")
    if args.raw.exists() or args.raw.is_symlink():
        parser.error("--raw output must not already exist")
    if args.summary.exists() or args.summary.is_symlink():
        parser.error("--summary output must not already exist")
    if args.raw.resolve() == args.summary.resolve():
        parser.error("--raw and --summary must be different paths")

    started = datetime.now(timezone.utc).isoformat()
    capture_started = time.monotonic()
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
    result["capture_elapsed_seconds"] = round(
        time.monotonic() - capture_started, 3
    )
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
