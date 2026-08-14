#!/usr/bin/env python3

"""Verify the fail-closed Elecrow 10.1-inch speaker-topology review."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-10.1-audio-path-review.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(f"audio-path verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(tree: pathlib.Path) -> str:
    return subprocess.run(
        ["git", "-C", str(tree), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def schematic_data(path: pathlib.Path) -> tuple[dict[str, dict[str, str]], dict[tuple[str, str], str]]:
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


def require_same_net(pin_nets: dict[tuple[str, str], str], name: str, *pins: tuple[str, str]) -> None:
    require(all(pin_nets.get(pin) == name for pin in pins), f"{name} path differs: {pins}")


def verify_structure(evidence: dict) -> None:
    require(evidence.get("schema") == 1, "unsupported schema")
    require(evidence.get("classification") == "cross-revision-audio-path-review", "wrong classification")
    require(evidence.get("result") == "reject-direct-i2s-amplifier-assumption", "wrong result")
    require(evidence.get("scope") == "speaker_topology_review_no_driver_authorization", "scope changed")
    source = evidence.get("official_source", {})
    require(source.get("vendor") == "Elecrow" and source.get("model") == "DHE04310D", "wrong board source")
    require(source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"], "revision coverage changed")
    require(re.fullmatch(r"[0-9a-f]{40}", str(source.get("commit"))) is not None, "invalid vendor commit")

    schematics = evidence.get("schematics", [])
    require(len(schematics) == 3, "expected three schematic records")
    require([item.get("revision") for item in schematics] == ["V1.0", "V1.1", "V1.2"], "schematic order changed")
    require(all(SHA256_RE.fullmatch(str(item.get("sha256"))) for item in schematics), "invalid schematic hash")

    topology = evidence.get("cross_revision_invariant_topology", {})
    require(topology.get("direct_i2s_speaker_amplifier") is False, "direct-I2S premise is no longer rejected")
    require(topology.get("codec", {}).get("part") == "ES8311", "codec changed")
    require(topology.get("power_amplifier", {}).get("part") == "NS4263B", "power amplifier changed")
    alternatives = topology.get("unpopulated_direct_i2s_alternatives", [])
    require([item.get("part") for item in alternatives] == ["NS4168-NC", "NS4168-NC"], "NC alternatives changed")

    lesson = evidence.get("official_lesson_11_review", {})
    settings = lesson.get("effective_i2s_settings", {})
    require(
        settings.get("lrck_gpio") == 21
        and settings.get("bclk_gpio") == 22
        and settings.get("data_gpio") == 23
        and settings.get("amplifier_control_gpio") == 30
        and settings.get("mclk_gpio") is None,
        "Lesson 11 settings changed",
    )
    require(len(lesson.get("blocking_omissions", [])) == 4, "Lesson 11 omissions are incomplete")

    authorization = evidence.get("authorization", {})
    require(authorization.get("authorized") is False, "audio must remain unauthorized")
    require(authorization.get("scope") == [], "unauthorized review grants a scope")
    denied = set(authorization.get("explicitly_not_authorized", []))
    require(
        {
            "gpio21_audio_lrck",
            "gpio22_audio_bclk",
            "gpio23_audio_data",
            "gpio24_audio_mclk",
            "gpio30_amplifier_shutdown",
            "gpio45_codec_i2c_sda",
            "gpio46_codec_i2c_scl",
            "es8311_codec",
            "ns4263b_power_amplifier",
            "speaker_output",
        }
        <= denied,
        "audio denial list is incomplete",
    )
    require(len(authorization.get("blockers", [])) >= 5, "audio blockers are incomplete")


def verify_profile() -> None:
    profile = json.loads((ROOT / "hardware/board-profile.json").read_text())
    audio = profile.get("peripheral_authorizations", {}).get("audio", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(audio.get("authorized") is False, "profile authorizes audio")
    require(audio.get("scope") == "topology_review_only_no_gpio_or_driver_authorized", "profile audio scope changed")
    require(audio.get("evidence") == "hardware/evidence/elecrow-10.1-audio-path-review.json", "profile evidence changed")
    require(audio.get("hardware_test") is None, "profile claims an audio hardware test")


def verify_schematic(path: pathlib.Path, expected_hash: str, revision: str) -> None:
    require(path.is_file(), f"missing {revision} schematic")
    require(sha256_file(path) == expected_hash, f"{revision} schematic hash differs")
    parts, nets = schematic_data(path)

    require(parts.get("IC6", {}).get("deviceset") == "ES8311", f"{revision} codec differs")
    require(parts.get("U4", {}).get("deviceset") == "NS4263B", f"{revision} power amp differs")
    require(parts.get("U13", {}).get("value") == "NS4168-NC", f"{revision} U13 population differs")
    require(parts.get("U3", {}).get("value") == "NS4168-NC", f"{revision} U3 population differs")
    require(parts.get("Q8", {}).get("value") == "BSS138W_SOT323", f"{revision} SDA shifter differs")
    require(parts.get("Q9", {}).get("value") == "BSS138W_SOT323", f"{revision} SCL shifter differs")
    for resistor in ("R111", "R126", "R128", "R130"):
        require(parts.get(resistor, {}).get("value") == "0R", f"{revision} {resistor} differs")
    for resistor in ("R112", "R113", "R114", "R115", "R129", "R149"):
        require(parts.get(resistor, {}).get("value") == "0R_NC", f"{revision} {resistor} is not NC")
    require(parts.get("R147", {}).get("value") == "OR", f"{revision} R147 literal value differs")
    require(parts.get("C140", {}).get("value") == "1uF", f"{revision} C140 differs")
    require(parts.get("C141", {}).get("value") == "1uF", f"{revision} C141 differs")

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

    require_same_net(nets, "N$115", ("IC6", "OUTN"), ("C140", "1"))
    require_same_net(nets, "N$130", ("C140", "2"), ("R146", "1"))
    require_same_net(nets, "INRN", ("R146", "2"), ("C135", "1"))
    require_same_net(nets, "N$44", ("C135", "2"), ("R178", "2"))
    require_same_net(nets, "INRN1", ("R178", "1"), ("U4", "11"))
    require_same_net(nets, "N$116", ("IC6", "OUTP"), ("C141", "1"))
    require_same_net(nets, "N$132", ("C141", "2"), ("R148", "1"))
    require_same_net(nets, "INLN", ("R148", "2"), ("C147", "1"))
    require_same_net(nets, "N$39", ("C147", "2"), ("R179", "2"))
    require_same_net(nets, "INLN1", ("R179", "1"), ("U4", "6"))

    require_same_net(nets, "AUDIO_OUT_SD", ("U7", "GPIO30"), ("R147", "1"))
    require_same_net(nets, "N$100", ("R147", "2"), ("U4", "1"))
    require_same_net(nets, "N$54", ("U4", "3"), ("FB9", "1"))
    require_same_net(nets, "VOLP", ("FB9", "2"), ("J4", "P$1"))
    require_same_net(nets, "N$58", ("U4", "5"), ("FB12", "1"))
    require_same_net(nets, "VOLN", ("FB12", "2"), ("J4", "P$2"))
    require_same_net(nets, "N$25", ("U4", "14"), ("FB8", "1"))
    require_same_net(nets, "VORP", ("FB8", "2"), ("J6", "P$1"))
    require_same_net(nets, "N$3", ("U4", "12"), ("FB7", "1"))
    require_same_net(nets, "VORN", ("FB7", "2"), ("J6", "P$2"))


def verify_vendor_tree(evidence: dict, vendor_tree: pathlib.Path) -> None:
    require(vendor_tree.is_dir(), f"vendor tree is not a directory: {vendor_tree}")
    require(git_head(vendor_tree) == evidence["official_source"]["commit"], "vendor commit differs")
    for record in evidence["schematics"]:
        verify_schematic(vendor_tree / record["path"], record["sha256"], record["revision"])

    for record in evidence["official_lesson_11_review"]["revisions"]:
        driver = vendor_tree / record["driver_path"]
        header = vendor_tree / record["header_path"]
        require(driver.is_file() and header.is_file(), f"missing Lesson 11 {record['revision']}")
        require(sha256_file(driver) == record["driver_sha256"], f"Lesson 11 {record['revision']} driver differs")
        require(sha256_file(header) == record["header_sha256"], f"Lesson 11 {record['revision']} header differs")
        driver_text = driver.read_text()
        header_text = header.read_text()
        require(
            all(
                token in driver_text
                for token in (
                    ".mclk = I2S_GPIO_UNUSED",
                    ".bclk = AUDIO_GPIO_BCLK",
                    ".ws = AUDIO_GPIO_LRCLK",
                    ".dout = AUDIO_GPIO_SDATA",
                )
            ),
            f"Lesson 11 {record['revision']} I2S setup differs",
        )
        require(
            all(
                re.search(pattern, header_text)
                for pattern in (
                    r"AUDIO_GPIO_LRCLK\s+21",
                    r"AUDIO_GPIO_BCLK\s+22",
                    r"AUDIO_GPIO_SDATA\s+23",
                    r"AUDIO_GPIO_CTRL\s+30",
                )
            ),
            f"Lesson 11 {record['revision']} pins differ",
        )
        lesson_root = driver.parents[2]
        source_text = "\n".join(
            path.read_text(errors="strict")
            for path in lesson_root.rglob("*")
            if path.is_file() and path.suffix in {".c", ".h"}
        ).lower()
        require("es8311" not in source_text and "esp_codec_dev" not in source_text, f"Lesson 11 {record['revision']} gained codec init")


def verify_idf_tree(evidence: dict, idf_tree: pathlib.Path) -> None:
    require(idf_tree.is_dir(), f"IDF tree is not a directory: {idf_tree}")
    reference = evidence["pinned_idf_reference"]
    require(git_head(idf_tree) == reference["esp_idf_commit"], "IDF commit differs")
    source = idf_tree / reference["example_source"]
    manifest = idf_tree / reference["component_manifest"]
    require(sha256_file(source) == reference["example_source_sha256"], "IDF ES8311 example differs")
    require(sha256_file(manifest) == reference["component_manifest_sha256"], "IDF ES8311 manifest differs")
    require('espressif/esp_codec_dev: ^1.3.4' in manifest.read_text(), "IDF codec requirement differs")
    source_text = source.read_text()
    require(
        all(
            token in source_text
            for token in (
                "i2c_new_master_bus",
                "audio_codec_new_i2c_ctrl",
                "es8311_codec_new",
                "esp_codec_dev_open",
                ".mclk = I2S_MCK_IO",
            )
        ),
        "IDF reference is incomplete",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vendor-tree", type=pathlib.Path)
    parser.add_argument("--idf-tree", type=pathlib.Path)
    args = parser.parse_args()

    evidence = json.loads(EVIDENCE_PATH.read_text())
    verify_structure(evidence)
    verify_profile()
    if args.vendor_tree is not None:
        verify_vendor_tree(evidence, args.vendor_tree.resolve())
        vendor_status = "verified"
    else:
        vendor_status = "not-supplied"
    if args.idf_tree is not None:
        verify_idf_tree(evidence, args.idf_tree.resolve())
        idf_status = "verified"
    else:
        idf_status = "not-supplied"
    print(
        "audio path review: PASS result=reject-direct-i2s-amplifier-assumption "
        f"authorization=false vendor_tree={vendor_status} idf_tree={idf_status}"
    )


if __name__ == "__main__":
    main()
