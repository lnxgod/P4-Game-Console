#!/usr/bin/env python3

"""Verify the bounded Elecrow 10.1-inch one-bit SDMMC authorization."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-10.1-storage-path.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def fail(message: str) -> None:
    raise SystemExit(f"storage-path verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def schematic_data(path: pathlib.Path) -> tuple[dict[str, list[tuple[str, str]]], dict[str, dict[str, str]]]:
    root = ET.parse(path).getroot()
    nets = {
        net.get("name", ""): sorted(
            (pinref.get("part", ""), pinref.get("pin", ""))
            for pinref in net.findall(".//pinref")
        )
        for net in root.findall(".//sheets/sheet/nets/net")
    }
    parts = {
        part.get("name", ""): dict(part.attrib)
        for part in root.findall(".//parts/part")
    }
    return nets, parts


def verify_structure(evidence: dict) -> None:
    require(evidence.get("schema") == 1, "unsupported evidence schema")
    require(evidence.get("classification") == "cross-revision-storage-path", "wrong classification")
    require(evidence.get("result") == "pass", "evidence result is not pass")
    require(evidence.get("scope") == "sdmmc_read_only_service_no_format", "scope changed")

    source = evidence.get("official_source", {})
    require(source.get("vendor") == "Elecrow" and source.get("model") == "DHE04310D", "wrong model")
    require(COMMIT_RE.fullmatch(str(source.get("commit"))) is not None, "invalid vendor commit")
    require(source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"], "revision coverage changed")

    schematics = evidence.get("schematics")
    require(isinstance(schematics, list) and len(schematics) == 3, "expected three schematics")
    require([item.get("revision") for item in schematics] == ["V1.0", "V1.1", "V1.2"], "schematic order changed")
    require(all(SHA256_RE.fullmatch(str(item.get("sha256"))) for item in schematics), "invalid schematic hash")

    invariants = evidence.get("cross_revision_invariants", {})
    require(
        invariants.get("controller")
        == {
            "host": "SDMMC",
            "slot": 0,
            "bus_width": 1,
            "maximum_clock_khz": 10000,
            "internal_pullups": True,
        },
        "controller configuration changed",
    )
    signals = invariants.get("signals", {})
    require(signals.get("clock", {}).get("gpio") == 43, "clock GPIO changed")
    require(signals.get("command", {}).get("gpio") == 44, "command GPIO changed")
    require(signals.get("data0", {}).get("gpio") == 39, "data0 GPIO changed")
    require(invariants.get("card_detect") is None and invariants.get("write_protect") is None, "unexpected sideband")

    lesson = evidence.get("official_lesson_08", {})
    require(
        lesson.get("effective_settings_match") is True
        and lesson.get("mount_point") == "/sdcard"
        and lesson.get("format_if_mount_failed") is False
        and lesson.get("slot") == 0
        and lesson.get("maximum_clock_khz") == 10000
        and lesson.get("bus_width") == 1
        and lesson.get("internal_pullups") is True,
        "Lesson 08 effective settings changed",
    )
    revisions = lesson.get("revisions")
    require(isinstance(revisions, list) and len(revisions) == 3, "Lesson 08 provenance incomplete")
    for item in revisions:
        require(
            SHA256_RE.fullmatch(str(item.get("driver_sha256"))) is not None
            and SHA256_RE.fullmatch(str(item.get("header_sha256"))) is not None,
            "invalid Lesson 08 source hash",
        )

    authorization = evidence.get("authorization", {})
    require(authorization.get("authorized") is True, "storage path is not authorized")
    require(
        set(authorization.get("scope", []))
        == {
            "sdmmc_slot_0_one_bit_at_or_below_10000_khz",
            "gpio43_sd_clock",
            "gpio44_sd_command",
            "gpio39_sd_data0",
            "internal_sdmmc_pullups",
            "micro_sd_connector_j5_on_existing_vdd_3v3_rail",
            "fat_mount_at_sdcard_with_format_if_mount_failed_false",
            "read_and_hash_existing_files",
        },
        "authorization grants unexpected resources",
    )
    denied = set(authorization.get("explicitly_not_authorized", []))
    require(
        {
            "sd_data1_through_data7",
            "card_detect",
            "write_protect",
            "formatting",
            "file_mutation_by_reusable_platform_storage_service",
            "all_other_gpio",
            "all_other_peripherals",
        }
        <= denied,
        "out-of-scope resources are not denied",
    )


def verify_profile_binding() -> None:
    profile = json.loads((ROOT / "hardware/board-profile.json").read_text())
    storage = profile.get("peripheral_authorizations", {}).get("storage", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(storage.get("authorized") is True, "profile storage scope is disabled")
    require(
        storage.get("scope") == "sdmmc_read_only_service_no_format_cross_revision_v1_0_through_v1_2",
        "profile storage scope changed",
    )
    require(storage.get("evidence") == "hardware/evidence/elecrow-10.1-storage-path.json", "profile evidence link changed")
    require(storage.get("hardware_test") is None, "profile claims an unrecorded storage hardware test")


def verify_component_scope() -> None:
    source = (ROOT / "components/platform_storage/src/platform_storage.c").read_text()
    required = (
        "#define STORAGE_SDMMC_SLOT SDMMC_HOST_SLOT_0",
        "#define STORAGE_SDMMC_FREQUENCY_KHZ 10000",
        "#define STORAGE_SDMMC_BUS_WIDTH 1",
        "#define STORAGE_SDMMC_CLK GPIO_NUM_43",
        "#define STORAGE_SDMMC_CMD GPIO_NUM_44",
        "#define STORAGE_SDMMC_D0 GPIO_NUM_39",
        ".format_if_mount_failed = false",
        "slot.d1 = GPIO_NUM_NC",
        "slot.d7 = GPIO_NUM_NC",
        "slot.cd = GPIO_NUM_NC",
        "slot.wp = GPIO_NUM_NC",
        "slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP",
    )
    require(all(token in source for token in required), "component resource constants changed")
    require("fopen(path, \"rb\")" in source, "WAD inspector is not read-only")
    forbidden = ("fwrite(", "unlink(", "remove(", "rename(", "mkdir(", "format(")
    require(not any(token in source for token in forbidden), "reusable storage service contains mutation code")


def verify_vendor_tree(evidence: dict, vendor_tree: pathlib.Path) -> None:
    require(vendor_tree.is_dir(), f"vendor tree is not a directory: {vendor_tree}")
    commit = subprocess.run(
        ["git", "-C", str(vendor_tree), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    require(commit == evidence["official_source"]["commit"], "vendor commit differs")

    expected_paths = {
        "N$65": [("R191", "2"), ("U7", "GPIO39")],
        "N$66": [("R192", "2"), ("U7", "GPIO43")],
        "N$67": [("R193", "2"), ("U7", "GPIO44")],
    }
    expected_card_pinrefs = {
        "SD1_D0": [("J5", "DO"), ("R191", "1")],
        "SD1_SCK": [("J5", "SCLK"), ("R192", "1")],
        "SD1_CMD": [("J5", "DI"), ("R193", "1")],
    }
    for schematic in evidence["schematics"]:
        path = vendor_tree / schematic["path"]
        require(path.is_file(), f"missing schematic: {schematic['path']}")
        require(sha256_file(path) == schematic["sha256"], f"schematic hash differs: {schematic['revision']}")
        nets, parts = schematic_data(path)
        for net, expected in expected_paths.items():
            require(nets.get(net) == expected, f"{schematic['revision']} {net} path differs")
        for net, expected in expected_card_pinrefs.items():
            actual = [item for item in nets.get(net, []) if item[0] in {pair[0] for pair in expected}]
            require(actual == expected, f"{schematic['revision']} {net} card path differs")
        require(("J5", "VDD") in nets.get("VDD_3V3", []), f"{schematic['revision']} card power differs")
        require(("J5", "VSS") in nets.get("GND", []), f"{schematic['revision']} card ground differs")
        require(parts.get("J5", {}).get("deviceset") == "HOLDER-MICROSD-9P", "card connector differs")
        for resistor in ("R191", "R192", "R193"):
            require(parts.get(resistor, {}).get("value") == "0R", f"{schematic['revision']} {resistor} differs")
        require(parts.get("R27", {}).get("value") == "10K", f"{schematic['revision']} D0 pull-up differs")
        require(parts.get("R28", {}).get("value") == "10K", f"{schematic['revision']} CMD pull-up differs")

    required_tokens = (
        ".format_if_mount_failed = false",
        "host.slot = SDMMC_HOST_SLOT_0",
        "host.max_freq_khz = 10000",
        "slot_config.clk = GPIO_NUM_43",
        "slot_config.cmd = GPIO_NUM_44",
        "slot_config.d0 = GPIO_NUM_39",
        "slot_config.width = 1",
        "slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP",
    )
    for record in evidence["official_lesson_08"]["revisions"]:
        driver = vendor_tree / record["driver_path"]
        header = vendor_tree / record["header_path"]
        require(driver.is_file() and header.is_file(), f"missing Lesson 08 {record['revision']}")
        require(sha256_file(driver) == record["driver_sha256"], f"Lesson 08 {record['revision']} driver differs")
        require(sha256_file(header) == record["header_sha256"], f"Lesson 08 {record['revision']} header differs")
        text = driver.read_text(errors="strict")
        require(all(token in text for token in required_tokens), f"Lesson 08 {record['revision']} settings differ")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vendor-tree", type=pathlib.Path)
    args = parser.parse_args()

    evidence = json.loads(EVIDENCE_PATH.read_text())
    verify_structure(evidence)
    verify_profile_binding()
    verify_component_scope()
    if args.vendor_tree is not None:
        verify_vendor_tree(evidence, args.vendor_tree.resolve())
        vendor_status = "verified"
    else:
        vendor_status = "not-supplied"
    print(
        "storage path evidence: PASS "
        "scope=sdmmc_read_only_service_no_format revisions=V1.0,V1.1,V1.2 "
        f"vendor_tree={vendor_status}"
    )


if __name__ == "__main__":
    main()
