#!/usr/bin/env python3
"""Verify a Tab5 build candidate. This never authorizes or writes hardware."""
from __future__ import annotations
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_usb_host(enabled: bool, components: set[str], sources: set[str], symbols: str) -> None:
    """Qualify the selected build feature, including an explicitly disabled host.

    An existing exact-artifact install may predate the controller candidate.
    Disabled builds must contain neither the host component nor power symbols.
    """
    required_components = {"platform_usb_host", "platform_gamepad_usb", "platform_gamepad", "doom_gamepad_input"}
    required_sources = {"key_merge.c", "usb_power_control.c", "platform_usb_host.c", "platform_gamepad_usb.c"}
    required_symbols = {"platform_tab5_usb_host_power", "platform_usb_host_start",
                        "platform_usb_host_enable_root_port", "platform_gamepad_usb_start",
                        "doom_gamepad_input_update", "doom_key_merge_update"}
    present_symbols = {name for name in required_symbols
                       if re.search(r"\b" + name + r"$", symbols, re.M)}
    if enabled:
        require(required_components <= components, "enabled Tab5 USB host lacks components")
        require(required_sources <= sources, "enabled Tab5 USB host lacks source adapters")
        require(required_symbols <= present_symbols, "enabled Tab5 USB host lacks linked drivers")
    else:
        require(not ({"platform_usb_host", "platform_gamepad_usb"} & components),
                "USB host component present in a disabled build")
        require(not present_symbols, "USB host entry points present in a disabled build")


def verify(build: pathlib.Path) -> dict:
    profile = json.loads((ROOT / "hardware/boards/m5stack-tab5/board-profile.json").read_text())
    require(profile["id"] == "m5stack-tab5", "wrong board profile")
    require(profile["flash_authorized"] is False,
            "global flash gate must stay closed; use an exact-unit/artifact authorization")
    provenance = json.loads((ROOT / "third_party/tab5-bsp.json").read_text())
    for entry in provenance["files"]:
        require(digest(ROOT / entry["path"]) == entry["sha256"],
                f"upstream vendor file changed: {entry['path']}")
    bmi = json.loads((ROOT / "third_party/bmi270/source.json").read_text())
    require(digest(ROOT / "third_party/bmi270/config.inc") == bmi["config_inc_sha256"],
            "BMI270 configuration differs from pinned Bosch data")
    sdk = (build / "sdkconfig").read_text()
    usb_host_enabled = "CONFIG_P4_TAB5_USB_HOST=y" in sdk.splitlines()
    for line in (
        "CONFIG_P4_BOARD_M5STACK_TAB5=y", 'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-tab5.csv"',
        "CONFIG_ESP32P4_REV_MIN_FULL=100", "CONFIG_ESP32P4_REV_MAX_FULL=199",
        "CONFIG_SPIRAM=y", "CONFIG_SPIRAM_SPEED_200M=y",
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y",
        "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y", "CONFIG_CODEC_ES8388_SUPPORT=y",
    ):
        require(line in sdk.splitlines(), f"missing required sdkconfig: {line}")
    for option in ("P4_BOARD_ELECROW_CROWPANEL_ADVANCED_10", "P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B",
                   "P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3", "P4_MP_DIRECT_UART",
                   "P4_WAVESHARE_H2_USB_HOST_MODE"):
        require(f"CONFIG_{option}=y" not in sdk.splitlines(), f"unexpected peripheral/board enabled: {option}")
    lock = (APP / "dependencies-tab5.lock").read_text()
    manifest = (ROOT / "components/board_deps_tab5/idf_component.yml").read_text()
    for name, version in re.findall(r'(espressif/[\w_]+):\s*["\']?==([\d.]+)', manifest):
        block = re.search(r'^  ' + re.escape(name) + r':\n(.*?)(?=^  \S)', lock, re.M | re.S)
        require(block is not None and f"    version: {version}" in block[1], f"dependency lock mismatch: {name}")
    project = json.loads((build / "project_description.json").read_text())
    require(project["target"] == "esp32p4" and project["git_revision"] == "v5.5.3" and
            project["min_rev"] == "100" and project["max_rev"] == "199", "wrong SDK/SoC/revision family")
    components = set(project["build_components"])
    require({"platform_tab5", "board_deps_tab5", "platform_touch", "platform_audio", "console_shell"} <= components,
            "missing Tab5 core component")
    require(not ({"platform_audio_factory", "platform_audio_es8311", "platform_radio_hosted"} & components),
            "unexpected legacy audio or unqualified radio")
    commands = json.loads((build / "compile_commands.json").read_text())
    sources = {pathlib.Path(command["file"]).name for command in commands}
    for name in ("platform_display_tab5.c", "platform_touch_tab5.c", "platform_audio_tab5.c", "tab5.c",
                 "sensors.c", "sensor_decode.c"):
        require(name in sources, f"dedicated adapter not compiled: {name}")
    require("platform_touch.c" not in sources, "legacy touch reset code was compiled")
    flasher = json.loads((build / "flasher_args.json").read_text())
    require(flasher["flash_settings"]["flash_size"] == "16MB", "wrong flash capacity")
    require(flasher["flash_files"] == {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
            "0x10000": "ota_data_initial.bin", "0x20000": "p4_console_os.bin"}, "unexpected flash layout")
    image_path = build / project["app_bin"]
    image = image_path.read_bytes()
    require(24 < len(image) <= 0x7f0000 and image[0] == 0xe9, "app image does not fit OTA slot")
    require(struct.unpack_from("<H", image, 12)[0] == 18 and
            struct.unpack_from("<HH", image, 15) == (100, 199), "binary chip/revision gate mismatch")
    partition = (build / "partition_table/partition-table.bin").read_bytes()
    app_slots = []
    for offset in range(0, len(partition), 32):
        magic, kind, subtype, start, size = struct.unpack_from("<HBBII", partition, offset)
        if magic == 0x50aa and kind == 0:
            app_slots.append((subtype, start, size))
    require(app_slots == [(0x10, 0x20000, 0x7f0000), (0x11, 0x810000, 0x7f0000)], "binary OTA partitions differ")
    update = (build / "P4UPDATE.P4U").read_bytes()
    require(update[:8] == b"P4OSUP1\0" and struct.unpack_from("<6I", update, 8) ==
            (256, len(update), 256, len(image), 1, 0) and update[256:] == image and
            update[32:64] == hashlib.sha256(image).digest(), "update package does not bind built app")
    require(update[160:176] == b"esp32p4-tab5" + bytes(4), "update lacks Tab5 board binding")
    bundle = build / "sd-card"
    require({p.name for p in bundle.iterdir()} == {"DOOM1.WAD", "GAMES", "P4", "README.TXT", "UPDATE"}, "unexpected SD root")
    require((bundle / "UPDATE/P4UPDATE.P4U").read_bytes() == update, "staged update differs")
    require(digest(bundle / "DOOM1.WAD") == "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771",
            "local shareware WAD identity mismatch")
    packages = sorted((bundle / "GAMES").glob("*.P4G"))
    manifests = [json.loads(p.read_text()) for p in (ROOT / "games").glob("*/game.json")]
    expected = {m["package_file"] for m in manifests if m.get("enabled") is True}
    require({p.name for p in packages} == expected, "native game seed differs from enabled manifests")
    for path in packages:
        data = path.read_bytes()
        require(256 < len(data) <= 512*1024 and data[:8] == b"P4GAME1\0" and
                struct.unpack_from("<4I", data, 8) == (256, len(data), 256, len(data)-256) and
                data[48:80] == hashlib.sha256(data[256:]).digest(), f"corrupt native cartridge: {path.name}")
    nm = pathlib.Path(project["c_compiler"]).with_name("riscv32-esp-elf-nm")
    symbols = subprocess.check_output([str(nm), "--defined-only", str(build / project["app_elf"])], text=True)
    verify_usb_host(usb_host_enabled, components, sources, symbols)
    for name in ("platform_tab5_display_reset", "es8388_codec_new", "esp_lcd_new_panel_ili9881c",
                 "esp_lcd_new_panel_st7123", "esp_lcd_touch_new_i2c_gt911", "esp_lcd_touch_new_i2c_st7123"):
        require(re.search(r'\b' + name + r'$', symbols, re.M) is not None, f"missing linked driver: {name}")
    return {"result": "tab5-build-candidate-verified", "board": "m5stack-tab5", "image_bytes": len(image),
            "image_sha256": digest(image_path), "native_cartridges": len(packages),
            "usb_host_enabled": usb_host_enabled,
            "hardware_verified": False, "flash_authorized": False}


if __name__ == "__main__":
    try:
        print(json.dumps(verify(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else APP / "build-tab5"), sort_keys=True))
    except (ValueError, KeyError, OSError, struct.error) as error:
        raise SystemExit(f"Tab5 verification failed: {error}") from error
