#!/usr/bin/env python3

"""Strict retained-UART startup analyzer for Game Manager Console OS."""

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

STORAGE_READY_RE = re.compile(
    rb"P4_CONSOLE_OS GAME_STORAGE state=app-ready usb_attached=0 "
    rb"generation=(\d+) capacity=(\d+) last_error=ESP_OK"
)
CATALOG_RE = re.compile(
    rb"P4_CONSOLE_OS GAME_CATALOG available=1 packages=(\d+) "
    rb"valid=(\d+) omitted=(\d+) generation=(\d+) update=(\d+) "
    rb"result=ESP_OK"
)
START_RE = re.compile(
    rb"P4_CONSOLE_OS START shell=freertos-native apps=(\d+) "
    rb"surface=rgb565-320x200 touch=gt911 native_game_api=1 "
    rb"native_format=p4-native-elf-v1 game_storage=app-ready "
    rb"execution=build-candidate"
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
TOUCH_READY = b"P4_CONSOLE_OS TOUCH_READY controller=gt911 contacts_max=5"
CONSOLE_READY = (
    b"P4_CONSOLE_OS READY page=home amp_energized=0 "
    b"doom_audio=deferred-until-exclusive-handoff"
)
OTA_VALID = b"P4_CONSOLE_OS OTA_BOOT_VALID result=ESP_OK"

FIXED = (
    ("display_start", DISPLAY_START),
    ("display_scope", DISPLAY_SCOPE),
    ("display_dark", DISPLAY_DARK),
    ("display_power", DISPLAY_POWER),
    ("display_ready", DISPLAY_READY),
    ("touch_ready", TOUCH_READY),
    ("console_ready", CONSOLE_READY),
    ("ota_valid", OTA_VALID),
)

STATS_FIELDS = (
    "loops", "page", "renders", "touch_ready", "touch_polls",
    "touch_failures", "display_submits", "display_completions",
    "display_timeouts", "display_failures", "doom_handoffs",
    "storage_generation", "usb_attached",
)
STATS_RE = re.compile(
    rb"P4_CONSOLE_OS STATS loops=(\d+) page=(\d+) renders=(\d+) "
    rb"touch_ready=(\d+) touch_polls=(\d+) touch_failures=(\d+) "
    rb"display_submits=(\d+) display_completions=(\d+) "
    rb"display_timeouts=(\d+) display_failures=(\d+) "
    rb"amp_energized=0 doom_handoffs=(\d+) storage=app-ready "
    rb"storage_generation=(\d+) usb_attached=(\d+)"
)

REJECT = (
    b"P4_CONSOLE_OS HALT",
    b"P4_CONSOLE_OS GAME_STORAGE_DEGRADED",
    b"P4_CONSOLE_OS TOUCH_DEGRADED",
    b"P4_CONSOLE_OS TOUCH_POLL_FAIL",
    b"P4_CONSOLE_OS STATS_UNAVAILABLE",
    b"P4_CONSOLE_OS HANDOFF_BEGIN",
    b"P4_CONSOLE_OS HANDOFF_REJECTED",
    b"state=format-required",
    b"state=app-missing",
    b"state=app-invalid",
    b"state=fault",
    b"state=usb-host",
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
        if match is not None:
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
        and record["doom_handoffs"] == 0
        and record["storage_generation"] >= 1
        and record["usb_attached"] == 0
    )


def analyze(payload: bytes, min_stats: int = 2) -> dict[str, Any]:
    if not isinstance(payload, bytes) or not 1 <= min_stats <= 20:
        raise ValueError("invalid analyzer input")
    normalized = ANSI_COLOR_RE.sub(b"", payload)
    storage_matches = list(STORAGE_READY_RE.finditer(normalized))
    catalog_matches = list(CATALOG_RE.finditer(normalized))
    start_matches = list(START_RE.finditer(normalized))
    storage_ready = bool(storage_matches) and all(
        int(match.group(1)) >= 1 and int(match.group(2)) >= 4_196_020
        for match in storage_matches
    )
    catalog_valid = False
    package_count = None
    valid_count = None
    app_count = None
    if catalog_matches and start_matches:
        catalog = catalog_matches[0]
        start = start_matches[0]
        package_count = int(catalog.group(1))
        valid_count = int(catalog.group(2))
        omitted_count = int(catalog.group(3))
        generation = int(catalog.group(4))
        update_state = int(catalog.group(5))
        app_count = int(start.group(1))
        catalog_valid = (
            0 <= valid_count <= package_count <= 16
            and omitted_count >= 0
            and generation >= 1
            and 0 <= update_state <= 3
            and app_count == 7 + valid_count
            and app_count <= 23
        )

    marker_positions = {
        name: normalized.find(marker) for name, marker in FIXED
    }
    storage_position = storage_matches[0].start() if storage_matches else -1
    catalog_position = catalog_matches[0].start() if catalog_matches else -1
    start_position = start_matches[0].start() if start_matches else -1
    ordered_positions = [
        storage_position, catalog_position, start_position,
        *marker_positions.values(),
    ]
    fixed_ordered = (
        storage_ready
        and catalog_valid
        and all(position >= 0 for position in ordered_positions)
        and ordered_positions == sorted(ordered_positions)
        and normalized.find(b"P4_CONSOLE_OS STATS")
            > marker_positions["ota_valid"]
    )
    rejected = [
        token.decode("ascii", "replace") for token in REJECT
        if token in normalized
    ]
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
        and newer["storage_generation"] >= older["storage_generation"]
        for older, newer in zip(selected, selected[1:])
    )
    passed = fixed_ordered and not rejected and records_valid and monotonic
    return {
        "schema": 1,
        "result": "pass" if passed else "fail",
        "classification": "console-os-game-manager-retained-uart-startup",
        "storage_ready": storage_ready,
        "catalog_valid": catalog_valid,
        "package_count": package_count,
        "valid_package_count": valid_count,
        "registered_apps": app_count,
        "marker_positions": {
            "storage_ready": storage_position,
            "catalog": catalog_position,
            "start": start_position,
            **marker_positions,
        },
        "fixed_markers_ordered": fixed_ordered,
        "rejected_markers": rejected,
        "stats_required": min_stats,
        "stats_observed": len(records),
        "stats_selected": selected,
        "stats_valid": records_valid,
        "stats_monotonic": monotonic,
        "amp_energized": False if records_valid else None,
        "doom_handoff_observed": False if records_valid else None,
        "usb_attached": False if records_valid else None,
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
        raise CaptureFailure(
            f"{type(error).__name__}: {error}", bytes(raw), min_stats
        ) from error
    payload = bytes(raw)
    return payload, analyze(payload, min_stats)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="analyze a Game Manager Console OS UART transcript"
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
