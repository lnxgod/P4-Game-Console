#!/usr/bin/env python3
"""Verify a Tab5 build candidate. This never authorizes or writes hardware."""
from __future__ import annotations
import hashlib
import json
import pathlib
import re
import shlex
import struct
import subprocess
import sys
from p4_game_release import development_only

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_native_only(components: set[str], symbols: str) -> None:
    """Reject accidental reintroduction of the retired Lua execution path."""
    retired = {"p4_lua_runtime", "p4_script_renderer", "p4_script_audio",
               "p4_game_runtime_core", "p4_content_catalog", "lua"}
    require(not retired & components, "retired Lua execution component in native-only build")
    require(re.search(r"\b(?:lua[A-Za-z0-9_]*|p4_lua_[A-Za-z0-9_]*|run_script_cart|"
                      r"p4_content_(?:validate_cart_file|load_cart_source|catalog_scan))$",
                      symbols, re.M) is None,
            "retired Lua execution symbol in native-only build")


def verify_arena_memory(symbols: str) -> None:
    """Arena histories/caches must not consume the startup DMA reserve."""
    for name in ("gc", "s_arena_reader", "visplanes", "openings"):
        match = re.search(r"^([0-9a-fA-F]+) [bB] " + name + r"$", symbols, re.M)
        require(match is not None and 0x48000000 <= int(match[1], 16) < 0x4a000000,
                f"Arena {name} must be in PSRAM to preserve startup DMA memory")


def verify_storage_seek(sdk: str) -> None:
    """Require bounded FAT cluster maps for random Arena resource reads."""
    for line in ("CONFIG_FATFS_USE_FASTSEEK=y",
                 "CONFIG_FATFS_ALLOC_PREFER_EXTRAM=y",
                 "CONFIG_FATFS_FAST_SEEK_BUFFER_SIZE=112456"):
        require(line in sdk.splitlines(), f"missing required sdkconfig: {line}")


def verify_udp_mailbox(sdk: str) -> None:
    """Keep the per-socket UDP receive queue at the reviewed Tab5 bound."""
    values = re.findall(r"^CONFIG_LWIP_UDP_RECVMBOX_SIZE=(.*)$", sdk, re.M)
    require(values == ["32"], "Tab5 UDP receive mailbox must be exactly 32")


def compile_definitions(command: dict) -> dict[str, list[str | None]]:
    """Keep every -D/-U occurrence so a later override cannot hide drift."""
    arguments = command.get("arguments")
    if arguments is None:
        arguments = shlex.split(command["command"])
    require(isinstance(arguments, list) and
            all(isinstance(arg, str) for arg in arguments),
            "invalid compiler arguments")
    definitions: dict[str, list[str | None]] = {}
    index = 0
    while index < len(arguments):
        arg = arguments[index]
        if arg.startswith(("-D", "-U")):
            operation, value = arg[:2], arg[2:]
            if not value:
                index += 1
                require(index < len(arguments), "missing compiler macro operand")
                value = arguments[index]
            name, separator, content = value.partition("=")
            require(bool(name), "empty compiler macro name")
            definitions.setdefault(name, []).append(
                None if operation == "-U" else content if separator else "1")
        index += 1
    return definitions


def verify_native_video(commands: list[dict]) -> None:
    """Check compiled source/flag contracts, not runtime FPS or acceptance."""
    dimensions = {"P4_DOOM_NATIVE_WIDTH": "768", "P4_DOOM_NATIVE_HEIGHT": "480",
                  "DOOMGENERIC_RESX": "768", "DOOMGENERIC_RESY": "480"}
    native = {"DOOM_VIDEO_NATIVE_TAB5": "1"}
    indexed = {**native, "P4_DOOM_INDEXED_PACKET_EXPERIMENT": "1"}
    required_sources = {
        "apps/console_os/main/console_os_main.c": {**dimensions, **native},
        "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c":
            {**dimensions, **native},
        "components/doom_video/src/doom_video_tab5_worker.c": indexed,
        "components/doom_video/src/doom_video_indexed.c": indexed,
    }
    vendor_prefix = "third_party/doomgeneric/doomgeneric/"
    for name in ("r_draw.c", "r_main.c", "r_plane.c", "v_video.c", "i_video.c",
                 "doomgeneric.c"):
        required_sources[vendor_prefix + name] = dimensions
    forbidden_enabled = (
        "P4_CONSOLE_NATIVE_AIR_LOW_RES", "P4_CONSOLE_NATIVE_TIDE_BLAST_LOW_RES",
        "P4_CONSOLE_NATIVE_CHECKERS_LOW_RES", "P4_DOOM_TAB5_FUSED_PRESCALE",
    )
    seen: set[str] = set()
    for command in commands:
        source = pathlib.Path(command["file"])
        if not source.is_absolute():
            source = pathlib.Path(command["directory"]) / source
        source_path = source.resolve().as_posix()
        definitions = compile_definitions(command)
        for name in forbidden_enabled:
            require(name not in definitions or definitions[name] == ["0"],
                    f"retired Tab5 video override: {name} in {source.name}")
        require(not source_path.endswith("/components/doom_video/src/doom_video_espidf.c"),
                "legacy Doom video adapter compiled for Tab5")
        expected = {}
        for suffix, values in required_sources.items():
            if source_path.endswith("/" + suffix):
                seen.add(suffix)
                expected.update(values)
        # Every compiled vendor translation unit shares raster dimensions,
        # including ones not in the minimum required renderer source set.
        if "/" + vendor_prefix in source_path:
            expected.update(dimensions)
        for name, value in expected.items():
            require(definitions.get(name) == [value],
                    f"native Tab5 video requires exactly one {name}={value} in {source.name}")
        # Optional explicit raster/path flags must also agree in video sources.
        if expected:
            for name, value in {**dimensions, **indexed}.items():
                if name in definitions:
                    require(definitions[name] == [value],
                            f"conflicting native Tab5 video flag {name} in {source.name}")
    require(set(required_sources) <= seen,
            "missing native Tab5 video source: " + ", ".join(sorted(set(required_sources) - seen)))


def verify_usb_host(enabled: bool, components: set[str], sources: set[str], symbols: str) -> None:
    """Qualify the selected build feature, including an explicitly disabled host.

    An existing exact-artifact install may predate the controller candidate.
    Disabled builds must contain neither the host component nor power symbols.
    """
    required_components = {"platform_usb_host", "platform_gamepad_usb", "platform_gamepad_xusb", "platform_gamepad", "doom_gamepad_input"}
    required_sources = {"key_merge.c", "usb_power_control.c", "platform_usb_host.c", "platform_gamepad_usb.c", "xusb.c", "xusb_host.c"}
    required_symbols = {"platform_tab5_usb_host_power", "platform_usb_host_start",
                        "platform_usb_host_enable_root_port", "platform_gamepad_usb_start",
                        "doom_gamepad_input_update", "doom_key_merge_update",
                        "platform_gamepad_xusb_start", "gamepad_xusb_decode"}
    present_symbols = {name for name in required_symbols
                       if re.search(r"\b" + name + r"$", symbols, re.M)}
    if enabled:
        require(required_components <= components, "enabled Tab5 USB host lacks components")
        require(required_sources <= sources, "enabled Tab5 USB host lacks source adapters")
        require(required_symbols <= present_symbols, "enabled Tab5 USB host lacks linked drivers")
        buffers = re.search(r"^([0-9a-fA-F]+) [bB] s_report_buffers$", symbols, re.M)
        require(buffers is not None and 0x48000000 <= int(buffers[1], 16) < 0x4a000000,
                "Tab5 HID report/parser buffers must be in PSRAM to preserve startup DMA memory")
    else:
        require(not ({"platform_usb_host", "platform_gamepad_usb", "platform_gamepad_xusb"} & components),
                "USB host component present in a disabled build")
        require(not present_symbols, "USB host entry points present in a disabled build")


def verify(build: pathlib.Path, firmware_only: bool = False) -> dict:
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
    verify_storage_seek(sdk)
    verify_udp_mailbox(sdk)
    usb_host_enabled = "CONFIG_P4_TAB5_USB_HOST=y" in sdk.splitlines()
    charger_control_enabled = "CONFIG_P4_TAB5_CHARGER_500MA=y" in sdk.splitlines()
    for line in (
        "CONFIG_P4_BOARD_M5STACK_TAB5=y", 'CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
        'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-tab5.csv"',
        "CONFIG_ESP32P4_REV_MIN_FULL=100", "CONFIG_ESP32P4_REV_MAX_FULL=199",
        "CONFIG_SPIRAM=y", "CONFIG_SPIRAM_SPEED_200M=y",
        "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y",
        "CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536",
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
    version = json.loads((APP / "app-metadata.json").read_text())["version"]
    brand = (ROOT / "components/console_shell/include/console/brand.h").read_text()
    cmake = (APP / "CMakeLists.txt").read_text()
    require(project["project_version"] == version and
            re.search(r'#define CONSOLE_PRODUCT_VERSION\s+"' + re.escape(version) + r'"', brand) and
            re.search(r'set\(PROJECT_VER\s+"' + re.escape(version) + r'"\)', cmake),
            "OS build, metadata and interface versions differ")
    components = set(project["build_components"])
    require({"platform_tab5", "board_deps_tab5", "platform_touch", "platform_audio", "console_shell"} <= components,
            "missing Tab5 core component")
    require(not ({"platform_audio_factory", "platform_audio_es8311"} & components),
            "unexpected legacy audio or unqualified radio")
    ble_enabled = "CONFIG_P4_TAB5_BLE_MULTIPLAYER=y" in sdk.splitlines()
    radio_components = {"platform_radio_hosted", "platform_ble_host", "platform_multiplayer_ble", "platform_multiplayer_wifi"}
    if ble_enabled:
        require(radio_components <= components, "missing Tab5 BLE service")
        for key, value in {"CLK":12,"CMD":13,"D0":11,"D1":10,"D2":9,"D3":8}.items():
            require(f"CONFIG_ESP_HOSTED_SDIO_PIN_{key}={value}" in sdk.splitlines(), "wrong Tab5 SDIO pin")
        for value in ("CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=15", "CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=10000",
                      "CONFIG_ESP_HOSTED_SDIO_RESET_ACTIVE_HIGH=y", "CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI=y", "CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y"):
            require(value in sdk.splitlines(), "wrong Tab5 C6 transport")
    else:
        require(not radio_components & components, "BLE service linked in radio-off build")
    commands = json.loads((build / "compile_commands.json").read_text())
    verify_native_video(commands)
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
    require(update[64:96].split(b"\0", 1)[0].decode("ascii") == version,
            "update package version differs from OS")
    packages = []
    if not firmware_only:
        bundle = build / "sd-card"
        require({p.name for p in bundle.iterdir()} == {"DOOM1.WAD", "GAMES", "README.TXT", "UPDATE"}, "unexpected native-only SD root")
        require((bundle / "UPDATE/P4UPDATE.P4U").read_bytes() == update, "staged update differs")
        require(digest(bundle / "DOOM1.WAD") == "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771",
                "local shareware WAD identity mismatch")
        packages = sorted((bundle / "GAMES").glob("*.P4G"))
        manifests = [json.loads(p.read_text()) for p in (ROOT / "games").glob("*/game.json")]
        released = [m for m in manifests if not development_only(m)]
        expected = {m["package_file"] for m in released}
        require({p.name for p in packages} == expected, "native game seed differs from standard manifests")
        expected_files = expected | {m["resource_file"] for m in released if m.get("resource_file")}
        require({p.name for p in (bundle / "GAMES").iterdir()} == expected_files,
                "standard bundle contains stale or unexpected game files")
        for path in packages:
            data = path.read_bytes()
            require(256 < len(data) <= 512*1024 and data[:8] == b"P4GAME1\0" and
                    struct.unpack_from("<4I", data, 8) == (256, len(data), 256, len(data)-256) and
                    data[48:80] == hashlib.sha256(data[256:]).digest(), f"corrupt native cartridge: {path.name}")
    nm = pathlib.Path(project["c_compiler"]).with_name("riscv32-esp-elf-nm")
    symbols = subprocess.check_output([str(nm), "--defined-only", str(build / project["app_elf"])], text=True)
    verify_native_only(components, symbols)
    if "doom_gc_p4mp.c" in sources:
        verify_arena_memory(symbols)
    if ble_enabled:
        require("esp_hosted_host_init.c" not in sources, "radio constructor must be excluded")
        for name in ("platform_tab5_radio_power", "platform_multiplayer_ble_enable", "platform_multiplayer_wifi_enable"):
            require(re.search(r'\b' + name + r'$', symbols, re.M) is not None, "missing linked BLE adapter")
    verify_usb_host(usb_host_enabled, components, sources, symbols)
    if charger_control_enabled:
        require(re.search(r"\bplatform_tab5_charger_init$", symbols, re.M) is not None, "enabled charger control is missing")
    if not charger_control_enabled:
        require(re.search(r"\bplatform_tab5_charger_init$", symbols, re.M) is None,
                "charger control linked in a charger-disabled UI build")
    for name in ("platform_tab5_display_reset", "es8388_codec_new", "esp_lcd_new_panel_ili9881c",
                 "esp_lcd_new_panel_st7123", "esp_lcd_touch_new_i2c_gt911", "esp_lcd_touch_new_i2c_st7123"):
        require(re.search(r'\b' + name + r'$', symbols, re.M) is not None, f"missing linked driver: {name}")
    return {"result": "tab5-build-candidate-verified", "board": "m5stack-tab5", "version": version, "image_bytes": len(image), "ble_multiplayer_enabled": ble_enabled, "wifi_multiplayer_enabled": ble_enabled,
            "image_sha256": digest(image_path), "native_cartridges": None if firmware_only else len(packages), "content_bundle_verified": not firmware_only,
            "usb_host_enabled": usb_host_enabled,
            "charger_control_enabled": charger_control_enabled,
            "hardware_verified": False, "flash_authorized": False}


if __name__ == "__main__":
    try:
        args = [arg for arg in sys.argv[1:] if arg != "--firmware-only"]
        require(len(args) <= 1, "usage: verify-console-os-tab5.py [--firmware-only] [build]")
        print(json.dumps(verify(pathlib.Path(args[0]) if args else APP / "build-tab5", "--firmware-only" in sys.argv), sort_keys=True))
    except (ValueError, KeyError, OSError, struct.error) as error:
        raise SystemExit(f"Tab5 verification failed: {error}") from error
