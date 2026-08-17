#!/usr/bin/env python3
"""Fail-closed verifier for the Waveshare Console OS and SD bundle."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"
DEFAULT_BUILD = APP / "build-waveshare-landscape"
WAD_BYTES = 4_196_020
WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
LOGO_BYTES = 25_628
LOGO_SHA256 = "6f3963e2d3182eadacbf0ac4397b719578395bff894bb00c07ca65849c9e570f"
P4CART_SEED = pathlib.Path("P4/GAMES/BOUNCE-LAB.P4CART")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"Waveshare Console OS verification failed: {message}")


def read_json(path: pathlib.Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(value, dict), f"{path} is not a JSON object")
    return value


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def c_string(field: bytes, label: str) -> str:
    require(b"\0" in field, f"{label} is not terminated")
    value, padding = field.split(b"\0", 1)
    require(value and not any(padding), f"{label} padding is invalid")
    return value.decode("ascii")


def verify_game(path: pathlib.Path, manifest: dict) -> dict[str, object]:
    package = path.read_bytes()
    require(256 < len(package) <= 512 * 1024,
            f"{path.name} size is invalid")
    require(package[:8] == b"P4GAME1\0", f"{path.name} magic differs")
    fields = struct.unpack_from("<9IHH", package, 8)
    header, total, offset, payload, fmt, api, launcher, required, optional, accent, flags = fields
    require(header == offset == 256 and total == len(package) and
            payload == len(package) - offset,
            f"{path.name} layout differs")
    require(fmt == api == 1 and launcher == manifest["launcher_id"],
            f"{path.name} API or launcher differs")
    require(required & 1 and not (required & optional) and flags == 1,
            f"{path.name} capabilities or disclosure flags differ")
    require(accent == int(manifest["accent_rgb565"], 16),
            f"{path.name} accent differs")
    require(package[48:80] == hashlib.sha256(package[offset:]).digest(),
            f"{path.name} payload digest differs")
    require(c_string(package[80:128], f"{path.name} game ID") == manifest["id"],
            f"{path.name} game ID differs")
    require(c_string(package[128:144], f"{path.name} title") == manifest["title"],
            f"{path.name} title differs")
    require(c_string(package[176:208], f"{path.name} folder") == manifest["folder"],
            f"{path.name} folder differs")
    require(c_string(package[208:224], f"{path.name} version") == manifest["version"],
            f"{path.name} version differs")
    require(not any(package[240:256]), f"{path.name} reserved bytes differ")
    return {"file": path.name, "bytes": len(package), "sha256": sha256(path)}


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
    require(version == 1 and flags == 0, "update version or flags differ")
    require(package[32:64] == hashlib.sha256(image).digest(),
            "update digest differs")
    require(c_string(package[160:176], "update target") == "esp32p4",
            "update targets the wrong SoC")
    require(not any(package[176:256]), "update reserved bytes differ")
    return {"file": path.name, "bytes": len(package), "sha256": sha256(path)}


def verify_p4cart(path: pathlib.Path) -> dict[str, object]:
    inspect = subprocess.run(
        [sys.executable,
         str(ROOT / "game-platform/scripts/p4cart.py"),
         "inspect", str(path)],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(inspect.returncode == 0,
            f"seed P4 Cart is invalid: {inspect.stderr.strip()}")
    with tempfile.TemporaryDirectory(prefix="p4cart-seed-") as temporary:
        rebuilt = pathlib.Path(temporary) / "BOUNCE-LAB.P4CART"
        packed = subprocess.run(
            [sys.executable,
             str(ROOT / "game-platform/scripts/p4cart.py"),
             "pack", str(ROOT / "game-platform/templates/bounce-lab"),
             str(rebuilt)],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(packed.returncode == 0 and rebuilt.read_bytes() == path.read_bytes(),
                f"seed P4 Cart is not deterministic: {packed.stderr.strip()}")
    return {"file": str(P4CART_SEED), "bytes": path.stat().st_size,
            "sha256": sha256(path)}


def main() -> None:
    build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else DEFAULT_BUILD
    require(build.is_dir(), f"missing build directory: {build}")

    toolchain = read_json(ROOT / "toolchain.lock.json")
    require(toolchain["esp_idf"]["version"] == "5.5.3",
            "unexpected ESP-IDF lock")
    require(toolchain["target"] == {
        "chip": "esp32p4",
        "silicon_family": "revision_1_x",
        "min_revision_full": 100,
        "max_revision_full": 199,
    }, "unexpected silicon lock")
    lock = (APP / "dependencies-waveshare.lock").read_text(encoding="utf-8")
    for component, version in (
        ("espressif/elf_loader", "1.3.1"),
        ("espressif/esp_codec_dev", "1.5.4"),
        ("espressif/esp_lcd_st7701", "1.1.2"),
        ("espressif/esp_lcd_touch", "1.1.2"),
        ("espressif/esp_lcd_touch_gt911", "1.1.3"),
        ("idf", "5.5.3"),
    ):
        require(re.search(
            rf"(?ms)^  {re.escape(component)}:.*?^    version: "
            rf"{re.escape(version)}$", lock) is not None,
            f"dependency lock is missing {component} {version}")

    profile = read_json(
        ROOT / "hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json"
    )
    storage = profile["peripheral_authorizations"]["storage"]
    require(storage.get("authorized") is True and
            storage.get("formatting_authorized") is False and
            storage.get("writes_authorized") is False,
            "microSD contract must remain exact-unit read-only")
    require(profile["usb_vbus_assessment"]["controller_host_power_ready"] is False,
            "USB host power gate changed")

    sdkconfig = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    for setting in (
        "CONFIG_P4_BOARD_WAVESHARE_ESP32_P4_WIFI6_TOUCH_LCD_4_3",
        "CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3",
        "CONFIG_PLATFORM_DISPLAY_WAVESHARE_4_3_BUILD_ONLY",
        "CONFIG_PLATFORM_TOUCH_WAVESHARE_4_3_BUILD_ONLY",
        "CONFIG_PLATFORM_AUDIO_ES8311_WAVESHARE_4_3_BUILD_ONLY",
        "CONFIG_ELF_LOADER",
        "CONFIG_ELF_LOADER_LOAD_PSRAM",
    ):
        require(f"#define {setting} 1" in sdkconfig,
                f"build is missing {setting}")
    require("CONFIG_ELF_LOADER_ESPIDF_SYMBOLS" not in sdkconfig,
            "cartridges must not resolve arbitrary ESP-IDF symbols")

    project = read_json(build / "project_description.json")
    require(project.get("project_name") == "p4_console_os" and
            project.get("target") == "esp32p4" and
            project.get("min_rev") == "100" and
            project.get("max_rev") == "199",
            "project identity or silicon range differs")
    components = set(project.get("build_components", []))
    required_components = {
        "board_deps_waveshare", "console_shell", "p4_desktop",
        "p4_content_catalog", "p4_game_api", "p4_multiplayer",
        "platform_board", "platform_display", "platform_touch",
        "platform_game_catalog", "platform_game_loader",
        "platform_game_storage", "platform_os_update",
    }
    require(required_components <= components, "required component missing")
    require(not ({"platform_usb_host", "platform_gamepad_usb"} & components),
            "USB host components must remain absent")
    require(not ({"asteroids", "byte_buddy", "maze_chase", "space_invaders"} & components),
            "games were linked statically into the OS")

    flasher = read_json(build / "flasher_args.json")
    require(flasher.get("flash_settings") == {
        "flash_mode": "dio", "flash_size": "32MB", "flash_freq": "80m",
    }, "flash geometry differs")
    require(flasher.get("flash_files") == {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": "ota_data_initial.bin",
        "0x20000": "p4_console_os.bin",
    }, "full-project flash map differs")

    partition_tool = pathlib.Path(str(project["idf_path"])) / \
        "components/partition_table/gen_esp32part.py"
    partition = subprocess.run(
        [sys.executable, str(partition_tool),
         str(build / "partition_table/partition-table.bin")],
        check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(partition.returncode == 0, "partition table cannot be decoded")
    for row in (
        "otadata,data,ota,0x10000,8K,",
        "ota_0,app,ota_0,0x20000,8128K,",
        "ota_1,app,ota_1,0x810000,8128K,",
    ):
        require(row in partition.stdout, f"partition table is missing {row}")
    require("game_data" not in partition.stdout,
            "Waveshare must not contain the internal game-data partition")

    app = build / str(project["app_bin"])
    require(app.is_file() and app.stat().st_size <= 0x7F0000,
            "application is missing or does not fit OTA")
    bundle = build / "sd-card"
    require(sorted(item.name for item in bundle.iterdir()) ==
            ["DOOM1.WAD", "GAMES", "P4", "README.TXT", "UPDATE"],
            "SD bundle root differs")
    wad = bundle / "DOOM1.WAD"
    require(wad.is_file() and wad.stat().st_size == WAD_BYTES and
            sha256(wad) == WAD_SHA256, "bundle Doom WAD differs")
    require("*.[Ww][Aa][Dd]" in (ROOT / ".gitignore").read_text(encoding="utf-8"),
            "repository no longer ignores supplied WAD files")

    logo = APP / "main/assets/gamechangers_ai_logo.rgb565"
    require(logo.stat().st_size == LOGO_BYTES and sha256(logo) == LOGO_SHA256,
            "Game Changers AI logo differs")
    source = (APP / "main/console_os_main.c").read_text(encoding="utf-8")
    for token in (
        "present_boot_screen", "play_boot_chime",
        "CONSOLE_ACTION_COLOR_MODE_CHANGED", "cartridge_unlock_achievement",
        "_binary_bytebud_p4g_start",
        "platform_game_catalog_add_embedded_fallback",
        'game->embedded ? "embedded-default" : "removable-storage"',
        "static console_shell_t s_shell",
        "static platform_game_catalog_t s_catalog_staging",
        "P4_CONSOLE_OS MAIN_STACK stage=storage-ready",
        "P4CART_SCAN_BEGIN", "P4CART_READY",
    ):
        require(token in source, f"firmware source is missing {token}")
    require("console_shell_t shell;" not in source,
            "large shell state must not live on the main task stack")

    manifests = [
        read_json(path) for path in sorted((ROOT / "games").glob("*/game.json"))
        if read_json(path).get("enabled") is True
    ]
    reports = [
        verify_game(bundle / "GAMES" / manifest["package_file"], manifest)
        for manifest in manifests
    ]
    expected = read_json(APP / "app-metadata.json")["native_game_api"]["seed_packages"]
    require([item["file"] for item in reports] == expected,
            "built cartridge list differs from app metadata")
    legacy = read_json(APP / "app-metadata.json")["legacy_p4cart"]
    require(legacy.get("format") == "p4-cart-source-v1" and
            legacy.get("game_manager_visible") is True and
            legacy.get("runtime_implemented") is False and
            legacy.get("seed_cart") == str(P4CART_SEED),
            "legacy P4 Cart metadata differs")
    default_game = bundle / "GAMES/BYTEBUD.P4G"
    require(default_game.read_bytes() in app.read_bytes(),
            "BYTEBUD.P4G is not embedded as the firmware fallback")
    metadata = read_json(APP / "app-metadata.json")["native_game_api"]
    require(metadata.get("embedded_fallback_package") == "BYTEBUD.P4G" and
            metadata.get("embedded_fallback_replaceable_from_storage") is True,
            "embedded default-game policy differs")
    update = verify_update(bundle / "UPDATE/P4UPDATE.P4U", app)
    p4cart = verify_p4cart(bundle / P4CART_SEED)

    print(json.dumps({
        "result": "waveshare-console-os-build-verified",
        "build": str(build),
        "application": {"bytes": app.stat().st_size, "sha256": sha256(app)},
        "logo_sha256": LOGO_SHA256,
        "embedded_default": next(
            item for item in reports if item["file"] == "BYTEBUD.P4G"),
        "games": reports,
        "legacy_p4cart": p4cart,
        "update": update,
        "storage_policy": "read-only-at-runtime; install bundle with powered-off card reader",
        "hardware_tested": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
