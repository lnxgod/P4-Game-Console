#!/usr/bin/env python3

"""Capture one receive-only Console OS scroll-boundary diagnostic record."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import stat
import time
from datetime import datetime, timezone
from typing import Any


MAX_TRANSCRIPT_BYTES = 1024 * 1024
DRAIN_SECONDS = 0.75
MARKER = b"P4_CONSOLE_OS SCROLL_GESTURE_GDMA_FRAME_BOUNDARY"
READY = b"P4_CONSOLE_OS READY "
START = b"P4_CONSOLE_OS START shell=freertos-native"
POWERON_REASONS = (b"(POWERON)", b"(POWERON_RESET)")
ANSI_COLOR_RE = re.compile(rb"\x1b\[[0-9;]*m")
REJECT = (
    b"P4_CONSOLE_OS START shell=freertos-native",
    b"P4_CONSOLE_OS HALT",
    b"Guru Meditation Error",
    b"panic'ed",
    b"abort() was called",
    b"assert failed:",
    b"Task watchdog got triggered",
    b"Interrupt wdt timeout",
    b"Brownout detector was triggered",
    b"rst:",
    b"SW_RESET",
    b"TG0WDT_SYS_RESET",
    b"TG1WDT_SYS_RESET",
    b"RTCWDT_RTC_RESET",
    b"RTCWDT_BROWN_OUT_RESET",
)

_INT = rb"(\d+)"
_LINE_RE = re.compile(
    rb"^P4_CONSOLE_OS SCROLL_GESTURE_GDMA_FRAME_BOUNDARY "
    rb"idle_us=" + _INT + rb" "
    rb"mb_reports=" + _INT + rb" "
    rb"mb_cadence_avg_us=" + _INT + rb" "
    rb"mb_age_peak_us=" + _INT + rb" "
    rb"mb_age_last_us=" + _INT + rb" "
    rb"mb_samples=" + _INT + rb" "
    rb"mb_failures=" + _INT + rb" "
    rb"mb_stale=" + _INT + rb" "
    rb"late_checks=" + _INT + rb" "
    rb"late_updates=" + _INT + rb" "
    rb"late_scroll=" + _INT + rb" "
    rb"late_wait_us=" + _INT + rb" "
    rb"refreshes=" + _INT + rb" "
    rb"interactive=" + _INT + rb" "
    rb"path=full:" + _INT + rb",partial:" + _INT + rb",composite:" + _INT + rb" "
    rb"comp_submits=" + _INT + rb" "
    rb"comp_bootstraps=" + _INT + rb" "
    rb"comp_failures=" + _INT + rb" "
    rb"partial_full_fallbacks=" + _INT + rb" "
    rb"input_gdma_boundary_avg_us=" + _INT + rb" "
    rb"handoff_gdma_boundary_avg_us=" + _INT + rb" "
    rb"present_samples=" + _INT + rb" "
    rb"render_avg_max_us=" + _INT + rb"/" + _INT + rb" "
    rb"submit_avg_max_us=" + _INT + rb"/" + _INT + rb" "
    rb"reuse_avg_max_us=" + _INT + rb"/" + _INT + rb" "
    rb"transform_avg_max_us=" + _INT + rb"/" + _INT + rb" "
    rb"handoff_avg_max_us=" + _INT + rb"/" + _INT + rb"$"
)
_FIELD_NAMES = (
    "idle_us", "mb_reports", "mb_cadence_avg_us", "mb_age_peak_us",
    "mb_age_last_us", "mb_samples", "mb_failures", "mb_stale",
    "late_checks", "late_updates", "late_scroll", "late_wait_us",
    "refreshes", "interactive", "path_full", "path_partial",
    "path_composite", "comp_submits", "comp_bootstraps", "comp_failures",
    "partial_full_fallbacks", "input_gdma_boundary_avg_us",
    "handoff_gdma_boundary_avg_us", "present_samples", "render_avg_us",
    "render_max_us", "submit_avg_us", "submit_max_us", "reuse_avg_us",
    "reuse_max_us", "transform_avg_us", "transform_max_us",
    "handoff_avg_us", "handoff_max_us",
)


def _required_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        raise RuntimeError(f"{name} is required")
    return value


def _settings() -> dict[str, str]:
    return {
        "version": _required_env("P4_CAPTURE_VERSION"),
        "application_path": _required_env("P4_CAPTURE_APPLICATION_PATH"),
        "application_bytes": _required_env("P4_CAPTURE_APPLICATION_BYTES"),
        "application_sha256": _required_env("P4_CAPTURE_APPLICATION_SHA256"),
        "authorization_path": _required_env("P4_CAPTURE_AUTHORIZATION_PATH"),
        "authorization_sha256": _required_env(
            "P4_CAPTURE_AUTHORIZATION_SHA256"
        ),
    }


def _project_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parent.parent


def _sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_exact_artifact() -> dict[str, str]:
    settings = _settings()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", settings["version"]):
        raise RuntimeError("P4_CAPTURE_VERSION is invalid")
    if not settings["application_bytes"].isdigit():
        raise RuntimeError("P4_CAPTURE_APPLICATION_BYTES is invalid")
    for key in ("application_sha256", "authorization_sha256"):
        if re.fullmatch(r"[0-9a-f]{64}", settings[key]) is None:
            raise RuntimeError(f"{key} is invalid")

    root = _project_root()
    auth_path = root / settings["authorization_path"]
    if _sha256(auth_path) != settings["authorization_sha256"]:
        raise RuntimeError("exact-unit authorization digest changed")
    authorization = json.loads(auth_path.read_text(encoding="utf-8"))
    candidate = authorization["candidate"]
    application = candidate["application"]
    if candidate["version"] != settings["version"]:
        raise RuntimeError("capture version differs from authorization")
    if application["path"] != settings["application_path"]:
        raise RuntimeError("capture application path differs from authorization")
    expected_bytes = int(settings["application_bytes"])
    if application["bytes"] != expected_bytes:
        raise RuntimeError("capture application byte count differs from authorization")
    if application["sha256"] != settings["application_sha256"]:
        raise RuntimeError("capture application digest differs from authorization")

    application_path = root / settings["application_path"]
    if not application_path.is_file():
        raise RuntimeError("sealed application is missing")
    if application_path.stat().st_size != expected_bytes:
        raise RuntimeError("sealed application byte count changed")
    if _sha256(application_path) != settings["application_sha256"]:
        raise RuntimeError("sealed application digest changed")
    if stat.S_IMODE(application_path.stat().st_mode) != 0o400:
        raise RuntimeError("sealed application mode is not 0400")
    return settings


def _normalized_lines(payload: bytes) -> list[bytes]:
    return ANSI_COLOR_RE.sub(b"", payload).splitlines(keepends=True)


def _marker_lines(payload: bytes) -> list[bytes]:
    lines = []
    for line in _normalized_lines(payload):
        position = line.find(MARKER)
        if position >= 0:
            lines.append(line[position:])
    return lines


def _parse_marker(line: bytes) -> dict[str, int] | None:
    if not line.endswith(b"\n"):
        return None
    body = line.rstrip(b"\r\n")
    match = _LINE_RE.fullmatch(body)
    if match is None:
        return None
    values = [int(value) for value in match.groups()]
    result: dict[str, int] = {}
    for name, value in zip(_FIELD_NAMES, values, strict=True):
        result[name] = value
    return result


def _allow_initial_boot() -> bool:
    return os.environ.get("P4_CAPTURE_ALLOW_INITIAL_BOOT", "0") == "1"


def analyze(payload: bytes) -> dict[str, Any]:
    normalized = ANSI_COLOR_RE.sub(b"", payload)
    lines = normalized.splitlines(keepends=True)
    marker_lines = _marker_lines(normalized)
    ready_positions = [index for index, line in enumerate(lines) if READY in line]
    ready_position = ready_positions[0] if ready_positions else None
    marker_positions = [
        index for index, line in enumerate(lines) if MARKER in line
    ]
    boot_allowed = _allow_initial_boot()
    start_positions = [index for index, line in enumerate(lines) if START in line]
    reset_positions = [
        (index, any(reason in line for reason in POWERON_REASONS))
        for index, line in enumerate(lines)
        if b"rst:" in line
    ]
    rejected_tokens = list(REJECT)
    if boot_allowed:
        rejected_tokens = [token for token in rejected_tokens if token not in (START, b"rst:")]
    rejected = [token.decode("ascii", "replace") for token in rejected_tokens if token in normalized]
    boot_rejected: list[str] = []
    if len(marker_positions) != 1 or ready_position is None or marker_positions[0] <= ready_position:
        boot_rejected.append("diagnostic marker must occur after READY")
    if not boot_allowed and (start_positions or reset_positions):
        boot_rejected.append("initial boot markers are not allowed")
    if boot_allowed:
        pre_ready_starts = [position for position in start_positions if ready_position is not None and position < ready_position]
        post_ready_starts = [position for position in start_positions if ready_position is not None and position > ready_position]
        pre_ready_resets = [event for event in reset_positions if ready_position is not None and event[0] < ready_position]
        post_ready_resets = [event for event in reset_positions if ready_position is not None and event[0] > ready_position]
        if len(pre_ready_starts) > 1:
            boot_rejected.append("more than one initial Console OS START")
        if post_ready_starts:
            boot_rejected.append("Console OS START occurred after READY")
        if len(pre_ready_resets) > 1:
            boot_rejected.append("more than one initial reset")
        if any(not poweron for _, poweron in pre_ready_resets):
            boot_rejected.append("initial reset is not POWERON_RESET")
        if post_ready_resets:
            boot_rejected.append("reset occurred after READY")
    rejected.extend(boot_rejected)
    parsed = _parse_marker(marker_lines[0]) if len(marker_lines) == 1 else None
    passed = len(marker_lines) == 1 and parsed is not None and not rejected
    result: dict[str, Any] = {
        "schema": 1,
        "result": "pass" if passed else "fail",
        "classification": "waveshare-console-os-scroll-gdma-boundary",
        "marker": MARKER.decode("ascii"),
        "marker_count": len(marker_lines),
        "marker_complete": parsed is not None,
        "rejected_markers": rejected,
        "ready_seen": ready_position is not None,
        "initial_boot_allowed": boot_allowed,
        "raw_bytes": len(payload),
        "raw_sha256": hashlib.sha256(payload).hexdigest(),
    }
    if parsed is not None:
        result["fields"] = parsed
        result["line"] = marker_lines[0].decode("ascii", "replace").rstrip("\r\n")
    return result


class CaptureFailure(RuntimeError):
    def __init__(self, reason: str, partial_raw: bytes) -> None:
        self.reason = reason[:512]
        self.partial_raw = bytes(partial_raw[:MAX_TRANSCRIPT_BYTES])
        super().__init__(self.reason)


def capture(port: str, seconds: float) -> bytes:
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
    marker_seen_at: float | None = None
    descriptor = None
    try:
        device.open()
        if not device.is_open or device.exclusive is not True:
            raise RuntimeError("retained UART is not open and exclusive")
        if bool(device.dtr) or bool(device.rts):
            raise RuntimeError("retained UART reset controls are active")
        # Bytes produced by the preceding retained-UART startup gate can remain
        # queued by the host driver after that reader closes. Drop only this
        # host-side backlog before arming; the capture sends no device data and
        # any reboot after this point is rejected by START/reset markers.
        device.reset_input_buffer()
        descriptor = device.fileno()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if (
                not device.is_open
                or device.fileno() != descriptor
                or device.exclusive is not True
            ):
                raise RuntimeError("retained UART descriptor changed")
            if bool(device.dtr) or bool(device.rts):
                raise RuntimeError("retained UART reset controls changed")
            chunk = device.read(4096)
            if chunk:
                available = MAX_TRANSCRIPT_BYTES - len(raw)
                if len(chunk) > available:
                    raw.extend(chunk[:available])
                    raise RuntimeError("serial transcript exceeded 1 MiB")
                raw.extend(chunk)
                markers = _marker_lines(bytes(raw))
                if len(markers) > 1:
                    raise RuntimeError("more than one scroll-boundary marker")
                if markers and marker_seen_at is None:
                    marker_seen_at = time.monotonic()
            if marker_seen_at is not None and time.monotonic() - marker_seen_at >= DRAIN_SECONDS:
                break
    except BaseException as error:
        if isinstance(error, CaptureFailure):
            raise
        raise CaptureFailure(f"{type(error).__name__}: {error}", bytes(raw)) from error
    finally:
        if device.is_open:
            device.close()
    return bytes(raw)


def _write_new(path: pathlib.Path, data: bytes) -> None:
    if path.exists() or path.is_symlink():
        raise RuntimeError(f"refusing to replace existing output: {path}")
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--raw", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--seconds", type=float, default=90.0)
    args = parser.parse_args()
    if not 20.0 <= args.seconds <= 120.0:
        parser.error("--seconds must be in [20, 120]")
    if args.raw.resolve() == args.summary.resolve():
        parser.error("--raw and --summary must be different paths")

    started = datetime.now(timezone.utc).isoformat()
    raw = b""
    result: dict[str, Any]
    try:
        settings = validate_exact_artifact()
        raw = capture(args.port, args.seconds)
        result = analyze(raw)
        result.update({
            "version": settings["version"],
            "application_path": settings["application_path"],
            "application_bytes": int(settings["application_bytes"]),
            "application_sha256": settings["application_sha256"],
            "authorization_path": settings["authorization_path"],
            "authorization_sha256": settings["authorization_sha256"],
        })
    except CaptureFailure as error:
        raw = error.partial_raw
        result = analyze(raw)
        result["capture_error"] = error.reason
    except BaseException as error:
        result = analyze(raw)
        result["capture_error"] = f"{type(error).__name__}: {error}"[:512]
    result["timestamp_utc"] = started
    result["port"] = args.port
    _write_new(args.raw, raw)
    _write_new(args.summary, (json.dumps(result, indent=2, sort_keys=True) + "\n").encode())
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
