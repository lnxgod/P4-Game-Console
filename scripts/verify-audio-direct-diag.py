#!/usr/bin/env python3

"""Fail-closed exact-image gate for the reusable D2.3 direct-audio probe."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/audio_direct_diag"
BUILD_EVIDENCE_REL = "test-runs/2026-08-13-audio-d23-build.json"
AUTHORIZATION_REL = (
    "hardware/evidence/audio-direct-diag-d23-one-shot-authorization-attempt2.json"
)
ATTEMPT1_AUTHORIZATION_REL = (
    "hardware/evidence/audio-direct-diag-d23-one-shot-authorization.json"
)
ATTEMPT1_AUTHORIZATION_SHA256 = (
    "368d8a7028d30d4dcd6f2ac76121efde511634380a75a693b3ce9c0d9aeebd76"
)
ATTEMPT1_FAILURE_REL = (
    "hardware/test-runs/2026-08-13-audio-direct-d23-attempt1.json"
)
ATTEMPT1_FAILURE_SHA256 = (
    "ffdd3c0e63950081c671d8e158dda4810af567c8695e5f014be466a430f0e78d"
)
ATTEMPT2_LEDGER_REL = "hardware/local-state/audio-direct-d23-attempt2.json"
FACTORY_EVIDENCE_REL = (
    "hardware/evidence/elecrow-10.1-factory-audio-semantics.json"
)
ELECTRICAL_EVIDENCE_REL = (
    "hardware/evidence/elecrow-10.1-audio-driver-path.json"
)
PRIOR_RUNTIME_REL = "hardware/test-runs/2026-08-12-audio-diag-d22.json"
AUTHORIZATION_SHA256 = (
    "1c52e293ac67249a20f19c68e7e02b013d99cad742e8556f7a9a2457cd13ad9a"
)
ANALYZER_REL = "scripts/analyze-audio-tone.py"
ANALYZER_SHA256 = "1126a0f2f556a9ffefef8cc2eaa11ea1bc994c3a8d5d51d16d76f218d39571f0"
CAPTURE_CLI_REL = "scripts/capture-audio-direct-diag.py"
CAPTURE_CLI_SHA256 = "92944401d288e6f3bc90603bb85b7350e57afd48dca2f98e9189f9fbb00c4649"
CAPTURE_TRANSPORT_REL = "scripts/d23_capture_transport.py"
CAPTURE_TRANSPORT_SHA256 = "7e908368ae7fc3a9efb7e928ad52ee7c779b640127c3f81256957bc7e7fa950a"
CAPTURE_TRANSPORT_TEST_REL = "scripts/tests/test-d23-capture-transport.py"
CAPTURE_TRANSPORT_TEST_SHA256 = "95e74b0a4c4d78356f4ef061786ebddb6a0c873fbbd371563b17b7d12abcbcb4"
CENTRAL_FLASH_REL = "scripts/flash.sh"
CENTRAL_FLASH_SHA256 = "db9a3390d3c641f955ac91a096699c28ec1fa5b1bf46ae3e3285e561f6658fc2"
APP_OFFSET = 0x10000
APP_BYTES = 196896
APP_SHA256 = "6500b7a86d56ef4e007b6ac979c8cb81017eaca09c4e779a22fdb5286d635c60"
ELF_BYTES = 4422960
ELF_SHA256 = "c8b3f5c9d7d7337e09a8a163b3e3cac60fc0f775be5ecf3ff3dcb2e527525e80"
SDKCONFIG_SHA256 = "0a71ea77dd8a4ed09ab386fd0f2775c58eb0c9ebb3af311b42f33682edefc56b"
BOOTLOADER_BYTES = 22912
BOOTLOADER_SHA256 = (
    "fcb629f826b8cd79fb21533b8691650a775804f337db37451b03127e36a32ca9"
)
PARTITION_BYTES = 3072
PARTITION_SHA256 = (
    "7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820"
)
EXPECTED_INTERFACES = {
    "internal_ldo3_2500mv_ldo4_3300mv_standalone_test_ownership",
    "i2s1_gpio21_lrclk_gpio22_bclk_gpio23_dout_no_mclk",
    "gpio30_active_low_amplifier_shutdown_candidate",
    "speaker_output_bounded_440hz_tone",
}
EXPECTED_ABSENT = {
    "display",
    "touch",
    "sd_card",
    "i2c",
    "audio_codec",
    "mclk",
    "microphone_input",
    "camera",
    "wireless",
    "usb_host",
}
EXPECTED_SOURCE_INVENTORY = {
    "apps/audio_direct_diag/CMakeLists.txt": "b094817e8bb3bb439c344989cffb61560bba4dca6b9590089a3978a9425c02c1",
    "apps/audio_direct_diag/main/CMakeLists.txt": "5670c67c3e1717000691184e67e6baa37d5109008a0423e8deaa7a83fb400ee9",
    "apps/audio_direct_diag/main/audio_direct_diag_main.c": "b03023e699a11dfd8d035d3efceaf23fcc6e1cb9cc83533ef5a82d1f6badcaa5",
    "apps/audio_direct_diag/main/idf_component.yml": "12caf5b7509c0f6af70ad4324ee29db8268f678022ef5da49835678f9bc48bac",
    "apps/audio_direct_diag/sdkconfig.defaults": "12112f9b11df81a398b9d55cfb3cda7df7c2e48e341f1ce62b1d987d63541d85",
    "apps/audio_direct_diag/dependencies.lock": "e6ae2189715ed9d6a07ffda8eca0c5830dc92206611d37682b16ba78df6e72fc",
    "components/platform_audio/CMakeLists.txt": "398e59026062c128c549fcee088db1c0380e9fb8dabf707d0091c775bc7163b9",
    "components/platform_audio/Kconfig": "d92b6114da51923b7af3f0123e87d61973395ddf752a3fcb135a0b228fa15376",
    "components/platform_audio/include/platform/audio.h": "07098da697b236f65640e540443b0d6a4696ba6439ac637d34ab6ba6d1cc82c4",
    "components/platform_audio/include/platform/audio_tone.h": "90b8715f51f049cf8f73ce3dfc64a023f44bd7cc73a2d7f9011d300731434978",
    "components/platform_audio/src/platform_audio.c": "a80a5a0ade6c28f85f6e6252d1e4d1cd5d8ad17fb123d2b9b53eb7fc916f193a",
    "components/platform_audio/src/platform_audio_policy.c": "1da6ded17aa60cc9441c7b67801d4a32e178d98d38dca0891bebb204f0f18e9b",
    "components/platform_audio/src/platform_audio_policy.h": "8052e63f74b18199f4c8267e80dba096d78ecda7e2ce10a76a5202a788192ca3",
    "components/platform_audio/src/platform_audio_tone.c": "a1e482a8ef5d8b4c26ca391f3a8d9152956e21794a584728245eeed42ee6252c",
    "components/platform_audio/tests/CMakeLists.txt": "2720bcec6e6bc680db2e2b97149ae45f5ba11c0b954a936bc67c59ae610bfeba",
    "components/platform_audio/tests/test_platform_audio.c": "19b21866576c20cbcaa82c971a98cd185aca5fb2656fc02bde7c87c928647c6d",
    "components/platform_audio/tests/test_platform_audio_runtime.c": "c8ac326e28c4f5ba9eb750bdfbb3418e5b01bcf3937c360993e5fa9a30911dc9",
    "components/platform_audio/tests/mocks/driver/gpio.h": "5d93281644a82e43a474ba8ba7567767f0545b1d9790c5dc3b0620ad03326aed",
    "components/platform_audio/tests/mocks/driver/i2s_std.h": "b6714cd06cc81de0da7fde8bd99548fbcbf7c5d5f2285936820bf749a57d9b8e",
    "components/platform_audio/tests/mocks/esp_err.h": "4be4b9e70e51e383b02a61542fbc9b2ca947144848ef5e1389900069bd77c9c6",
    "components/platform_audio/tests/mocks/freertos/FreeRTOS.h": "f8efda89f80a980a3a574f83666794b021eed21b99a356ee787996408e6219f7",
    "components/platform_audio/tests/mocks/freertos/task.h": "151d90c44d210bbcf5bb5db9862f9d6fc2e0fdb104f17660464e5e4ca4a10de7",
    "components/platform_audio/tests/mocks/mock_esp32_runtime.c": "e9d24e5d86bb005cfd40f6420e3b368017dba69d3d2c345f0883fe186b18898c",
    "components/platform_audio/tests/mocks/mock_esp32_runtime.h": "b9accc3cfc429a0cda53caf4afc83a750190080605c286f8e8b2d5570d95780c",
    "components/platform_audio/tests/mocks/sdkconfig.h": "8c48e0d8ec477fe5b2f48ad16ef6f95391a5fb8bae4c0301f0382fe74457c46d",
}


def fail(message: str) -> None:
    raise SystemExit(f"audio_direct_diag verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def checked_child(directory: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    candidate = (directory / relative).resolve()
    require(candidate.is_relative_to(directory), f"{label} leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def ordered(text: str, tokens: tuple[str, ...], message: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        require(position >= 0, f"{message}: missing {token!r}")
        cursor = position + len(token)


def verify_source_inventory(evidence: dict) -> None:
    recorded = evidence.get("source_inventory")
    require(recorded == EXPECTED_SOURCE_INVENTORY, "recorded source inventory changed")
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT), f"source leaves repository: {relative}")
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source changed: {relative}")


def verify_host_tool_inventory(evidence: dict) -> None:
    expected = {
        CAPTURE_CLI_REL: CAPTURE_CLI_SHA256,
        CAPTURE_TRANSPORT_REL: CAPTURE_TRANSPORT_SHA256,
        CAPTURE_TRANSPORT_TEST_REL: CAPTURE_TRANSPORT_TEST_SHA256,
        ANALYZER_REL: ANALYZER_SHA256,
        CENTRAL_FLASH_REL: CENTRAL_FLASH_SHA256,
    }
    require(
        evidence.get("host_tool_inventory") == expected,
        "recorded D2.3 host-tool inventory changed",
    )
    for relative, digest in expected.items():
        path = ROOT / relative
        require(path.is_file() and sha256_file(path) == digest,
                f"frozen D2.3 host tool changed: {relative}")


def verify_source_contract() -> None:
    app = (APP_DIR / "main/audio_direct_diag_main.c").read_text()
    backend = (ROOT / "components/platform_audio/src/platform_audio.c").read_text()
    policy = (ROOT / "components/platform_audio/src/platform_audio_policy.h").read_text()
    policy_source = (
        ROOT / "components/platform_audio/src/platform_audio_policy.c"
    ).read_text()
    public = (ROOT / "components/platform_audio/include/platform/audio.h").read_text()

    app_required = (
        "#define AUDIO_DIRECT_DIAG_CAPTURE_ARM_MS 5000U",
        "#define AUDIO_DIRECT_DIAG_SERIAL_ATTACH_MS 2000U",
        "#define AUDIO_DIRECT_DIAG_HOST_ARM_TIMEOUT_MS 15000U",
        "#define AUDIO_DIRECT_DIAG_HOST_ARM_DUPLICATE_GUARD_MS 100U",
        "#define AUDIO_DIRECT_DIAG_TONE_HZ 440U",
        "#define AUDIO_DIRECT_DIAG_TONE_MS 400U",
        "#define AUDIO_DIRECT_DIAG_FADE_MS 50U",
        "#define AUDIO_DIRECT_DIAG_TONE_FRAMES 6400U",
        "#define AUDIO_DIRECT_DIAG_CHUNK_FRAMES 128U",
        "#define AUDIO_DIRECT_DIAG_TONE_CHUNKS 50U",
        "#define AUDIO_DIRECT_DIAG_DMA_DESC_COUNT 6U",
        "#define AUDIO_DIRECT_DIAG_DMA_FRAMES 256U",
        ".peak_amplitude = (uint16_t)INT16_MAX",
        ".control_bus = NULL",
        ".volume_percent = PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT",
        "P4_AUDIO_D23_ARM ",
        "P4_AUDIO D2.3 SERIAL_ATTACH wait_ms=%u complete=1 rails=off",
        "P4_AUDIO D2.3 WAIT_ARM auth=%s timeout_ms=%u rails=off",
        "P4_AUDIO D2.3 ARM_ACCEPTED auth=%s nonce=%s tx_count=1",
        "P4_AUDIO D2.3 PASS backend=platform_audio tone_count=1",
    )
    require(all(token in app for token in app_required), "D2.3 bounded tone contract changed")
    app_main = app[app.index("void app_main(void)") :]
    ordered(
        app_main,
        (
            "platform_audio_force_safe_shutdown()",
            "vTaskDelay(pdMS_TO_TICKS(AUDIO_DIRECT_DIAG_SERIAL_ATTACH_MS))",
            "P4_AUDIO D2.3 SERIAL_ATTACH",
            "wait_for_host_arm(arm_nonce, sizeof(arm_nonce))",
            "P4_AUDIO D2.3 ARM_ACCEPTED",
            "P4_AUDIO D2.3 START",
            "acquire_board_power()",
            "platform_audio_create(&audio_config, &s_audio)",
            "P4_AUDIO D2.3 CAPTURE_ARM",
            "platform_audio_start(s_audio)",
            "write_tone()",
            "platform_audio_stop(s_audio)",
            "platform_audio_destroy(&s_audio)",
            "platform_audio_recover()",
            "release_board_power()",
            "P4_AUDIO D2.3 PASS",
        ),
        "D2.3 startup/tone/shutdown order changed",
    )
    pre_arm = app_main[:app_main.index("P4_AUDIO D2.3 ARM_ACCEPTED")]
    require(
        "acquire_board_power()" not in pre_arm
        and "platform_audio_create(" not in pre_arm
        and "platform_audio_start(" not in pre_arm,
        "D2.3 can energize audio before the exact host ARM frame",
    )
    wait_arm = app[app.index("static esp_err_t wait_for_host_arm"):
                   app.index("static esp_err_t acquire_board_power")]
    require(
        "while (esp_rom_output_rx_one_char(&discarded) == 0)" in wait_arm
        and "received < 0x20U || received > 0x7eU" in wait_arm
        and "used + 1U >= sizeof(line)" in wait_arm
        and "memcmp(line, s_host_arm_line" in wait_arm
        and "AUDIO_DIRECT_DIAG_HOST_ARM_DUPLICATE_GUARD_MS" in wait_arm,
        "D2.3 host ARM parser is not exact, bounded, and duplicate-rejecting",
    )
    release = app[app.index("static esp_err_t release_board_power") :]
    release = release[: release.index("static esp_err_t cleanup_runtime")]
    require(release.index("s_ldo4") < release.index("s_ldo3"), "LDO release order changed")
    halt = app[app.index("static void halt_safe") : app.index("static esp_err_t write_tone")]
    require(
        "for (;;)" in halt
        and "vTaskDelay(pdMS_TO_TICKS(1000U))" in halt
        and halt.count("cleanup_runtime()") == 2
        and "if (s_audio != NULL || s_power_owned)" not in halt
        and "P4_AUDIO D2.3 CLEANUP_RETRY" in halt,
        "HALT no longer retries safe cleanup unconditionally",
    )

    backend_required = (
        "static platform_audio_t *s_failed_create_owner;",
        "static platform_audio_t *s_live_owner;",
        ".mclk = I2S_GPIO_UNUSED",
        "channel_config.dma_desc_num = AUDIO_DMA_DESCRIPTOR_COUNT",
        "channel_config.dma_frame_num = AUDIO_DMA_FRAMES_PER_DESCRIPTOR",
        "channel_config.auto_clear = true",
        "i2s_channel_preload_data(",
        "loaded_bytes != requested_bytes",
        "i2s_channel_write(",
        "bytes_written == byte_count ? ESP_OK : ESP_ERR_INVALID_RESPONSE",
        "s_failed_create_owner = audio",
        "platform_audio_attenuate_pcm16(",
        "reprime_zero_dma_ring_amp_off(audio)",
        "instance != s_live_owner",
        "s_live_owner = NULL",
    )
    require(all(token in backend for token in backend_required), "reusable backend contract changed")
    ordered(
        backend[backend.index("esp_err_t platform_audio_destroy") :],
        (
            "platform_audio_force_safe_shutdown()",
            "release_i2s(instance)",
            "s_live_owner = NULL",
            "free(instance)",
            "*audio = NULL",
        ),
        "destroy no longer retains ownership until safe cleanup succeeds",
    )
    policy_required = (
        "PLATFORM_AUDIO_I2S_CONTROLLER = 1",
        "PLATFORM_AUDIO_GPIO_LRCLK = 21",
        "PLATFORM_AUDIO_GPIO_BCLK = 22",
        "PLATFORM_AUDIO_GPIO_DOUT = 23",
        "PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN = 30",
        "PLATFORM_AUDIO_DMA_DESCRIPTOR_COUNT = 6",
        "PLATFORM_AUDIO_DMA_FRAMES_PER_DESCRIPTOR = 256",
        "PLATFORM_AUDIO_ZERO_PREROLL_FRAMES = 1536",
        "PLATFORM_AUDIO_WRITE_TIMEOUT_MS = 100",
    )
    require(all(token in policy for token in policy_required), "direct-I2S policy changed")
    require(
        "UINT32_C(512) * (uint32_t)volume_percent" in policy_source
        and "ranges_overlap(input, output, sample_count)" in policy_source
        and "input[index]" in policy_source,
        "immutable capped attenuation changed",
    )
    require(
        "PLATFORM_AUDIO_MAX_OUTPUT_PEAK 512U" in public
        and "PLATFORM_AUDIO_MAX_BRINGUP_VOLUME_PERCENT 10U" in public
        and "PLATFORM_AUDIO_MAX_WRITE_FRAMES 128U" in public,
        "public safety limits changed",
    )
    forbidden_apis = (
        "i2c_new_master_bus",
        "esp_codec_dev_",
        "es8311_",
        "esp_lcd_",
        "sdmmc_host_",
        "usb_host_",
        "esp_wifi_",
    )
    executable = app + backend
    require(not any(token in executable for token in forbidden_apis), "out-of-scope API entered D2.3")
    require(
        "CONFIG_ESPTOOLPY_AFTER_NORESET=y" in (APP_DIR / "sdkconfig.defaults").read_text(),
        "D2.3 would launch before exact readback",
    )


def verify_authorization(binary: pathlib.Path, evidence: dict) -> None:
    path = ROOT / AUTHORIZATION_REL
    require(path.is_file(), "D2.3 one-shot authorization record is missing")
    require(sha256_file(path) == AUTHORIZATION_SHA256, "D2.3 authorization record changed")
    auth = load_json(path)
    require(
        auth.get("schema") == 1
        and auth.get("attempt") == 2
        and auth.get("classification") == "user-requested-bounded-reusable-audio-one-shot"
        and auth.get("result") == "authorized-exact-app-preflash-reviewed"
        and auth.get("scope")
        == "audio_direct_diag_d2_3_single_bounded_service_tone_on_connected_unit_attempt2",
        "D2.3 authorization classification changed",
    )
    attempt1_auth = ROOT / ATTEMPT1_AUTHORIZATION_REL
    attempt1_failure = ROOT / ATTEMPT1_FAILURE_REL
    require(
        attempt1_auth.is_file()
        and sha256_file(attempt1_auth) == ATTEMPT1_AUTHORIZATION_SHA256
        and attempt1_failure.is_file()
        and sha256_file(attempt1_failure) == ATTEMPT1_FAILURE_SHA256
        and auth.get("previous_attempt") == {
            "attempt": 1,
            "authorization_record": ATTEMPT1_AUTHORIZATION_REL,
            "authorization_sha256": ATTEMPT1_AUTHORIZATION_SHA256,
            "failure_evidence": ATTEMPT1_FAILURE_REL,
            "failure_evidence_sha256": ATTEMPT1_FAILURE_SHA256,
            "result": "fail-safe-no-tone",
            "receipt_consumed_once": True,
            "authorization_revoked": True,
            "host_arm_transmitted": False,
            "tone_played": False,
        },
        "attempt-1 terminal safe failure is not preserved exactly",
    )
    failure = load_json(attempt1_failure)
    require(
        failure.get("result") == "fail-safe-no-tone"
        and failure.get("authorization", {}).get("consumed_once") is True
        and failure.get("authorization", {}).get("revoked_after_failure") is True
        and failure.get("transport", {}).get("host_arm_frame_transmitted") is False
        and failure.get("safety_observation", {}).get("tone_played") is False,
        "attempt-1 failure evidence no longer proves terminal no-ARM/no-tone",
    )
    require(
        auth.get("source_evidence")
        == {
            "factory_semantics": FACTORY_EVIDENCE_REL,
            "prior_serial_run": PRIOR_RUNTIME_REL,
            "build_evidence": BUILD_EVIDENCE_REL,
        },
        "D2.3 authorization provenance changed",
    )
    require(
        auth.get("exact_artifact")
        == {"offset": "0x10000", "bytes": APP_BYTES, "sha256": APP_SHA256}
        and binary.stat().st_size == APP_BYTES
        and sha256_file(binary) == APP_SHA256,
        "D2.3 authorization is not bound to this exact application",
    )
    require(
        auth.get("exact_host_arm")
        == {
            "authorization_id": auth.get("id"),
            "nonce_derivation": "sha256-ascii-authorization-id-space-host-arm-space-v1",
            "nonce": "0c47666f7da543e93831e0dfdcb7fecd6bb9887f0d0d0c72ee4422826b573562",
            "frame_bytes": 138,
            "frame_sha256": "cce24d670298f6d62755d240d14f928c212d82fea6246517eb8edaf48f0fade9",
            "line_ending": "LF-only",
            "silent_force_safe_delay_ms": 2000,
            "arm_timeout_ms": 15000,
            "duplicate_guard_ms": 100,
            "rails_off_until_arm_accepted": True,
            "amplifier_shutdown_until_backend_start": True,
            "no_missing_malformed_duplicate_or_timed_out_frame_may_energize_audio": True,
        },
        "D2.3 fixed receipt-bound host ARM contract changed",
    )
    require(
        auth.get("exact_runtime_limits")
        == {
            "i2s_controller": 1,
            "lrclk_gpio": 21,
            "bclk_gpio": 22,
            "dout_gpio": 23,
            "mclk": "unused",
            "sample_rate_hz": 16000,
            "channels": 2,
            "sample_bits": 16,
            "dma_descriptor_count": 6,
            "dma_frames_per_descriptor": 256,
            "public_write_frames": 128,
            "write_timeout_ms": 100,
            "tone_count": 1,
            "frequency_hz": 440,
            "duration_ms": 400,
            "fade_ms": 50,
            "input_pcm_peak": 32767,
            "maximum_i2s_pcm_peak": 512,
            "amplifier_shutdown_gpio": 30,
            "amplifier_shutdown_level": 1,
        },
        "D2.3 authorized runtime limits changed",
    )
    require(
        auth.get("exact_host_tooling")
        == {
            "capture_cli": {"path": CAPTURE_CLI_REL, "sha256": CAPTURE_CLI_SHA256},
            "capture_transport": {
                "path": CAPTURE_TRANSPORT_REL,
                "sha256": CAPTURE_TRANSPORT_SHA256,
            },
            "capture_transport_test": {
                "path": CAPTURE_TRANSPORT_TEST_REL,
                "sha256": CAPTURE_TRANSPORT_TEST_SHA256,
            },
            "analyzer": {"path": ANALYZER_REL, "sha256": ANALYZER_SHA256},
            "central_flash": {
                "path": CENTRAL_FLASH_REL,
                "sha256": CENTRAL_FLASH_SHA256,
            },
        },
        "D2.3 authorization host-tool binding changed",
    )
    require(
        auth.get("one_shot_transport")
        == {
            "deferred_capture_flag_required_before_hardware": True,
            "launch_receipt_argument_required_before_host_preflight": True,
            "independent_host_preflight_runs": 3,
            "minimum_microphone_capture_seconds_per_preflight": 1,
            "preflights_complete_before_receipt_reservation": True,
            "receipt_private_path_validation_before_reservation_and_board_probe": True,
            "receipt_parent_mode": "0700",
            "receipt_file_mode": "0600",
            "reservation_is_atomic_and_durable": True,
            "reservation_precedes_any_board_probe": True,
            "reservation_binds_authorization_artifact_and_saved_device_identity": True,
            "one_pending_or_consumed_receipt_per_authorization_hash": True,
            "failure_after_reservation_is_terminal": True,
            "failure_after_consumption_requires_new_authorization": True,
            "exact_snapshot_mode": "0400",
            "direct_app_only_write_after": "no_reset",
            "exact_chunked_readback_before_receipt_emit": True,
            "central_flash_never_runs_d2_3": True,
            "microphone_capture_started_before_run": True,
            "arm_frame_tx_count": 1,
            "signals_fail_active_reservation": ["EXIT", "HUP", "INT", "TERM"],
        },
        "D2.3 one-shot receipt/transport authorization changed",
    )
    require(
        auth.get("attempt2_launch_transport") == {
            "ledger_path": ATTEMPT2_LEDGER_REL,
            "same_open_pyserial_handle": True,
            "uart_transport_only": True,
            "live_device_identity_checked_before_receipt_consumption": True,
            "esptool_flush_input_before_receipt_consumption": True,
            "pre_run_rx_bytes_required": 0,
            "receipt_atomically_consumed_before_launch": True,
            "esptool_version": "4.12.0",
            "custom_hard_reset_sequence_required": None,
            "launch_sequence": [
                "esp.run", "esp.hard_reset", "restore_dtr_false",
                "restore_rts_false", "start_serial_read_without_post_launch_flush",
            ],
            "run_count": 1,
            "hard_reset_count": 1,
            "hard_reset_rts_en_pulse_ms": 100,
            "dtr_assertion_count": 0,
            "exactly_one_allowed_reset_reason_required_before_serial_attach": True,
            "reset_or_rom_banner_rejected_from_serial_attach_onward": True,
        },
        "attempt-2 CLI-equivalent same-handle launch transport changed",
    )
    require(
        auth.get("exception_boundary")
        == {
            "supersedes_audio_runtime_denial_for_this_exact_app_once": True,
            "reusable_board_profile_audio_policy_unchanged_until_acceptance_passes": True,
            "application_flash_scope": "factory_app_partition_only_at_0x10000",
            "full_project_flash_authorized": False,
            "single_launch_after_exact_readback_and_receipt_consumption": True,
            "display_touch_storage_usb_i2c_codec_mclk_camera_wireless_authorized": False,
            "connected_unit_only": True,
            "doom_composite_authorized": False,
            "hardware_pass_requires_serial_microphone_and_operator_audibility_evidence": True,
        },
        "D2.3 authorization escaped its connected-unit one-shot boundary",
    )
    require(
        auth.get("serial_acceptance")
        == {
            "exact_marker_order": [
                "SERIAL_ATTACH", "WAIT_ARM", "ARM_ACCEPTED", "START", "SAFE",
                "POWER_READY", "BACKEND_READY", "CAPTURE_ARM", "TONE_BEGIN",
                "POSTROLL", "PASS", "HEARTBEAT",
            ],
            "each_exact_marker_count": 1,
            "reject_markers_absent": True,
            "host_wait_after_heartbeat_seconds_minimum": 1,
        },
        "D2.3 exact serial marker/count/order contract changed",
    )
    require(
        auth.get("acoustic_acceptance")
        == {
            "capture_device": "MacBook Pro Microphone",
            "capture_format": "mono_pcm_s16le_wav_48000hz",
            "capture_must_span": "before_capture_arm_through_heartbeat_and_at_least_tone_begin_plus_1.35_seconds",
            "analyzer": ANALYZER_REL,
            "analyzer_sha256": ANALYZER_SHA256,
            "analyzer_schema": 1,
            "analyzer_thresholds_not_cli_overridable": True,
            "ordered_serial_markers_required": [
                "CAPTURE_ARM",
                "TONE_BEGIN",
                "POSTROLL",
                "PASS",
                "HEARTBEAT",
            ],
            "serial_correlated_tone_window_required": True,
            "capture_span_wall_clock_verified_from_capture_arm_through_heartbeat": True,
            "portaudio_frames_per_callback": 480,
            "portaudio_callback_status_flags": 0,
            "portaudio_adc_continuity_required": True,
            "portaudio_to_monotonic_start_uncertainty_max_ns": 2000000,
            "portaudio_to_monotonic_end_uncertainty_max_ns": 2000000,
            "portaudio_to_monotonic_offset_jump_max_ns": 2000000,
            "monotonic_to_wall_offset_jump_max_ns": 2000000,
            "expected_dominant_frequency_hz": 440,
            "frequency_tolerance_hz": 5,
            "minimum_dominant_over_pre_post_noise_db": 12,
            "minimum_adjacent_band_margin_db": 6,
            "guaranteed_full_pre_post_baselines_required": True,
            "exactly_one_sustained_tone_event_required": True,
            "tone_event_duration_min_ms": 300,
            "tone_event_duration_max_ms": 650,
            "full_acoustic_pass_requires": [
                "synchronized_microphone_detection_pass",
                "contemporaneous_operator_audibility_confirmation",
            ],
            "pop_observation_recorded_separately": True,
        },
        "D2.3 acoustic acceptance limits changed",
    )
    analyzer = ROOT / ANALYZER_REL
    require(
        analyzer.is_file() and sha256_file(analyzer) == ANALYZER_SHA256,
        "D2.3 acoustic analyzer changed",
    )
    require(
        evidence.get("current_one_shot_authorization_evidence") == AUTHORIZATION_REL
        and evidence.get("current_one_shot_authorization_sha256")
        == AUTHORIZATION_SHA256,
        "build evidence is not bound to the exact authorization record",
    )


def verify_central_flash_path() -> None:
    flash_path = ROOT / CENTRAL_FLASH_REL
    flash = flash_path.read_text()
    require(sha256_file(flash_path) == CENTRAL_FLASH_SHA256,
            "central D2.3 flash route changed")
    early = flash[:flash.index("P4_SCRIPT_DIR=")]
    require(
        "Refusing audio_direct_diag app-only execution without the synchronized capture path."
        in early
        and "Refusing deferred capture launch: --launch-receipt is required." in early,
        "D2.3 nondeferred or receipt-free route is not rejected before hardware",
    )
    blocks = list(re.finditer(
        r'if \[ "\$P4_APP" = audio_direct_diag \]; then\n(?P<body>.*?)\nfi',
        flash, re.DOTALL))
    require(len(blocks) == 5,
            "D2.3 must have preflight, verifier, snapshot, readback, and receipt branches")
    preflight_blocks = [item for item in blocks
                        if "P4_D23_PREFLIGHT_RUN=" in item.group("body")]
    verifier_blocks = [item for item in blocks
                       if 'python3 "$P4_SCRIPT_DIR/verify-audio-direct-diag.py"'
                       in item.group("body")]
    snapshot_blocks = [item for item in blocks
                       if "P4_AUDIO_DIRECT_AUTH=" in item.group("body")]
    readback_blocks = [item for item in blocks
                       if "P4_READBACK_AFTER=no_reset" in item.group("body")]
    receipt_blocks = [item for item in blocks
                      if "emit-receipt" in item.group("body")]
    require(all(len(group) == 1 for group in (
        preflight_blocks, verifier_blocks, snapshot_blocks,
        readback_blocks, receipt_blocks)),
        "D2.3 central route branch classification changed")

    preflight = preflight_blocks[0].group("body")
    require(
        'P4_D23_STATE="$P4_PROJECT_ROOT/hardware/local-state/audio-direct-d23-attempt2.json"'
        in preflight
        and 'P4_D23_AUTH="$P4_PROJECT_ROOT/hardware/evidence/audio-direct-diag-d23-one-shot-authorization-attempt2.json"'
        in preflight,
        "central route is not isolated to fresh attempt-2 authorization/ledger paths",
    )
    ordered(preflight, (
        'P4_D23_PREFLIGHT_RUN=1',
        'while [ "$P4_D23_PREFLIGHT_RUN" -le 3 ]; do',
        'preflight-host --port "$P4_PORT"',
        'P4_D23_PREFLIGHT_RUN=$((P4_D23_PREFLIGHT_RUN + 1))',
        'check-issuable',
        'P4_D23_RESERVATION_ACTIVE=true',
        '"$P4_D23_CAPTURE_TOOL" reserve',
    ), "D2.3 three-preflight/durable-reservation order changed")
    probe_position = flash.index("P4_PROBE_OUTPUT=$(esptool.py")
    require(
        preflight_blocks[0].start() < flash.index("preflight-host --port")
        < flash.index("check-issuable")
        < flash.index("P4_D23_RESERVATION_ACTIVE=true")
        < flash.index('"$P4_D23_CAPTURE_TOOL" reserve') < probe_position,
        "D2.3 reservation is not active and durable before any board probe",
    )
    require(
        "trap p4_cleanup_flash_temps EXIT\n" in flash
        and "trap p4_handle_hup HUP" in flash
        and "trap p4_handle_int INT" in flash
        and "trap p4_handle_term TERM" in flash
        and "fail-reservation" in flash
        and all(token in flash for token in ("exit 129", "exit 130", "exit 143")),
        "D2.3 reservation is not terminalized on EXIT/HUP/INT/TERM",
    )
    probe_branch = re.search(
        r'case "\$P4_APP" in\n\s*(?P<apps>[A-Za-z0-9_|-]+)\)\n'
        r'(?P<body>.*?)\n\s*;;', flash, re.DOTALL)
    require(probe_branch is not None
            and "audio_direct_diag" in probe_branch.group("apps").split("|")
            and "P4_PROBE_AFTER=no_reset" in probe_branch.group("body"),
            "D2.3 board probes are not held in the ROM loader")
    verifier = verifier_blocks[0].group("body")
    require('python3 "$P4_SCRIPT_DIR/verify-audio-direct-diag.py"' in verifier
            and '"$P4_BUILD_DIR" "$P4_FLASH_TARGET"' in verifier,
            "central flash route does not invoke the exact D2.3 verifier")

    snapshot = snapshot_blocks[0].group("body")
    ordered(snapshot, (
        'P4_AUDIO_DIRECT_AUTH="$P4_PROJECT_ROOT/hardware/evidence/audio-direct-diag-d23-one-shot-authorization-attempt2.json"',
        '["exact_artifact"]["bytes"]',
        '["exact_artifact"]["sha256"]',
        'P4_VERIFIED_APP_DIR=$(mktemp -d',
        'chmod 700 "$P4_VERIFIED_APP_DIR"',
        'cp -- "$P4_BUILT_APP_PATH" "$P4_VERIFIED_APP_PATH"',
        'chmod 400 "$P4_VERIFIED_APP_PATH"',
        'P4_VERIFIED_APP_ACTUAL_BYTES=$(wc -c < "$P4_VERIFIED_APP_PATH"',
        'P4_VERIFIED_APP_ACTUAL_HASH=$(p4_sha256_file "$P4_VERIFIED_APP_PATH")',
        'P4_BUILT_APP_PATH="$P4_VERIFIED_APP_PATH"',
        'P4_VERIFIED_DIRECT_WRITE=true',
    ), "D2.3 immutable authorization-bound snapshot order changed")
    direct_begin = flash.index('elif [ "$P4_VERIFIED_DIRECT_WRITE" = true ]; then')
    normal_begin = flash.index('else\n    P4_FLASH_OUTPUT=$(idf.py', direct_begin)
    direct = flash[direct_begin:normal_begin]
    require("idf.py" not in direct, "D2.3 direct-write path can rebuild after verification")
    ordered(direct, (
        'P4_VERIFIED_APP_PREWRITE_BYTES=$(wc -c < "$P4_BUILT_APP_PATH"',
        'P4_VERIFIED_APP_PREWRITE_HASH=$(p4_sha256_file "$P4_BUILT_APP_PATH")',
        'P4_FLASH_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT"',
        '--baud 460800 --before default_reset --after no_reset write_flash',
        '"$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_PATH"',
    ), "D2.3 no-build exact direct-write order changed")
    postwrite = flash.index('P4_BUILT_APP_BYTES=$(wc -c < "$P4_BUILT_APP_PATH"')
    readback = flash.index("if ! p4_verify_chunked_application_readback")
    generic = flash.index('if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then')
    literal = flash.index(
        '[ "$P4_READBACK_ACTUAL_BYTES" != "$P4_VERIFIED_APP_EXPECTED_BYTES" ]',
        generic)
    literal_hash = flash.index(
        '[ "$P4_READBACK_HASH" != "$P4_VERIFIED_APP_EXPECTED_HASH" ]', literal)
    confirmed = flash.index("Application readback verified:")
    emit = flash.index('"$P4_D23_CAPTURE_TOOL" emit-receipt')
    require(
        verifier_blocks[0].start() < snapshot_blocks[0].start() < direct_begin
        < postwrite < readback_blocks[0].start() < readback < generic
        < literal < literal_hash < confirmed < receipt_blocks[0].start() < emit,
        "D2.3 receipt is not deferred until exact literal readback succeeds",
    )
    receipt = receipt_blocks[0].group("body")
    require(
        "emit-receipt" in receipt
        and "esptool.py --chip esp32p4 --port" not in receipt
        and "remains stopped in the ROM loader for synchronized capture" in receipt,
        "central route launches D2.3 instead of emitting one stopped receipt",
    )
    readback_helper = (ROOT / "scripts/lib/app-readback.sh").read_text()
    chunk_helper = (ROOT / "scripts/verify-readback-chunks.py").read_text()
    require(
        "P4_READBACK_CHUNK_BYTES=524288" in readback_helper
        and "for P4_READBACK_BAUD in $P4_READBACK_BAUDS; do" in readback_helper
        and 'verify-readback-chunks.py" chunk' in readback_helper
        and 'verify-readback-chunks.py" aggregate' in readback_helper
        and "for index in range(args.chunks)" in chunk_helper,
        "D2.3 readback is not bounded, per-chunk verified, and ordered",
    )


def verify_capture_transport_contract() -> None:
    cli = (ROOT / CAPTURE_CLI_REL).read_text()
    transport = (ROOT / CAPTURE_TRANSPORT_REL).read_text()
    test = (ROOT / CAPTURE_TRANSPORT_TEST_REL).read_text()
    require(all(token in cli for token in (
        "check-issuable", "preflight-host", "reserve", "fail-reservation",
        "emit-receipt", "capture")), "D2.3 capture CLI surface changed")
    require(all(token in transport for token in (
        'ARM_NONCE_SEED = AUTH_ID + " host-arm v1"',
        'ARM_FRAME_BYTES = 138',
        'FRAMES_PER_BUFFER = 480',
        'captured_frames < SAMPLE_RATE or captured_seconds < 1.0',
        'os.chmod(path.parent, 0o700)',
        'os.link(receipt_path, consumed)',
        'device = open_serial_once(args.port)',
        'audio.start()',
        'live_device_identity(esp) != receipt["device_identity_sha256"]',
        'consumed = atomic_consume(receipt_path, args.state, receipt)',
        'esp.run()',
        'esp.hard_reset()',
        'custom_hard_reset_sequence',
        'custom esptool hard-reset sequence is prohibited',
        '"esptool_custom_hard_reset_sequence": None',
        'attributes[2] &= ~getattr(termios, "HUPCL", 0)',
        'device.dtr = False',
        'device.rts = False',
        'serial_attach_seen and any(marker in lower for marker in RESET_SUBSTRINGS)',
        'if len(matches) != 1',
        'marker_events["TONE_BEGIN"] + 1_350_000_000',
        'last_sample_end_monotonic_ns < marker_events["HEARTBEAT"]',
        'audio.clock_map["uncertainty_ns"] > 2_000_000',
        'audio.clock_map["offset_jump_ns"] > 2_000_000',
        'wall_offset_jump_ns > 2_000_000',
        'any(item["status_flags"] != 0 for item in callbacks)',
        'if len(reset_codes) != 1',
    )), "D2.3 same-handle receipt/capture safety contract changed")
    capture_body = transport[transport.index("def capture(args: Any)") :]
    ordered(capture_body, (
        "audio.preflight_open()", "device = open_serial_once(args.port)",
        "audio.start()", "ESP32P4ROM(device, 115200, False)",
        "live_device_identity(esp)",
        "atomic_consume(receipt_path, args.state, receipt)",
        "launch_cli_equivalent(esp, device)",
    ), "D2.3 identity/consume/same-handle launch order changed")
    require(
        "subprocess" not in transport
        and 'source.count("open_serial_once(args.port)") == 1' in test
        and 'source.index("atomic_consume(receipt_path") < source.index("esp.run()")' in test,
        "D2.3 host-only transport regression contract changed",
    )


def verify_analyzer_contract() -> None:
    analyzer = (ROOT / ANALYZER_REL).read_text()
    require(all(token in analyzer for token in (
        "expected_hz = 440.0", "min_snr_db = 12.0", "min_adjacent_db = 6.0",
        "sample_rate != 48000", "capture must be mono PCM16 WAV",
        '"CAPTURE_ARM", "TONE_BEGIN", "POSTROLL", "PASS", "HEARTBEAT"',
        "audio_first_sample_wall_ns > marker_times[0]",
        "audio_end_wall_ns < marker_times[-1]",
        "capture lacks guaranteed pre-tone or post-tone baseline",
        "len(events) == 1", "0.30 <= correlated_event", "<= 0.65",
        "abs(dominant_hz - expected_hz) <= 5.0",
        "tone_over_baseline_db >= min_snr_db",
        "tone_over_adjacent_db >= min_adjacent_db",
    )), "D2.3 exact 48k/timing/signal analyzer contract changed")


def verify_linked_symbols(elf: pathlib.Path, description: dict) -> None:
    compiler = description.get("c_compiler")
    require(isinstance(compiler, str) and compiler, "build does not record its compiler")
    nm = pathlib.Path(compiler).with_name("riscv32-esp-elf-nm")
    require(nm.is_file(), "pinned RISC-V nm is missing")
    try:
        symbols = subprocess.run(
            [str(nm), "-g", str(elf)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect linked ELF: {error}")
    required = (
        "platform_audio_force_safe_shutdown",
        "platform_audio_recover",
        "platform_audio_create",
        "platform_audio_start",
        "platform_audio_write_frames",
        "platform_audio_stop",
        "platform_audio_destroy",
        "platform_audio_attenuate_pcm16",
        "platform_audio_generate_quiet_tone",
        "i2s_channel_init_std_mode",
        "i2s_channel_preload_data",
        "i2s_channel_enable",
        "i2s_channel_write",
        "i2s_channel_disable",
        "i2s_del_channel",
        "esp_ldo_acquire_channel",
        "esp_ldo_release_channel",
        "gpio_set_level",
    )
    for symbol in required:
        require(re.search(rf"\b{re.escape(symbol)}$", symbols, re.MULTILINE) is not None,
                f"required linked symbol is missing: {symbol}")
    forbidden = (
        "i2c_new_master_bus",
        "esp_codec_dev_open",
        "es8311_codec_new",
        "esp_lcd_new_panel_ek79007",
        "sdmmc_host_init",
        "usb_host_install",
    )
    for symbol in forbidden:
        require(re.search(rf"\b{re.escape(symbol)}$", symbols, re.MULTILINE) is None,
                f"out-of-scope symbol is linked: {symbol}")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-audio-direct-diag.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"},
            "mode must be build-only or app-flash; full-project flash is prohibited")

    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "audio_direct_diag",
            "wrong app metadata")
    require(
        metadata.get("stage") == "D2.3-reusable-direct-audio-build-only"
        and metadata.get("authorization_evidence") == AUTHORIZATION_REL
        and metadata.get("runtime_evidence") is None,
        "D2.3 metadata evidence binding changed",
    )
    require(metadata.get("flash_authorized") is False,
            "legacy broad flash must remain false")
    require(metadata.get("flash_project_authorized") is False,
            "full-project flash must remain false")
    if mode == "build-only":
        require(metadata.get("evidence_class") == "source-integrated",
                "build-only metadata unexpectedly claims completed review")
        require(metadata.get("build_evidence") is None,
                "source-integrated metadata unexpectedly claims build evidence")
        require(metadata.get("runtime_supported") is False,
                "build-only metadata claims runtime support")
        require(metadata.get("flash_app_authorized") is False,
                "build-only verification requires app flash to remain false")
        require(metadata.get("runtime_blockers") == [
            "The exact reproducible firmware is unchanged, but attempt 2 has not completed its refreshed host-transport gate.",
            "Attempt 1 failed safely before host ARM and is preserved at hardware/test-runs/2026-08-13-audio-direct-d23-attempt1.json; its authorization was consumed and revoked.",
            "The connected unit has not yet produced synchronized microphone detection and contemporaneous operator audibility confirmation for this exact diagnostic.",
            "No attempt-2 app-only execution authorization has been activated; broad and full-project flash remain denied.",
        ], "build-only D2.3 blocker wording changed")
    else:
        require(metadata.get("evidence_class") == "build-tested",
                "app-flash requires completed build review")
        require(metadata.get("build_evidence") == BUILD_EVIDENCE_REL,
                "app-flash metadata is not bound to D2.3 build evidence")
        require(metadata.get("runtime_supported") is True,
                "one-shot runtime has not been activated")
        require(metadata.get("flash_app_authorized") is True,
                "app-only flash has not been explicitly activated")
        require(metadata.get("runtime_blockers") == [
            "Attempt 2 is authorized only through the fresh receipt-bound synchronized capture path and CLI-equivalent same-handle hard-reset launch; broad and full-project flash remain denied.",
            "The connected unit has not yet produced synchronized microphone detection and contemporaneous operator audibility confirmation for this exact diagnostic.",
            "The attempt-2 authorization must be revoked after receipt consumption or any terminal failure; another attempt requires a new authorization record and ledger.",
        ], "active D2.3 one-shot constraints were not installed exactly")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES,
            "metadata interface scope changed")
    require(set(metadata.get("hardware_interfaces_explicitly_absent", [])) == EXPECTED_ABSENT,
            "metadata interface denials changed")

    profile = load_json(ROOT / "hardware/board-profile.json")
    audio = profile.get("peripheral_authorizations", {}).get("audio", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(
        audio.get("authorized") is False
        and audio.get("scope") == "topology_review_only_no_gpio_or_driver_authorized"
        and audio.get("hardware_test") is None,
        "reusable board profile unexpectedly authorizes audio runtime",
    )

    factory = load_json(ROOT / FACTORY_EVIDENCE_REL)
    speaker = factory.get("source_semantics", {}).get("speaker_tx", {})
    require(
        factory.get("classification") == "exact-official-factory-source-audio-semantics"
        and factory.get("result") == "pass-source-audit-build-input-runtime-unproven"
        and speaker.get("controller") == 1
        and speaker.get("lrclk_gpio") == 21
        and speaker.get("bclk_gpio") == 22
        and speaker.get("dout_gpio") == 23
        and speaker.get("mclk") == "I2S_GPIO_UNUSED"
        and speaker.get("amplifier_shutdown_gpio") == 30
        and speaker.get("amplifier_enable_level") == 0,
        "factory direct-I2S semantics changed",
    )
    electrical = load_json(ROOT / ELECTRICAL_EVIDENCE_REL)
    require(
        electrical.get("classification") == "cross-revision-audio-driver-path"
        and electrical.get("result") == "pass-build-only-runtime-denied"
        and electrical.get("authorization", {}).get("authorized") is False,
        "electrical review no longer has its build-only boundary",
    )
    prior = load_json(ROOT / PRIOR_RUNTIME_REL)
    prior_runtime = prior.get("runtime", {})
    require(
        prior.get("result") == "pass-direct-i2s-serial-audibility-unconfirmed"
        and prior_runtime.get("selected_path") == "factory-direct-i2s"
        and prior_runtime.get("i2s_controller") == 1
        and prior_runtime.get("lrclk_gpio") == 21
        and prior_runtime.get("bclk_gpio") == 22
        and prior_runtime.get("dout_gpio") == 23
        and prior_runtime.get("mclk") == "unused"
        and prior_runtime.get("tone_peak_pcm") == 512
        and prior_runtime.get("pass_marker_seen") is True
        and prior_runtime.get("halt_seen") is False,
        "prior connected-unit direct-I2S serial evidence changed",
    )

    evidence = load_json(ROOT / BUILD_EVIDENCE_REL)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested"
        and evidence.get("result") == "pass-build-only-runtime-denied"
        and evidence.get("scope")
        == "audio_direct_diag_d2_3_reusable_direct_i2s_single_bounded_tone"
        and evidence.get("factory_source_evidence") == FACTORY_EVIDENCE_REL
        and evidence.get("electrical_path_evidence") == ELECTRICAL_EVIDENCE_REL
        and evidence.get("prior_direct_runtime_evidence") == PRIOR_RUNTIME_REL,
        "D2.3 build evidence classification or provenance changed",
    )
    require(
        evidence.get("execution")
        == {
            "current_attempt": 2,
            "attempt2_firmware_flashed": False,
            "attempt2_firmware_executed": False,
            "attempt2_hardware_accessed": False,
            "attempt2_audio_path_energized": False,
            "attempt2_runtime_supported": False,
            "attempt2_serial_evidence": None,
            "attempt2_microphone_evidence": None,
            "attempt1_failure_evidence": ATTEMPT1_FAILURE_REL,
            "attempt1_failure_evidence_sha256": ATTEMPT1_FAILURE_SHA256,
            "attempt1_result": "fail-safe-no-tone",
        },
        "build evidence does not separate attempt-2 preflash from attempt-1 failure",
    )
    require(
        evidence.get("previous_attempt_failure_evidence") == ATTEMPT1_FAILURE_REL
        and evidence.get("previous_attempt_failure_evidence_sha256")
        == ATTEMPT1_FAILURE_SHA256,
        "build evidence does not preserve the exact attempt-1 failure",
    )
    verify_source_inventory(evidence)
    verify_source_contract()

    lock = load_json(ROOT / "toolchain.lock.json")
    target = lock.get("target", {})
    recorded_toolchain = evidence.get("toolchain", {})
    recorded_build = evidence.get("build", {})
    require(
        recorded_toolchain.get("esp_idf_version") == lock["esp_idf"]["version"]
        and recorded_toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"]
        and recorded_toolchain.get("compiler")
        == "riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20251107) 14.2.0"
        and recorded_build.get("target") == target.get("chip")
        and recorded_build.get("minimum_revision_full") == target.get("min_revision_full")
        and recorded_build.get("maximum_revision_full") == target.get("max_revision_full")
        and recorded_build.get("application_offset") == "0x10000"
        and recorded_build.get("sdkconfig_sha256") == SDKCONFIG_SHA256
        and recorded_build.get("application_execution_deferred_until_exact_readback") is True
        and recorded_build.get("flash_after_action") == "no_reset"
        and recorded_build.get("reproducible_build") is True
        and recorded_build.get("compile_time_date_enabled") is False
        and recorded_build.get("reviewed_build_only_kconfig_enabled") is True
        and recorded_build.get("global_pin_map_authorized") is False
        and recorded_build.get("audio_runtime_authorized_by_board_profile") is False,
        "recorded toolchain, target, or build policy changed",
    )
    reproducibility = evidence.get("reproducibility", {})
    identities = reproducibility.get("identities", [])
    require(
        reproducibility.get("clean_builds_compared") == 3
        and reproducibility.get("independent_build_directories_compared") == 2
        and reproducibility.get("identical_application_binary") is True
        and reproducibility.get("identical_application_elf") is True
        and reproducibility.get("identical_bootloader_binary") is True
        and reproducibility.get("identical_partition_table_binary") is True
        and isinstance(identities, list)
        and [entry.get("label") for entry in identities]
        == ["canonical", "independent-a", "independent-b"]
        and all(
            entry.get("app_binary_bytes") == APP_BYTES
            and entry.get("app_binary_sha256") == APP_SHA256
            and entry.get("elf_bytes") == ELF_BYTES
            and entry.get("elf_sha256") == ELF_SHA256
            for entry in identities
        ),
        "canonical plus two-independent reproducibility identities changed",
    )

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    app_sdkconfig = APP_DIR / "sdkconfig"
    if build_dir == (APP_DIR / "build").resolve():
        require(app_sdkconfig.is_file() and sha256_file(app_sdkconfig) == SDKCONFIG_SHA256,
                "canonical frozen sdkconfig changed")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True,
            "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True,
            "compile-time date is enabled")
    require(sdkconfig.get("IDF_TARGET") == target.get("chip"),
            "generated target differs from lock")
    require(
        sdkconfig.get("ESP32P4_SELECTS_REV_LESS_V3") is True
        and sdkconfig.get("ESP32P4_REV_MIN_100") is True
        and sdkconfig.get("ESP32P4_REV_MIN_FULL") == 100,
        "generated revision-1.x silicon gate changed",
    )
    require(
        sdkconfig.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdkconfig.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated image would launch before readback",
    )
    require(
        sdkconfig.get("ESPTOOLPY_FLASHSIZE") == "16MB"
        and sdkconfig.get("ESPTOOLPY_FLASHFREQ") == "80m"
        and sdkconfig.get("PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY") is True
        and sdkconfig.get("ESP_MAIN_TASK_STACK_SIZE") == 8192,
        "generated flash, component, or stack policy changed",
    )

    description = load_json(build_dir / "project_description.json")
    require(
        description.get("project_name") == "p4_audio_direct_diag"
        and description.get("project_version") == "0.1.0"
        and description.get("target") == target.get("chip")
        and int(description.get("min_rev")) == target.get("min_revision_full")
        and int(description.get("max_rev")) == target.get("max_revision_full")
        and "platform_audio" in description.get("build_components", []),
        "generated project identity, revision, or component set changed",
    )
    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    bootloader = flash_args.get("bootloader", {})
    partition = flash_args.get("partition-table", {})
    require(
        int(str(app.get("offset")), 0) == APP_OFFSET
        and int(str(bootloader.get("offset")), 0) == 0x2000
        and int(str(partition.get("offset")), 0) == 0x8000
        and flash_args.get("flash_settings", {}).get("flash_size") == "16MB"
        and flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
        "generated offsets, flash size, or no-reset action changed",
    )
    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    factory_apps = [
        item for item in manifest.get("factory_partition_table", [])
        if item.get("type") == "app" and item.get("subtype") == "factory"
    ]
    require(
        len(factory_apps) == 1
        and int(factory_apps[0].get("offset"), 0) == APP_OFFSET
        and factory_apps[0].get("size") == "11M",
        "saved factory application boundary changed",
    )

    binary = checked_child(build_dir, app.get("file"), "application binary")
    elf = checked_child(build_dir, description.get("app_elf"), "application ELF")
    bootloader_binary = checked_child(build_dir, bootloader.get("file"), "bootloader binary")
    partition_binary = checked_child(build_dir, partition.get("file"), "partition table binary")
    artifacts = evidence.get("artifacts", {})
    exact_artifacts = (
        (binary, APP_BYTES, APP_SHA256, "app_binary_bytes", "app_binary_sha256"),
        (elf, ELF_BYTES, ELF_SHA256, "elf_bytes", "elf_sha256"),
        (bootloader_binary, BOOTLOADER_BYTES, BOOTLOADER_SHA256,
         "bootloader_bytes", "bootloader_sha256"),
        (partition_binary, PARTITION_BYTES, PARTITION_SHA256,
         "partition_table_bytes", "partition_table_sha256"),
    )
    for path, size, digest, size_key, hash_key in exact_artifacts:
        require(path.stat().st_size == size == artifacts.get(size_key),
                f"{path.name} byte count differs")
        require(sha256_file(path) == digest == artifacts.get(hash_key),
                f"{path.name} hash differs")
    require(
        artifacts.get("generated_app_partition_free_bytes")
        == artifacts.get("generated_app_partition_bytes") - APP_BYTES
        and artifacts.get("saved_factory_app_partition_free_bytes")
        == artifacts.get("saved_factory_app_partition_bytes") - APP_BYTES,
        "recorded application capacity arithmetic changed",
    )
    verify_authorization(binary, evidence)

    require(
        evidence.get("reusable_backend_policy")
        == {
            "service_boundary": "components/platform_audio",
            "singleton_externally_serialized": True,
            "i2s_controller": 1,
            "lrclk_gpio": 21,
            "bclk_gpio": 22,
            "dout_gpio": 23,
            "mclk": "unused",
            "control_bus": "must_be_null",
            "sample_rate_hz": 16000,
            "channels": 2,
            "bits_per_sample": 16,
            "dma_descriptor_count": 6,
            "dma_frames_per_descriptor": 256,
            "complete_zero_ring_frames": 1536,
            "public_write_frames_max": 128,
            "write_timeout_ms": 100,
            "every_write_requires_exact_byte_count": True,
            "input_buffer_immutable": True,
            "component_owned_attenuation_staging": True,
            "maximum_volume_percent": 10,
            "maximum_output_peak_at_volume_10": 512,
            "amplifier_shutdown_gpio": 30,
            "amplifier_shutdown_level": 1,
            "amplifier_enable_level": 0,
            "amplifier_settle_ms": 20,
            "failed_create_owner_retained_until_recovery": True,
            "destroy_handle_retained_until_cleanup_success": True,
            "ldo3_ldo4_owned_by_caller": True,
            "i2c_codec_display_storage_usb_absent": True,
        },
        "recorded reusable-backend safety policy changed",
    )
    require(
        evidence.get("diagnostic_policy")
        == {
            "host_arm": {
                "silent_force_safe_delay_ms": 2000,
                "serial_attach_before_wait_arm": True,
                "fixed_nonce": "0c47666f7da543e93831e0dfdcb7fecd6bb9887f0d0d0c72ee4422826b573562",
                "fixed_frame_bytes": 138,
                "fixed_frame_sha256": "cce24d670298f6d62755d240d14f928c212d82fea6246517eb8edaf48f0fade9",
                "timeout_ms": 15000,
                "duplicate_guard_ms": 100,
                "rails_off_until_arm_accepted": True,
            },
            "board_power": {
                "ldo3_channel": 3,
                "ldo3_voltage_mv": 2500,
                "ldo4_channel": 4,
                "ldo4_voltage_mv": 3300,
                "settle_ms": 20,
                "acquisition_order": "ldo3_then_ldo4",
                "release_order": "ldo4_then_ldo3",
                "retain_rails_until_backend_cleanup_confirmed": True,
            },
            "tone": {
                "count": 1,
                "capture_arm_ms": 5000,
                "frequency_hz": 440,
                "duration_ms": 400,
                "fade_ms": 50,
                "source_pcm_peak": 32767,
                "service_output_peak_cap": 512,
                "tone_frames": 6400,
                "tone_write_calls": 50,
                "postroll_zero_frames": 1536,
                "postroll_write_calls": 12,
                "total_exact_write_calls": 62,
                "partial_writes_allowed": False,
            },
        },
        "recorded D2.3 diagnostic policy changed",
    )
    app_gate = evidence.get("app_only_gate", {})
    require(
        app_gate.get("authorization_record_path") == AUTHORIZATION_REL
        and app_gate.get("authorization_attempt") == 2
        and app_gate.get("authorization_record_present") is True
        and app_gate.get("authorization_record_sha256") == AUTHORIZATION_SHA256
        and app_gate.get("ledger_path") == ATTEMPT2_LEDGER_REL
        and app_gate.get("app_flash_metadata_flag") is False
        and app_gate.get("full_project_flash_authorized") is False
        and app_gate.get("application_flash_scope")
        == "saved_factory_app_partition_only_at_0x10000"
        and app_gate.get("exact_readback_required_before_launch") is True
        and app_gate.get("deferred_capture_flag_required") is True
        and app_gate.get("private_receipt_required") is True
        and app_gate.get(
            "single_explicit_run_after_exact_readback_and_atomic_receipt_consumption"
        ) is True,
        "recorded app-only/readback gate changed",
    )
    central = evidence.get("central_flash_policy", {})
    require(
        central.get("capture_transport_cli_equivalent_launch_sequence")
        == "esp.run_then_esp.hard_reset_then_dtr_rts_false"
        and central.get("custom_hard_reset_sequence") is None
        and central.get("exactly_one_allowed_reset_reason_required_before_serial_attach") is True
        and central.get("post_launch_serial_flush_or_drain_allowed") is False,
        "recorded attempt-2 launch/reset policy changed",
    )
    serial = evidence.get("serial_acceptance", {})
    require(
        serial.get("pass_exact")
        == "P4_AUDIO D2.3 PASS backend=platform_audio tone_count=1 bounded_ms=400 exact_writes=62 partial_writes=0 amp_shutdown=1 ldo3_released=1 ldo4_released=1"
        and serial.get("heartbeat_exact")
        == "P4_AUDIO D2.3 HEARTBEAT amp_shutdown=1 rails_released=1"
        and serial.get("halt_prefix") == "P4_AUDIO D2.3 HALT stage="
        and serial.get("reject_markers")
        == [
            "P4_AUDIO D2.3 HALT",
            "Guru Meditation Error",
            "Task watchdog got triggered",
            "assert failed",
        ]
        and "the synchronized microphone analyzer reports exactly one sustained 440 Hz event within plus or minus 5 Hz and at least 12 dB over pre/post noise"
        in serial.get("runtime_pass_requirements", [])
        and "the operator contemporaneously confirms that the bounded tone was audible"
        in serial.get("runtime_pass_requirements", [])
        and "any pop observation is recorded separately and does not substitute for either acoustic requirement"
        in serial.get("runtime_pass_requirements", []),
        "serial/acoustic acceptance contract changed",
    )
    require(
        evidence.get("acoustic_acceptance")
        == {
            "analyzer": ANALYZER_REL,
            "analyzer_sha256": ANALYZER_SHA256,
            "analyzer_thresholds_not_cli_overridable": True,
            "exact_48000hz_mono_pcm16_required": True,
            "ordered_serial_timing_markers_required": [
                "CAPTURE_ARM",
                "TONE_BEGIN",
                "POSTROLL",
                "PASS",
                "HEARTBEAT",
            ],
            "serial_correlated_tone_window_required": True,
            "capture_span_wall_clock_verified_from_capture_arm_through_heartbeat": True,
            "capture_spans_at_least_tone_begin_plus_1_35_seconds": True,
            "host_waits_at_least_one_second_after_heartbeat_before_stopping_capture": True,
            "portaudio_frames_per_callback": 480,
            "portaudio_callback_status_flags": 0,
            "portaudio_adc_continuity_required": True,
            "portaudio_to_monotonic_start_uncertainty_max_ns": 2000000,
            "portaudio_to_monotonic_end_uncertainty_max_ns": 2000000,
            "portaudio_to_monotonic_offset_jump_max_ns": 2000000,
            "monotonic_to_wall_offset_jump_max_ns": 2000000,
            "synchronized_microphone_detection_required": True,
            "exactly_one_sustained_tone_event_required": True,
            "tone_event_duration_min_ms": 300,
            "tone_event_duration_max_ms": 650,
            "frequency_tolerance_hz": 5,
            "minimum_dominant_over_pre_post_noise_db": 12,
            "minimum_adjacent_band_margin_db": 6,
            "guaranteed_full_pre_post_baselines_required": True,
            "contemporaneous_operator_audibility_confirmation_required": True,
            "full_acoustic_pass_requires_both": True,
            "pop_observation_recorded_separately": True,
        },
        "recorded D2.3 acoustic gate changed",
    )
    host = evidence.get("host_tests", {})
    require(
        host.get("result") == "pass"
        and host.get("cases") == 2
        and host.get("sanitizers") == ["AddressSanitizer", "UndefinedBehaviorSanitizer"]
        and len(host.get("acceptance_markers", [])) == 2,
        "host runtime/policy acceptance record changed",
    )
    require(
        evidence.get("preflash_gate_tests")
        == {
            "canonical_build_only": "pass",
            "independent_build_a_build_only": "pass",
            "independent_build_b_build_only": "pass",
            "stale_intermediate_artifact": "rejected-byte-count-differs",
            "app_flash_while_metadata_flag_false": "rejected-before-port-or-hardware-access",
            "full_project_flash": "rejected-before-port-or-hardware-access",
            "nondeferred_d23_app_flash": "rejected-before-port-or-hardware-access",
            "missing_or_unbound_launch_receipt": "rejected-before-port-or-hardware-access",
            "capture_transport_receipt_ledger_and_static_route_tests": "pass",
            "chunked_readback_transport_tests": "pass",
            "project_environment_policy_tests": "pass",
            "frozen_doom_e1_regression_verifier": "outside-d2.3-existing-verifier-needs-central-route-refresh",
            "repository_hardware_metadata_verifier": "pass",
        },
        "recorded preflash-gate validation changed",
    )
    verify_host_tool_inventory(evidence)
    verify_capture_transport_contract()
    verify_analyzer_contract()
    verify_linked_symbols(elf, description)
    verify_central_flash_path()

    print(
        "audio_direct_diag verification: PASS "
        f"mode={mode} offset=0x{APP_OFFSET:x} bytes={APP_BYTES} "
        f"sha256={APP_SHA256} "
        f"runtime_authorization={'connected-unit-one-shot' if mode == 'app-flash' else 'false'}"
    )


if __name__ == "__main__":
    main()
