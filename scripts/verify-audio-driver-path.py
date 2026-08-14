#!/usr/bin/env python3

"""Verify the cross-revision audio driver path without authorizing hardware."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-10.1-audio-driver-path.json"


def fail(message: str) -> None:
    raise SystemExit(f"audio driver path verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def git_head(tree: pathlib.Path) -> str:
    return subprocess.run(
        ["git", "-C", str(tree), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def schematic_data(
    path: pathlib.Path,
) -> tuple[dict[str, dict[str, str]], dict[tuple[str, str], str]]:
    root = ET.parse(path).getroot()
    parts = {
        part.get("name", ""): dict(part.attrib)
        for part in root.findall(".//parts/part")
    }
    pin_nets: dict[tuple[str, str], str] = {}
    for net in root.findall(".//sheets/sheet/nets/net"):
        name = net.get("name", "")
        for pinref in net.findall(".//pinref"):
            pin_nets[(pinref.get("part", ""), pinref.get("pin", ""))] = name
    return parts, pin_nets


def require_same_net(
    pin_nets: dict[tuple[str, str], str],
    name: str,
    *pins: tuple[str, str],
) -> None:
    require(all(pin_nets.get(pin) == name for pin in pins), f"{name} differs: {pins}")


def verify_structure(evidence: dict) -> None:
    require(evidence.get("schema") == 1, "unsupported schema")
    require(
        evidence.get("classification") == "cross-revision-audio-driver-path",
        "wrong classification",
    )
    require(
        evidence.get("result") == "pass-build-only-runtime-denied",
        "result must remain build-only",
    )
    source = evidence.get("official_elecrow_source", {})
    require(source.get("model") == "DHE04310D", "wrong Elecrow model")
    require(
        source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"],
        "published revision coverage changed",
    )
    invariants = evidence.get("cross_revision_invariants", {})
    power = invariants.get("board_power_prerequisite", {})
    require(
        power.get("ldo3")
        == {
            "channel": 3,
            "voltage_mv": 2500,
            "output_net": "ESP32_LDO3",
            "destination": "VDD_MIPI_DPHY",
            "series": "R80=0R",
        }
        and power.get("ldo4")
        == {
            "channel": 4,
            "voltage_mv": 3300,
            "output_net": "ESP32_LDO4",
            "destination": "VDDPST_5",
            "series": "R109=0R",
            "audio_dependency": "GPIO45/GPIO46 pad pull-ups and Q8/Q9 gates",
        }
        and power.get("diagnostic_order")
        == "gpio30_high_then_ldo3_then_ldo4_then_i2c",
        "board power prerequisite changed",
    )
    codec = invariants.get("codec", {})
    require(codec.get("part") == "ES8311", "codec changed")
    address = codec.get("fixed_control_address", {})
    require(
        address.get("esp_codec_dev_legacy_8bit_value") == "0x30"
        and address.get("idf_5_5_3_master_7bit_value") == "0x18",
        "ES8311 address contract changed",
    )
    amplifier = invariants.get("power_amplifier", {})
    require(
        amplifier.get("part") == "NS4263B"
        and amplifier.get("active_low") is True
        and amplifier.get("enabled_gpio_level") == 0
        and amplifier.get("shutdown_gpio_level") == 1,
        "amplifier active-low contract changed",
    )
    contract = evidence.get("driver_safety_contract", {})
    require(contract.get("esp_codec_dev_pa_pin") == -1, "codec driver regained PA ownership")
    tone = contract.get("tone_limit", {})
    require(
        tone == {
            "count": 1,
            "frequency_hz": 440,
            "duration_ms": 600,
            "pcm_peak": 512,
            "pcm_full_scale": 32767,
            "fade_ms": 80,
            "codec_volume_percent": 5,
        },
        "bounded tone contract changed",
    )
    authorization = evidence.get("authorization", {})
    require(authorization.get("authorized") is False, "audio runtime was authorized")
    require(authorization.get("flash_app_authorized") is False, "app flash was authorized")
    require(authorization.get("flash_project_authorized") is False, "full flash was authorized")
    require(authorization.get("hardware_test") is None, "unrecorded hardware test claimed")

    profile = load_json(ROOT / "hardware/board-profile.json")
    audio = profile.get("peripheral_authorizations", {}).get("audio", {})
    require(profile.get("pin_map_authorized") is False, "global pin map is authorized")
    require(audio.get("authorized") is False, "board profile authorizes audio")
    require(audio.get("hardware_test") is None, "board profile claims audio runtime")


def verify_schematic(path: pathlib.Path, expected_hash: str, revision: str) -> None:
    require(path.is_file(), f"missing {revision} schematic")
    require(sha256_file(path) == expected_hash, f"{revision} schematic hash differs")
    parts, nets = schematic_data(path)
    require(parts.get("IC6", {}).get("deviceset") == "ES8311", f"{revision} codec differs")
    require(parts.get("U4", {}).get("deviceset") == "NS4263B", f"{revision} PA differs")
    require(parts.get("Q8", {}).get("value") == "BSS138W_SOT323", f"{revision} Q8 differs")
    require(parts.get("Q9", {}).get("value") == "BSS138W_SOT323", f"{revision} Q9 differs")
    for resistor in ("R111", "R126", "R128", "R130"):
        require(parts.get(resistor, {}).get("value") == "0R", f"{revision} {resistor} differs")
    for resistor in ("R80", "R109"):
        require(parts.get(resistor, {}).get("value") == "0R", f"{revision} {resistor} differs")
    require(parts.get("R147", {}).get("value") == "OR", f"{revision} R147 literal differs")
    require(parts.get("C140", {}).get("value") == "1uF", f"{revision} C140 differs")
    require(parts.get("C141", {}).get("value") == "1uF", f"{revision} C141 differs")
    require(parts.get("U13", {}).get("value") == "NS4168-NC", f"{revision} U13 populated")
    require(parts.get("U3", {}).get("value") == "NS4168-NC", f"{revision} U3 populated")
    for resistor in ("R112", "R113", "R114", "R115", "R129", "R149"):
        require(parts.get(resistor, {}).get("value") == "0R_NC", f"{revision} {resistor} populated")

    require_same_net(nets, "I2S_LRCK", ("U7", "GPIO21"), ("R130", "1"))
    require_same_net(nets, "I2S_LRCK_N", ("R130", "2"), ("IC6", "LRCK"))
    require_same_net(nets, "I2S_SCLK", ("U7", "GPIO22"), ("R128", "1"))
    require_same_net(nets, "I2S_SCLK_N", ("R128", "2"), ("IC6", "SCLK/DMIC_SCL"))
    require_same_net(nets, "I2S_SDOUT", ("U7", "GPIO23"), ("R111", "1"))
    require_same_net(nets, "I2S_SDOUT_N", ("R111", "2"), ("IC6", "DSDIN"))
    require_same_net(nets, "I2S_MCLK", ("U7", "GPIO24"), ("R126", "1"))
    require_same_net(nets, "I2S_MCLK_N", ("R126", "2"), ("IC6", "MCLK"))
    require_same_net(nets, "I2C1_SDA", ("U7", "GPIO45"), ("Q8", "S"))
    require_same_net(nets, "I2C1_SDA_3V3", ("Q8", "D"), ("IC6", "CDATA"))
    require_same_net(nets, "I2C1_SCL", ("U7", "GPIO46"), ("Q9", "S"))
    require_same_net(nets, "I2C1_SCL_3V3", ("Q9", "D"), ("IC6", "CCLK"))
    require_same_net(nets, "ESP32_LDO3", ("U7", "VFB3/VO3"), ("R80", "2"))
    require_same_net(nets, "VDD_MIPI_DPHY", ("R80", "1"), ("U7", "VDD_MIPI_DPHY"))
    require_same_net(nets, "ESP32_LDO4", ("U7", "VFB4/VO4"), ("R109", "2"))
    require_same_net(
        nets,
        "VDDPST_5",
        ("R109", "1"),
        ("U7", "VDDPST_5"),
        ("R97", "1"),
        ("R98", "1"),
        ("Q8", "G"),
        ("Q9", "G"),
    )
    require_same_net(nets, "AUDIO_OUT_SD", ("U7", "GPIO30"), ("R147", "1"))
    require_same_net(nets, "N$100", ("R147", "2"), ("U4", "1"))
    require_same_net(nets, "N$115", ("IC6", "OUTN"), ("C140", "1"))
    require_same_net(nets, "N$116", ("IC6", "OUTP"), ("C141", "1"))


def verify_vendor_tree(evidence: dict, vendor_tree: pathlib.Path) -> None:
    require(git_head(vendor_tree) == evidence["official_elecrow_source"]["commit"], "vendor commit differs")
    for record in evidence["schematics"]:
        verify_schematic(vendor_tree / record["path"], record["sha256"], record["revision"])
    for record in evidence["elecrow_active_level_evidence"]:
        driver = vendor_tree / record["driver_path"]
        header = vendor_tree / record["header_path"]
        main = vendor_tree / record["main_path"]
        for path, key in ((driver, "driver_sha256"), (header, "header_sha256"), (main, "main_sha256")):
            require(path.is_file(), f"missing Elecrow source {path}")
            require(sha256_file(path) == record[key], f"Elecrow source hash differs: {path}")
        driver_text = driver.read_text()
        header_text = header.read_text()
        main_text = main.read_text()
        require("bool status = !state;" in driver_text, f"{record['revision']} active-low inversion differs")
        require("gpio_set_level(AUDIO_GPIO_CTRL, status)" in driver_text, f"{record['revision']} PA write differs")
        require(re.search(r"AUDIO_GPIO_CTRL\s+30", header_text) is not None, f"{record['revision']} PA pin differs")
        require(".mclk = I2S_GPIO_UNUSED" in driver_text, f"{record['revision']} Lesson11 omission changed")
        require(main_text.index("audio_ctrl_init") < main_text.index("set_Audio_ctrl(false)") < main_text.index("audio_init"),
                f"{record['revision']} initial shutdown order differs")
    for record in evidence["elecrow_power_sequence_evidence"]:
        main = vendor_tree / record["main_path"]
        require(main.is_file(), f"missing Elecrow power source {main}")
        require(sha256_file(main) == record["main_sha256"],
                f"Elecrow power source hash differs: {main}")
        text = main.read_text()
        tokens = (
            ".chan_id = 3",
            ".voltage_mv = 2500",
            "esp_ldo_acquire_channel(&ldo3_cof, &ldo3)",
            ".chan_id = 4",
            ".voltage_mv = 3300",
            "esp_ldo_acquire_channel(&ldo4_cof, &ldo4)",
            "audio_ctrl_init",
            "audio_init",
        )
        positions = [text.index(token) for token in tokens]
        require(positions == sorted(positions),
                f"{record['revision']} LDO-before-audio sequence differs")


def verify_idf_tree(evidence: dict, idf_tree: pathlib.Path) -> None:
    reference = evidence["pinned_espressif_references"]["esp_idf"]
    require(git_head(idf_tree) == reference["commit"], "IDF commit differs")
    for path_key, hash_key in (
        ("example_source", "example_source_sha256"),
        ("example_manifest", "example_manifest_sha256"),
        ("example_sdkconfig_defaults", "example_sdkconfig_defaults_sha256"),
    ):
        path = idf_tree / reference[path_key]
        require(path.is_file(), f"missing IDF reference {path}")
        require(sha256_file(path) == reference[hash_key], f"IDF reference hash differs: {path}")
    source_text = (idf_tree / reference["example_source"]).read_text()
    require(all(token in source_text for token in (
        "i2c_new_master_bus",
        "audio_codec_new_i2c_ctrl",
        "audio_codec_new_i2s_data",
        "es8311_codec_new",
        "esp_codec_dev_open",
        ".mclk = I2S_MCK_IO",
    )), "pinned IDF ES8311 example is incomplete")
    defaults = (idf_tree / reference["example_sdkconfig_defaults"]).read_text()
    require("CONFIG_CODEC_I2C_BACKWARD_COMPATIBLE=n" in defaults, "new IDF I2C API is not selected")


def verify_codec_tree(evidence: dict, codec_tree: pathlib.Path) -> None:
    reference = evidence["pinned_espressif_references"]["esp_codec_dev"]
    manifest = codec_tree / "idf_component.yml"
    require(manifest.is_file() and "version: 1.3.4" in manifest.read_text(), "codec version differs")
    for relative, expected_hash in reference["source_hashes"].items():
        path = codec_tree / relative
        require(path.is_file(), f"missing codec source {relative}")
        require(sha256_file(path) == expected_hash, f"codec source hash differs: {relative}")
    header = (codec_tree / "device/include/es8311_codec.h").read_text()
    control = (codec_tree / "platform/audio_codec_ctrl_i2c.c").read_text()
    codec = (codec_tree / "device/es8311/es8311.c").read_text()
    require("ES8311_CODEC_DEFAULT_ADDR (0x30)" in header, "codec address differs")
    require(".device_address = (i2c_cfg->addr >> 1)" in control, "7-bit address conversion differs")
    require("DEFAULT_I2C_CLOCK         (100000)" in control, "codec I2C speed differs")
    require("es8311_pa_power(codec, ES_PA_SETUP | ES_PA_ENABLE);" in codec,
            "reviewed codec constructor PA behavior differs")
    require("codec->cfg.pa_reverted ? false : true" in codec, "PA enable polarity semantics differ")
    require("codec->cfg.pa_reverted ? true : false" in codec, "PA disable polarity semantics differ")


def verify_project_source() -> None:
    source = (ROOT / "components/platform_audio/src/platform_audio.c").read_text()
    app = (ROOT / "apps/audio_diag/main/audio_diag_main.c").read_text()
    metadata = load_json(ROOT / "apps/audio_diag/app-metadata.json")
    require(metadata.get("flash_authorized") is False, "legacy flash flag is enabled")
    require(metadata.get("flash_app_authorized") is False, "app-only flash flag is enabled")
    require(metadata.get("flash_project_authorized") is False, "full flash flag is enabled")
    require(metadata.get("runtime_supported") is False, "runtime support is claimed")
    required_source = (
        "platform_audio_force_safe_shutdown();",
        ".pa_pin = -1",
        ".use_mclk = true",
        ".mclk = (gpio_num_t)PLATFORM_AUDIO_GPIO_MCLK",
        "esp_codec_dev_set_out_mute",
        "write_zero_preroll(audio)",
        "platform_audio_amp_gpio_level(enabled)",
        "AUDIO_AMP_SETTLE_MS 20U",
        "AUDIO_CODEC_MUTE_SETTLE_MS 10U",
    )
    require(all(token in source for token in required_source), "project safe sequence changed")
    require(source.index("platform_audio_force_safe_shutdown();") < source.index("configure_i2s(audio)"),
            "amplifier is not shut down before I2S initialization")
    require(source.index("esp_codec_dev_set_out_mute") < source.index("set_amplifier_enabled(true)"),
            "amplifier can enable before the codec mute path exists")
    required_app = (
        "AUDIO_DIAG_LDO3_CHANNEL 3",
        "AUDIO_DIAG_LDO3_MV 2500",
        "AUDIO_DIAG_LDO4_CHANNEL 4",
        "AUDIO_DIAG_LDO4_MV 3300",
        "esp_ldo_acquire_channel",
        "i2c_master_probe",
        "AUDIO_DIAG_TONE_DURATION_MS 600U",
        "AUDIO_DIAG_FADE_DURATION_MS 80U",
        "AUDIO_DIAG_PEAK_AMPLITUDE 512U",
        "AUDIO_DIAG_VOLUME_PERCENT 5U",
        "P4_AUDIO D2 PASS",
        "tone_count=1",
    )
    require(all(token in app for token in required_app), "bounded diagnostic changed")
    app_main = app[app.index("void app_main(void)"):]
    startup = (
        "platform_audio_force_safe_shutdown()",
        "ESP_LOGI",
        "acquire_board_power()",
        "create_control_bus(&s_control_bus)",
        "i2c_master_probe",
        "platform_audio_create",
    )
    positions = [app_main.index(token) for token in startup]
    require(positions == sorted(positions), "safe board-power startup order changed")
    cleanup = app[app.index("static esp_err_t cleanup_runtime(void)"):
                  app.index("static void halt_safe")]
    shutdown = (
        "platform_audio_force_safe_shutdown()",
        "platform_audio_destroy",
        "i2c_del_master_bus",
        "platform_audio_force_safe_shutdown()",
        "release_board_power()",
    )
    cursor = 0
    for token in shutdown:
        cursor = cleanup.index(token, cursor) + len(token)
    release = app[app.index("static esp_err_t release_board_power(void)"):
                  app.index("static esp_err_t cleanup_runtime(void)")]
    require(release.index("s_ldo4") < release.index("s_ldo3"),
            "board power is not released in reverse order")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vendor-tree", type=pathlib.Path)
    parser.add_argument("--idf-tree", type=pathlib.Path)
    parser.add_argument("--codec-tree", type=pathlib.Path)
    args = parser.parse_args()

    evidence = load_json(EVIDENCE_PATH)
    verify_structure(evidence)
    verify_project_source()
    statuses = []
    if args.vendor_tree is not None:
        verify_vendor_tree(evidence, args.vendor_tree.resolve())
        statuses.append("vendor=verified")
    else:
        statuses.append("vendor=not-supplied")
    if args.idf_tree is not None:
        verify_idf_tree(evidence, args.idf_tree.resolve())
        statuses.append("idf=verified")
    else:
        statuses.append("idf=not-supplied")
    if args.codec_tree is not None:
        verify_codec_tree(evidence, args.codec_tree.resolve())
        statuses.append("codec=verified")
    else:
        statuses.append("codec=not-supplied")
    print("audio driver path: PASS runtime_authorized=false " + " ".join(statuses))


if __name__ == "__main__":
    main()
