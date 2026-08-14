#!/usr/bin/env python3

"""Bounded, receive-only E5 Doom runtime capture with reset lines inactive.

The standalone transport never launches or resets the target.  A capture made
after a separate deferred launch may therefore miss early boot markers.  The
summary keeps that distinction explicit: ``--startup optional`` proves only
the sustained runtime counters, while ``--startup required`` additionally
requires the exact reviewed E5 initialization transcript and ordering.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import termios
import time
from typing import Any


UINT32_MAX = (1 << 32) - 1
MAX_TRANSCRIPT_BYTES = 1024 * 1024
ANSI_COLOR_RE = re.compile(rb"\x1b\[[0-9;]*m")

START = (
    b"P4_DOOM_E5 START input=gt911-multitouch "
    b"sound=disabled-backend-linked-build-only usb=absent runtime=guarded"
)
MODE_TOUCH_ONLY = (
    b"P4_DOOM_E5 MODE composite_gate=1 touch_gate=1 audio_gate=0 "
    b"mode=touch-only"
)
SOUND_DISABLED = (
    b"P4_DOOM_E5 SOUND_DISABLED audio_gate=0 audio_calls=0 "
    b"gpio30=untouched hardware_pullup=R71"
)
CLEANUP_REGISTERED = (
    b"P4_DOOM_E5 CLEANUP_REGISTERED "
    b"order=audio-touch-bus-video-display"
)
SHARED_I2C_READY = (
    b"P4_DOOM_E5 SHARED_I2C_READY port=1 sda=45 scl=46 "
    b"hz=100000 owner=app borrowers=touch-only"
)
TOUCH_READY = (
    b"P4_DOOM_E5 TOUCH_READY primary=0x5d fallback=0x14 "
    b"resolution=1024x600 contacts=5 poll_ms=16 i2c_device_hz=400000 "
    b"gpio40=reset-active-low gpio42=address-latch-input-active-low "
    b"interrupt_callback=none"
)
WAD_VERIFIED = (
    b"P4_DOOM_E5 WAD_VERIFIED identity=doom-shareware-1.9 bytes=4196020 "
    b"sha256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
)
VFS_READY = (
    b"P4_DOOM_E5 VFS_READY path=/doom/doom1.wad "
    b"mode=read-only max_open=8"
)
ENGINE_START_RE = re.compile(
    rb"P4_DOOM_E5 ENGINE_START wad=/doom/doom1\.wad "
    rb"touch=(ready|degraded) overlay=visible sfx=(enabled|disabled) "
    rb"music=disabled usb=absent"
)
VIDEO_READY = (
    b"P4_DOOM_E5 VIDEO_READY input=0x00RRGGBB overlay=touch "
    b"output=rgb565 source=320x200 viewport=960x600 margins=32/32"
)
TOUCH_DEGRADED_RE = re.compile(
    rb"P4_DOOM_E5 TOUCH_DEGRADED stage=([^\s]+) error=([^\s]+) "
    rb"neutral=1 overlay=visible(?: cleanup_proven=([01]) bus_owned=([01]))?"
)
STATS_RE = re.compile(
    rb"P4_DOOM_E5 STATS frames=(\d+) submits=(\d+) "
    rb"completions=(\d+) video_timeouts=(\d+) video_failures=(\d+) "
    rb"touch_polls=(\d+) touch_failures=(\d+) touch_retries=(\d+) "
    rb"composite_gate=(\d+) touch_gate=(\d+) audio_gate=(\d+) "
    rb"audio_calls=(\d+) gpio30=([a-z-]+) "
    rb"audio_enabled=([01]) audio_frames=(\d+) "
    rb"audio_write_failures=(\d+)"
)

FIXED_MARKERS = {
    "start": START,
    "mode_touch_only": MODE_TOUCH_ONLY,
    "sound_disabled": SOUND_DISABLED,
    "cleanup_registered": CLEANUP_REGISTERED,
    "shared_i2c_ready": SHARED_I2C_READY,
    "touch_ready": TOUCH_READY,
    "wad_verified": WAD_VERIFIED,
    "vfs_ready": VFS_READY,
    "video_ready": VIDEO_READY,
}

PREFIXES = {
    "start": b"P4_DOOM_E5 START",
    "mode_touch_only": b"P4_DOOM_E5 MODE",
    "sound_disabled": b"P4_DOOM_E5 SOUND_DISABLED",
    "cleanup_registered": b"P4_DOOM_E5 CLEANUP_REGISTERED",
    "shared_i2c_ready": b"P4_DOOM_E5 SHARED_I2C_READY",
    "touch_ready": b"P4_DOOM_E5 TOUCH_READY",
    "touch_degraded": b"P4_DOOM_E5 TOUCH_DEGRADED",
    "wad_verified": b"P4_DOOM_E5 WAD_VERIFIED",
    "vfs_ready": b"P4_DOOM_E5 VFS_READY",
    "engine_start": b"P4_DOOM_E5 ENGINE_START",
    "video_ready": b"P4_DOOM_E5 VIDEO_READY",
    "stats": b"P4_DOOM_E5 STATS",
}

REJECT_FIXED = (
    b"P4_DOOM_E5 BLOCKED",
    b"P4_DOOM_E5 HALT stage=",
    b"P4_DOOM_E5 AUDIO_SAFE",
    b"P4_DOOM_E5 SOUND_READY",
    b"P4_DOOM_E5 SOUND_DEGRADED",
    b"P4_DOOM_E5 CLEANUP audio_released=",
    b"Guru Meditation Error",
    b"panic'ed",
    b"abort() was called",
    b"assert failed:",
    b"Task watchdog got triggered",
    b"Interrupt wdt timeout",
    b"Brownout detector was triggered",
    b"P4_DOOM_GAMEPAD",
    b"GAMEPAD_D1",
    b"P4_USB",
    b"USB_HOST",
    b"native_usb",
    b"platform_usb",
    b"usb=present",
    b"usb=enabled",
    b"usb=ready",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--raw", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--seconds", type=float, default=45.0)
    parser.add_argument("--min-stats", type=int, default=2)
    parser.add_argument(
        "--startup",
        choices=("optional", "required"),
        default="optional",
        help=(
            "require the complete exact boot transcript, or accept a truthful "
            "late-attach sustained-runtime proof"
        ),
    )
    args = parser.parse_args()
    if not 1.0 <= args.seconds <= 180.0:
        parser.error("--seconds must be in [1, 180]")
    if not 2 <= args.min_stats <= 20:
        parser.error("--min-stats must be in [2, 20]")
    if args.raw.resolve() == args.summary.resolve():
        parser.error("capture outputs must be distinct")
    if args.raw.exists() or args.summary.exists():
        parser.error("capture outputs must not already exist")
    return args


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def line_with_prefix_is_exact(
    payload: bytes, prefix: bytes, exact: bytes | re.Pattern[bytes]
) -> bool:
    matching = [line for line in payload.splitlines() if prefix in line]
    if not matching:
        return True
    if isinstance(exact, bytes):
        return all(line[line.index(prefix) :] == exact for line in matching)
    return all(
        exact.fullmatch(line[line.index(prefix) :]) is not None
        for line in matching
    )


def marker_position(payload: bytes, marker: bytes | re.Pattern[bytes]) -> int:
    if isinstance(marker, bytes):
        return payload.find(marker)
    match = marker.search(payload)
    return -1 if match is None else match.start()


def parse_stats(payload: bytes) -> list[dict[str, int | str]]:
    names = (
        "frames",
        "submits",
        "completions",
        "video_timeouts",
        "video_failures",
        "touch_polls",
        "touch_failures",
        "touch_retries",
        "composite_gate",
        "touch_gate",
        "audio_gate",
        "audio_calls",
        "gpio30",
        "audio_enabled",
        "audio_frames",
        "audio_write_failures",
    )
    records = []
    for match in STATS_RE.finditer(payload):
        record: dict[str, int | str] = {}
        for name, value in zip(names, match.groups()):
            record[name] = (
                value.decode("ascii", "strict")
                if name == "gpio30"
                else int(value)
            )
        records.append(record)
    return records


def analyze(payload: bytes, min_stats: int, startup: str) -> dict[str, Any]:
    if startup not in {"optional", "required"}:
        raise ValueError("startup must be optional or required")
    if not 2 <= min_stats <= 20:
        raise ValueError("min_stats must be in [2, 20]")

    payload = ANSI_COLOR_RE.sub(b"", payload)
    lower_payload = payload.lower()
    reject_markers = [
        marker.decode("ascii", "replace")
        for marker in REJECT_FIXED
        if marker.lower() in lower_payload
    ]

    fixed_seen = {
        name: payload.count(marker) for name, marker in FIXED_MARKERS.items()
    }
    engine_matches = list(ENGINE_START_RE.finditer(payload))
    touch_degraded_matches = list(TOUCH_DEGRADED_RE.finditer(payload))
    marker_counts = {
        **fixed_seen,
        "touch_degraded": len(touch_degraded_matches),
        "engine_start": len(engine_matches),
    }

    malformed = []
    for name, marker in FIXED_MARKERS.items():
        if not line_with_prefix_is_exact(payload, PREFIXES[name], marker):
            malformed.append(name)
    for name, pattern in (
        ("touch_degraded", TOUCH_DEGRADED_RE),
        ("engine_start", ENGINE_START_RE),
        ("stats", STATS_RE),
    ):
        if not line_with_prefix_is_exact(payload, PREFIXES[name], pattern):
            malformed.append(name)

    startup_any = any(
        PREFIXES[name] in payload
        for name in (
            "start",
            "mode_touch_only",
            "sound_disabled",
            "cleanup_registered",
            "shared_i2c_ready",
            "touch_ready",
            "touch_degraded",
            "wad_verified",
            "vfs_ready",
            "engine_start",
            "video_ready",
        )
    )
    touch_startup_seen = fixed_seen["touch_ready"] > 0 or bool(
        touch_degraded_matches
    )
    startup_complete = all(
        fixed_seen[name] > 0
        for name in (
            "start",
            "mode_touch_only",
            "sound_disabled",
            "cleanup_registered",
            "shared_i2c_ready",
            "wad_verified",
            "vfs_ready",
            "video_ready",
        )
    ) and touch_startup_seen and bool(engine_matches)

    positions = {
        name: marker_position(payload, marker)
        for name, marker in FIXED_MARKERS.items()
    }
    positions["touch_degraded"] = marker_position(payload, TOUCH_DEGRADED_RE)
    positions["engine_start"] = marker_position(payload, ENGINE_START_RE)
    touch_position_candidates = [
        position
        for position in (
            positions["touch_ready"],
            positions["touch_degraded"],
        )
        if position >= 0
    ]
    touch_position = min(touch_position_candidates, default=-1)
    startup_sequence_valid = startup_complete and (
        positions["start"]
        < positions["mode_touch_only"]
        < positions["sound_disabled"]
        < positions["cleanup_registered"]
        < positions["shared_i2c_ready"]
        < touch_position
        < positions["wad_verified"]
        < positions["vfs_ready"]
        < positions["engine_start"]
        < positions["video_ready"]
    ) and fixed_seen["sound_disabled"] >= 2 and (
        payload.rfind(SOUND_DISABLED)
        > positions["vfs_ready"]
        and payload.rfind(SOUND_DISABLED) < positions["engine_start"]
    )
    startup_exact = startup_complete and startup_sequence_valid and not malformed

    rom_reset_positions = [
        match.start() for match in re.finditer(rb"ESP-ROM:", payload, re.I)
    ]
    rst_positions = [
        match.start() for match in re.finditer(rb"(?:^|[\r\n])rst:", payload, re.I)
    ]
    start_position = positions["start"]
    reset_marker_positions = rom_reset_positions + rst_positions
    reset_loop_detected = (
        len(rom_reset_positions) > 1
        or len(rst_positions) > 1
        or any(
            start_position < position
            for position in reset_marker_positions
            if start_position >= 0
        )
        or (bool(reset_marker_positions) and start_position < 0)
    )
    if reset_loop_detected:
        reject_markers.append("reset-loop-or-unpaired-reset-marker")

    stats = parse_stats(payload)
    stats_values_bounded = all(
        all(
            not isinstance(value, int) or 0 <= value <= UINT32_MAX
            for value in record.values()
        )
        for record in stats
    )
    periodic_runtime_contract_exact = bool(stats) and all(
        record["composite_gate"] == 1
        and record["touch_gate"] == 1
        and record["audio_gate"] == 0
        and record["audio_calls"] == 0
        and record["gpio30"] == "untouched"
        for record in stats
    )
    stats_individually_valid = all(
        record["frames"] > 0
        and record["frames"] % 300 == 0
        and record["submits"] == record["frames"] + 1
        and record["completions"] == record["submits"]
        and record["video_timeouts"] == 0
        and record["video_failures"] == 0
        and record["touch_polls"] > record["touch_failures"]
        and record["touch_retries"] >= 1
        and record["composite_gate"] == 1
        and record["touch_gate"] == 1
        and record["audio_gate"] == 0
        and record["audio_calls"] == 0
        and record["gpio30"] == "untouched"
        and record["audio_enabled"] == 0
        and record["audio_frames"] == 0
        and record["audio_write_failures"] == 0
        for record in stats
    )
    stats_monotonic = all(
        later["frames"] > earlier["frames"]
        and later["submits"] > earlier["submits"]
        and later["completions"] > earlier["completions"]
        and later["touch_polls"] > earlier["touch_polls"]
        and (
            later["touch_polls"] - later["touch_failures"]
            > earlier["touch_polls"] - earlier["touch_failures"]
        )
        and later["touch_failures"] >= earlier["touch_failures"]
        and later["touch_retries"] >= earlier["touch_retries"]
        and later["audio_frames"] == earlier["audio_frames"] == 0
        for earlier, later in zip(stats, stats[1:])
    )
    runtime_valid = (
        len(stats) >= min_stats
        and stats_values_bounded
        and stats_individually_valid
        and stats_monotonic
        and "stats" not in malformed
    )

    engine_sfx_valid = not engine_matches or all(
        match.group(2) == b"disabled" for match in engine_matches
    )
    if not engine_sfx_valid:
        reject_markers.append("P4_DOOM_E5 ENGINE_START sfx!=disabled")

    startup_requirement_met = startup != "required" or startup_exact
    passed = (
        runtime_valid
        and startup_requirement_met
        and not malformed
        and not reject_markers
        and engine_sfx_valid
    )

    if startup_exact:
        startup_status = "complete-exact"
    elif startup_any:
        startup_status = "partial-or-invalid"
    else:
        startup_status = "not-sampled-late-attach"

    touch_ready_observed = fixed_seen["touch_ready"] > 0
    touch_degraded_observed = bool(touch_degraded_matches)
    if touch_ready_observed and touch_degraded_observed:
        touch_runtime_status = "ready-with-recorded-degradation"
    elif touch_ready_observed:
        touch_runtime_status = "ready-marker-and-successful-polls"
    elif runtime_valid:
        touch_runtime_status = "successful-polls-late-attach-marker-not-sampled"
    else:
        touch_runtime_status = "not-proven"

    return {
        "startup_policy": startup,
        "startup_status": startup_status,
        "startup_complete": startup_complete,
        "startup_sequence_valid": startup_sequence_valid,
        "startup_exact": startup_exact,
        "marker_counts": marker_counts,
        "touch_degraded": [
            {
                "stage": match.group(1).decode("ascii", "replace"),
                "error": match.group(2).decode("ascii", "replace"),
                "cleanup_proven": (
                    None if match.group(3) is None else int(match.group(3))
                ),
                "bus_owned": (
                    None if match.group(4) is None else int(match.group(4))
                ),
            }
            for match in touch_degraded_matches
        ],
        "malformed_markers": malformed,
        "rom_banner_count": len(rom_reset_positions),
        "rst_marker_count": len(rst_positions),
        "reset_loop_detected": reset_loop_detected,
        "stats_count": len(stats),
        "stats": stats,
        "stats_values_bounded": stats_values_bounded,
        "periodic_runtime_contract_exact": periodic_runtime_contract_exact,
        "stats_individually_valid": stats_individually_valid,
        "stats_monotonic": stats_monotonic,
        "runtime_valid": runtime_valid,
        "touch_runtime_status": touch_runtime_status,
        "runtime_gate_evidence": (
            "every accepted periodic record reports composite=1 touch=1 "
            "audio=0 from retained runtime gates and audio_calls=0 from "
            "the counted API path"
            if runtime_valid
            else "not proven"
        ),
        "touch_initialization_evidence": (
            "increasing successful GT911 poll frames; no contact assertion"
            if runtime_valid
            else "not proven"
        ),
        "contacts_observed": None,
        "contact_claim": "not tested by receive-only runtime capture",
        "audio_runtime_evidence": (
            "touch-only gate held audio_enabled=0, audio_frames=0, "
            "audio_write_failures=0"
            if runtime_valid
            else "not proven"
        ),
        "sound_runtime_status": "disabled-by-reviewed-touch-only-gate",
        "sound_backend_status": "build-tested-only; no hardware claim",
        "gpio30_runtime_evidence": (
            "every accepted periodic record reports gpio30=untouched"
            if runtime_valid
            else "not proven"
        ),
        "audible_acoustic_output": "not enabled in this runtime",
        "usb_runtime_markers_seen": any(
            marker in reject_markers
            for marker in (
                "P4_DOOM_GAMEPAD",
                "GAMEPAD_D1",
                "P4_USB",
                "USB_HOST",
                "native_usb",
                "platform_usb",
                "usb=present",
                "usb=enabled",
                "usb=ready",
            )
        ),
        "reject_markers": reject_markers,
        "classification": (
            "runtime-pass-startup-observed"
            if passed and startup_exact
            else "runtime-pass-late-attach"
            if passed
            else "runtime-fail"
        ),
        "result": "pass" if passed else "fail",
    }


def capture_receive_only(port: str, seconds: float) -> tuple[bytes, int, int]:
    try:
        import serial
    except ImportError as error:
        raise SystemExit("activate the pinned ESP-IDF environment first") from error

    device = serial.Serial(
        port=None,
        baudrate=115200,
        timeout=0.05,
        write_timeout=0.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
        exclusive=True,
    )
    device.port = port
    device.dtr = False
    device.rts = False
    opened_ns = time.time_ns()
    raw = bytearray()
    try:
        device.open()
        attributes = termios.tcgetattr(device.fileno())
        attributes[2] &= ~getattr(termios, "HUPCL", 0)
        termios.tcsetattr(device.fileno(), termios.TCSANOW, attributes)
        verify = termios.tcgetattr(device.fileno())
        if (
            (getattr(termios, "HUPCL", 0) and verify[2] & termios.HUPCL)
            or not device.exclusive
            or device.dtr
            or device.rts
        ):
            raise RuntimeError(
                "exclusive/DTR/RTS/HUPCL receive-only contract failed"
            )
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            chunk = device.read(4096)
            if chunk:
                raw.extend(chunk)
                if len(raw) > MAX_TRANSCRIPT_BYTES:
                    raise RuntimeError("serial transcript exceeded 1 MiB bound")
    finally:
        device.close()
    return bytes(raw), opened_ns, time.time_ns()


def main() -> int:
    args = parse_args()
    payload, opened_ns, closed_ns = capture_receive_only(args.port, args.seconds)
    analysis = analyze(payload, args.min_stats, args.startup)
    summary = {
        "schema": 1,
        "capture": "doom-embedded-touch-audio-e5-receive-only-no-reset",
        "serial_port": args.port,
        "baud": 115200,
        "serial_exclusive": True,
        "dtr": False,
        "rts": False,
        "hupcl": False,
        "transmitted_bytes": 0,
        "launch_performed": False,
        "reset_performed": False,
        "opened_wall_ns": opened_ns,
        "closed_wall_ns": closed_ns,
        "duration_seconds": (closed_ns - opened_ns) / 1_000_000_000.0,
        "raw_bytes": len(payload),
        "raw_sha256": sha256(payload),
        **analysis,
    }
    args.raw.parent.mkdir(parents=True, exist_ok=True)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    with args.raw.open("xb") as raw_file:
        raw_file.write(payload)
    with args.summary.open("x", encoding="utf-8") as summary_file:
        json.dump(summary, summary_file, indent=2)
        summary_file.write("\n")
    print(json.dumps(summary, separators=(",", ":")))
    return 0 if analysis["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
