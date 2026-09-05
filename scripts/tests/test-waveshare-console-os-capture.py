#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
import os
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
PATH = ROOT / "scripts/capture-waveshare-console-os-0.4.84.py"


def load(name: str, require_gt911: bool, require_tune: bool = False, **settings):
    if require_gt911:
        os.environ["P4_CAPTURE_REQUIRE_GT911_CONFIG"] = "1"
    else:
        os.environ.pop("P4_CAPTURE_REQUIRE_GT911_CONFIG", None)
    if require_tune:
        os.environ["P4_CAPTURE_REQUIRE_GT911_TUNE"] = "1"
    else:
        os.environ.pop("P4_CAPTURE_REQUIRE_GT911_TUNE", None)
    for key, value in {
        "P4_CAPTURE_REQUIRE_GT911_RESTORE": "0",
        "P4_CAPTURE_REQUIRE_RUNTIME_STATS": "1",
        "P4_CAPTURE_FORBID_RUNTIME_STATS": "0",
        "P4_CAPTURE_MIN_SECONDS": "0",
        **settings,
    }.items():
        if value is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = str(value)
    spec = importlib.util.spec_from_file_location(name, PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def line(marker: bytes) -> bytes:
    return b"I (100) test: " + marker + b"\r\n"


def stats() -> bytes:
    return line(
        b"P4_CONSOLE_OS STATS usb_input_ready=1 audio=es8311-ready "
        b"storage=app-ready p4cart_rejected=0 display_submits=1 "
        b"display_completions=1 display_timeouts=0 display_failures=0 "
        b"display_accel_failures=0 display_accelerated=1"
    )


def tune(action: bytes = b"applied") -> bytes:
    if action == b"applied":
        state = (
            b"before_filter=8 before_checksum=0x79 "
            b"after_filter=4 after_checksum=0x7d "
            b"changed=1 may_have_changed=1"
        )
    else:
        state = (
            b"before_filter=4 before_checksum=0x7d "
            b"after_filter=4 after_checksum=0x7d "
            b"changed=0 may_have_changed=0"
        )
    return line(
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=ready action=" + action +
        b" vendor=0x05 " + state +
        b" restore_attempted=0 restored=0 "
        b"restore_result=ESP_ERR_INVALID_STATE apply_result=ESP_OK"
    )


def restore(action: bytes = b"restored") -> bytes:
    if action == b"restored":
        state = (
            b"before_filter=4 before_checksum=0x7d "
            b"after_filter=8 after_checksum=0x79 "
            b"changed=1 already_original=0 may_have_changed=1"
        )
    else:
        state = (
            b"before_filter=8 before_checksum=0x79 "
            b"after_filter=8 after_checksum=0x79 "
            b"changed=0 already_original=1 may_have_changed=0"
        )
    return line(
        b"P4_CONSOLE_OS GT911_CONFIG_RESTORE target_normal_filter=8 "
        b"result=ready action=" + action + b" vendor=0x05 " + state +
        b" restore_result=ESP_OK"
    )


def payload(module, include_gt911: bool, include_tune: bool = False) -> bytes:
    markers = list(module.REQUIRED.items())
    if not include_gt911:
        markers = [item for item in markers if item[0] != "gt911_config"]
    markers = [item for item in markers if item[0] != "gt911_tune"]
    tune_marker = tune() if include_tune else b""
    return (b"".join(line(marker) for _name, marker in markers) + tune_marker +
            stats() + stats())


def payload_without_stats(module) -> bytes:
    markers = list(module.REQUIRED.items())
    return b"".join(line(marker) for _name, marker in markers)


def main() -> None:
    legacy = load("waveshare_capture_legacy", False)
    assert "gt911_config" not in legacy.REQUIRED
    assert "gt911_tune" not in legacy.REQUIRED
    legacy_result = legacy.analyze(payload(legacy, False))
    assert legacy_result["result"] == "pass"
    assert "gt911_tune_valid" not in legacy_result
    assert "gt911_tune_lines" not in legacy_result

    configured = load("waveshare_capture_gt911", True)
    assert configured.REQUIRE_GT911_CONFIG is True
    assert configured.analyze(payload(configured, True))["result"] == "pass"
    assert configured.analyze(payload(configured, False))["result"] == "fail"
    failed_read = payload(configured, False) + line(
        b"P4_CONSOLE_OS GT911_CONFIG mode=read-only result=error error=ESP_FAIL"
    )
    assert configured.analyze(failed_read)["result"] == "fail"

    tuned = load("waveshare_capture_gt911_tune", False, True)
    assert tuned.REQUIRE_GT911_TUNE is True
    assert "gt911_tune" in tuned.REQUIRED
    assert tuned.analyze(payload(tuned, False, True))["result"] == "pass"
    already_target = payload(tuned, False, False) + tune(b"already-target")
    assert tuned.analyze(already_target)["result"] == "pass"
    assert tuned.analyze(payload(tuned, False, False))["result"] == "fail"
    failed_tune = payload(tuned, False, False) + line(
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=not-ready action=error "
        b"apply_result=ESP_FAIL"
    )
    assert tuned.analyze(failed_tune)["result"] == "fail"
    mixed_tune = payload(tuned, False, True) + line(
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=not-ready action=error "
        b"apply_result=ESP_FAIL"
    )
    assert tuned.analyze(mixed_tune)["result"] == "fail"
    malformed_ready = payload(tuned, False, False) + line(
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=ready action=error vendor=0x05 "
        b"before_filter=8 before_checksum=0x79 "
        b"after_filter=4 after_checksum=0x7d changed=1 may_have_changed=1 "
        b"restore_attempted=0 restored=0 "
        b"restore_result=ESP_ERR_INVALID_STATE apply_result=ESP_OK"
    )
    assert tuned.analyze(malformed_ready)["result"] == "fail"
    incomplete_ready = payload(tuned, False, False) + line(
        b"P4_CONSOLE_OS GT911_CONFIG_TUNE "
        b"target_normal_filter=4 result=ready action=applied"
    )
    assert tuned.analyze(incomplete_ready)["result"] == "fail"
    inconsistent_apply = payload(tuned, False, False) + tune().replace(
        b"changed=1 may_have_changed=1", b"changed=0 may_have_changed=0"
    )
    assert tuned.analyze(inconsistent_apply)["result"] == "fail"
    double_tune = payload(tuned, False, True) + tune(b"already-target")
    double_result = tuned.analyze(double_tune)
    assert double_result["result"] == "fail"
    assert double_result["gt911_tune_lines"] == 2

    restored = load("waveshare_capture_gt911_restore", False,
                    P4_CAPTURE_REQUIRE_GT911_RESTORE="1")
    assert restored.analyze(payload(restored, False) + restore())["result"] == "pass"
    assert restored.analyze(payload(restored, False) + restore(b"already-original"))["result"] == "pass"
    assert restored.analyze(payload(restored, False))["result"] == "fail"
    assert restored.analyze(payload(restored, False) + restore() + restore())["result"] == "fail"
    assert restored.analyze(payload(restored, False) + restore().replace(
        b"restore_result=ESP_OK", b"restore_result=ESP_FAIL"))["result"] == "fail"

    forbid = load("waveshare_capture_no_stats", False,
                  P4_CAPTURE_REQUIRE_RUNTIME_STATS="0",
                  P4_CAPTURE_FORBID_RUNTIME_STATS="1")
    assert forbid.analyze(payload_without_stats(forbid))["result"] == "pass"
    assert forbid.analyze(payload(forbid, False))["result"] == "fail"
    os.environ["P4_CAPTURE_REQUIRE_RUNTIME_STATS"] = "1"
    try:
        load("waveshare_capture_incompatible", False,
             P4_CAPTURE_FORBID_RUNTIME_STATS="1")
    except RuntimeError as error:
        assert "incompatible" in str(error)
    else:
        raise AssertionError("incompatible runtime stats modes accepted")
    minimum = load("waveshare_capture_minimum", False,
                   P4_CAPTURE_MIN_SECONDS="2.5")
    assert minimum.MIN_SECONDS == 2.5
    print("Waveshare Console OS capture GT911 marker tests passed")


if __name__ == "__main__":
    main()
