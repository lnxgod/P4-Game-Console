#!/usr/bin/env python3

"""Strict retained-UART startup analyzer for the E6 factory-audio Doom image.

The authorized installer imports :func:`capture_open_handle` after its one
application-launch reset.  That function only reads the already-open,
exclusive descriptor: it never reopens the port, transmits, flushes, or
changes reset controls.  Acceptance requires the complete startup sequence
and sustained, non-zero mixer/backend telemetry.  Acoustic evidence is a
separate record and is never inferred from these electrical/software counters.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import time
from typing import Any


UINT32_MAX = (1 << 32) - 1
MAX_TRANSCRIPT_BYTES = 1024 * 1024
ANSI_COLOR_RE = re.compile(rb"\x1b\[[0-9;]*m")

START = (
    b"P4_DOOM_E6 START input=gt911-multitouch "
    b"sound=factory-complete-i2s0-pdm-rx-i2s1-speaker-tx-sfx-mus "
    b"pdm_clk_gpio24_may_feed_codec_mclk=1 codec_i2c_transactions=0 "
    b"tx_mclk=none music=wad-mus-procedural-16voice usb=absent "
    b"runtime=exact-unit-factory-audio"
)
MODE = (
    b"P4_DOOM_E6 MODE composite_gate=1 touch_gate=1 audio_gate=1 "
    b"mode=touch-and-audio"
)
AUDIO_SAFE = (
    b"P4_DOOM_E6 AUDIO_SAFE gpio30=high-pad-readback-proven "
    b"source=platform-first-call audio_calls=1"
)
CLEANUP_REGISTERED = (
    b"P4_DOOM_E6 CLEANUP_REGISTERED "
    b"order=audio-touch-bus-dark-video-display-blob"
)
SHARED_I2C_READY = (
    b"P4_DOOM_E6 SHARED_I2C_READY port=1 sda=45 scl=46 "
    b"hz=100000 owner=app borrowers=touch-only"
)
TOUCH_READY = (
    b"P4_DOOM_E6 TOUCH_READY primary=0x5d fallback=0x14 "
    b"resolution=1024x600 contacts=5 poll_ms=16 i2c_device_hz=400000 "
    b"gpio40=reset-active-low gpio42=address-latch-input-active-low "
    b"interrupt_callback=none"
)
WAD_VERIFIED = (
    b"P4_DOOM_E6 WAD_VERIFIED identity=doom-shareware-1.9 bytes=4196020 "
    b"sha256=1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
)
VFS_READY = (
    b"P4_DOOM_E6 VFS_READY path=/doom/doom1.wad mode=read-only max_open=8"
)
SOUND_BOUND_RE = re.compile(
    rb"P4_DOOM_E6 SOUND_BOUND backend=factory-complete-audio-init "
    rb"state=ready-muted gpio30=high-pad-readback-proven "
    rb"pdm_i2s_port=0 pdm_clk_gpio=24 pdm_clk_hz=1024000 "
    rb"pdm_din_gpio=26 gpio24_may_feed_codec_mclk=1 "
    rb"speaker_i2s_port=1 rate_hz=16000 format=pcm16-stereo channels=2 "
    rb"lrclk_gpio=21 bclk_gpio=22 dout_gpio=23 tx_mclk=none "
    rb"codec_i2c_transactions=0 required_startup_zero_ms=350 "
    rb"backend_volume_step=6/10 gain=attenuated-60-percent "
    rb"music=wad-mus synth=procedural-16voice "
    rb"activation=doom-sfx-init-pending audio_calls=(\d+)"
)
ENGINE_START_RE = re.compile(
    rb"P4_DOOM_E6 ENGINE_START wad=/doom/doom1\.wad touch=ready "
    rb"overlay=visible sfx_request=enabled music_request=enabled "
    rb"music_synth=procedural-16voice usb=absent"
)
VIDEO_READY = (
    b"P4_DOOM_E6 VIDEO_READY input=0x00RRGGBB overlay=touch "
    b"output=rgb565 source=320x200 viewport=960x600 margins=32/32"
)
SOUND_READY_RE = re.compile(
    rb"P4_DOOM_E6 SOUND_READY state=running "
    rb"gpio30=low-readback-proven-at-start "
    rb"backend_volume_step=6/10 gain=attenuated-60-percent "
    rb"music_pipeline=wad-mus-procedural-16voice "
    rb"audio_calls=(\d+)"
)

STATS_FIELDS = (
    "frames", "submits", "completions", "video_timeouts",
    "video_failures", "touch_polls", "touch_failures", "touch_retries",
    "composite_gate", "touch_gate", "audio_gate", "audio_mutating_calls",
    "audio_telemetry_snapshot_valid", "audio_start_proof",
    "audio_write_calls", "audio_frames_forwarded", "audio_nonzero_frames",
    "audio_nonzero_samples", "audio_peak", "backend_telemetry_valid",
    "backend_snapshot_sequence", "backend_gpio30_high_attempts",
    "backend_gpio30_high_successes", "backend_gpio30_high_readbacks",
    "backend_state", "backend_running", "pdm_created", "pdm_enabled",
    "pdm_create_successes", "pdm_enable_successes", "tx_created",
    "tx_enabled", "tx_create_successes", "tx_enable_successes",
    "zero_preload_frames", "gpio30_low_attempts", "gpio30_low_successes",
    "gpio30_low_initial_readbacks", "measured_settle_us",
    "gpio30_low_second_readbacks", "backend_write_successes",
    "backend_write_failures", "backend_frames_written",
    "backend_samples_written", "backend_nonzero_frames",
    "backend_nonzero_samples", "backend_max_abs",
    "backend_rollback_attempts", "backend_rollback_successes",
    "backend_rollback_high_proofs", "backend_resources_retained",
    "backend_resources_owned", "audio_frames", "audio_write_failures",
    "audio_worker_stack_hwm", "music_playing", "music_paused",
    "music_songs", "music_events", "music_notes", "music_loops",
    "music_frames", "music_parse_failures", "music_peak",
)
STATS_RE = re.compile(
    rb"P4_DOOM_E6 STATS frames=(\d+) submits=(\d+) completions=(\d+) "
    rb"video_timeouts=(\d+) video_failures=(\d+) touch_polls=(\d+) "
    rb"touch_failures=(\d+) touch_retries=(\d+) composite_gate=(\d+) "
    rb"touch_gate=(\d+) audio_gate=(\d+) audio_mutating_calls=(\d+) "
    rb"audio_telemetry_snapshot_valid=(\d+) audio_start_proof=([a-z-]+) "
    rb"audio_write_calls=(\d+) audio_frames_forwarded=(\d+) "
    rb"audio_nonzero_frames=(\d+) audio_nonzero_samples=(\d+) "
    rb"audio_peak=(\d+) backend_telemetry_valid=(\d+) "
    rb"backend_snapshot_sequence=(\d+) backend_gpio30_high_attempts=(\d+) "
    rb"backend_gpio30_high_successes=(\d+) "
    rb"backend_gpio30_high_readbacks=(\d+) backend_state=(\d+) "
    rb"backend_running=(\d+) pdm_created=(\d+) pdm_enabled=(\d+) "
    rb"pdm_create_successes=(\d+) pdm_enable_successes=(\d+) "
    rb"tx_created=(\d+) tx_enabled=(\d+) tx_create_successes=(\d+) "
    rb"tx_enable_successes=(\d+) zero_preload_frames=(\d+) "
    rb"gpio30_low_attempts=(\d+) gpio30_low_successes=(\d+) "
    rb"gpio30_low_initial_readbacks=(\d+) measured_settle_us=(\d+) "
    rb"gpio30_low_second_readbacks=(\d+) backend_write_successes=(\d+) "
    rb"backend_write_failures=(\d+) backend_frames_written=(\d+) "
    rb"backend_samples_written=(\d+) backend_nonzero_frames=(\d+) "
    rb"backend_nonzero_samples=(\d+) backend_max_abs=(\d+) "
    rb"backend_rollback_attempts=(\d+) backend_rollback_successes=(\d+) "
    rb"backend_rollback_high_proofs=(\d+) backend_resources_retained=(\d+) "
    rb"backend_resources_owned=(\d+) audio_frames=(\d+) "
    rb"audio_write_failures=(\d+) audio_worker_stack_hwm=(\d+) "
    rb"music_playing=(\d+) music_paused=(\d+) music_songs=(\d+) "
    rb"music_events=(\d+) music_notes=(\d+) music_loops=(\d+) "
    rb"music_frames=(\d+) music_parse_failures=(\d+) music_peak=(\d+)"
)

FIXED = {
    "start": START,
    "mode": MODE,
    "audio_safe": AUDIO_SAFE,
    "cleanup_registered": CLEANUP_REGISTERED,
    "shared_i2c_ready": SHARED_I2C_READY,
    "touch_ready": TOUCH_READY,
    "wad_verified": WAD_VERIFIED,
    "vfs_ready": VFS_READY,
    "video_ready": VIDEO_READY,
}
PATTERNS = {
    "sound_bound": SOUND_BOUND_RE,
    "engine_start": ENGINE_START_RE,
    "sound_ready": SOUND_READY_RE,
    "stats": STATS_RE,
}
REJECT = (
    b"P4_DOOM_E6 BLOCKED", b"P4_DOOM_E6 HALT",
    b"P4_DOOM_E6 SOUND_DISABLED", b"P4_DOOM_E6 SOUND_DEGRADED",
    b"P4_DOOM_E6 AUDIO_SAFETY_FAULT", b"P4_DOOM_E6 CLEANUP ",
    b"Guru Meditation Error", b"panic'ed", b"abort() was called",
    b"assert failed:", b"Task watchdog got triggered",
    b"Interrupt wdt timeout", b"Brownout detector was triggered",
    b"P4_DOOM_GAMEPAD", b"GAMEPAD_D1", b"P4_USB", b"USB_HOST",
    b"native_usb", b"platform_usb", b"usb=present", b"usb=enabled",
    b"usb=ready",
)


def _marker_suffix(line: bytes) -> bytes | None:
    offset = line.find(b"P4_DOOM_E6")
    return None if offset < 0 else line[offset:]


def _match_name(marker: bytes) -> str | None:
    for name, expected in FIXED.items():
        if marker == expected:
            return name
    for name, pattern in PATTERNS.items():
        if pattern.fullmatch(marker) is not None:
            return name
    return None


def parse_stats(payload: bytes) -> list[dict[str, int | str]]:
    records: list[dict[str, int | str]] = []
    for line in payload.splitlines():
        marker = _marker_suffix(line)
        if marker is None:
            continue
        match = STATS_RE.fullmatch(marker)
        if match is None:
            continue
        record: dict[str, int | str] = {}
        for name, value in zip(STATS_FIELDS, match.groups(), strict=True):
            record[name] = (
                value.decode("ascii", "strict")
                if name == "audio_start_proof" else int(value)
            )
        records.append(record)
    return records


def _u32_record(record: dict[str, int | str]) -> bool:
    return all(
        isinstance(value, str) or 0 <= value <= UINT32_MAX
        for value in record.values()
    )


def _record_exact(record: dict[str, int | str]) -> bool:
    return (
        record["frames"] > 0
        and record["frames"] % 150 == 0
        and record["submits"] == record["frames"] + 1
        and record["completions"] == record["submits"]
        and record["video_timeouts"] == 0
        and record["video_failures"] == 0
        and record["touch_polls"] > record["touch_failures"]
        and record["composite_gate"] == 1
        and record["touch_gate"] == 1
        and record["audio_gate"] == 1
        # The adapter publishes one coherent nonblocking snapshot. Startup
        # contributes exactly force-safe/create/bind-state/start, followed by
        # one accepted counted call for each successful write.
        and record["audio_mutating_calls"]
            == record["audio_write_calls"] + 4
        and record["audio_telemetry_snapshot_valid"] == 1
        and record["audio_start_proof"] == "low-readback-proven-at-start"
        and record["audio_write_calls"] > 0
        and record["audio_frames_forwarded"] > 0
        and 0 < record["audio_nonzero_frames"]
            <= record["audio_frames_forwarded"]
        and record["audio_nonzero_frames"]
            <= record["audio_nonzero_samples"]
            <= 2 * record["audio_frames_forwarded"]
        and 0 < record["audio_peak"] <= 32768
        and record["backend_telemetry_valid"] == 1
        and record["backend_snapshot_sequence"] > 0
        # The accepted boot has four proven-safe-high operations. Each makes
        # two physical high requests and one readback; create/start each finish
        # one complete READY_MUTED rollback before RUNNING.
        and record["backend_gpio30_high_attempts"] == 8
        and record["backend_gpio30_high_successes"] == 8
        and record["backend_gpio30_high_readbacks"] == 4
        and record["backend_state"] == 1
        and record["backend_running"] == 1
        and record["pdm_created"] == 1
        and record["pdm_enabled"] == 1
        and record["pdm_create_successes"] == 1
        and record["pdm_enable_successes"] == 1
        and record["tx_created"] == 1
        and record["tx_enabled"] == 1
        and record["tx_create_successes"] == 1
        # Create establishes READY_MUTED once; start re-primes and establishes
        # it a second time before requesting the amplifier low.
        and record["tx_enable_successes"] == 2
        and record["zero_preload_frames"] == 1536
        and record["gpio30_low_attempts"] == 1
        and record["gpio30_low_successes"] == 1
        and record["gpio30_low_initial_readbacks"] == 1
        and record["measured_settle_us"] >= 350000
        and record["gpio30_low_second_readbacks"] == 1
        and record["backend_write_successes"] > 0
        and record["backend_write_failures"] == 0
        and record["audio_frames_forwarded"] == 128 * record["audio_write_calls"]
        and record["backend_frames_written"] == 128 * record["backend_write_successes"]
        # Main snapshots worker, adapter, then backend in that order. A write
        # can complete between snapshots, so the only truthful cross-layer
        # relation is worker <= adapter <= backend, not artificial equality.
        and record["backend_frames_written"] >= record["audio_frames_forwarded"]
        and record["backend_samples_written"] == 2 * record["backend_frames_written"]
        and record["backend_nonzero_frames"] >= record["audio_nonzero_frames"]
        and record["backend_nonzero_frames"] <= record["backend_frames_written"]
        and record["backend_nonzero_samples"] >= record["audio_nonzero_samples"]
        and record["backend_nonzero_frames"]
            <= record["backend_nonzero_samples"]
            <= 2 * record["backend_frames_written"]
        and record["backend_max_abs"] >= record["audio_peak"]
        and record["backend_max_abs"] <= 32768
        and record["backend_rollback_attempts"] == 2
        and record["backend_rollback_successes"] == 2
        and record["backend_rollback_high_proofs"] == 2
        and record["backend_resources_retained"] == 0
        and record["backend_resources_owned"] == 2
        and 0 < record["audio_frames"] <= record["audio_frames_forwarded"]
        and record["audio_frames"] % 128 == 0
        and record["audio_write_failures"] == 0
        and 512 <= record["audio_worker_stack_hwm"] <= 6144
        and record["music_playing"] in (0, 1)
        and record["music_paused"] == 0
        and record["music_songs"] > 0
        and record["music_events"] > 0
        and record["music_notes"] > 0
        and 0 <= record["music_loops"] <= record["music_events"]
        and 0 < record["music_frames"] <= record["audio_frames"]
        and record["music_parse_failures"] == 0
        and 0 < record["music_peak"] <= 32768
    )


def _monotonic(earlier: dict[str, int | str],
               later: dict[str, int | str]) -> bool:
    increasing = (
        "frames", "submits", "completions", "touch_polls",
        "audio_mutating_calls", "audio_write_calls",
        "audio_frames_forwarded", "audio_nonzero_frames",
        "audio_nonzero_samples", "backend_snapshot_sequence",
        "backend_write_successes", "backend_frames_written",
        "backend_samples_written", "backend_nonzero_frames",
        "backend_nonzero_samples", "audio_frames",
        "music_frames",
    )
    nondecreasing = (
        "touch_failures", "touch_retries", "audio_peak",
        "backend_max_abs", "backend_rollback_attempts",
        "backend_rollback_successes", "backend_rollback_high_proofs",
        "music_songs", "music_events", "music_notes", "music_loops",
        "music_peak",
    )
    return (
        all(later[name] > earlier[name] for name in increasing)
        and all(later[name] >= earlier[name] for name in nondecreasing)
        and later["audio_worker_stack_hwm"]
            <= earlier["audio_worker_stack_hwm"]
    )


def analyze(payload: bytes, min_stats: int = 2) -> dict[str, Any]:
    if not 2 <= min_stats <= 20:
        raise ValueError("min_stats must be in [2, 20]")
    payload = ANSI_COLOR_RE.sub(b"", payload)
    lines = payload.splitlines()
    counts = {name: 0 for name in (*FIXED, *PATTERNS)}
    positions: dict[str, int] = {}
    malformed: list[str] = []
    for index, line in enumerate(lines):
        marker = _marker_suffix(line)
        if marker is None:
            continue
        name = _match_name(marker)
        if name is None:
            malformed.append(marker.decode("ascii", "replace"))
            continue
        counts[name] += 1
        positions.setdefault(name, index)

    stats = parse_stats(payload)
    fixed_exact = all(counts[name] == 1 for name in FIXED)
    patterns_exact = all(
        counts[name] == 1 for name in ("sound_bound", "engine_start", "sound_ready")
    )
    order = (
        "start", "mode", "audio_safe", "cleanup_registered",
        "shared_i2c_ready", "touch_ready", "wad_verified", "vfs_ready",
        "sound_bound", "engine_start", "video_ready", "sound_ready", "stats",
    )
    startup_order_exact = all(name in positions for name in order) and all(
        positions[left] < positions[right] for left, right in zip(order, order[1:])
    )
    bound_match = SOUND_BOUND_RE.fullmatch(
        _marker_suffix(lines[positions["sound_bound"]]) or b""
    ) if "sound_bound" in positions else None
    ready_match = SOUND_READY_RE.fullmatch(
        _marker_suffix(lines[positions["sound_ready"]]) or b""
    ) if "sound_ready" in positions else None
    startup_calls_valid = (
        bound_match is not None and ready_match is not None
        and int(bound_match.group(1)) == 3
        and int(ready_match.group(1)) > int(bound_match.group(1))
    )

    rom_banners = len(re.findall(rb"ESP-ROM:", payload, re.I))
    reset_markers = len(re.findall(rb"(?:^|[\r\n])rst:", payload, re.I))
    reject_markers = [
        marker.decode("ascii", "replace") for marker in REJECT
        if marker.lower() in payload.lower()
    ]
    if rom_banners != 1 or reset_markers != 1:
        reject_markers.append("exactly-one-boot-reset-pair-not-observed")

    records_exact = len(stats) >= min_stats and all(
        _u32_record(record) and _record_exact(record) for record in stats
    )
    records_monotonic = len(stats) >= min_stats and all(
        _monotonic(earlier, later) for earlier, later in zip(stats, stats[1:])
    )
    passed = (
        fixed_exact and patterns_exact and startup_order_exact
        and startup_calls_valid and records_exact and records_monotonic
        and not malformed and not reject_markers
    )
    return {
        "schema": 1,
        "capture_class": "retained-exclusive-uart-after-sole-launch-reset",
        "transmitted_bytes": 0,
        "serial_reopen_count": 0,
        "serial_flush_count": 0,
        "additional_reset_count": 0,
        "marker_counts": counts,
        "startup_order_exact": startup_order_exact,
        "startup_calls_valid": startup_calls_valid,
        "rom_banner_count": rom_banners,
        "reset_marker_count": reset_markers,
        "stats_count": len(stats),
        "stats": stats,
        "stats_exact": records_exact,
        "stats_monotonic": records_monotonic,
        "malformed_protected_lines": malformed,
        "reject_markers": reject_markers,
        "software_audio_witness": (
            "nonzero Doom mixer PCM accepted by the full factory PDM+TX backend"
            if passed else "not proven"
        ),
        "software_music_witness": (
            "real WAD MUS events and notes produced nonzero procedural-synth PCM"
            if passed else "not proven"
        ),
        "acoustic_output": "not inferred; requires separate microphone/user evidence",
        "result": "pass" if passed else "fail",
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


def capture_open_handle(device: Any, seconds: float, min_stats: int = 2) -> tuple[bytes, dict[str, Any]]:
    """Read startup from the installer's retained descriptor without side effects."""

    if not 5.0 <= seconds <= 180.0:
        raise ValueError("seconds must be in [5, 180]")
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
            if device.fileno() != descriptor or not device.is_open or not device.exclusive:
                raise RuntimeError("retained UART descriptor changed during capture")
            if bool(device.dtr) or bool(device.rts):
                raise RuntimeError("reset controls changed during capture")
            chunk = device.read(4096)
            if chunk:
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


def _write_new(path: pathlib.Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(payload)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="analyze a retained-UART E6 transcript; does not open serial"
    )
    parser.add_argument("--raw-input", type=pathlib.Path, required=True)
    parser.add_argument("--summary", type=pathlib.Path, required=True)
    parser.add_argument("--min-stats", type=int, default=2)
    args = parser.parse_args()
    if args.summary.exists() or args.summary.is_symlink():
        parser.error("summary must not already exist")
    payload = args.raw_input.read_bytes()
    result = {
        "raw_bytes": len(payload),
        "raw_sha256": hashlib.sha256(payload).hexdigest(),
        **analyze(payload, args.min_stats),
    }
    _write_new(
        args.summary,
        (json.dumps(result, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(json.dumps(result, separators=(",", ":")))
    return 0 if result["result"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
