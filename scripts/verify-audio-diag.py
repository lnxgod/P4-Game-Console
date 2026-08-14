#!/usr/bin/env python3

"""Fail-closed exact-image gate for the bounded Elecrow D2.2 audio probe."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/audio_diag"
# D2.2 is frozen historical evidence. Its exact ES8311 candidate is archived
# app-locally so later platform_audio backends cannot rewrite that result.
COMPONENT_DIR = APP_DIR / "components/platform_audio"
MANAGED_CODEC_DIR = APP_DIR / "managed_components/espressif__esp_codec_dev"
ELECTRICAL_REL = "hardware/evidence/elecrow-10.1-audio-driver-path.json"
FACTORY_REL = "hardware/evidence/elecrow-10.1-factory-audio-semantics.json"
PRIOR_AUTHORIZATION_REL = "hardware/evidence/audio-diag-one-shot-authorization.json"
BUILD_EVIDENCE_REL = "test-runs/2026-08-12-audio-d22-build.json"
PRIOR_RUNTIME_EVIDENCE_RELS = [
    "hardware/test-runs/2026-08-12-audio-diag-d2.json",
    "hardware/test-runs/2026-08-12-audio-diag-d21.json",
]
FACTORY_ARCHIVE_LOCAL = pathlib.Path(
    "/private/tmp/elecrow-factory-10.1-c5a4373.zip"
)
FACTORY_ARCHIVE_BYTES = 13819158
FACTORY_ARCHIVE_SHA256 = (
    "73b32c4d4dc89cc0b091388d6a7862d827d6548412717bcb1f36f7adb8da2e28"
)
FACTORY_MEMBERS = {
    "components/espressif__esp32_p4_function_ev_board/esp32_p4_function_ev_board.c": (
        25517,
        "f0aa354307710744f37d57b8ea23942b13d6ae38c26a98ad118c606ae5b11b69",
    ),
    "components/espressif__esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h": (
        12418,
        "b9dba4a11ff952ac42f20314ef8fec7db7e87affbe04667ce62e5d0c655c7341",
    ),
    "components/espressif__esp32_p4_function_ev_board/my_codec.c": (
        12051,
        "f80ee68cd9e079725e9ea68d218b04c06438a86a91988e98750dfbc4b319ee18",
    ),
    "sdkconfig": (
        105671,
        "a695398707cc2a541be38a68090e0fc8447f3e6c3285346035ca625f80aec189",
    ),
}
CODEC_VERSION = "1.3.4"
CODEC_COMPONENT_HASH = (
    "18c22e1411224ba6103c4aca1b01bc740af33926756e9e106370a477d52bcba1"
)
CODEC_CHECKSUMS_SHA256 = (
    "59aae667bf615f3def46f12f76414a0a871010245b982a1f80a89ddb2dd7f73b"
)
PRIOR_AUTHORIZATION_SHA256 = (
    "25bde2b585df1cf66ba3b83d576f6c0b07406b650a7f3352d9cd10180ce0d7fd"
)
EXPECTED_INTERFACES = {
    "internal_ldo3_2500mv_ldo4_3300mv_vendor_power_prerequisite",
    "i2c1_gpio45_sda_gpio46_scl_bounded_scan_addr7_0x08_through_0x77",
    "i2s1_gpio21_lrclk_gpio22_bclk_gpio23_dout_no_mclk_factory_direct_fallback",
    "i2s1_gpio21_lrclk_gpio22_bclk_gpio23_dout_gpio24_mclk_guarded_es8311_if_addr7_0x18_acks",
    "gpio30_ns4263b_active_low_shutdown",
    "speaker_output_bounded_quiet_tone",
}
EXPECTED_ABSENT = {
    "microphone",
    "display",
    "touch",
    "sd_card",
    "camera",
    "wireless",
    "usb_host",
}
EXPECTED_SOURCE_INVENTORY = {
    "apps/audio_diag/CMakeLists.txt": "be8a6da7e751d7676ae8440c42ea725d08a798276e4848cc834daa6a96a3186e",
    "apps/audio_diag/main/CMakeLists.txt": "35c411e95e57839806869b58ded7a07f1624e7da5b50989f4f8d70d6398fec4d",
    "apps/audio_diag/main/audio_diag_main.c": "ae81644bda59a23b587945783fe24d71c0be8461aa5a12869e729f455045f79c",
    "apps/audio_diag/main/idf_component.yml": "c02968cafb1b7543f0f5452ec63ac9e87652e71ecd27ebc60a2cc3d43fe6ee5a",
    "apps/audio_diag/sdkconfig.defaults": "233f4bee9fd3e46206c67ea1b352f54fe81ff0c0c0a55efdd49ad2c5d7dde1e7",
    "apps/audio_diag/dependencies.lock": "3215c4561eb715bf74f0bba99161c215c478c7296936492e6802a050afe3deda",
    "components/platform_audio/CMakeLists.txt": "4fce36702e3d18e44bc59f8e65d91fdeb9b81072b57a37b79f62212162827653",
    "components/platform_audio/Kconfig": "b77a0fb7d46bf421ad7df80aa4485f77051b482aeec55de14350074ae38e8835",
    "components/platform_audio/include/platform/audio.h": "78c6ea13ea20b0920a502e2beac534818ece0556a60c5231bde89bdac2029c53",
    "components/platform_audio/include/platform/audio_tone.h": "90b8715f51f049cf8f73ce3dfc64a023f44bd7cc73a2d7f9011d300731434978",
    "components/platform_audio/src/platform_audio.c": "a296a8fe8a5bc0378b2d55f1a920755928f913a52abaf9ecb5611608506d8412",
    "components/platform_audio/src/platform_audio_policy.c": "ae13993595492c277ddd399825b87a9818dcea5eb13f0add4ef7bdcacb3528c1",
    "components/platform_audio/src/platform_audio_policy.h": "189186874e3a3700730765552472990e89304d85984f7165a9066e84b6b6249c",
    "components/platform_audio/src/platform_audio_tone.c": "a1e482a8ef5d8b4c26ca391f3a8d9152956e21794a584728245eeed42ee6252c",
    "components/platform_audio/tests/CMakeLists.txt": "64daaa7077f56eff72e56c8de9708c9ad28007b6be18cd9fa4c49bcaff9def5c",
    "components/platform_audio/tests/test_platform_audio.c": "e917aa9f5e898b10d901e634d7e281ca17ca2fe5442d0030336aad951e89aee5",
}


def fail(message: str) -> None:
    raise SystemExit(f"audio_diag build verification failed: {message}")


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


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def historical_source_path(relative: str) -> pathlib.Path:
    prefix = "components/platform_audio/"
    if relative.startswith(prefix):
        return COMPONENT_DIR / relative.removeprefix(prefix)
    return ROOT / relative


def checked_child(base: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    candidate = (base / relative).resolve()
    require(candidate.is_relative_to(base), f"{label} path leaves expected directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def ordered(text: str, tokens: tuple[str, ...], message: str) -> None:
    cursor = 0
    for token in tokens:
        position = text.find(token, cursor)
        require(position >= 0, f"{message}: missing {token!r}")
        cursor = position + len(token)


def verify_source_inventory(build_evidence: dict) -> None:
    recorded = build_evidence.get("source_inventory")
    require(
        recorded == EXPECTED_SOURCE_INVENTORY,
        "recorded source inventory differs from verifier",
    )
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = historical_source_path(relative)
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(
            sha256_file(path) == expected_hash,
            f"reviewed source hash differs: {relative}",
        )


def verify_managed_codec() -> None:
    require(MANAGED_CODEC_DIR.is_dir(), "managed esp_codec_dev 1.3.4 source is missing")
    require(
        (MANAGED_CODEC_DIR / ".component_hash").read_text() == CODEC_COMPONENT_HASH,
        "managed component registry hash differs",
    )
    checksums_path = MANAGED_CODEC_DIR / "CHECKSUMS.json"
    require(
        sha256_file(checksums_path) == CODEC_CHECKSUMS_SHA256,
        "managed component checksum manifest differs from registry package",
    )
    checksums = load_json(checksums_path)
    require(
        checksums.get("version") == "1.0" and checksums.get("algorithm") == "sha256",
        "managed component checksum format changed",
    )
    entries = checksums.get("files")
    require(isinstance(entries, list) and entries, "managed component checksums are empty")
    declared: set[str] = set()
    for entry in entries:
        require(isinstance(entry, dict), "invalid managed component checksum entry")
        relative = entry.get("path")
        path = checked_child(MANAGED_CODEC_DIR, relative, "managed component file")
        require(relative not in declared, f"duplicate managed component path: {relative}")
        declared.add(relative)
        require(path.stat().st_size == entry.get("size"), f"managed size differs: {relative}")
        require(sha256_file(path) == entry.get("hash"), f"managed hash differs: {relative}")

    actual = {
        path.relative_to(MANAGED_CODEC_DIR).as_posix()
        for path in MANAGED_CODEC_DIR.rglob("*")
        if path.is_file()
    }
    actual -= {".component_hash", "CHECKSUMS.json"}
    require(actual == declared, "managed component contains missing or unregistered files")

    high_level = (MANAGED_CODEC_DIR / "esp_codec_dev.c").read_text()
    es8311 = (MANAGED_CODEC_DIR / "device/es8311/es8311.c").read_text()
    require(
        "codec->set_vol(codec, db_value);\n        return ESP_CODEC_DEV_OK;" in high_level
        and "codec->mute(codec, mute);\n        return ESP_CODEC_DEV_OK;" in high_level,
        "pinned codec setter behavior changed; re-audit register-readback mitigation",
    )
    mute = es8311[
        es8311.index("static int es8311_set_mute"):
        es8311.index("static int es8311_set_vol")
    ]
    require(
        "es8311_write_reg(codec, ES8311_DAC_REG31" in mute and "return ret;" in mute,
        "pinned ES8311 mute-write behavior changed",
    )


def verify_factory_source_evidence() -> None:
    evidence = load_json(ROOT / FACTORY_REL)
    official = evidence.get("official_source", {})
    require(
        evidence.get("schema") == 1
        and evidence.get("classification")
        == "exact-official-factory-source-audio-semantics"
        and evidence.get("result") == "pass-source-audit-build-input-runtime-unproven"
        and evidence.get("scope")
        == "audio_diag_d2_2_controller_and_direct_i2s_probe_selection",
        "factory audio semantic evidence classification changed",
    )
    require(
        official.get("vendor") == "Elecrow"
        and official.get("model") == "DHE04310D"
        and official.get("commit") == "c5a437311b951aaa9d17115bf420877a8f1f7b83"
        and official.get("archive_repository_path")
        == "factory_sourcecode/V1.0/ESP32-P4-Adcance-brookesia_phone_inch10_1.zip"
        and official.get("archive_bytes") == FACTORY_ARCHIVE_BYTES
        and official.get("archive_sha256") == FACTORY_ARCHIVE_SHA256,
        "factory archive binding changed",
    )
    reviewed = {
        entry.get("path"): (entry.get("bytes"), entry.get("sha256"))
        for entry in evidence.get("reviewed_members", [])
        if isinstance(entry, dict)
    }
    require(reviewed == FACTORY_MEMBERS, "factory archive member binding changed")

    semantics = evidence.get("source_semantics", {})
    controllers = semantics.get("controller_selection", {})
    speaker = semantics.get("speaker_tx", {})
    codec = semantics.get("codec_control", {})
    microphone = semantics.get("microphone_path_excluded", {})
    require(
        controllers.get("sdkconfig_i2c") == "CONFIG_BSP_I2C_NUM=1"
        and controllers.get("sdkconfig_i2s") == "CONFIG_BSP_I2S_NUM=1"
        and controllers.get("bsp_i2c_binding") == ".i2c_port = BSP_I2C_NUM"
        and controllers.get("bsp_i2s_binding") == ".id = CONFIG_BSP_I2S_NUM"
        and speaker.get("controller") == 1
        and speaker.get("lrclk_gpio") == 21
        and speaker.get("bclk_gpio") == 22
        and speaker.get("dout_gpio") == 23
        and speaker.get("mclk") == "I2S_GPIO_UNUSED"
        and speaker.get("amplifier_shutdown_gpio") == 30
        and speaker.get("amplifier_enable_level") == 0
        and speaker.get("ordering") == "i2s_channel_enable_before_gpio30_low"
        and codec.get("implementation") == "fake_in_memory_register_array"
        and codec.get("external_i2c_transactions") is False
        and microphone.get("d2_2_initializes_rx") is False,
        "reviewed factory source semantics changed",
    )
    contract = evidence.get("d2_2_derived_contract", {})
    require(
        contract.get("gpio30_high_before_any_log_power_bus_or_clock") is True
        and contract.get("i2c_controller") == 1
        and contract.get("i2c_scan_first_7bit") == "0x08"
        and contract.get("i2c_scan_last_7bit") == "0x77"
        and contract.get("i2c_probe_timeout_per_address_ms") == 10
        and contract.get("es8311_ack_path")
        == "retain_project_guarded_es8311_driver_with_gpio24_mclk"
        and contract.get("es8311_no_ack_path")
        == "delete_i2c_bus_then_factory_style_i2s1_tx_gpio21_gpio22_gpio23_without_mclk"
        and contract.get("zero_preroll_frames") == 256
        and contract.get("tone_count") == 1
        and contract.get("tone_duration_ms") == 400
        and contract.get("tone_peak_pcm") == 512,
        "D2.2 derived factory contract changed",
    )

    # The build remains portable after the temporary archive is removed. When
    # the exact downloaded archive is present, independently re-check every
    # reviewed member and the semantic tokens that drove this probe.
    if FACTORY_ARCHIVE_LOCAL.is_file():
        require(
            FACTORY_ARCHIVE_LOCAL.stat().st_size == FACTORY_ARCHIVE_BYTES
            and sha256_file(FACTORY_ARCHIVE_LOCAL) == FACTORY_ARCHIVE_SHA256,
            "local exact factory archive differs from evidence",
        )
        try:
            with zipfile.ZipFile(FACTORY_ARCHIVE_LOCAL) as archive:
                member_data = {}
                for member, (expected_size, expected_hash) in FACTORY_MEMBERS.items():
                    data = archive.read(member)
                    require(len(data) == expected_size, f"factory member size differs: {member}")
                    require(sha256_bytes(data) == expected_hash, f"factory member hash differs: {member}")
                    member_data[member] = data.decode("utf-8", errors="strict")
        except (OSError, KeyError, UnicodeError, zipfile.BadZipFile) as error:
            fail(f"cannot reverify local factory archive: {error}")

        board = member_data[
            "components/espressif__esp32_p4_function_ev_board/esp32_p4_function_ev_board.c"
        ]
        header = member_data[
            "components/espressif__esp32_p4_function_ev_board/include/bsp/esp32_p4_function_ev_board.h"
        ]
        fake = member_data[
            "components/espressif__esp32_p4_function_ev_board/my_codec.c"
        ]
        sdkconfig = member_data["sdkconfig"]
        require(
            "CONFIG_BSP_I2C_NUM=1" in sdkconfig
            and "CONFIG_BSP_I2S_NUM=1" in sdkconfig
            and ".i2c_port = BSP_I2C_NUM" in board
            and ".id = CONFIG_BSP_I2S_NUM" in board
            and ".mclk = I2S_GPIO_UNUSED" in board
            and board.index("i2s_channel_enable(i2s_tx_chan)")
            < board.index("gpio_set_level(BSP_POWER_AMP_IO, 0)")
            and "#define BSP_I2S_SCLK          (GPIO_NUM_22)" in header
            and "#define BSP_I2S_LCLK          (GPIO_NUM_21)" in header
            and "#define BSP_I2S_DOUT          (GPIO_NUM_23)" in header
            and "#define BSP_POWER_AMP_IO      (GPIO_NUM_30)" in header
            and "*(uint8_t *) data = ctrl_if->reg[addr];" in fake
            and "ctrl_if->reg[addr] = *(uint8_t *) data;" in fake
            and "i2c_master_" not in fake,
            "local factory source semantics differ from reviewed interpretation",
        )


def verify_prior_records() -> None:
    prior_auth = ROOT / PRIOR_AUTHORIZATION_REL
    require(
        prior_auth.is_file() and sha256_file(prior_auth) == PRIOR_AUTHORIZATION_SHA256,
        "prior D2.1 one-shot authorization record changed",
    )

    d20 = load_json(ROOT / PRIOR_RUNTIME_EVIDENCE_RELS[0])
    artifact = d20.get("artifact", {})
    observed = d20.get("runtime", {})
    require(
        d20.get("schema") == 1
        and d20.get("result") == "fail-safe-codec-i2c-no-ack"
        and d20.get("app") == "audio_diag"
        and artifact.get("offset") == "0x10000"
        and artifact.get("bytes") == 243184
        and artifact.get("sha256")
        == "4c87744b31fd4317d978a5f922ec22904b223d5d5a1a1b945d1c6306484a7e6a"
        and artifact.get("readback_verified") is True
        and observed.get("codec_address_7bit") == "0x18"
        and observed.get("tone_started") is False
        and observed.get("amp_enabled") is False
        and observed.get("amp_shutdown_gpio30") == 1,
        "prior D2.0 fail-safe runtime evidence changed",
    )

    d21 = load_json(ROOT / PRIOR_RUNTIME_EVIDENCE_RELS[1])
    artifact = d21.get("artifact", {})
    observed = d21.get("runtime", {})
    require(
        d21.get("schema") == 1
        and d21.get("result") == "fail-safe-codec-probe-no-ack-after-board-power"
        and d21.get("app") == "audio_diag"
        and artifact.get("offset") == "0x10000"
        and artifact.get("bytes") == 244752
        and artifact.get("sha256")
        == "920b8100411416c0dd197cb7c1f2c1986335ae1ebf0d30585f4be80ad4da8c98"
        and artifact.get("readback_verified") is True
        and observed.get("board_power_ready_marker_seen") is True
        and observed.get("i2c_controller") == 0
        and observed.get("i2c_result") == "ESP_ERR_NOT_FOUND"
        and observed.get("tone_started") is False
        and observed.get("amp_enabled") is False
        and observed.get("amp_shutdown_gpio30") == 1,
        "prior D2.1 fail-safe runtime evidence changed",
    )


def verify_source_contract() -> None:
    source = (COMPONENT_DIR / "src/platform_audio.c").read_text()
    policy = (COMPONENT_DIR / "src/platform_audio_policy.h").read_text()
    tone = (COMPONENT_DIR / "src/platform_audio_tone.c").read_text()
    tests = (COMPONENT_DIR / "tests/test_platform_audio.c").read_text()
    diag = (APP_DIR / "main/audio_diag_main.c").read_text()

    policy_tokens = (
        "PLATFORM_AUDIO_I2S_CONTROLLER = 1",
        "PLATFORM_AUDIO_GPIO_LRCLK = 21",
        "PLATFORM_AUDIO_GPIO_BCLK = 22",
        "PLATFORM_AUDIO_GPIO_DOUT = 23",
        "PLATFORM_AUDIO_GPIO_MCLK = 24",
        "PLATFORM_AUDIO_GPIO_AMP_SHUTDOWN = 30",
        "PLATFORM_AUDIO_GPIO_I2C_SDA = 45",
        "PLATFORM_AUDIO_GPIO_I2C_SCL = 46",
        "PLATFORM_AUDIO_ES8311_WIRE_ADDRESS = 0x30",
        "PLATFORM_AUDIO_ES8311_7BIT_ADDRESS = 0x18",
    )
    require(all(token in policy for token in policy_tokens), "reviewed electrical constants changed")

    guard_tokens = (
        "#define ES8311_CLOCK_SOURCE_REGISTER 0x01",
        "#define ES8311_DAC_MUTE_REGISTER 0x31",
        "#define ES8311_DAC_VOLUME_REGISTER 0x32",
        "audio->codec_interface->get_reg",
        "CLOCK_FORMAT_GUARDS",
        "verify_codec_clock_and_format",
        "verify_codec_mute_register",
        "verify_codec_volume_register",
        "verify_codec_output_guard",
        "fail_with_amplifier_shutdown",
        "result != ESP_OK ? result : ESP_ERR_INVALID_RESPONSE",
    )
    require(all(token in source for token in guard_tokens), "ES8311 raw-readback guard changed")

    create = source[
        source.index("esp_err_t platform_audio_create"):
        source.index("esp_err_t platform_audio_start")
    ]
    ordered(
        create,
        (
            "platform_audio_force_safe_shutdown()",
            "configure_i2s(audio)",
            "audio_codec_new_i2c_ctrl",
            "audio_codec_new_i2s_data",
            ".pa_pin = -1",
            "es8311_codec_new",
            "esp_codec_dev_new",
            "esp_codec_dev_open",
            "set_codec_mute_verified(audio, true)",
            "set_codec_volume_verified(audio)",
            "write_zero_preroll",
        ),
        "safe guarded-codec creation order changed",
    )
    require(
        ".use_mclk = true" in create
        and ".master_mode = false" in create
        and ".mclk_div = PLATFORM_AUDIO_MCLK_MULTIPLE" in create,
        "guarded ES8311 MCLK/slave policy changed",
    )

    start = source[
        source.index("esp_err_t platform_audio_start"):
        source.index("esp_err_t platform_audio_write_frames")
    ]
    ordered(
        start,
        (
            "write_zero_preroll",
            "verify_codec_output_guard(audio, true)",
            "set_amplifier_enabled(true)",
            "vTaskDelay",
            "set_codec_mute_verified(audio, false)",
        ),
        "safe guarded-codec amplifier order changed",
    )
    stop = source[
        source.index("esp_err_t platform_audio_stop"):
        source.index("esp_err_t platform_audio_get_state")
    ]
    require(
        stop.index("set_codec_mute_verified(audio, true)")
        < stop.index("vTaskDelay")
        < stop.rindex("platform_audio_force_safe_shutdown"),
        "safe guarded-codec stop order changed",
    )

    require(
        "((int64_t)scaled * (int64_t)envelope)" in tone
        and "(int64_t)config->fade_frames" in tone
        and "test_large_fade_math_stays_bounded" in tests
        and "FADE_FRAMES = 65537" in tests
        and "PEAK = INT16_MAX" in tests,
        "bounded tone arithmetic regression guard changed",
    )

    diag_tokens = (
        "#define AUDIO_DIAG_I2C_PORT I2C_NUM_1",
        "#define AUDIO_DIAG_I2C_SCAN_FIRST_ADDRESS 0x08U",
        "#define AUDIO_DIAG_I2C_SCAN_LAST_ADDRESS 0x77U",
        "#define AUDIO_DIAG_I2C_SCAN_PROBE_TIMEOUT_MS 10",
        "#define AUDIO_DIAG_ES8311_7BIT_ADDRESS 0x18U",
        "#define AUDIO_DIAG_I2S_PORT I2S_NUM_1",
        "#define AUDIO_DIAG_I2S_LRCLK GPIO_NUM_21",
        "#define AUDIO_DIAG_I2S_BCLK GPIO_NUM_22",
        "#define AUDIO_DIAG_I2S_DOUT GPIO_NUM_23",
        "#define AUDIO_DIAG_AMP_SHUTDOWN GPIO_NUM_30",
        "#define AUDIO_DIAG_TONE_DURATION_MS 400U",
        "#define AUDIO_DIAG_FADE_DURATION_MS 50U",
        "#define AUDIO_DIAG_PEAK_AMPLITUDE 512U",
        "#define AUDIO_DIAG_ZERO_PREROLL_FRAMES 256U",
        "#define AUDIO_DIAG_I2S_WRITE_TIMEOUT_MS 100U",
        "#define AUDIO_DIAG_MAIN_TASK_STACK_BYTES 3584U",
        "#define AUDIO_DIAG_LDO3_CHANNEL 3",
        "#define AUDIO_DIAG_LDO4_CHANNEL 4",
        "i2c_master_probe",
        "i2c_del_master_bus",
        "i2s_channel_write",
        "platform_audio_create",
        "platform_audio_start",
        "platform_audio_stop",
        "platform_audio_destroy",
    )
    require(all(token in diag for token in diag_tokens), "bounded D2.2 policy changed")

    app_main = diag[diag.index("void app_main(void)"):]
    require(
        app_main.index("platform_audio_force_safe_shutdown()") < app_main.index("ESP_LOGI"),
        "GPIO30 shutdown is not the first app_main side effect",
    )
    ordered(
        app_main,
        (
            "platform_audio_force_safe_shutdown()",
            "ESP_LOGI",
            "acquire_board_power()",
            "create_control_bus(&s_control_bus)",
            "scan_control_bus(&es8311_ack, &ack_count)",
            "platform_audio_generate_quiet_tone",
            "if (es8311_ack)",
        ),
        "D2.2 startup order changed",
    )
    branch = app_main[app_main.index("if (es8311_ack)"):]
    require(
        branch.index("run_guarded_codec_tone") < branch.index("} else {")
        and branch.index("delete_control_bus()") < branch.index("run_direct_i2s_tone"),
        "ACK-gated guarded/direct path selection changed",
    )

    scan = diag[
        diag.index("static esp_err_t scan_control_bus"):
        diag.index("static esp_err_t direct_i2s_write")
    ]
    require(
        "address = AUDIO_DIAG_I2C_SCAN_FIRST_ADDRESS" in scan
        and "address <= AUDIO_DIAG_I2C_SCAN_LAST_ADDRESS" in scan
        and "AUDIO_DIAG_I2C_SCAN_PROBE_TIMEOUT_MS" in scan
        and "P4_AUDIO D2.2 I2C_ACK" in scan
        and "P4_AUDIO D2.2 I2C_SCAN_DONE" in scan
        and "result != ESP_ERR_NOT_FOUND && result != ESP_ERR_TIMEOUT" in scan,
        "bounded I2C1 scan or ACK logging changed",
    )

    direct_create = diag[
        diag.index("static esp_err_t create_direct_i2s"):
        diag.index("static esp_err_t run_guarded_codec_tone")
    ]
    ordered(
        direct_create,
        ("i2s_new_channel", "i2s_channel_init_std_mode", "i2s_channel_enable"),
        "direct I2S creation order changed",
    )
    require(
        ".mclk = I2S_GPIO_UNUSED" in direct_create
        and ".bclk = AUDIO_DIAG_I2S_BCLK" in direct_create
        and ".ws = AUDIO_DIAG_I2S_LRCLK" in direct_create
        and ".dout = AUDIO_DIAG_I2S_DOUT" in direct_create
        and ".din = I2S_GPIO_UNUSED" in direct_create,
        "factory-style direct I2S pin or no-MCLK contract changed",
    )
    direct_run = diag[
        diag.index("static esp_err_t run_direct_i2s_tone"):
        diag.index("void app_main(void)")
    ]
    ordered(
        direct_run,
        (
            "create_direct_i2s()",
            "direct_i2s_write(\n        s_direct_zeros, AUDIO_DIAG_ZERO_PREROLL_FRAMES",
            "gpio_set_level(AUDIO_DIAG_AMP_SHUTDOWN, 0U)",
            "vTaskDelay",
            "P4_AUDIO D2.2 TONE_BEGIN",
            "direct_i2s_write(\n            &tone",
            "direct_i2s_write(\n            s_direct_zeros, AUDIO_DIAG_ZERO_PREROLL_FRAMES",
            "platform_audio_force_safe_shutdown()",
            "delete_direct_i2s()",
        ),
        "direct I2S zero-preroll/amp/tone/fail-high order changed",
    )
    direct_write = diag[
        diag.index("static esp_err_t direct_i2s_write"):
        diag.index("static esp_err_t create_direct_i2s")
    ]
    require(
        "frame_count * PLATFORM_AUDIO_CHANNEL_COUNT" in direct_write
        and "sizeof(*samples)" in direct_write
        and "AUDIO_DIAG_I2S_WRITE_TIMEOUT_MS" in direct_write
        and "bytes_written == bytes ? ESP_OK : ESP_ERR_INVALID_RESPONSE" in direct_write,
        "direct I2S exact-byte write contract changed",
    )

    acquire = diag[
        diag.index("static esp_err_t acquire_board_power"):
        diag.index("static esp_err_t create_control_bus")
    ]
    ordered(
        acquire,
        (
            "AUDIO_DIAG_LDO3_CHANNEL",
            "esp_ldo_acquire_channel(&ldo3_config, &s_ldo3)",
            "AUDIO_DIAG_LDO4_CHANNEL",
            "esp_ldo_acquire_channel(&ldo4_config, &s_ldo4)",
            "vTaskDelay",
        ),
        "LDO acquisition order changed",
    )
    release = diag[
        diag.index("static esp_err_t release_board_power"):
        diag.index("static esp_err_t delete_control_bus")
    ]
    require(release.index("s_ldo4") < release.index("s_ldo3"), "LDO release order changed")
    cleanup = diag[
        diag.index("static esp_err_t cleanup_runtime"):
        diag.index("static void halt_safe")
    ]
    ordered(
        cleanup,
        (
            "platform_audio_force_safe_shutdown()",
            "platform_audio_destroy(&s_audio)",
            "delete_control_bus()",
            "delete_direct_i2s()",
            "platform_audio_force_safe_shutdown()",
            "release_board_power()",
        ),
        "fail-high cleanup order changed",
    )
    require(
        "if (result != ESP_OK) {\n        /* Do not stop clocks or remove rails while shutdown is unconfirmed. */\n        return result;" in cleanup,
        "cleanup may tear down clocks or rails without confirmed GPIO30 shutdown",
    )

    forbidden = ("esp_lcd", "lvgl", "sdmmc", "esp_wifi", "usb_host", "tinyusb")
    require(not any(token in diag for token in forbidden), "diagnostic initializes an absent interface")
    main_cmake = (APP_DIR / "main/CMakeLists.txt").read_text()
    require(
        all(token in main_cmake for token in ("esp_driver_gpio", "esp_driver_i2c", "esp_driver_i2s")),
        "D2.2 direct driver dependencies changed",
    )
    require(
        "CONFIG_ESPTOOLPY_AFTER_NORESET=y" in (APP_DIR / "sdkconfig.defaults").read_text()
        and "CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584" in (APP_DIR / "sdkconfig.defaults").read_text(),
        "audio image would launch before exact readback",
    )

    flash = (ROOT / "scripts/flash.sh").read_text()
    require(
        'if [ "$P4_APP" = audio_diag ]; then' in flash
        and '"$P4_SCRIPT_DIR/verify-audio-diag.py"' in flash
        and flash.count('[ "$P4_APP" = audio_diag ]') >= 3,
        "central flash path is not bound to the audio verifier",
    )
    ordered(
        flash,
        (
            "P4_READBACK_AFTER=no_reset",
            "p4_verify_chunked_application_readback",
            'if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then',
            "Application readback verified:",
            'esptool.py --chip esp32p4 --port "$P4_PORT" run',
            "launched exactly once after successful application readback",
        ),
        "deferred exact-readback launch gate changed",
    )


def verify_linked_symbols(elf: pathlib.Path, description: dict) -> None:
    compiler = description.get("c_compiler")
    require(isinstance(compiler, str) and compiler, "build does not record its C compiler")
    nm = pathlib.Path(compiler).with_name("riscv32-esp-elf-nm")
    require(nm.is_file(), "pinned RISC-V nm is missing")
    try:
        result = subprocess.run(
            [str(nm), "-g", str(elf)],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect linked ELF: {error}")
    symbols = result.stdout
    required = (
        "platform_audio_force_safe_shutdown",
        "platform_audio_create",
        "platform_audio_start",
        "platform_audio_write_frames",
        "platform_audio_stop",
        "platform_audio_destroy",
        "es8311_codec_new",
        "esp_codec_dev_open",
        "esp_codec_dev_write",
        "i2c_new_master_bus",
        "i2c_master_probe",
        "i2c_del_master_bus",
        "i2s_new_channel",
        "i2s_channel_init_std_mode",
        "i2s_channel_enable",
        "i2s_channel_write",
        "i2s_channel_disable",
        "i2s_del_channel",
        "esp_ldo_acquire_channel",
        "esp_ldo_release_channel",
        "gpio_set_level",
    )
    require(all(symbol in symbols for symbol in required), "required dual audio path is not linked")
    forbidden = (
        "sdmmc_card_init",
        "usb_host_install",
        "esp_wifi_init",
        "esp_lcd_new_panel",
        "tinyusb_driver_install",
        "lv_init",
    )
    require(not any(symbol in symbols for symbol in forbidden), "an absent peripheral is linked")


def verify_stack_layout(
    elf: pathlib.Path,
    description: dict,
    sdkconfig: dict,
    recorded_build: dict,
) -> None:
    compiler = description.get("c_compiler")
    require(isinstance(compiler, str) and compiler, "build does not record its C compiler")
    tool_dir = pathlib.Path(compiler).parent
    objdump = tool_dir / "riscv32-esp-elf-objdump"
    nm = tool_dir / "riscv32-esp-elf-nm"
    require(objdump.is_file() and nm.is_file(), "pinned RISC-V ELF tools are missing")
    try:
        disassembly = subprocess.run(
            [str(objdump), "-d", str(elf)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        symbols = subprocess.run(
            [str(nm), "-S", "--size-sort", str(elf)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect exact ELF stack layout: {error}")

    app_match = re.search(
        r"<app_main>:\n[^\n]*\baddi\s+sp,sp,-(\d+)\b", disassembly
    )
    caller_match = re.search(
        r"<main_task>:\n[^\n]*\baddi\s+sp,sp,-(\d+)\b", disassembly
    )
    require(app_match is not None, "cannot derive optimized app_main stack frame")
    require(caller_match is not None, "cannot derive IDF main_task stack frame")
    app_frame = int(app_match.group(1))
    caller_frame = int(caller_match.group(1))
    require(
        re.search(r"\b[bB]\s+s_direct_zeros$", symbols, re.MULTILINE) is not None,
        "direct silence buffer is not linked in BSS",
    )
    zero_match = re.search(
        r"^[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+[bB]\s+s_direct_zeros$",
        symbols,
        re.MULTILINE,
    )
    require(zero_match is not None, "cannot derive direct silence-buffer size")
    zero_bytes = int(zero_match.group(1), 16)

    configured = sdkconfig.get("ESP_MAIN_TASK_STACK_SIZE")
    require(configured == 3584, "generated main-task stack differs from frozen review")
    require(
        sdkconfig.get("LIBC_NEWLIB_NANO_FORMAT") is False,
        "Newlib nano setting changed; IDF main-task extra stack must be re-derived",
    )
    idf_extra = 512
    allocated = configured + idf_extra
    app_local_headroom = allocated - app_frame
    call_chain_headroom = allocated - caller_frame - app_frame
    require(
        recorded_build.get("stack_review")
        == {
            "configured_main_task_stack_bytes": configured,
            "idf_newlib_extra_stack_bytes": idf_extra,
            "allocated_main_task_stack_bytes": allocated,
            "idf_main_task_static_frame_bytes": caller_frame,
            "app_main_static_frame_bytes": app_frame,
            "app_main_local_static_headroom_before_callees_bytes": app_local_headroom,
            "main_task_chain_static_headroom_before_app_callees_bytes": call_chain_headroom,
            "direct_zero_buffer_storage": "bss",
            "direct_zero_buffer_bytes": zero_bytes,
            "dynamic_tone_storage": "heap_calloc_25600_bytes",
            "scope_caveat": "static_frames_only_excludes_called_driver_library_and_interrupt_frames",
        },
        "recorded optimized stack/BSS review differs from exact ELF",
    )


def verify_current_authorization(
    relative: object,
    binary: pathlib.Path,
    build_evidence: dict,
) -> None:
    require(isinstance(relative, str) and relative, "D2.2 app-flash lacks authorization evidence")
    auth = load_json(ROOT / relative)
    artifact = auth.get("exact_artifact", {})
    limits = auth.get("exact_runtime_limits", {})
    boundary = auth.get("exception_boundary", {})
    require(
        auth.get("schema") == 1
        and auth.get("classification") == "user-requested-bounded-audio-one-shot"
        and auth.get("result") == "authorized-exact-app-preflash-reviewed"
        and auth.get("scope") == "audio_diag_d2_2_i2c1_scan_single_bounded_tone"
        and auth.get("factory_source_evidence") == FACTORY_REL
        and auth.get("build_evidence") == BUILD_EVIDENCE_REL,
        "current D2.2 one-shot authorization classification changed",
    )
    require(
        artifact.get("offset") == "0x10000"
        and artifact.get("bytes") == binary.stat().st_size
        and artifact.get("sha256") == sha256_file(binary)
        and artifact.get("sha256")
        == build_evidence.get("artifacts", {}).get("app_binary_sha256"),
        "current D2.2 authorization is not bound to this exact application",
    )
    require(
        limits
        == {
            "i2c_controller": 1,
            "scan_first_7bit": "0x08",
            "scan_last_7bit": "0x77",
            "probe_timeout_per_address_ms": 10,
            "tone_count": 1,
            "frequency_hz": 440,
            "duration_ms": 400,
            "fade_ms": 50,
            "pcm_peak": 512,
            "zero_preroll_frames": 256,
            "amplifier_shutdown_gpio": 30,
            "amplifier_shutdown_level": 1,
        },
        "current D2.2 authorization limits changed",
    )
    require(
        boundary.get("application_flash_scope") == "factory_app_partition_only_at_0x10000"
        and boundary.get("full_project_flash_authorized") is False
        and boundary.get("single_launch_after_exact_readback") is True
        and boundary.get("reusable_board_profile_audio_policy_unchanged") is True
        and boundary.get("prior_runtime_evidence") == PRIOR_RUNTIME_EVIDENCE_RELS,
        "current D2.2 authorization boundary changed",
    )


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-audio-diag.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "target must be build-only or app-flash")

    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "audio_diag", "wrong metadata")
    require(
        metadata.get("stage") == "D2.2-factory-semantics-dual-path-build-test"
        and metadata.get("evidence_class") == "build-tested"
        and metadata.get("authorization_evidence") == ELECTRICAL_REL
        and metadata.get("factory_source_evidence") == FACTORY_REL
        and metadata.get("prior_one_shot_authorization_evidence") == PRIOR_AUTHORIZATION_REL
        and metadata.get("build_evidence") == BUILD_EVIDENCE_REL,
        "D2.2 metadata evidence binding changed",
    )
    for flag in ("runtime_supported", "flash_authorized", "flash_project_authorized"):
        require(metadata.get(flag) is False, f"{flag} must remain false")
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False, "build-only requires app flash false")
        require(metadata.get("one_shot_authorization_evidence") is None, "build-only has active auth")
    else:
        require(metadata.get("flash_app_authorized") is True, "app-only flash is not authorized")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES, "interface scope changed")
    require(
        set(metadata.get("hardware_interfaces_explicitly_absent", [])) == EXPECTED_ABSENT,
        "interface denials changed",
    )
    require(metadata.get("runtime_evidence") is None, "metadata claims unrecorded runtime evidence")
    require(
        metadata.get("prior_runtime_evidence") == PRIOR_RUNTIME_EVIDENCE_RELS,
        "metadata lost prior fail-safe runtime bindings",
    )

    verify_prior_records()
    verify_factory_source_evidence()

    profile = load_json(ROOT / "hardware/board-profile.json")
    audio = profile.get("peripheral_authorizations", {}).get("audio", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(
        audio.get("authorized") is False
        and audio.get("scope") == "topology_review_only_no_gpio_or_driver_authorized"
        and audio.get("hardware_test") is None,
        "board profile unexpectedly authorizes audio runtime",
    )

    electrical = load_json(ROOT / ELECTRICAL_REL)
    invariants = electrical.get("cross_revision_invariants", {})
    codec_path = invariants.get("codec", {})
    amp = invariants.get("power_amplifier", {})
    power = invariants.get("board_power_prerequisite", {})
    require(
        electrical.get("schema") == 1
        and electrical.get("classification") == "cross-revision-audio-driver-path"
        and electrical.get("result") == "pass-build-only-runtime-denied"
        and power.get("ldo3", {}).get("channel") == 3
        and power.get("ldo3", {}).get("voltage_mv") == 2500
        and power.get("ldo4", {}).get("channel") == 4
        and power.get("ldo4", {}).get("voltage_mv") == 3300
        and codec_path.get("data_path", {}).get("controller") == "I2S_NUM_1"
        and codec_path.get("data_path", {}).get("lrclk", {}).get("p4_gpio") == 21
        and codec_path.get("data_path", {}).get("bclk", {}).get("p4_gpio") == 22
        and codec_path.get("data_path", {}).get("dout", {}).get("p4_gpio") == 23
        and amp.get("enabled_gpio_level") == 0
        and amp.get("shutdown_gpio_level") == 1,
        "cross-revision electrical boundary changed",
    )

    build_evidence = load_json(ROOT / BUILD_EVIDENCE_REL)
    require(
        build_evidence.get("schema") == 1
        and build_evidence.get("classification") == "build-tested"
        and build_evidence.get("result") == "pass-build-only-runtime-denied"
        and build_evidence.get("scope")
        == "elecrow_factory_semantics_dual_path_bounded_audio_probe"
        and build_evidence.get("authorization_evidence") == ELECTRICAL_REL
        and build_evidence.get("factory_source_evidence") == FACTORY_REL
        and build_evidence.get("prior_one_shot_authorization_evidence")
        == PRIOR_AUTHORIZATION_REL
        and build_evidence.get("current_one_shot_authorization_evidence") is None,
        "build evidence classification or binding changed",
    )
    execution = build_evidence.get("execution", {})
    require(
        execution
        == {
            "firmware_flashed": False,
            "firmware_executed": False,
            "hardware_accessed": False,
            "audio_path_energized": False,
            "runtime_supported": False,
            "serial_evidence": None,
        },
        "build evidence claims hardware execution",
    )
    verify_source_inventory(build_evidence)
    verify_source_contract()

    lock = load_json(ROOT / "toolchain.lock.json")
    target = lock["target"]
    recorded_toolchain = build_evidence.get("toolchain", {})
    recorded_build = build_evidence.get("build", {})
    require(
        recorded_toolchain.get("esp_idf_version") == lock["esp_idf"]["version"]
        and recorded_toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"]
        and recorded_build.get("target") == target["chip"]
        and recorded_build.get("minimum_revision_full") == target["min_revision_full"]
        and recorded_build.get("maximum_revision_full") == target["max_revision_full"],
        "recorded toolchain or target differs from lock",
    )
    require(
        recorded_build.get("reproducible_build") is True
        and recorded_build.get("compile_time_date_enabled") is False
        and recorded_build.get("clean_builds_compared", 0) >= 2
        and recorded_build.get("independent_build_directories_compared", 0) >= 2
        and recorded_build.get("identical_binary") is True
        and recorded_build.get("identical_elf") is True
        and recorded_build.get("identical_bootloader") is True
        and recorded_build.get("identical_partition_table") is True
        and recorded_build.get("no_reset_before_readback") is True
        and recorded_build.get("first_app_main_call") == "platform_audio_force_safe_shutdown"
        and recorded_build.get("platform_audio_component_modified_for_d22") is False
        and recorded_build.get("exact_factory_archive_rehashed_locally") is True
        and recorded_build.get("i2c1_bounded_full_legal_address_scan") is True
        and recorded_build.get("factory_direct_i2s_fallback_no_mclk") is True
        and recorded_build.get("guarded_es8311_path_retained_on_ack") is True
        and recorded_build.get("prior_runtime_failures_bound")
        == PRIOR_RUNTIME_EVIDENCE_RELS
        and recorded_build.get("global_pin_map_authorized") is False
        and recorded_build.get("audio_runtime_authorized") is False,
        "reviewed reproducibility or D2.2 safety evidence changed",
    )

    managed = build_evidence.get("managed_codec", {})
    require(
        managed.get("component") == "espressif/esp_codec_dev"
        and managed.get("version") == CODEC_VERSION
        and managed.get("registry_component_hash") == CODEC_COMPONENT_HASH
        and managed.get("dependencies_lock_sha256")
        == EXPECTED_SOURCE_INVENTORY["apps/audio_diag/dependencies.lock"],
        "recorded codec dependency differs from the reviewed pin",
    )
    verify_managed_codec()

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True, "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True, "compile-time date is enabled")
    require(sdkconfig.get("IDF_TARGET") == target["chip"], "generated target differs")
    require(
        sdkconfig.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdkconfig.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated build would launch before readback",
    )
    require(
        sdkconfig.get("PLATFORM_AUDIO_ELECROW_10_1_REVIEWED_BUILD_ONLY") is True
        and sdkconfig.get("CODEC_I2C_BACKWARD_COMPATIBLE") is False
        and sdkconfig.get("CODEC_ES8311_SUPPORT") is True,
        "guarded ES8311 build configuration changed",
    )

    description = load_json(build_dir / "project_description.json")
    require(description.get("project_name") == "p4_audio_diag", "wrong project")
    require(description.get("target") == target["chip"], "generated target differs from lock")
    require(int(description.get("min_rev")) == target["min_revision_full"], "minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"], "maximum revision differs")

    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    bootloader = flash_args.get("bootloader", {})
    partition = flash_args.get("partition-table", {})
    require(int(str(app.get("offset")), 0) == 0x10000, "application offset changed")
    require(int(str(bootloader.get("offset")), 0) == 0x2000, "bootloader offset changed")
    require(int(str(partition.get("offset")), 0) == 0x8000, "partition offset changed")
    require(flash_args.get("flash_settings", {}).get("flash_size") == "16MB", "flash size changed")
    require(
        flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
        "flash after-action must remain no_reset",
    )

    binary = checked_child(build_dir, app.get("file"), "application binary")
    elf = checked_child(build_dir, description.get("app_elf"), "application ELF")
    bootloader_binary = checked_child(build_dir, bootloader.get("file"), "bootloader binary")
    partition_binary = checked_child(build_dir, partition.get("file"), "partition table binary")

    artifacts = build_evidence.get("artifacts", {})
    for path, size_key, hash_key in (
        (binary, "app_binary_bytes", "app_binary_sha256"),
        (elf, "elf_bytes", "elf_sha256"),
        (bootloader_binary, "bootloader_bytes", "bootloader_sha256"),
        (partition_binary, "partition_table_bytes", "partition_table_sha256"),
    ):
        require(path.stat().st_size == artifacts.get(size_key), f"{path.name} size differs")
        require(sha256_file(path) == artifacts.get(hash_key), f"{path.name} hash differs")
    require(
        artifacts.get("generated_app_partition_free_bytes")
        == artifacts.get("generated_app_partition_bytes") - binary.stat().st_size
        and artifacts.get("saved_factory_app_partition_free_bytes")
        == artifacts.get("saved_factory_app_partition_bytes") - binary.stat().st_size,
        "artifact free-space arithmetic changed",
    )

    policy_record = build_evidence.get("diagnostic_policy", {})
    require(
        policy_record.get("board_power")
        == {
            "ldo3_channel": 3,
            "ldo3_voltage_mv": 2500,
            "ldo4_channel": 4,
            "ldo4_voltage_mv": 3300,
            "settle_ms": 20,
            "acquisition_order": "ldo3_then_ldo4",
            "release_order": "ldo4_then_ldo3",
        }
        and policy_record.get("i2c_scan")
        == {
            "controller": 1,
            "sda_gpio": 45,
            "scl_gpio": 46,
            "first_7bit": "0x08",
            "last_7bit": "0x77",
            "probe_timeout_per_address_ms": 10,
            "ack_addresses_logged": True,
            "guarded_codec_address_7bit": "0x18",
        }
        and policy_record.get("tone")
        == {
            "count": 1,
            "frequency_hz": 440,
            "duration_ms": 400,
            "fade_ms": 50,
            "pcm_peak": 512,
            "pcm_full_scale": 32767,
        }
        and policy_record.get("direct_fallback")
        == {
            "i2s_controller": 1,
            "lrclk_gpio": 21,
            "bclk_gpio": 22,
            "dout_gpio": 23,
            "mclk": "unused",
            "sample_rate_hz": 16000,
            "channels": 2,
            "bits_per_sample": 16,
            "zero_preroll_frames": 256,
            "zero_postroll_frames": 256,
            "amp_enable_after_i2s_ready_and_zero_preroll": True,
            "amp_settle_ms": 20,
            "write_timeout_ms": 100,
            "exact_write_accounting": {
                "bytes_per_frame": 4,
                "preroll_write_calls": 1,
                "preroll_bytes": 1024,
                "tone_write_calls": 50,
                "tone_bytes_per_call": 512,
                "tone_bytes": 25600,
                "postroll_write_calls": 1,
                "postroll_bytes": 1024,
                "total_write_calls": 52,
                "total_bytes": 27648,
                "every_call_requires_bytes_written_equal_requested": True,
            },
        }
        and policy_record.get("guarded_codec_path")
        == {
            "selected_only_if_addr7_0x18_acks_on_i2c1": True,
            "i2s_controller": 1,
            "mclk_gpio": 24,
            "mclk_hz": 4096000,
            "codec_volume_percent": 5,
            "raw_register_readback_guards_retained": True,
        }
        and policy_record.get("amplifier")
        == {"gpio": 30, "shutdown_level": 1, "enable_level": 0},
        "D2.2 diagnostic policy changed",
    )
    require(
        build_evidence.get("serial_acceptance")
        == {
            "pass_no_ack_exact": "P4_AUDIO D2.2 PASS path=factory-direct-i2s tone_count=1 bounded_ms=400 amp_shutdown=1 ldo3_released=1 ldo4_released=1",
            "pass_ack_exact": "P4_AUDIO D2.2 PASS path=guarded-es8311 tone_count=1 bounded_ms=400 amp_shutdown=1 ldo3_released=1 ldo4_released=1",
            "halt_prefix": "P4_AUDIO D2.2 HALT stage=",
            "halt_suffix": "amp_shutdown_requested=1",
            "reject_markers": [
                "P4_AUDIO D2.2 HALT",
                "Guru Meditation Error",
                "Task watchdog got triggered",
                "assert failed",
            ],
            "runtime_pass_requirements": [
                "exact application readback verified before the single launch",
                "exactly one path-specific PASS marker",
                "operator confirms the bounded tone was audible",
                "no reject marker",
            ],
        },
        "D2.2 serial acceptance contract changed",
    )
    verify_linked_symbols(elf, description)
    verify_stack_layout(elf, description, sdkconfig, recorded_build)

    if mode == "app-flash":
        verify_current_authorization(
            metadata.get("one_shot_authorization_evidence"), binary, build_evidence
        )

    print(
        f"audio_diag verification: PASS mode={mode} offset=0x10000 "
        f"bytes={binary.stat().st_size} sha256={artifacts['app_binary_sha256']} "
        f"runtime_authorization={'one-shot-exception' if mode == 'app-flash' else 'false'}"
    )


if __name__ == "__main__":
    main()
