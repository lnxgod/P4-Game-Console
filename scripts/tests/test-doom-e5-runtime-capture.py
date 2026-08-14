#!/usr/bin/env python3

"""Host/adversarial tests for the receive-only E5 runtime capture."""

from __future__ import annotations

import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/capture-doom-e5-runtime.py"


def load_module():
    spec = importlib.util.spec_from_file_location("capture_doom_e5", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


capture = load_module()


def line(value: bytes) -> bytes:
    return b"I (123) p4_doom_touch_audio: " + value + b"\r\n"


def stats(
    frames: int,
    *,
    touch_polls: int,
    touch_failures: int = 0,
    touch_retries: int = 1,
    audio_frames: int = 0,
    video_timeouts: int = 0,
    video_failures: int = 0,
    composite_gate: int = 1,
    touch_gate: int = 1,
    audio_gate: int = 0,
    audio_calls: int = 0,
    gpio30: str = "untouched",
    audio_enabled: int = 0,
    audio_write_failures: int = 0,
) -> bytes:
    return line(
        (
            "P4_DOOM_E5 STATS "
            f"frames={frames} submits={frames + 1} completions={frames + 1} "
            f"video_timeouts={video_timeouts} video_failures={video_failures} "
            f"touch_polls={touch_polls} touch_failures={touch_failures} "
            f"touch_retries={touch_retries} composite_gate={composite_gate} "
            f"touch_gate={touch_gate} audio_gate={audio_gate} "
            f"audio_calls={audio_calls} gpio30={gpio30} "
            f"audio_enabled={audio_enabled} "
            f"audio_frames={audio_frames} "
            f"audio_write_failures={audio_write_failures}"
        ).encode()
    )


def full_startup(*, degraded_first: bool = False) -> bytes:
    values = [
        capture.START,
        capture.MODE_TOUCH_ONLY,
        capture.SOUND_DISABLED,
        capture.CLEANUP_REGISTERED,
        capture.SHARED_I2C_READY,
    ]
    if degraded_first:
        values.append(
            b"P4_DOOM_E5 TOUCH_DEGRADED stage=gt911-create "
            b"error=ESP_ERR_NOT_FOUND neutral=1 overlay=visible "
            b"cleanup_proven=1 bus_owned=1"
        )
    values.extend(
        [
            capture.TOUCH_READY,
            capture.WAD_VERIFIED,
            capture.VFS_READY,
            capture.SOUND_DISABLED,
            (
                b"P4_DOOM_E5 ENGINE_START wad=/doom/doom1.wad touch=ready "
                b"overlay=visible sfx=disabled music=disabled usb=absent"
            ),
            capture.VIDEO_READY,
        ]
    )
    return b"".join(line(value) for value in values)


def healthy_stats() -> bytes:
    return b"".join(
        (
            stats(300, touch_polls=400),
            stats(600, touch_polls=800),
            stats(900, touch_polls=1200),
        )
    )


def outcome(payload: bytes, startup: str = "optional"):
    return capture.analyze(payload, min_stats=2, startup=startup)


complete = outcome(full_startup() + healthy_stats(), "required")
assert complete["result"] == "pass"
assert complete["classification"] == "runtime-pass-startup-observed"
assert complete["startup_exact"] is True
assert complete["touch_runtime_status"] == "ready-marker-and-successful-polls"
assert complete["contacts_observed"] is None
assert complete["audible_acoustic_output"] == "not enabled in this runtime"

recovered = outcome(full_startup(degraded_first=True) + healthy_stats(), "required")
assert recovered["result"] == "pass"
assert recovered["touch_runtime_status"] == "ready-with-recorded-degradation"
assert recovered["touch_degraded"] == [
    {
        "stage": "gt911-create",
        "error": "ESP_ERR_NOT_FOUND",
        "cleanup_proven": 1,
        "bus_owned": 1,
    }
]

late = outcome(healthy_stats())
assert late["result"] == "pass"
assert late["classification"] == "runtime-pass-late-attach"
assert late["startup_status"] == "not-sampled-late-attach"
assert "late-attach" in late["touch_runtime_status"]
assert "composite=1 touch=1 audio=0" in late["runtime_gate_evidence"]
assert "gpio30=untouched" in late["gpio30_runtime_evidence"]
assert late["periodic_runtime_contract_exact"] is True
assert outcome(healthy_stats(), "required")["result"] == "fail"
assert outcome(b"")["periodic_runtime_contract_exact"] is False

# A fixed marker with changed reviewed fields fails closed even in late mode.
malformed_start = line(capture.START.replace(b"usb=absent", b"usb=maybe"))
malformed = outcome(malformed_start + healthy_stats())
assert malformed["result"] == "fail"
assert "start" in malformed["malformed_markers"]

# Video and audio counters must remain healthy and advance.
bad_video = healthy_stats() + stats(1200, touch_polls=1600, video_failures=1)
assert outcome(bad_video)["result"] == "fail"
bad_audio = b"".join(
    (
        stats(300, touch_polls=400),
        stats(600, touch_polls=800, audio_enabled=1, audio_frames=64000),
    )
)
assert outcome(bad_audio)["result"] == "fail"
write_failure = b"".join(
    (
        stats(300, touch_polls=400),
        stats(
            600,
            touch_polls=800,
            audio_write_failures=1,
        ),
    )
)
assert outcome(write_failure)["result"] == "fail"

# Increasing polls do not prove touch if every read failed.  No contact claim
# is manufactured in either the passing or failing case.
all_touch_failed = b"".join(
    (
        stats(300, touch_polls=400, touch_failures=400),
        stats(600, touch_polls=800, touch_failures=800),
    )
)
touch_failure = outcome(all_touch_failed)
assert touch_failure["result"] == "fail"
assert touch_failure["contacts_observed"] is None

for rejected in (
    b"Guru Meditation Error: Core 0 panic'ed",
    b"Task watchdog got triggered",
    b"Brownout detector was triggered",
    b"P4_DOOM_E5 HALT stage=engine-frame error=ESP_FAIL",
    b"P4_DOOM_GAMEPAD E3 START",
    b"USB_HOST READY",
    b"P4_DOOM_E5 ENGINE_START usb=enabled",
):
    with_marker = outcome(healthy_stats() + line(rejected))
    assert with_marker["result"] == "fail", rejected

# A single boot banner/reset pair before START is recorded, not mislabeled as
# a loop.  A second boot or any reset after START fails.
single_boot = outcome(
    b"ESP-ROM:esp32p4-eco2-20240710\r\nrst:0x1 (POWERON_RESET)\r\n"
    + full_startup()
    + healthy_stats(),
    "required",
)
assert single_boot["result"] == "pass"
assert single_boot["rom_banner_count"] == 1
assert single_boot["rst_marker_count"] == 1
assert single_boot["reset_loop_detected"] is False
for reset_loop in (
    b"ESP-ROM:first\r\nESP-ROM:second\r\n",
    full_startup() + b"\r\nrst:0x3 (SW_RESET)\r\n",
    b"rst:0x3 (SW_RESET)\r\n",
):
    assert outcome(reset_loop + healthy_stats())["result"] == "fail"

# The required E5 usb=absent markers are not mistaken for USB activation.
assert outcome(full_startup() + healthy_stats(), "required")[
    "usb_runtime_markers_seen"
] is False

# Malformed and decreasing stats are rejected rather than silently skipped.
assert outcome(
    healthy_stats()
    + line(b"P4_DOOM_E5 STATS frames=not-a-number submits=1")
)["result"] == "fail"

# ANSI log coloring is transport decoration, not marker drift; trailing text
# after a reviewed marker is drift and must fail exact matching.
colored = b"\x1b[0;32m" + full_startup() + b"\x1b[0m" + healthy_stats()
assert outcome(colored, "required")["result"] == "pass"
extra = line(capture.START + b" unexpected=1") + healthy_stats()
assert outcome(extra)["result"] == "fail"

# Every periodic gate, counted-call, and derived GPIO field is captured as
# evidence, then validated semantically.  A plausible alternative value must
# be parsed and rejected rather than disappearing as a non-matching line.
for field, value in (
    ("composite_gate", 0),
    ("touch_gate", 0),
    ("audio_gate", 1),
    ("audio_calls", 1),
    ("gpio30", "low"),
):
    kwargs = {field: value}
    drift = stats(1200, touch_polls=1600, **kwargs)
    drifted = outcome(healthy_stats() + drift)
    assert drifted["stats_count"] == 4, (field, value)
    assert drifted["stats"][-1][field] == value, (field, value)
    assert drifted["periodic_runtime_contract_exact"] is False, (field, value)
    assert drifted["result"] == "fail", (field, value)
decreasing = b"".join(
    (
        stats(600, touch_polls=800),
        stats(300, touch_polls=400),
    )
)
assert outcome(decreasing)["result"] == "fail"

# Static transport contract: configure inactive reset lines before opening,
# clear HUPCL, hold exclusivity, and never invoke a serial write/flush/reset.
source = SCRIPT.read_text()
constructor = source.index("serial.Serial(\n        port=None")
dtr = source.index("device.dtr = False", constructor)
rts = source.index("device.rts = False", constructor)
opened = source.index("device.open()", constructor)
assert constructor < dtr < opened and constructor < rts < opened
assert "exclusive=True" in source[constructor:opened]
assert 'attributes[2] &= ~getattr(termios, "HUPCL", 0)' in source
assert "device.write(" not in source
assert "device.reset_input_buffer(" not in source
assert "device.reset_output_buffer(" not in source

print("E5 receive-only runtime capture adversarial tests PASS")
