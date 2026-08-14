#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import pathlib
import re


APP = pathlib.Path(__file__).resolve().parents[1]
ROOT = APP.parents[1]
RUNTIME_BASIS = ROOT / "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json"
BOARD_PROFILE = ROOT / "hardware/board-profile.json"
RUNTIME_BASIS_SHA256 = "eaba0e61ccfd8c11e95b688709358bf13c47554421840cb8d8a8ced4b5c6c416"
BOARD_PROFILE_SHA256 = "f5c38e1aceabc633aaf78b844f8b1f5f1f82d25adbf75291479c57d7c1fe2701"
FACTORY_AUDIO_HEADER_SHA256 = "4fb0957057a66b61c1b3e0c20eee86025e0ca5c2739e440231697d85d4ad1780"
FACTORY_AUDIO_SOURCE_SHA256 = "75aa8d1a388bd0d999844a360af2adc989893cee2ea9f406cb90a9f0b549c725"
FACTORY_AUDIO_POLICY_SHA256 = "83543ee750b5128b7ece1566fb08021d5b665153108a791cb1b0b76d9f230dfd"
FACTORY_AUDIO_POLICY_HEADER_SHA256 = "cc70be430cd3d384efb7fc9587a7c2ea5ad14ee62ffd009a64c3ee969a882b65"
ADAPTER_HEADER_SHA256 = "0f1706d29cc6a3b0f0d8a9543c5da88fe695e0f22af943af7c20980e79ff738b"
ADAPTER_SOURCE_SHA256 = "fe3b4d04a495958b6ac9bfb2479dc6c4aa5c7815d6934d6745e2a49027a0222e"


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    inventory = json.loads((APP / "architecture-inventory.json").read_text())
    assert inventory["schema"] == 1
    for record in inventory["baseline_apps"].values():
        baseline = ROOT / "apps" / record["app"] / "main"
        assert sha256(baseline / f"{record['app']}_main.c") == record["main_sha256"]
        assert sha256(baseline / "CMakeLists.txt") == record["main_cmake_sha256"]

    assert sha256(RUNTIME_BASIS) == RUNTIME_BASIS_SHA256
    assert sha256(BOARD_PROFILE) == BOARD_PROFILE_SHA256
    basis = json.loads(RUNTIME_BASIS.read_text())
    profile = json.loads(BOARD_PROFILE.read_text())
    assert basis["result"] == "touch-runtime-authorized-audio-implementation-basis-only"
    assert basis["touch_authorization_decision"]["authorized"] is True
    assert basis["audio_authorization_decision"]["authorized"] is False
    assert basis["audio_authorization_decision"]["required_runtime_gate"] == 0
    assert basis["audio_authorization_decision"]["required_gpio30_access"] is False
    assert profile["pin_map_authorized"] is False
    assert profile["peripheral_authorizations"]["touch"]["authorized"] is True
    assert profile["peripheral_authorizations"]["audio"]["authorized"] is False

    factory = ROOT / "components/platform_audio_factory"
    adapter = APP / "components/platform_audio"
    assert sha256(factory / "include/platform_audio_factory/audio.h") == FACTORY_AUDIO_HEADER_SHA256
    assert sha256(factory / "src/platform_audio_factory.c") == FACTORY_AUDIO_SOURCE_SHA256
    assert sha256(factory / "src/platform_audio_factory_policy.c") == FACTORY_AUDIO_POLICY_SHA256
    assert sha256(factory / "src/platform_audio_factory_policy.h") == FACTORY_AUDIO_POLICY_HEADER_SHA256
    assert sha256(adapter / "include/platform/audio.h") == ADAPTER_HEADER_SHA256
    assert sha256(adapter / "src/platform_audio_adapter.c") == ADAPTER_SOURCE_SHA256

    metadata = json.loads((APP / "app-metadata.json").read_text())
    assert metadata["app"] == "doom_embedded_touch_audio"
    assert metadata["stage"] == "E6-sound-successor-build-only-electrical-release-pending"
    assert metadata["runtime_supported"] is False
    assert metadata["top_level_runtime_authorized"] is False
    assert metadata["touch_runtime_authorized"] is True
    assert metadata["audio_runtime_authorized"] is False
    for key in (
        "flash_authorized",
        "flash_app_authorized",
        "flash_project_authorized",
        "game_data_redistribution_authorized",
    ):
        assert metadata[key] is False
    assert metadata["successor_mode"]["required_exact_gates"] == {
        "composite": 1, "touch": 1, "audio": 1
    }
    assert metadata["successor_mode"]["artifact_execution_authorized"] is False
    assert metadata["successor_mode"]["first_audio_hardware_call"] == \
        "platform_audio_force_safe_shutdown"
    assert metadata["execution_contract"] == {
        "build_only": True,
        "app_flash_allowed": False,
        "project_flash_allowed": False,
        "hardware_access_allowed": False,
        "runtime_capture_allowed": False,
        "predecessor_remains_installed": True,
    }
    assert {"usb_host", "usb_hid", "platform_usb_host", "platform_gamepad_usb"} <= set(
        metadata["hardware_interfaces_explicitly_absent"]
    )

    graph_text = "\n".join(
        path.read_text()
        for path in (
            APP / "CMakeLists.txt",
            APP / "main" / "CMakeLists.txt",
            APP / "main" / "idf_component.yml",
        )
    )
    for forbidden in (
        "platform_usb_host",
        "platform_gamepad_usb",
        "doom_gamepad_input",
        "usb_host_install",
        "hid_host_install",
        "platform_audio_es8311",
        "esp_codec_dev",
    ):
        assert forbidden not in graph_text

    gate_source = (APP / "main" / "runtime_gate.c").read_text()
    assert re.search(r"s_composite_authorized = 1U;", gate_source)
    assert re.search(r"s_touch_authorized = 1U;", gate_source)
    assert re.search(r"s_audio_authorized = 1U;", gate_source)

    main_source = (APP / "main" / "doom_embedded_touch_audio_main.c").read_text()
    mode_branch = re.search(
        r"s_audio_gate_enabled\s*=\s*\n?\s*mode\s*==\s*"
        r"DOOM_TOUCH_AUDIO_RUNTIME_TOUCH_AND_AUDIO;(?P<body>.*?)"
        r"esp_err_t result = platform_display_init\(\);",
        main_source,
        re.DOTALL,
    )
    assert mode_branch is not None
    assert "platform_audio_force_safe_shutdown" in mode_branch.group("body")
    first_safe = main_source.index("platform_audio_force_safe_shutdown", mode_branch.start())
    first_display = main_source.index("platform_display_init", first_safe)
    assert first_safe < first_display
    assert "platform_audio_factory_" not in main_source
    assert "P4_DOOM_E6 START input=gt911-multitouch" in main_source
    assert "sound=factory-complete-i2s0-pdm-rx-i2s1-speaker-tx-sfx" in main_source
    assert '"runtime=exact-unit-factory-audio");' in main_source
    assert "SOUND_BOUND backend=factory-complete-audio-init" in main_source
    assert '"sfx_request=%s music=disabled usb=absent",' in main_source
    assert '"sfx_request=%s music=disabled usb=absent ",' not in main_source
    assert '"pdm_i2s_port=0 pdm_clk_gpio=24 pdm_clk_hz=1024000 "' in main_source
    assert '"pdm_din_gpio=26 gpio24_may_feed_codec_mclk=1 "' in main_source
    assert '"speaker_i2s_port=1 rate_hz=%u format=pcm16-stereo channels=%u "' in main_source
    assert '"lrclk_gpio=21 bclk_gpio=22 dout_gpio=23 tx_mclk=none "' in main_source
    assert '"codec_i2c_transactions=0 required_startup_zero_ms=350 "' in main_source
    assert '"backend_volume_step=10/10 gain=unity-no-amplification "' in main_source
    assert '" composite_gate=%u touch_gate=%u audio_gate=%u"' in main_source
    assert '" audio_mutating_calls=%" PRIu32' in main_source
    assert '" audio_telemetry_snapshot_valid=%u audio_start_proof=%s"' in main_source
    assert '" zero_preload_frames=%" PRIu32' in main_source
    assert '" measured_settle_us=%" PRIu32' in main_source
    assert '" backend_nonzero_frames=%" PRIu32' in main_source
    assert '" backend_max_abs=%" PRIu32' in main_source
    stats_format_start = main_source.index('"P4_DOOM_E6 STATS frames=')
    stats_format_end = main_source.index(
        ",\n             s_frame_count", stats_format_start
    )
    stats_format = main_source[stats_format_start:stats_format_end]
    for stats_key in (
        "frames", "submits", "completions", "video_timeouts",
        "video_failures", "touch_polls", "touch_failures", "touch_retries",
        "composite_gate", "touch_gate", "audio_gate",
        "audio_mutating_calls", "audio_telemetry_snapshot_valid",
        "audio_start_proof", "audio_write_calls", "audio_frames_forwarded",
        "audio_nonzero_frames", "audio_nonzero_samples", "audio_peak",
        "backend_telemetry_valid", "backend_snapshot_sequence",
        "backend_gpio30_high_attempts", "backend_gpio30_high_successes",
        "backend_gpio30_high_readbacks", "backend_state", "backend_running",
        "pdm_created", "pdm_enabled", "pdm_create_successes",
        "pdm_enable_successes", "tx_created", "tx_enabled",
        "tx_create_successes", "tx_enable_successes", "zero_preload_frames",
        "gpio30_low_attempts", "gpio30_low_successes",
        "gpio30_low_initial_readbacks", "measured_settle_us",
        "gpio30_low_second_readbacks", "backend_write_successes",
        "backend_write_failures", "backend_frames_written",
        "backend_samples_written", "backend_nonzero_frames",
        "backend_nonzero_samples", "backend_max_abs",
        "backend_rollback_attempts", "backend_rollback_successes",
        "backend_rollback_high_proofs", "backend_resources_retained",
        "backend_resources_owned", "audio_frames", "audio_write_failures",
    ):
        assert stats_format.count(f" {stats_key}=") == 1, stats_key
    assert "platform_audio_invocation_count()" in main_source
    assert "platform_audio_get_state" not in main_source
    assert "AUDIO_SAFETY_FAULT" in main_source
    blocked_end = main_source.index("return;", main_source.index("DOOM_TOUCH_AUDIO_RUNTIME_BLOCKED"))
    mode_marker = main_source.index(
        'P4_DOOM_E6 MODE composite_gate=%u touch_gate=%u audio_gate=%u '
    )
    first_peripheral = main_source.index("doom_touch_input_init", mode_marker)
    assert blocked_end < mode_marker < first_peripheral


if __name__ == "__main__":
    main()
