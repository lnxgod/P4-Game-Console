#!/usr/bin/env python3
"""Fail-closed source and build verifier for the P4 Console OS MVP."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import subprocess
import sys

import yaml


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"
EXPECTED_WAD_BYTES = 4_196_020
EXPECTED_WAD_SHA256 = (
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
)
EXPECTED_APP_PARTITION_BYTES = 11 * 1024 * 1024


def fail(message: str) -> None:
    raise SystemExit(f"console_os verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def read_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain a JSON object")
    return value


def read_yaml(path: pathlib.Path) -> dict:
    try:
        value = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, yaml.YAMLError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain a YAML mapping")
    return value


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_order(text: str, tokens: list[str], label: str) -> None:
    positions: list[int] = []
    for token in tokens:
        require(token in text, f"{label} is missing {token!r}")
        positions.append(text.index(token))
    require(positions == sorted(positions), f"unsafe order in {label}")


def main() -> None:
    build = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else APP / "build"
    require(build.is_dir(), f"missing build directory: {build}")

    toolchain = read_json(ROOT / "toolchain.lock.json")
    require(toolchain["esp_idf"]["version"] == "5.5.3", "unexpected IDF lock")
    require(toolchain["target"] == {
        "chip": "esp32p4",
        "silicon_family": "revision_1_x",
        "min_revision_full": 100,
        "max_revision_full": 199,
    }, "unexpected target lock")
    console_lock = read_yaml(APP / "dependencies.lock")
    require(console_lock.get("version") == "2.0.0" and
            console_lock.get("target") == "esp32p4",
            "unexpected Console OS component lock header")
    locked = console_lock.get("dependencies", {})
    expected_versions = {
        "idf": "5.5.3",
        "espressif/cmake_utilities": "0.5.3",
        "espressif/esp_codec_dev": "1.3.4",
        "espressif/esp_lcd_ek79007": "1.0.2",
        "espressif/esp_lcd_st7701": "1.1.2",
        "espressif/esp_lcd_touch": "1.1.2",
        "espressif/esp_lcd_touch_gt911": "1.1.3",
    }
    require({name: value.get("version") for name, value in locked.items()}
            == expected_versions,
            "Console OS component lock versions changed")

    metadata = read_json(APP / "app-metadata.json")
    require(metadata.get("app") == "console_os", "wrong app metadata")
    for key in (
        "runtime_supported",
        "top_level_runtime_authorized",
        "flash_authorized",
        "flash_app_authorized",
        "flash_project_authorized",
        "hardware_access_allowed",
        "game_data_committed",
        "game_data_redistribution_authorized",
    ):
        require(metadata.get(key) is False, f"metadata must deny {key}")
    require(metadata.get("game_data_embedded") is True,
            "metadata must disclose embedded game data")
    require(metadata["doom_handoff"]["audio_initialized_by_shell"] is False,
            "shell must not initialize Doom audio")
    require(metadata["doom_handoff"]["return_to_home_supported"] is False,
            "MVP must not claim a reentrant Doom return")
    native_api = metadata.get("native_game_api", {})
    require(native_api.get("api_version") == 1,
            "native game API version must be 1")
    require(native_api.get("format") == "p4-native-static-v1",
            "native game format must remain explicit")
    require(native_api.get("return_to_home_supported") is True,
            "native games must support returning to the launcher")
    require(native_api.get("uf2_supported") is False,
            "metadata must not mislabel ESP-IDF output as UF2")
    require(native_api.get("registry") ==
            "all enabled games/*/game.json manifests discovered at configure time",
            "metadata must describe manifest-driven game discovery")
    shell_metadata = metadata.get("shell", {})
    require(shell_metadata.get("maximum_folder_depth") == 2,
            "launcher folder depth must remain bounded")
    require(shell_metadata.get("dynamic_executable_loading") is False,
            "folder UI must not imply dynamic executable loading")
    volume = shell_metadata.get("master_volume", {})
    require(volume.get("default_step") == 8 and
            volume.get("minimum_step") == 1 and
            volume.get("maximum_step") == 10,
            "OS master volume must default to 8/10 within a 1..10 bound")
    timing = shell_metadata.get("native_game_timing", {})
    require(timing.get("update_and_audio_service_ms") == 16 and
            timing.get("render_every_service_ticks") == 2 and
            timing.get("render_target_fps") == 31,
            "native game timing metadata must match the ~30 FPS scheduler")
    require(shell_metadata.get("logical_surface") == "rgb565-768x480",
            "Console OS logical surface must remain 768x480")
    content = metadata.get("sd_content", {})
    require(content.get("mount_mode") == "no-format" and
            content.get("catalog_is_read_only") is True and
            content.get("cart_runtime_available") is False,
            "SD catalog policy changed unexpectedly")
    usb_copy = content.get("usb_copy", {})
    require(content.get("host_copy_tool") == "scripts/p4-content.py" and
            usb_copy.get("runtime_available") is False and
            usb_copy.get("host_supplied_paths") is False and
            usb_copy.get("formatting_allowed") is False and
            usb_copy.get("current_target") == "none",
            "disabled USB content-copy boundary changed")
    require("quake_handoff" not in metadata,
            "retired Quake handoff must not be advertised")

    registry_check = subprocess.run(
        ["python3", str(ROOT / "scripts/generate-game-registry.py"),
         "--games-root", str(ROOT / "games"), "--check"],
        cwd=ROOT, check=False, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True,
    )
    require(registry_check.returncode == 0,
            f"invalid game registry: {registry_check.stderr.strip()}")

    wad = ROOT / "local-data/doom/doom1.wad"
    require(wad.is_file(), "missing ignored exact Doom shareware WAD")
    require(wad.stat().st_size == EXPECTED_WAD_BYTES, "wrong WAD byte count")
    require(sha256(wad) == EXPECTED_WAD_SHA256, "wrong WAD SHA-256")
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", str(wad)], cwd=ROOT, check=False
    )
    require(ignored.returncode == 0, "local WAD is not ignored by Git")

    shell_main = (APP / "main/console_os_main.c").read_text(encoding="utf-8")
    require(re.search(r"\bplatform_audio_", shell_main) is None,
            "launcher source must not call the audio backend")
    require("p4_game_platform_audio_open" in shell_main,
            "reviewed native-game audio adapter is missing")
    require("p4-native-static-v1" in shell_main,
            "native game format log is missing")
    require("p4_generated_game_by_launcher_id" in shell_main,
            "generated native game dispatch is missing")
    require("p4_generated_game_folders" in shell_main,
            "generated native game folder metadata is missing")
    require("CONSOLE_ACTION_VOLUME_CHANGED" in shell_main,
            "OS volume action handling is missing")
    require("CONSOLE_RENDER_DIVISOR = 2" in shell_main,
            "native ~30 FPS render pacing is missing")
    require("CONSOLE_PAGE_EXTERNAL" in shell_main, "Doom launcher entry missing")
    require("CONSOLE_PAGE_COLORS" in shell_main, "Colors app missing")
    require("CONSOLE_PAGE_TOUCH" in shell_main, "Touch app missing")
    require("CONSOLE_PAGE_SYSTEM" in shell_main, "System app missing")
    require("CONSOLE_PAGE_AUDIO" in shell_main, "Audio status app missing")
    require("CONSOLE_PAGE_LIBRARY" in shell_main, "Library app missing")
    require("CONSOLE_PAGE_MULTIPLAYER" in shell_main,
            "Multiplayer app missing")
    require("CONSOLE_PAGE_FILES" in shell_main,
            "File Manager app missing")
    require("CONSOLE_PAGE_SAVES" in shell_main,
            "Save Manager app missing")
    require("CONSOLE_PAGE_TERMINAL" in shell_main,
            "Terminal app missing")
    require("update_desktop_catalog" in shell_main,
            "desktop file catalog integration missing")
    require("p4_content_catalog_scan" in shell_main,
            "SD content catalog integration missing")
    for token in (
        "CONSOLE_APP_QUAKE",
        "p4_quake_run",
        "p4_content_transfer_",
        "quake_shareware",
    ):
        require(token not in shell_main,
                f"retired Quake integration remains in launcher source: {token}")
    handoff = shell_main[
        shell_main.index(
            "static void launch_doom_exclusive(uint8_t master_volume_step)"):
        shell_main.index("void app_main(void)")
    ]
    require_order(handoff, [
        "platform_display_set_brightness(0U)",
        "platform_storage_deinit()",
        "destroy_touch_for_handoff()",
        "destroy_bus_for_handoff()",
        "platform_display_deinit()",
        "heap_caps_free(s_pixels)",
        "console_os_launch_doom(master_volume_step)",
    ], "Doom handoff")

    doom_main = (
        ROOT / "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c"
    ).read_text(encoding="utf-8")
    require("#ifdef P4_CONSOLE_OS_EMBEDDED" in doom_main,
            "Doom console entrypoint guard missing")
    require("void console_os_launch_doom(uint8_t master_volume_step)" in doom_main,
            "Doom console entrypoint missing")
    require("void app_main(void)" in doom_main,
            "standalone Doom entrypoint was not preserved")

    storage_source = (
        ROOT / "components/platform_storage/src/platform_storage.c"
    ).read_text(encoding="utf-8")
    board_source = (
        ROOT / "components/platform_board/include/platform/board.h"
    ).read_text(encoding="utf-8")
    for token in (
        "sd_pwr_ctrl_new_on_chip_ldo",
        "host.pwr_ctrl_handle = power_control",
        "sd_pwr_ctrl_del_on_chip_ldo",
    ):
        require(token in storage_source,
                f"Waveshare SD power contract is missing {token!r}")
    require("PLATFORM_BOARD_SDMMC_POWER_LDO_CHANNEL 4" in board_source,
            "Waveshare SD LDO channel contract changed")

    project = read_json(build / "project_description.json")
    require(project.get("project_name") == "p4_console_os", "wrong project name")
    require(project.get("target") == "esp32p4", "wrong build target")
    require(project.get("min_rev") == "100" and project.get("max_rev") == "199",
            "wrong silicon revision bounds")
    components = set(project.get("build_components", []))
    required_components = {
        "console_shell", "doom_audio", "doom_engine_audio", "doom_touch_input",
        "doom_video", "platform_audio", "platform_audio_es8311",
        "platform_audio_factory",
        "platform_display", "platform_i2c_shared", "platform_readonly_blob",
        "platform_storage", "platform_touch", "p4_content_catalog",
        "p4_desktop",
        "p4_game_api", "p4_game_platform", "p4_multiplayer",
        "maze_chase", "space_invaders", "solitaire",
    }
    forbidden_components = {
        "doom_gamepad_input", "p4_quake", "p4_usb_content_transfer",
        "platform_gamepad_usb", "platform_usb_host", "usb_host_hid",
        "espressif__usb_host_hid", "espressif__usb",
    }
    require(required_components <= components, "required component missing")
    require(not (forbidden_components & components), "forbidden component linked")
    require("lwip" not in components,
            "unqualified Wi-Fi/lwIP transport must not be linked yet")

    sdkconfig = (build / "sdkconfig").read_text(encoding="utf-8")
    for setting in (
        "CONFIG_PLATFORM_BOARD_TARGET_WAVESHARE_4_3=y",
        "CONFIG_PLATFORM_STORAGE_WAVESHARE_4_3_AUTHORIZED=y",
    ):
        require(setting in sdkconfig, f"missing required config {setting}")

    flasher = read_json(build / "flasher_args.json")
    require(flasher.get("flash_settings") == {
        "flash_mode": "dio", "flash_size": "32MB", "flash_freq": "80m"
    }, "unexpected flash geometry")
    require(flasher.get("flash_files", {}).get("0x10000") == "p4_console_os.bin",
            "unexpected application offset")

    binary = build / str(project.get("app_bin"))
    elf = build / str(project.get("app_elf"))
    require(binary.is_file() and elf.is_file(), "missing build artifacts")
    require(binary.stat().st_size < EXPECTED_APP_PARTITION_BYTES,
            "application does not fit the 11 MiB partition")

    compiler = pathlib.Path(str(project.get("c_compiler")))
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    require(nm.is_file(), f"missing pinned nm tool: {nm}")
    symbols_result = subprocess.run(
        [str(nm), "-g", str(elf)], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
    )
    require(symbols_result.returncode == 0, "cannot inspect ELF symbols")
    symbols = symbols_result.stdout
    for symbol in (
        "app_main", "console_os_launch_doom", "console_shell_init",
        "console_shell_handle_touch", "doomgeneric_Tick",
        "doom_music_player_mix", "platform_audio_es8311_start",
        "p4_game_instance_start", "p4_game_input_mapper_update",
        "p4_game_platform_audio_open", "p4_maze_chase_game",
        "p4_space_invaders_game", "p4_generated_game_by_launcher_id",
        "p4_generated_game_folders", "p4_content_catalog_scan",
        "p4_mp_session_init", "p4_mp_session_peer_count",
        "platform_display_submit_rgb565", "platform_storage_init",
        "esp_vfs_fat_sdmmc_mount", "sd_pwr_ctrl_new_on_chip_ldo",
        "sd_pwr_ctrl_del_on_chip_ldo",
        "_binary_doom_shareware_wad_start",
    ):
        require(f" {symbol}\n" in symbols, f"missing ELF symbol {symbol}")
    for symbol in (
        "usb_host_install", "hid_host_install", "platform_usb_host_start",
        "platform_gamepad_usb_start", "UDP_Init", "UDP_Read", "UDP_Write",
        "_binary_quake_shareware_pak_start", "p4_quake_run",
        "p4_content_transfer_init",
    ):
        require(f" {symbol}\n" not in symbols, f"forbidden ELF symbol {symbol}")

    report = {
        "result": "console-os-build-verified-not-hardware-tested",
        "target": "esp32p4-revision-1.x",
        "idf": "5.5.3",
        "binary": {
            "path": str(binary.relative_to(ROOT)),
            "bytes": binary.stat().st_size,
            "sha256": sha256(binary),
        },
        "elf": {
            "path": str(elf.relative_to(ROOT)),
            "bytes": elf.stat().st_size,
            "sha256": sha256(elf),
        },
        "hardware_execution_authorized": False,
        "native_game_format": "p4-native-static-v1",
        "native_game_api": 1,
    }
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
