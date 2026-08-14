#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/capture-console-os-runtime.py"
SPEC = importlib.util.spec_from_file_location("console_capture", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def line(marker: bytes) -> bytes:
    return b"I (100) test: " + marker + b"\r\n"


def stats(loops: int, polls: int) -> bytes:
    return line(
        b"P4_CONSOLE_OS STATS loops=" + str(loops).encode() +
        b" page=0 renders=1 touch_ready=1 touch_polls=" +
        str(polls).encode() +
        b" touch_failures=0 display_submits=1 display_completions=1 "
        b"display_timeouts=0 display_failures=0 amp_energized=0 "
        b"doom_handoffs=0"
    )


def passing_payload() -> bytes:
    return b"".join(line(marker) for _name, marker in MODULE.FIXED) + \
        stats(300, 299) + stats(600, 599)


def main() -> None:
    payload = passing_payload()
    result = MODULE.analyze(payload, 2)
    assert result["result"] == "pass"
    assert result["amp_energized"] is False
    assert result["doom_handoff_observed"] is False

    assert MODULE.analyze(payload.replace(
        b"touch_failures=0", b"touch_failures=1", 1
    ), 2)["result"] == "fail"
    assert MODULE.analyze(payload.replace(
        b"amp_energized=0", b"amp_energized=1", 1
    ), 2)["result"] == "fail"
    assert MODULE.analyze(payload + line(
        b"P4_CONSOLE_OS HANDOFF_BEGIN app=doom"
    ), 2)["result"] == "fail"
    assert MODULE.analyze(payload.replace(
        line(MODULE.TOUCH_READY), b""
    ), 2)["result"] == "fail"
    assert MODULE.analyze(stats(300, 299) + b"".join(
        line(marker) for _name, marker in MODULE.FIXED
    ) + stats(600, 599), 2)["result"] == "fail"
    assert MODULE.analyze(payload, 3)["result"] == "fail"
    print("console OS runtime capture tests passed")


if __name__ == "__main__":
    main()
