#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
import pathlib
import sys
import types


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/capture-waveshare-console-os-scroll-boundary.py"
SPEC = importlib.util.spec_from_file_location("scroll_boundary_capture", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def marker() -> bytes:
    return (
        b"I (100) test: " + MODULE.MARKER + b" "
        b"idle_us=250000 mb_reports=4 mb_cadence_avg_us=10000 "
        b"mb_age_peak_us=8849 mb_age_last_us=4200 mb_samples=8 "
        b"mb_failures=0 mb_stale=0 late_checks=8 late_updates=7 late_scroll=7 "
        b"late_wait_us=120 refreshes=3 interactive=4 "
        b"path=full:1,partial:2,composite:1 comp_submits=1 "
        b"comp_bootstraps=1 comp_failures=0 partial_full_fallbacks=0 "
        b"input_gdma_boundary_avg_us=39750 handoff_gdma_boundary_avg_us=8600 "
        b"present_samples=4 render_avg_max_us=100/200 "
        b"submit_avg_max_us=300/400 reuse_avg_max_us=10/20 "
        b"transform_avg_max_us=500/600 handoff_avg_max_us=700/800\r\n"
    )


def ready() -> bytes:
    return b"P4_CONSOLE_OS READY page=home display=mipi-dsi\r\n"


def main() -> None:
    payload = ready() + marker()
    result = MODULE.analyze(payload)
    assert result["result"] == "pass"
    assert result["marker_count"] == 1
    assert result["marker_complete"] is True
    assert result["fields"]["path_partial"] == 2
    assert result["fields"]["handoff_max_us"] == 800

    assert MODULE.analyze(marker())["result"] == "fail"

    old_allow = os.environ.pop("P4_CAPTURE_ALLOW_INITIAL_BOOT", None)
    try:
        initial_boot = (
            b"rst:0x1 (POWERON_RESET)\r\n"
            b"P4_CONSOLE_OS START shell=freertos-native\r\n"
            + payload
        )
        assert MODULE.analyze(initial_boot)["result"] == "fail"
        os.environ["P4_CAPTURE_ALLOW_INITIAL_BOOT"] = "1"
        assert MODULE.analyze(initial_boot)["result"] == "pass"
        assert MODULE.analyze(
            b"rst:0x1 (POWERON)\r\n"
            b"P4_CONSOLE_OS START shell=freertos-native\r\n"
            + payload
        )["result"] == "pass"
        assert MODULE.analyze(
            b"rst:0x1 (POWERON_RESET)\r\n" + payload + b"rst:0x3 (SW_RESET)\r\n"
        )["result"] == "fail"
        assert MODULE.analyze(
            b"P4_CONSOLE_OS START shell=freertos-native\r\n" + payload
        )["result"] == "pass"
        assert MODULE.analyze(
            b"rst:0x1 (POWERON_RESET)\r\n"
            b"rst:0x1 (POWERON_RESET)\r\n"
            + payload
        )["result"] == "fail"
    finally:
        if old_allow is None:
            os.environ.pop("P4_CAPTURE_ALLOW_INITIAL_BOOT", None)
        else:
            os.environ["P4_CAPTURE_ALLOW_INITIAL_BOOT"] = old_allow

    for mutated in (
        payload.replace(b"input_gdma_boundary_avg_us=39750", b"input_gdma_boundary_avg_us=x"),
        payload[:-2],
        payload + marker(),
        payload + b"I (100) test: P4_CONSOLE_OS HALT stage=test\r\n",
    ):
        assert MODULE.analyze(mutated)["result"] == "fail"

    class FakeDevice:
        def __init__(self) -> None:
            self.port = None
            self.baudrate = None
            self.timeout = None
            self.write_timeout = None
            self.exclusive = None
            self.dsrdtr = None
            self.rtscts = None
            self.dtr = False
            self.rts = False
            self.is_open = False
            self.reads = [payload]
            self.input_reset = False

        def open(self) -> None:
            self.is_open = True

        def fileno(self) -> int:
            return 42

        def reset_input_buffer(self) -> None:
            self.input_reset = True

        def read(self, _size: int) -> bytes:
            return self.reads.pop(0) if self.reads else b""

        def close(self) -> None:
            self.is_open = False

    fake = FakeDevice()
    sys.modules["serial"] = types.SimpleNamespace(Serial=lambda: fake)
    old_drain = MODULE.DRAIN_SECONDS
    MODULE.DRAIN_SECONDS = 0.0
    try:
        assert MODULE.analyze(MODULE.capture("/dev/fake", 20))["result"] == "pass"
        assert fake.baudrate == 115200
        assert fake.dtr is False and fake.rts is False
        assert fake.input_reset is True
        assert fake.is_open is False
    finally:
        MODULE.DRAIN_SECONDS = old_drain
        sys.modules.pop("serial", None)
    print("Console OS scroll-boundary capture tests passed")


if __name__ == "__main__":
    main()
