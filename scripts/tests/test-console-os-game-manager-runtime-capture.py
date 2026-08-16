#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/capture-console-os-game-manager-runtime.py"
SPEC = importlib.util.spec_from_file_location("console_game_manager_capture", PATH)
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


def catalog(packages: int = 2, valid: int = 2) -> bytes:
    return line(
        b"P4_CONSOLE_OS GAME_CATALOG available=1 packages=" +
        str(packages).encode() + b" valid=" + str(valid).encode() +
        b" omitted=0 generation=1 update=0 result=ESP_OK"
    )


def start(apps: int = 9) -> bytes:
    return line(
        b"P4_CONSOLE_OS START shell=freertos-native apps=" +
        str(apps).encode() +
        b" surface=rgb565-320x200 touch=gt911 native_game_api=1 "
        b"native_format=p4-native-elf-v1 game_storage=app-ready "
        b"execution=build-candidate"
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
    return (
        storage_ready() + catalog() + start() +
        b"".join(line(marker) for _name, marker in MODULE.FIXED) +
        stats(300, 299) + stats(600, 599)
    )


def main() -> None:
    payload = passing_payload()
    result = MODULE.analyze(payload, 2)
    assert result["result"] == "pass"
    assert result["valid_package_count"] == 2
    assert result["registered_apps"] == 9
    assert MODULE.analyze(
        storage_ready() + catalog(0, 0) + start(7) +
        b"".join(line(marker) for _name, marker in MODULE.FIXED) +
        stats(300, 299) + stats(600, 599),
        2,
    )["result"] == "pass"

    mutations = (
        payload.replace(b"apps=9", b"apps=8", 1),
        payload.replace(b"valid=2", b"valid=3", 1),
        payload.replace(b"native_format=p4-native-elf-v1",
                        b"native_format=p4-native-static-v1", 1),
        payload.replace(b"OTA_BOOT_VALID result=ESP_OK",
                        b"OTA_BOOT_VALID result=ESP_FAIL", 1),
        payload.replace(b"touch_failures=0", b"touch_failures=1", 1),
        payload + line(b"P4_CONSOLE_OS HALT stage=test error=ESP_FAIL"),
        stats(300, 299) + payload,
    )
    for mutated in mutations:
        assert MODULE.analyze(mutated, 2)["result"] == "fail"
    assert MODULE.analyze(payload, 3)["result"] == "fail"
    print("Console OS Game Manager runtime capture tests passed")


if __name__ == "__main__":
    main()
