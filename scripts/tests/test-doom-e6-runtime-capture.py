#!/usr/bin/env python3

"""Adversarial host tests for the retained-UART E6 startup analyzer."""

from __future__ import annotations

import importlib.util
import pathlib
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/capture-doom-e6-runtime.py"


def load_module():
    spec = importlib.util.spec_from_file_location("capture_doom_e6", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


capture = load_module()


def line(marker: bytes) -> bytes:
    return b"I (123) p4_doom_touch_audio: " + marker + b"\r\n"


def stats(frames: int, *, audio_peak: int, touch_failures: int = 0,
          overrides: dict[str, int | str] | None = None) -> bytes:
    writes = frames * 2
    forwarded = writes * 128
    nonzero_frames = max(1, forwarded // 8)
    nonzero_samples = nonzero_frames * 2
    values: dict[str, int | str] = {
        "frames": frames,
        "submits": frames + 1,
        "completions": frames + 1,
        "video_timeouts": 0,
        "video_failures": 0,
        "touch_polls": frames + 50,
        "touch_failures": touch_failures,
        "touch_retries": 1,
        "composite_gate": 1,
        "touch_gate": 1,
        "audio_gate": 1,
        "audio_mutating_calls": 4 + writes,
        "audio_telemetry_snapshot_valid": 1,
        "audio_start_proof": "low-readback-proven-at-start",
        "audio_write_calls": writes,
        "audio_frames_forwarded": forwarded,
        "audio_nonzero_frames": nonzero_frames,
        "audio_nonzero_samples": nonzero_samples,
        "audio_peak": audio_peak,
        "backend_telemetry_valid": 1,
        "backend_snapshot_sequence": 100 + writes,
        "backend_gpio30_high_attempts": 8,
        "backend_gpio30_high_successes": 8,
        "backend_gpio30_high_readbacks": 4,
        "backend_state": 1,
        "backend_running": 1,
        "pdm_created": 1,
        "pdm_enabled": 1,
        "pdm_create_successes": 1,
        "pdm_enable_successes": 1,
        "tx_created": 1,
        "tx_enabled": 1,
        "tx_create_successes": 1,
        "tx_enable_successes": 2,
        "zero_preload_frames": 1536,
        "gpio30_low_attempts": 1,
        "gpio30_low_successes": 1,
        "gpio30_low_initial_readbacks": 1,
        "measured_settle_us": 351000,
        "gpio30_low_second_readbacks": 1,
        "backend_write_successes": writes,
        "backend_write_failures": 0,
        "backend_frames_written": forwarded,
        "backend_samples_written": forwarded * 2,
        "backend_nonzero_frames": nonzero_frames,
        "backend_nonzero_samples": nonzero_samples,
        "backend_max_abs": audio_peak,
        "backend_rollback_attempts": 2,
        "backend_rollback_successes": 2,
        "backend_rollback_high_proofs": 2,
        "backend_resources_retained": 0,
        "backend_resources_owned": 2,
        "audio_frames": forwarded,
        "audio_write_failures": 0,
    }
    if overrides:
        values.update(overrides)
    text = "P4_DOOM_E6 STATS " + " ".join(
        f"{name}={values[name]}" for name in capture.STATS_FIELDS
    )
    return line(text.encode())


def startup() -> bytes:
    markers = (
        capture.START,
        capture.MODE,
        capture.AUDIO_SAFE,
        capture.CLEANUP_REGISTERED,
        capture.SHARED_I2C_READY,
        capture.TOUCH_READY,
        capture.WAD_VERIFIED,
        capture.VFS_READY,
        (
            b"P4_DOOM_E6 SOUND_BOUND backend=factory-complete-audio-init "
            b"state=ready-muted gpio30=high-pad-readback-proven "
            b"pdm_i2s_port=0 pdm_clk_gpio=24 pdm_clk_hz=1024000 "
            b"pdm_din_gpio=26 gpio24_may_feed_codec_mclk=1 "
            b"speaker_i2s_port=1 rate_hz=16000 format=pcm16-stereo channels=2 "
            b"lrclk_gpio=21 bclk_gpio=22 dout_gpio=23 tx_mclk=none "
            b"codec_i2c_transactions=0 required_startup_zero_ms=350 "
            b"backend_volume_step=10/10 gain=unity-no-amplification "
            b"music=disabled activation=doom-sfx-init-pending audio_calls=3"
        ),
        (
            b"P4_DOOM_E6 ENGINE_START wad=/doom/doom1.wad touch=ready "
            b"overlay=visible sfx_request=enabled music=disabled usb=absent"
        ),
        capture.VIDEO_READY,
        (
            b"P4_DOOM_E6 SOUND_READY state=running "
            b"gpio30=low-readback-proven-at-start "
            b"backend_volume_step=10/10 gain=unity-no-amplification "
            b"audio_calls=5"
        ),
    )
    return (
        b"ESP-ROM:esp32p4-eco2-20240710\r\nrst:0x1 (POWERON_RESET)\r\n"
        + b"".join(line(marker) for marker in markers)
    )


def healthy() -> bytes:
    return startup() + stats(300, audio_peak=12000) + stats(600, audio_peak=16000)


good = capture.analyze(healthy())
assert good["result"] == "pass", good
assert good["stats_exact"] is True
assert good["stats_monotonic"] is True
assert good["acoustic_output"].startswith("not inferred")

# Exact startup truth and ordering are mandatory; late attach is never enough.
assert capture.analyze(stats(300, audio_peak=12000) + stats(600, audio_peak=16000))["result"] == "fail"
assert capture.analyze(healthy().replace(line(capture.AUDIO_SAFE), b""))["result"] == "fail"
assert capture.analyze(healthy().replace(line(capture.MODE), line(capture.MODE.replace(b"audio_gate=1", b"audio_gate=0"))))["result"] == "fail"
assert capture.analyze(healthy().replace(line(capture.AUDIO_SAFE), line(capture.AUDIO_SAFE) + line(capture.AUDIO_SAFE)))["result"] == "fail"

# Every protected E6 line has an exact grammar; plausible extras cannot hide.
for extra in (
    b"P4_DOOM_E6 SOUND_READY state=running trailing=1",
    b"P4_DOOM_E6 UNKNOWN accepted=1",
    capture.START + b" trailing=1",
):
    result = capture.analyze(healthy() + line(extra))
    assert result["result"] == "fail", extra
    assert result["malformed_protected_lines"], extra

# Safety/degraded/reset/panic/USB paths are categorically rejected.
for rejected in (
    b"P4_DOOM_E6 SOUND_DEGRADED stage=create fallback=silent",
    b"P4_DOOM_E6 AUDIO_SAFETY_FAULT stage=worker halt=1",
    b"P4_DOOM_E6 HALT stage=audio error=ESP_FAIL",
    b"Guru Meditation Error: Core 0 panic'ed",
    b"USB_HOST READY",
):
    assert capture.analyze(healthy() + line(rejected))["result"] == "fail", rejected
assert capture.analyze(healthy() + b"rst:0x3 (SW_RESET)\r\n")["result"] == "fail"

# Exact factory bring-up and non-zero PCM witnesses must all hold.
drifts: tuple[tuple[str, int | str], ...] = (
    ("audio_gate", 0),
    ("audio_start_proof", "not-proven"),
    ("audio_nonzero_frames", 0),
    ("audio_peak", 0),
    ("backend_state", 0),
    ("backend_running", 0),
    ("pdm_enabled", 0),
    ("tx_enabled", 0),
    ("zero_preload_frames", 1535),
    ("gpio30_low_attempts", 2),
    ("gpio30_low_initial_readbacks", 0),
    ("measured_settle_us", 349999),
    ("gpio30_low_second_readbacks", 0),
    ("backend_write_failures", 1),
    ("backend_gpio30_high_attempts", 9),
    ("backend_gpio30_high_successes", 7),
    ("backend_gpio30_high_readbacks", 3),
    ("backend_rollback_attempts", 3),
    ("backend_rollback_successes", 1),
    ("backend_rollback_high_proofs", 1),
    ("backend_resources_retained", 1),
    ("audio_write_failures", 1),
)
for field, value in drifts:
    payload = startup() + stats(300, audio_peak=12000) + stats(
        600, audio_peak=16000, overrides={field: value}
    )
    result = capture.analyze(payload)
    assert result["stats_count"] == 2, field
    assert result["stats"][-1][field] == value, field
    assert result["result"] == "fail", field

# Cross-layer counters and monotonicity are bound without pretending separately
# sampled worker/adapter/backend records are one atomic snapshot.
for field, value in (
    ("backend_frames_written", 127),
    ("backend_samples_written", 1),
    ("backend_nonzero_frames", 0),
    ("backend_nonzero_samples", 0),
    ("backend_max_abs", 11999),
    ("audio_frames", 129),
):
    payload = startup() + stats(300, audio_peak=12000) + stats(
        600, audio_peak=16000, overrides={field: value}
    )
    assert capture.analyze(payload)["result"] == "fail", field

# Adapter invocation and non-zero witnesses remain correlated even though the
# app snapshots worker/adapter/backend records independently.
for field, value in (
    ("audio_mutating_calls", 5),
    ("audio_mutating_calls", 10_000),
    ("audio_nonzero_frames", 128 * 1201),
    ("audio_nonzero_samples", 1),
    ("audio_nonzero_samples", 128 * 1201 * 2 + 1),
    ("backend_nonzero_samples", 1),
    ("backend_nonzero_samples", 128 * 1201 * 2 + 1),
):
    payload = startup() + stats(300, audio_peak=12000) + stats(
        600, audio_peak=16000, overrides={field: value}
    )
    assert capture.analyze(payload)["result"] == "fail", (field, value)
decreasing = startup() + stats(600, audio_peak=16000) + stats(300, audio_peak=12000)
assert capture.analyze(decreasing)["result"] == "fail"


class FailingDevice:
    is_open = True
    exclusive = True
    dtr = False
    rts = False

    def __init__(self, reads):
        self.reads = list(reads)

    def fileno(self):
        return 7

    def read(self, _count):
        value = self.reads.pop(0)
        if isinstance(value, BaseException):
            raise value
        return value


# Every bounded byte survives read exceptions, overflow, and BaseException
# interruption so the installer can durably bind it before rolling back.
for failure in (OSError("uart read failed"), KeyboardInterrupt("signal")):
    device = FailingDevice([b"partial-startup\n", failure])
    with mock.patch.object(capture.time, "monotonic", side_effect=[0, 0, 0]):
        try:
            capture.capture_open_handle(device, 5.0)
        except capture.CaptureFailure as error:
            assert error.partial_raw == b"partial-startup\n"
            assert error.failure_summary["result"] == "fail"
            assert type(failure).__name__ in error.reason
        else:
            raise AssertionError("retained UART exception lost partial transcript")

device = FailingDevice([b"X" * (capture.MAX_TRANSCRIPT_BYTES + 1)])
with mock.patch.object(capture.time, "monotonic", side_effect=[0, 0]):
    try:
        capture.capture_open_handle(device, 5.0)
    except capture.CaptureFailure as error:
        assert len(error.partial_raw) == capture.MAX_TRANSCRIPT_BYTES
        assert "exceeded 1 MiB" in error.reason
    else:
        raise AssertionError("oversized transcript was accepted")

# Capture helper is receive-only on the retained object: no open/write/flush/reset.
source = SCRIPT.read_text()
capture_body = source[source.index("def capture_open_handle"):source.index("def _write_new")]
for forbidden in (
    ".open(", ".write(", ".flush(", "reset_input_buffer",
    "reset_output_buffer", ".dtr =", ".rts =",
):
    assert forbidden not in capture_body, forbidden
assert "device.read(4096)" in capture_body
assert "device.fileno() != descriptor" in capture_body

print("E6 retained-UART runtime capture adversarial tests PASS")
