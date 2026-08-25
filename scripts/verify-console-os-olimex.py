#!/usr/bin/env python3

"""Fail-closed verifier for the Olimex ESP32-P4-PC Console OS build."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys

from p4_multiplayer_manifest import expected_multiplayer_extension


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"
DEFAULT_BUILD = APP / "build-olimex-esp32-p4-pc"
WAD_BYTES = 4_196_020
WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
OTA_BYTES = 0x7F0000


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Olimex Console OS verification failed: {message}")


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: pathlib.Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path} is not a JSON object")
    return value


def c_string(field: bytes, label: str) -> str:
    require(b"\0" in field, f"{label} is not terminated")
    value, padding = field.split(b"\0", 1)
    require(value and not any(padding), f"{label} padding is invalid")
    return value.decode("ascii")


def verify_update(path: pathlib.Path, app: pathlib.Path) -> dict[str, object]:
    package = path.read_bytes()
    image = app.read_bytes()
    require(package[:8] == b"P4OSUP1\0", "update magic differs")
    header, total, offset, payload, version, flags = struct.unpack_from(
        "<6I", package, 8
    )
    require(header == offset == 256 and total == len(package),
            "update layout differs")
    require(payload == len(image) and package[offset:] == image,
            "update payload is not the built app")
    require(version == 1 and flags == 0, "update version/flags differ")
    require(package[32:64] == hashlib.sha256(image).digest(),
            "update digest differs")
    require(c_string(package[160:176], "update target") == "esp32p4",
            "update targets the wrong SoC")
    require(not any(package[176:256]), "update reserved bytes are dirty")
    return {"bytes": len(package), "sha256": hashlib.sha256(package).hexdigest()}


def verify_game(path: pathlib.Path, manifest: dict) -> dict[str, object]:
    package = path.read_bytes()
    require(256 < len(package) <= 512 * 1024, f"{path.name} size is invalid")
    require(package[:8] == b"P4GAME1\0", f"{path.name} magic differs")
    fields = struct.unpack_from("<9IHH", package, 8)
    header, total, offset, payload, fmt, api, launcher, required, optional, _, flags = fields
    require(header == offset == 256 and total == len(package) and
            payload == len(package) - 256, f"{path.name} layout differs")
    require(fmt == api == 1 and launcher == manifest["launcher_id"],
            f"{path.name} API or launcher differs")
    profile_flag, profile_bytes = expected_multiplayer_extension(
        manifest,
        set(manifest["required_capabilities"]) |
        set(manifest["optional_capabilities"]),
    )
    require(required & 1 and not (required & optional) and
            flags == (1 | profile_flag),
            f"{path.name} capabilities/flags differ")
    require(package[48:80] == hashlib.sha256(package[256:]).digest(),
            f"{path.name} payload digest differs")
    require(c_string(package[80:128], "game id") == manifest["id"],
            f"{path.name} manifest binding differs")
    require(package[240:256] == profile_bytes,
            f"{path.name} multiplayer profile differs")
    return {"file": path.name, "bytes": len(package), "sha256": sha256(path)}


def main() -> None:
    build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_BUILD
    require(build.is_dir(), f"missing build directory: {build}")
    profile = read_json(ROOT / "hardware/boards/olimex-esp32-p4-pc-rev-b.json")
    require(profile.get("flash_authorized") is False and
            profile.get("hardware_tested") is False,
            "unverified hardware profile must remain write-locked")
    audio_profile = profile.get("interfaces", {}).get("audio", {})
    require(audio_profile.get("implemented") is True and
            audio_profile.get("hardware_tested") is False,
            "audio profile must describe build-only ES8311 support")

    toolchain = read_json(ROOT / "toolchain.lock.json")
    managed = toolchain.get("managed_components", {})
    for component, version in (
        ("espressif/esp_codec_dev", "1.5.4"),
        ("espressif/esp_lcd_lt8912b", "0.2.0"),
        ("espressif/usb", "1.5.0"),
        ("espressif/usb_host_hid", "1.2.0"),
    ):
        require(managed.get(component) == version,
                f"toolchain lock differs for {component}")
    dependency_lock = (APP / "dependencies-olimex.lock").read_text(
        encoding="utf-8")
    for component, version in (
        ("espressif/esp_codec_dev", "1.5.4"),
        ("espressif/esp_lcd_lt8912b", "0.2.0"),
        ("espressif/usb", "1.5.0"),
        ("espressif/usb_host_hid", "1.2.0"),
        ("idf", "5.5.3"),
    ):
        require(re.search(
            rf"(?ms)^  {re.escape(component)}:.*?^    version: "
            rf"{re.escape(version)}$", dependency_lock) is not None,
            f"Olimex dependency lock is missing {component} {version}")

    config = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    for setting in (
        "CONFIG_P4_BOARD_OLIMEX_ESP32_P4_PC_REV_B",
        "CONFIG_ESPTOOLPY_FLASHSIZE_16MB",
        "CONFIG_SPIRAM_MODE_HEX",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE",
        "CONFIG_ELF_LOADER_LOAD_PSRAM",
        "CONFIG_DOOM_AUDIO_ENGINE_ADAPTER",
    ):
        require(f"#define {setting} 1" in config, f"missing {setting}")
    require('#define CONFIG_PARTITION_TABLE_FILENAME "partitions-olimex.csv"' in config,
            "wrong partition table selected")
    require("CONFIG_TINYUSB_MSC_ENABLED" not in config,
            "USB-C must not expose microSD as mass storage")

    project = read_json(build / "project_description.json")
    require(project.get("project_name") == "p4_console_os" and
            project.get("target") == "esp32p4" and
            project.get("min_rev") == "100" and
            project.get("max_rev") == "199", "target/revision bounds differ")
    components = set(project.get("build_components", []))
    for component in (
        "console_shell", "platform_board", "platform_display",
        "platform_game_storage", "platform_gamepad_usb", "platform_usb_host",
        "platform_game_loader", "platform_os_update", "gamepad_core",
        "p4_multiplayer_registry",
        "platform_audio", "platform_readonly_blob", "doom_audio",
        "doom_engine_audio", "doom_gamepad_input", "doom_video",
    ):
        require(component in components, f"missing component {component}")
    for component in (
        "platform_touch", "board_deps_elecrow", "esp_tinyusb",
        "espressif__esp_tinyusb", "espressif__tinyusb",
    ):
        require(component not in components, f"Elecrow component linked: {component}")

    flasher = read_json(build / "flasher_args.json")
    require(flasher.get("flash_settings") == {
        "flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"
    }, "flash geometry differs")
    require(flasher.get("flash_files") == {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": "ota_data_initial.bin",
        "0x20000": "p4_console_os.bin",
    }, "flash set must not contain internal game storage")

    partition_tool = pathlib.Path(project["idf_path"]) / (
        "components/partition_table/gen_esp32part.py"
    )
    decoded = subprocess.run(
        [sys.executable, str(partition_tool),
         str(build / "partition_table/partition-table.bin")],
        check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(decoded.returncode == 0, f"partition decode failed: {decoded.stderr}")
    for row in (
        "otadata,data,ota,0x10000,8K,",
        "ota_0,app,ota_0,0x20000,8128K,",
        "ota_1,app,ota_1,0x810000,8128K,",
    ):
        require(row in decoded.stdout, f"partition row missing: {row}")
    require("game_data" not in decoded.stdout, "internal game partition remains")

    app = build / project["app_bin"]
    elf = build / project["app_elf"]
    require(app.is_file() and elf.is_file() and app.stat().st_size <= OTA_BYTES,
            "app artifact is missing or too large")
    update = verify_update(build / "P4UPDATE.P4U", app)
    bundle = build / "sd-card"
    require((bundle / "UPDATE/P4UPDATE.P4U").read_bytes() ==
            (build / "P4UPDATE.P4U").read_bytes(), "SD update copy differs")
    wad = bundle / "DOOM1.WAD"
    require(wad.stat().st_size == WAD_BYTES and sha256(wad) == WAD_SHA256,
            "SD bundle has the wrong Doom shareware data")
    require((bundle / "README.TXT").read_bytes() ==
            (APP / "game-storage/README-OLIMEX.TXT").read_bytes(),
            "SD bundle has the wrong instructions")

    games: list[dict[str, object]] = []
    for manifest_path in sorted((ROOT / "games").glob("*/game.json")):
        manifest = read_json(manifest_path)
        if manifest.get("enabled") is True:
            games.append(verify_game(
                bundle / "GAMES" / manifest["package_file"], manifest))
    expected_games = read_json(APP / "app-metadata.json")[
        "native_game_api"]["seed_packages"]
    require([game["file"] for game in games] == expected_games,
            "SD seed game set differs")
    app_data = app.read_bytes()
    for game in games:
        package = bundle / "GAMES" / str(game["file"])
        require(package.read_bytes() not in app_data,
                f"{package.name} leaked into the OTA application")

    compiler = pathlib.Path(project["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols_result = subprocess.run(
        [str(nm), "-g", str(elf)], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(symbols_result.returncode == 0, "cannot inspect ELF symbols")
    symbols = symbols_result.stdout
    for symbol in (
        "app_main", "esp_lcd_new_panel_lt8912b", "esp_vfs_fat_sdmmc_mount",
        "usb_host_install", "hid_host_install",
        "platform_gamepad_usb_get_input_snapshot", "platform_os_update_install",
        "console_os_launch_doom", "platform_audio_create",
        "esp_codec_dev_open", "DG_sound_module", "DG_music_module",
    ):
        require(f" {symbol}\n" in symbols, f"missing ELF symbol {symbol}")
    for symbol in (
        "platform_touch_create",
        "tinyusb_driver_install", "__wrap_tud_msc_write10_cb",
        "p4_maze_chase_game", "p4_space_invaders_game",
        "_binary_bytebud_p4g_start",
        "platform_game_catalog_add_embedded_fallback",
    ):
        require(f" {symbol}\n" not in symbols, f"forbidden ELF symbol {symbol}")

    compile_commands = (build / "compile_commands.json").read_text(encoding="utf-8")
    require("platform_display_olimex.c" in compile_commands and
            "platform_usb_input_olimex.c" in compile_commands and
            "platform_audio_olimex.c" in compile_commands and
            "doom_embedded_gamepad_audio_main.c" in compile_commands and
            "doom_audio_sound_module.c" in compile_commands,
            "Olimex drivers were not compiled")
    require("platform_touch.c" not in compile_commands and
            "doom_embedded_touch_audio_main.c" not in compile_commands,
            "Elecrow runtime source entered the Olimex build")

    print(json.dumps({
        "result": "olimex-console-os-build-verified",
        "board": profile["id"],
        "app_bytes": app.stat().st_size,
        "app_sha256": sha256(app),
        "ota_slot_bytes": OTA_BYTES,
        "input": ["gamepad", "boot-keyboard", "boot-mouse"],
        "audio": "ES8311/I2S1 build-tested; hardware unverified",
        "doom": "storage-backed gamepad/keyboard/mouse/audio handoff linked",
        "games": games,
        "update": update,
        "hardware_tested": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
