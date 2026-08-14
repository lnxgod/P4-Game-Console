#!/usr/bin/env python3

"""Strict retained-UART startup analyzer for the P4 Console OS launcher."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import time
from typing import Any


MAX_TRANSCRIPT_BYTES = 1024 * 1024
ANSI_COLOR_RE = re.compile(rb"\x1b\[[0-9;]*m")

START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=5 "
    b"surface=rgb565-320x200 touch=gt911 audio=safe-handoff-only "
    b"execution=guarded-exact-unit-candidate"
)
DISPLAY_START = b"P4_DISPLAY M1 START profile=elecrow-10.1-ek79007"
DISPLAY_SCOPE = (
    b"P4_DISPLAY M1 SCOPE display-only gpio29=untouched gpio41=untouched"
)
DISPLAY_DARK = b"P4_DISPLAY M1 BACKLIGHT_DARK gpio=31 pwm_hz=30000"
DISPLAY_POWER = b"P4_DISPLAY M1 POWER_READY ldo3_mv=2500 ldo4_mv=3300"
DISPLAY_READY = (
    b"P4_DISPLAY M1 PANEL_READY resolution=1024x600 format=rgb565 "
    b"lane_mbps=900 dpi_mhz=51"
)
TOUCH_READY = (
    b"P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=5"
)
CONSOLE_READY = (
    b"P4_CONSOLE_OS READY page=home amp_energized=0 "
    b"doom_audio=deferred-until-exclusive-handoff"
)

FIXED = (
    ("start", START),
    ("display_start", DISPLAY_START),
    ("display_scope", DISPLAY_SCOPE),
    ("display_dark", DISPLAY_DARK),
    ("display_power", DISPLAY_POWER),
    ("display_ready", DISPLAY_READY),
    ("touch_ready", TOUCH_READY),
    ("console_ready", CONSOLE_READY),
)

STATS_FIELDS = (
    "loops", "page", "renders", "touch_ready", "touch_polls",
    "touch_failures", "display_submits", "display_completions",
    "display_timeouts", "display_failures", "amp_energized",
    "doom_handoffs",
)
STATS_RE = re.compile(
    rb"P4_CONSOLE_OS STATS loops=(\d+) page=(\d+) renders=(\d+) "
    rb"touch_ready=(\d+) touch_polls=(\d+) touch_failures=(\d+) "
    rb"display_submits=(\d+) display_completions=(\d+) "
    rb"display_timeouts=(\d+) display_failures=(\d+) "
    rb"amp_energized=(\d+) doom_handoffs=(\d+)"
)

REJECT = (
    b"P4_CONSOLE_OS HALT",
    b"P4_CONSOLE_OS TOUCH_DEGRADED",
    b"P4_CONSOLE_OS TOUCH_POLL_FAIL",
    b"P4_CONSOLE_OS STATS_UNAVAILABLE",
    b"P4_CONSOLE_OS HANDOFF_BEGIN",
    b"P4_DOOM_E6",
    b"P4_AUDIO",
    b"AUDIO_SAFETY_FAULT",
    b"gpio30=low",
    b"Guru Meditation Error",
    b"panic'ed",
    b"abort() was called",
    b"assert failed:",
    b"Task watchdog got triggered",
    b"Interrupt wdt timeout",
    b"Brownout detector was triggered",
    b"P4_USB",
    b"USB_HOST",
)


def _marker_suffix(line: bytes, prefix: bytes) -> bytes | None:
    offset = line.find(prefix)
    return None if offset < 0 else line[offset:]


def parse_stats(payload: bytes) -> list[dict[str, int]]:
    records: list[dict[str, int]] = []
    for line in ANSI_COLOR_RE.sub(b"", payload).splitlines():
        marker = _marker_suffix(line, b"P4_CONSOLE_OS STATS")
        if marker is None:
            continue
        match = STATS_RE.fullmatch(marker)
        if match is None:
            continue
        records.append({
            name: int(value)
            for name, value in zip(STATS_FIELDS, match.groups(), strict=True)
        })
    return records


def _record_valid(record: dict[str, int]) -> bool:
    return (
        record["loops"] > 0
        and record["loops"] % 300 == 0
        and record["page"] == 0
        and record["renders"] >= 1
        and record["touch_ready"] == 1
        and record["touch_polls"] > record["touch_failures"]
        and record["touch_failures"] == 0
        and record["display_submits"] >= 1
        and record["display_completions"] == record["display_submits"]
        and record["display_timeouts"] == 0
        and record["display_failures"] == 0
        and record["amp_energized"] == 0
        and record["doom_handoffs"] == 0
    )


def analyze(payload: bytes, min_stats: int = 2) -> dict[str, Any]:
    if not isinstance(payload, bytes) or not 1 <= min_stats <= 20:
        raise ValueError("invalid analyzer input")
    normalized = ANSI_COLOR_RE.sub(b"", payload)
    marker_positions: dict[str, int] = {}
    for name, marker in FIXED:
        marker_positions[name] = normalized.find(marker)
    fixed_present = all(position >= 0 for position in marker_positions.values())
    fixed_ordered = (
        fixed_present
        and list(marker_positions.values()) == sorted(marker_positions.values())
        and normalized.find(b"P4_CONSOLE_OS STATS")
            > marker_positions["console_ready"]
    )
    rejected = [token.decode("ascii", "replace") for token in REJECT
                if token in normalized]
    records = parse_stats(normalized)
    selected = records[-min_stats:] if len(records) >= min_stats else records
    records_valid = len(selected) >= min_stats and all(
        _record_valid(record) for record in selected
    )
    monotonic = len(selected) >= min_stats and all(
        newer["loops"] > older["loops"]
        and newer["touch_polls"] > older["touch_polls"]
        and newer["renders"] >= older["renders"]
        and newer["display_submits"] >= older["display_submits"]
        and newer["display_completions"] >= older["display_completions"]
        for older, newer in zip(selected, selected[1:])
    )
    passed = fixed_ordered and not rejected and records_valid and monotonic
    return {
        "schema": 1,
        "result": "pass" if passed else "fail",
        "classification": "console-os-retained-uart-startup",
        "fixed_markers_present": fixed_present,
        "fixed_markers_ordered": fixed_ordered,
        "marker_positions": marker_positions,
        "rejected_markers": rejected,
        "stats_required": min_stats,
        "stats_observed": len(records),
        "stats_selected": selected,
        "stats_valid": records_valid,
        "stats_monotonic": monotonic,
        "amp_energized": False if records_valid else None,
        "doom_handoff_observed": False if records_valid else None,
        "raw_bytes": len(payload),
        "raw_sha256": hashlib.sha256(payload).hexdigest(),
    }


class CaptureFailure(RuntimeError):
    """A retained-UART failure carrying every bounded byte read so far."""

    def __init__(self, reason: str, partial_raw: bytes, min_stats: int) -> None:
        self.reason = reason[:512]
        self.partial_raw = bytes(partial_raw[:MAX_TRANSCRIPT_BYTES])
        self.failure_summary = analyze(self.partial_raw, min_stats)
        self.failure_summary.update({
            "result": "fail",
            "capture_incomplete": True,
            "capture_error_reason": self.reason,
        })
        super().__init__(self.reason)


def capture_open_handle(
    device: Any, seconds: float, min_stats: int = 2,
) -> tuple[bytes, dict[str, Any]]:
    """Read startup on the retained exclusive descriptor without side effects."""

    if not 5.0 <= seconds <= 120.0:
        raise ValueError("seconds must be in [5, 120]")
    raw = bytearray()
    try:
        if (
            not getattr(device, "is_open", False)
            or getattr(device, "exclusive", None) is not True
        ):
            raise RuntimeError("retained UART is not open and exclusive")
        if bool(device.dtr) or bool(device.rts):
            raise RuntimeError("retained UART reset controls are active")
        descriptor = device.fileno()
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if (
                device.fileno() != descriptor
                or not device.is_open
                or not device.exclusive
            ):
                raise RuntimeError("retained UART descriptor changed during capture")
            if bool(device.dtr) or bool(device.rts):
                raise RuntimeError("reset controls changed during capture")
            chunk = device.read(4096)
            if not chunk:
                continue
            available = MAX_TRANSCRIPT_BYTES - len(raw)
            raw.extend(chunk[:available])
            if len(chunk) > available:
                raise RuntimeError("serial transcript exceeded 1 MiB bound")
            provisional = analyze(bytes(raw), min_stats)
            if provisional["result"] == "pass":
                return bytes(raw), provisional
    except BaseException as error:
        if isinstance(error, CaptureFailure):
            raise
        reason = f"{type(error).__name__}: {error}"
        raise CaptureFailure(reason, bytes(raw), min_stats) from error
    payload = bytes(raw)
    return payload, analyze(payload, min_stats)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="analyze a Console OS retained-UART transcript"
    )
    parser.add_argument("--raw-input", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--min-stats", type=int, default=2)
    args = parser.parse_args()
    if args.summary.exists() or args.summary.is_symlink():
        parser.error("summary must not already exist")
    payload = args.raw_input.read_bytes()
    result = analyze(payload, args.min_stats)
    args.summary.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
