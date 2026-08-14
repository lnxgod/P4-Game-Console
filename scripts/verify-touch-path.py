#!/usr/bin/env python3

"""Verify the bounded Elecrow 10.1-inch GT911 touch authorization."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-10.1-touch-path.json"
RUNTIME_BASIS_PATH = ROOT / "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json"


def fail(message: str) -> None:
    raise SystemExit(f"touch-path verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def schematic_data(path: pathlib.Path) -> tuple[dict[str, str | None], dict[tuple[str, str], str]]:
    root = ET.parse(path).getroot()
    parts = {part.get("name", ""): part.get("value") for part in root.findall(".//parts/part")}
    pin_nets: dict[tuple[str, str], str] = {}
    for net in root.findall(".//sheets/sheet/nets/net"):
        name = net.get("name", "")
        for pinref in net.findall(".//pinref"):
            pin_nets[(pinref.get("part", ""), pinref.get("pin", ""))] = name
    return parts, pin_nets


def same_net(nets: dict[tuple[str, str], str], name: str, *pins: tuple[str, str]) -> None:
    require(all(nets.get(pin) == name for pin in pins), f"{name} path differs: {pins}")


def verify_structure(evidence: dict) -> None:
    require(evidence.get("schema") == 1, "unsupported schema")
    require(evidence.get("classification") == "cross-revision-touch-path", "classification changed")
    require(evidence.get("result") == "pass-build-authorization", "result changed")
    source = evidence.get("official_source", {})
    require(source.get("commit") == "c5a437311b951aaa9d17115bf420877a8f1f7b83", "vendor commit changed")
    require(source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"], "revision coverage changed")
    invariants = evidence.get("cross_revision_invariants", {})
    i2c = invariants.get("shared_i2c", {})
    require(i2c.get("controller") == 1 and i2c.get("sda_gpio") == 45 and i2c.get("scl_gpio") == 46, "I2C resources changed")
    require(i2c.get("clock_hz") == 400000, "GT911 device speed changed")
    require(i2c.get("address_primary_7bit") == "0x5d" and i2c.get("address_backup_7bit") == "0x14", "GT911 addresses changed")
    require(invariants.get("reset", {}).get("gpio") == 40, "reset GPIO changed")
    require(invariants.get("interrupt", {}).get("gpio") == 42, "INT GPIO changed")
    coordinates = invariants.get("coordinate_space", {})
    require(coordinates.get("width") == 1024 and coordinates.get("height") == 600 and coordinates.get("maximum_contacts") == 5, "coordinate contract changed")
    driver = evidence.get("pinned_driver", {})
    require(driver.get("version") == "1.1.3", "GT911 version changed")
    require(driver.get("transitive_touch_version") == "1.1.2", "touch core version changed")
    demo = evidence.get("official_known_good_demo", {})
    require(demo.get("commit") == source.get("commit"), "known-good demo commit changed")
    require(demo.get("v1_0_v1_1_touch_blob") == "c4f0e331c453ab2f8a315aba765eae5492cac58b", "V1.0/V1.1 touch blob changed")
    require(demo.get("v1_2_touch_blob") == "dd710cb513aea12901428585b9f367409fcb32a9", "V1.2 touch blob changed")
    authorization = evidence.get("authorization", {})
    require(authorization.get("authorized") is True, "touch build authorization disabled")
    require(authorization.get("hardware_test") is None and authorization.get("flash_authorized") is False, "touch incorrectly claims hardware/flash authorization")
    denied = set(authorization.get("explicitly_not_authorized", []))
    require(
        {"gpio42_runtime_isr_registration",
         "creating_or_deleting_i2c1_inside_platform_touch"} <= denied,
        "denial list incomplete",
    )


def verify_profile() -> None:
    profile = json.loads((ROOT / "hardware/board-profile.json").read_text())
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    touch = profile.get("peripheral_authorizations", {}).get("touch", {})
    require(touch.get("authorized") is True, "profile does not authorize bounded touch runtime")
    require(touch.get("scope") == "gt911_shared_i2c1_gpio40_reset_gpio42_latch_cross_revision_v1_0_through_v1_2_narrow_runtime", "profile touch scope changed")
    require(touch.get("topology_evidence") == "hardware/evidence/elecrow-10.1-touch-path.json", "profile touch topology evidence changed")
    require(touch.get("evidence") == "hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json", "profile touch runtime evidence changed")
    require(touch.get("hardware_test") is None, "profile claims touch hardware test")

    runtime_basis = json.loads(RUNTIME_BASIS_PATH.read_text())
    require(runtime_basis.get("schema") == 1, "unsupported runtime-basis schema")
    require(runtime_basis.get("classification") == "exact-unit-user-attested-factory-touch-audio-runtime-basis", "runtime-basis classification changed")
    touch_decision = runtime_basis.get("touch_authorization_decision", {})
    require(touch_decision.get("authorized") is True, "runtime basis does not authorize touch")
    require("app_partition_only_write_after_exact_readback" in touch_decision.get("scope", []), "touch write scope changed")
    denied = set(touch_decision.get("explicitly_not_authorized", []))
    require({"usb_host", "usb_hid", "all_audio_runtime", "full_project_flash"} <= denied, "touch runtime denials incomplete")
    audio_decision = runtime_basis.get("audio_authorization_decision", {})
    require(audio_decision.get("authorized") is False, "touch runtime basis unexpectedly authorizes audio")
    require(audio_decision.get("required_runtime_gate") == 0, "audio runtime gate is not locked off")
    require(audio_decision.get("required_gpio30_access") is False, "touch-only runtime permits GPIO30 access")


def verify_revision(path: pathlib.Path, expected_hash: str, revision: str) -> None:
    require(path.is_file(), f"missing {revision} schematic")
    require(sha256_file(path) == expected_hash, f"{revision} schematic hash differs")
    parts, nets = schematic_data(path)
    same_net(nets, "I2C1_SDA", ("FPC2", "2"), ("U7", "GPIO45"), ("Q8", "S"), ("R98", "2"))
    same_net(nets, "I2C1_SCL", ("FPC2", "1"), ("U7", "GPIO46"), ("Q9", "S"), ("R97", "2"))
    same_net(nets, "RESET_TP", ("FPC2", "6"), ("U7", "GPIO40"), ("R74", "2"))
    same_net(nets, "INT_TP", ("FPC2", "3"), ("U7", "GPIO42"), ("R75", "2"))
    same_net(nets, "GND", ("FPC2", "4"), ("FPC2", "7"), ("FPC2", "8"))
    require(parts.get("Q8") == "BSS138W_SOT323" and parts.get("Q9") == "BSS138W_SOT323", f"{revision} level shifters differ")
    require(parts.get("R65") == "0R", f"{revision} touch power bridge differs")
    expected_pull = "4.7K" if revision == "V1.0" else "2K"
    require(parts.get("R74") == expected_pull and parts.get("R97") == expected_pull and parts.get("R98") == expected_pull, f"{revision} reset/I2C pull-ups differ")
    require(parts.get("R75") == ("4.7K" if revision == "V1.0" else "2K_NC"), f"{revision} INT pull-up differs")
    if revision in {"V1.0", "V1.1"}:
        same_net(nets, "LCD_VDD", ("FPC2", "5"), ("R65", "1"))
        same_net(nets, "VDD_3V3", ("R65", "2"))
    else:
        same_net(nets, "VDD_3V3", ("FPC2", "5"), ("R65", "2"))


def verify_vendor_tree(evidence: dict, vendor_tree: pathlib.Path) -> None:
    require(vendor_tree.is_dir(), f"vendor tree is not a directory: {vendor_tree}")
    commit = subprocess.run(["git", "-C", str(vendor_tree), "rev-parse", "HEAD"], check=True, capture_output=True, text=True).stdout.strip()
    require(commit == evidence["official_source"]["commit"], "vendor commit differs")
    for record in evidence["schematics"]:
        verify_revision(vendor_tree / record["path"], record["sha256"], record["revision"])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--vendor-tree", type=pathlib.Path)
    args = parser.parse_args()
    evidence = json.loads(EVIDENCE_PATH.read_text())
    verify_structure(evidence)
    verify_profile()
    if args.vendor_tree is not None:
        verify_vendor_tree(evidence, args.vendor_tree)
    print("P4_TOUCH_PATH VERIFY PASS revisions=V1.0,V1.1,V1.2 shared_i2c1=true contacts=5 narrow_runtime=true contact_hardware_test=pending audio_runtime=false")


if __name__ == "__main__":
    main()
