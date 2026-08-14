#!/usr/bin/env python3

"""Verify the bounded Elecrow 10.1-inch display authorization evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "hardware/evidence/elecrow-10.1-display-path.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


def fail(message: str) -> None:
    raise SystemExit(f"display-path verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def schematic_net_pinrefs(path: pathlib.Path) -> dict[str, list[tuple[str, str]]]:
    root = ET.parse(path).getroot()
    result: dict[str, list[tuple[str, str]]] = {}
    for net in root.findall(".//sheets/sheet/nets/net"):
        name = net.get("name")
        if name is None:
            continue
        result[name] = sorted(
            (pinref.get("part", ""), pinref.get("pin", ""))
            for pinref in net.findall(".//pinref")
        )
    return result


def schematic_parts(path: pathlib.Path) -> dict[str, dict[str, str]]:
    root = ET.parse(path).getroot()
    return {
        part.get("name", ""): dict(part.attrib)
        for part in root.findall(".//parts/part")
    }


def verify_structure(evidence: dict) -> None:
    require(evidence.get("schema") == 1, "unsupported evidence schema")
    require(evidence.get("classification") == "cross-revision-display-path", "wrong classification")
    require(evidence.get("result") == "pass", "evidence result is not pass")
    require(evidence.get("scope") == "display_only", "evidence scope is not display-only")

    source = evidence.get("official_source")
    require(isinstance(source, dict), "official_source must be an object")
    require(source.get("vendor") == "Elecrow", "unexpected vendor")
    require(source.get("model") == "DHE04310D", "unexpected model")
    require(COMMIT_RE.fullmatch(str(source.get("commit"))) is not None, "invalid source commit")
    require(
        source.get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"],
        "hardware revision coverage is incomplete",
    )

    schematics = evidence.get("schematics")
    require(isinstance(schematics, list) and len(schematics) == 3, "expected three schematics")
    require(
        [item.get("revision") for item in schematics if isinstance(item, dict)]
        == ["V1.0", "V1.1", "V1.2"],
        "unexpected schematic revision order",
    )
    for item in schematics:
        require(isinstance(item, dict), "schematic entry must be an object")
        require(isinstance(item.get("path"), str), "schematic path is missing")
        require(SHA256_RE.fullmatch(str(item.get("sha256"))) is not None, "invalid schematic hash")

    invariants = evidence.get("cross_revision_invariants")
    require(isinstance(invariants, dict), "cross_revision_invariants must be an object")
    require(invariants.get("resolution") == {"width": 1024, "height": 600}, "wrong resolution")
    require(invariants.get("controller") == "EK79007", "wrong controller")
    require(invariants.get("power") == {"ldo3_mv": 2500, "ldo4_mv": 3300}, "wrong LDO voltages")
    require(invariants.get("backlight", {}).get("gpio") == 31, "wrong backlight GPIO")
    require(
        invariants.get("backlight", {}).get("power_gate_gpio_29_required") is False,
        "optional GPIO29 gate must not be required",
    )
    require(invariants.get("reset", {}).get("policy") == "do_not_drive", "reset GPIO must stay unused")

    authorization = evidence.get("authorization")
    require(isinstance(authorization, dict), "authorization must be an object")
    require(authorization.get("authorized") is True, "display path is not authorized")
    require(
        set(authorization.get("scope", []))
        == {
            "mipi_dsi_bus_0_dedicated_pins",
            "internal_ldo_channel_3_at_2500_mv",
            "internal_ldo_channel_4_at_3300_mv",
            "gpio31_lcd_backlight_enable_pwm",
        },
        "authorization grants unexpected resources",
    )
    denied = set(authorization.get("explicitly_not_authorized", []))
    require(
        {"gpio29_lcd_backlight_optional_power_gate", "gpio41_lcd_reset", "all_other_gpio"} <= denied,
        "unneeded GPIOs are not explicitly denied",
    )


def verify_vendor_tree(evidence: dict, vendor_tree: pathlib.Path) -> None:
    require(vendor_tree.is_dir(), f"vendor tree is not a directory: {vendor_tree}")
    try:
        commit = subprocess.run(
            ["git", "-C", str(vendor_tree), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot resolve vendor-tree commit: {error}")
    require(commit == evidence["official_source"]["commit"], "vendor-tree commit does not match evidence")

    expected_common_nets = {
        "LCD_BK_EN": [("R160", "1"), ("U6", "4-EN"), ("U7", "GPIO31")],
        "LCD_BK_POWER": [("Q11", "G"), ("R180", "1"), ("U7", "GPIO29")],
        "LCD_RESET": [("J21", "27"), ("R69", "2"), ("U7", "GPIO41")],
        "ESP32_LDO3": [("C59", "1"), ("R80", "2"), ("U7", "VFB3/VO3")],
        "ESP32_LDO4": [("C66", "1"), ("R109", "2"), ("U7", "VFB4/VO4")],
    }
    dsi_connector_pins = {
        "DSI_CLK_P": ("J21", "17"),
        "DSI_CLK_N": ("J21", "18"),
        "DSI_DATA1_P": ("J21", "20"),
        "DSI_DATA1_N": ("J21", "21"),
        "DSI_DATA0_P": ("J21", "23"),
        "DSI_DATA0_N": ("J21", "24"),
    }

    for schematic in evidence["schematics"]:
        path = vendor_tree / schematic["path"]
        require(path.is_file(), f"missing schematic: {schematic['path']}")
        require(sha256_file(path) == schematic["sha256"], f"schematic hash mismatch: {schematic['revision']}")
        nets = schematic_net_pinrefs(path)
        for name, expected in expected_common_nets.items():
            actual = [item for item in nets.get(name, []) if item[0] in {part for part, _ in expected}]
            require(actual == expected, f"{schematic['revision']} {name} pinrefs differ")
        for name, connector_pin in dsi_connector_pins.items():
            require(connector_pin in nets.get(name, []), f"{schematic['revision']} {name} connector differs")
        parts = schematic_parts(path)
        require(parts.get("U6", {}).get("value") == "MT9201", "backlight driver differs")
        require(parts.get("Q11", {}).get("value") == "AO3401_NC", "optional backlight MOSFET differs")
        require(parts.get("R184", {}).get("value") == "0R", "backlight bypass differs")

    lesson = evidence["official_lesson_07"]
    required_driver_tokens = (
        ".num_data_lanes = 2",
        ".lane_bit_rate_mbps = 900",
        ".dpi_clock_freq_mhz = 51",
        ".hsync_back_porch = 160",
        ".hsync_pulse_width = 70",
        ".hsync_front_porch = 160",
        ".vsync_back_porch = 23",
        ".vsync_pulse_width = 10",
        ".vsync_front_porch = 12",
        ".reset_gpio_num = -1",
    )
    required_header_tokens = (
        "#define V_size 600",
        "#define H_size 1024",
        "#define BITS_PER_PIXEL 16",
        "#define LCD_GPIO_BLIGHT 31",
        "#define BLIGHT_PWM_Hz 30000",
    )
    for revision in ("v1_1", "v1_2"):
        record = lesson[revision]
        driver = vendor_tree / record["driver_path"]
        header = driver.parent / "include/bsp_illuminate.h"
        require(driver.is_file() and header.is_file(), f"missing Lesson 07 {revision} source")
        require(sha256_file(driver) == record["driver_sha256"], f"Lesson 07 {revision} driver hash mismatch")
        require(sha256_file(header) == record["header_sha256"], f"Lesson 07 {revision} header hash mismatch")
        driver_text = driver.read_text(errors="strict")
        header_text = header.read_text(errors="strict")
        require(all(token in driver_text for token in required_driver_tokens), f"Lesson 07 {revision} timing differs")
        require(all(token in header_text for token in required_header_tokens), f"Lesson 07 {revision} geometry differs")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--vendor-tree",
        type=pathlib.Path,
        help="optional checkout of the exact official Elecrow repository commit",
    )
    args = parser.parse_args()

    evidence = json.loads(EVIDENCE_PATH.read_text())
    verify_structure(evidence)
    if args.vendor_tree is not None:
        verify_vendor_tree(evidence, args.vendor_tree.resolve())
        source_status = "verified"
    else:
        source_status = "not-supplied"
    print(
        "display path evidence: PASS "
        f"scope=display_only revisions=V1.0,V1.1,V1.2 vendor_tree={source_status}"
    )


if __name__ == "__main__":
    main()
