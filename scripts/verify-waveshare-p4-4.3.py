#!/usr/bin/env python3
"""Validate the schematic-reviewed Waveshare 4.3 board contract."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / "hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"waveshare board contract failed: {message}")


profile = json.loads(PROFILE.read_text(encoding="utf-8"))
require(profile["vendor"] == "Waveshare", "vendor")
require(profile["sku"] == "ESP32-P4-WIFI6-Touch-LCD-4.3", "SKU")
require(profile["measured"]["display_resolution_portrait"] == "480x800", "display resolution")
usb = profile["usb_vbus_assessment"]
require(usb["otg_connector_has_vbus"] is True, "OTG VBUS fact")
require(usb["otg_data_pins"] == [24, 25], "OTG data GPIOs")
require(usb["cc1_cc2_resistors_ohms"] == 5100, "CC resistor fact")
require(usb["advertised_role_from_schematic"] == "sink/device", "USB role")
require(usb["controller_host_power_ready"] is False, "USB host gate")
for name in ("display", "touch", "audio", "usb_host"):
    require(profile["peripheral_authorizations"][name]["authorized"] is False,
            f"{name} must remain denied")
print("waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 contract valid")
