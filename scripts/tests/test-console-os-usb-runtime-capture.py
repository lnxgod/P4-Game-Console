#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/capture-console-os-usb-runtime.py"
SPEC = importlib.util.spec_from_file_location("console_usb_capture", PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def line(marker: bytes) -> bytes:
    return b"I (100) test: " + marker + b"\r\n"


def storage_ready(generation: int = 1) -> bytes:
    return line(
        b"P4_CONSOLE_OS GAME_STORAGE state=app-ready usb_attached=0 "
        b"generation=" + str(generation).encode() +
        b" capacity=9310208 last_error=ESP_OK"
    )


def stats(loops: int, polls: int, generation: int = 1) -> bytes:
    return line(
        b"P4_CONSOLE_OS STATS loops=" + str(loops).encode() +
        b" page=0 renders=1 touch_ready=1 touch_polls=" +
        str(polls).encode() +
        b" touch_failures=0 display_submits=1 display_completions=1 "
        b"display_timeouts=0 display_failures=0 amp_energized=0 "
        b"doom_handoffs=0 storage=app-ready storage_generation=" +
        str(generation).encode() + b" usb_attached=0"
    )


def passing_payload() -> bytes:
    return storage_ready() + b"".join(
        line(marker) for _name, marker in MODULE.FIXED
    ) + stats(300, 299) + stats(600, 599)


def main() -> None:
    payload = passing_payload()
    result = MODULE.analyze(payload, 2)
    assert result["result"] == "pass"
    assert result["storage_ready"] is True
    assert result["amp_energized"] is False
    assert result["usb_attached"] is False

    mutations = (
        payload.replace(b"state=app-ready", b"state=app-invalid", 1),
        payload.replace(b"usb_attached=0", b"usb_attached=1", 1),
        payload.replace(b"last_error=ESP_OK", b"last_error=ESP_FAIL", 1),
        payload.replace(b"touch_failures=0", b"touch_failures=1", 1),
        payload.replace(b"storage=app-ready", b"storage=usb-host", 1),
        payload.replace(b"doom_handoffs=0", b"doom_handoffs=1", 1),
        payload + line(b"P4_CONSOLE_OS HANDOFF_BEGIN app=doom"),
        payload.replace(line(MODULE.TOUCH_READY), b""),
        stats(300, 299) + storage_ready() + b"".join(
            line(marker) for _name, marker in MODULE.FIXED
        ) + stats(600, 599),
    )
    for mutated in mutations:
        assert MODULE.analyze(mutated, 2)["result"] == "fail"
    assert MODULE.analyze(payload, 3)["result"] == "fail"
    print("Console OS USB runtime capture tests passed")


if __name__ == "__main__":
    main()
