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

from p4_multiplayer_manifest import expected_multiplayer_extension


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP = ROOT / "apps/console_os"
DEFAULT_BUILD = APP / "build-waveshare-landscape"
WAD_BYTES = 4_196_020
WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
LOGO_BYTES = 25_088
LOGO_SHA256 = "48ee7b2a15a744547884ec6ea7f462277ab60e5dde4d0805bf39db9c0b2bd892"
P4CART_SEED = pathlib.Path("P4/GAMES/BOUNCE-LAB.P4CART")
EXT_PORT_UPSTREAM_BYTES = 49_731
EXT_PORT_UPSTREAM_SHA256 = "0760b3c8ef14813db621b66c19d27caea5391793c592ca480b6ce18121797736"
EXT_PORT_SERIALIZED_BYTES = 53_413
EXT_PORT_SERIALIZED_SHA256 = "6373859f47cd6cb88877186ebd915d8d31f442cfd13f27da814e524ec43355ee"
HCD_UPSTREAM_BYTES = 116_968
HCD_UPSTREAM_SHA256 = "de0471a749547c7d295af0fe2e3e5b61d1eedf46d88c5b57cf20cec202d6c749"
HCD_FSLS_BYTES = 120_226
HCD_FSLS_SHA256 = "c71577cbdcc808828216940671be511a074f51fcd88e4f24ef0948d8aebabb8a"
HUB_UPSTREAM_SHA256 = "2d7c79c8508f63be6b0243178eafae2351f4e4643a87d9cdc73e980b95985afe"


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


def verify_managed_component_integrity(
    directory_name: str, expected_component_hash: str
) -> None:
    """Reject local edits or extra source files in one locked component."""
    component = APP / "managed_components" / directory_name
    require(component.is_dir(), f"missing managed component {directory_name}")
    component_hash = component / ".component_hash"
    require(component_hash.is_file() and
            component_hash.read_text(encoding="utf-8").strip() ==
            expected_component_hash,
            f"{directory_name} component hash differs from the lock")

    checksums = read_json(component / "CHECKSUMS.json")
    require(checksums.get("algorithm") == "sha256" and
            isinstance(checksums.get("files"), list),
            f"{directory_name} checksum manifest is invalid")
    listed: set[str] = set()
    for entry in checksums["files"]:
        require(isinstance(entry, dict) and
                isinstance(entry.get("path"), str) and
                isinstance(entry.get("size"), int) and
                isinstance(entry.get("hash"), str),
                f"{directory_name} checksum entry is invalid")
        relative = pathlib.PurePosixPath(entry["path"])
        require(not relative.is_absolute() and ".." not in relative.parts,
                f"{directory_name} checksum path is unsafe")
        listed.add(relative.as_posix())
        path = component.joinpath(*relative.parts)
        require(path.is_file() and path.stat().st_size == entry["size"] and
                sha256(path) == entry["hash"],
                f"{directory_name}/{relative} differs from the locked package")

    for source_root in ("src", "include", "private_include"):
        root = component / source_root
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.is_file():
                relative = path.relative_to(component).as_posix()
                require(relative in listed,
                        f"{directory_name} has unmanifested source {relative}")


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
    profile_flag, profile_bytes = expected_multiplayer_extension(
        manifest,
        set(manifest["required_capabilities"]) |
        set(manifest["optional_capabilities"]),
    )
    require(required & 1 and not (required & optional) and
            flags == (1 | profile_flag),
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
    require(package[240:256] == profile_bytes,
            f"{path.name} multiplayer profile differs")
    return {"file": path.name, "bytes": len(package), "sha256": sha256(path)}


def verify_game_resource(path: pathlib.Path, manifest: dict) -> dict[str, object]:
    package = path.read_bytes()
    require(128 < len(package) <= 8 * 1024 * 1024,
            f"{path.name} size is invalid")
    require(package[:8] == b"P4RES01\0", f"{path.name} magic differs")
    header, total, offset, payload, version, flags = struct.unpack_from(
        "<6I", package, 8
    )
    require(header == offset == 128 and total == len(package) and
            payload == len(package) - offset and version == 1 and flags == 0,
            f"{path.name} layout differs")
    require(package[32:64] == hashlib.sha256(package[offset:]).digest(),
            f"{path.name} payload digest differs")
    require(c_string(package[64:112], f"{path.name} game ID") == manifest["id"],
            f"{path.name} game ID differs")
    require(not any(package[112:128]), f"{path.name} reserved bytes differ")
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
    arguments = sys.argv[1:]
    firmware_only = "--firmware-only" in arguments
    arguments = [item for item in arguments if item != "--firmware-only"]
    require(len(arguments) <= 1,
            "usage: verify-console-os-waveshare.py [--firmware-only] [build]")
    build = pathlib.Path(arguments[0]).resolve() if arguments else DEFAULT_BUILD
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
        ("espressif/esp_tinyusb", "2.0.1"),
        ("espressif/tinyusb", "0.21.0~1"),
        ("espressif/usb", "1.5.0"),
        ("espressif/usb_host_hid", "1.2.0"),
        ("idf", "5.5.3"),
    ):
        require(re.search(
            rf"(?ms)^  {re.escape(component)}:.*?^    version: "
            rf"{re.escape(version)}$", lock) is not None,
            f"dependency lock is missing {component} {version}")
    usb_hash_match = re.search(
        r"(?ms)^  espressif/usb:.*?^    component_hash: ([0-9a-f]{64})$",
        lock,
    )
    require(usb_hash_match is not None,
            "dependency lock is missing the espressif/usb component hash")
    verify_managed_component_integrity(
        "espressif__usb", usb_hash_match.group(1)
    )
    tinyusb_hash_match = re.search(
        r"(?ms)^  espressif/tinyusb:.*?^    component_hash: ([0-9a-f]{64})$",
        lock,
    )
    require(tinyusb_hash_match is not None,
            "dependency lock is missing the espressif/tinyusb component hash")
    verify_managed_component_integrity(
        "espressif__tinyusb", tinyusb_hash_match.group(1)
    )

    profile = read_json(
        ROOT / "hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json"
    )
    storage = profile["peripheral_authorizations"]["storage"]
    require(storage.get("authorized") is True and
            storage.get("formatting_authorized") is False and
            storage.get("writes_authorized") is True and
            "exclusive H2 USB-device MSC" in storage.get("scope", ""),
            "microSD contract must retain exact-unit exclusive H2 MSC writes")
    game_save = storage.get("game_save_store", {})
    require(game_save.get("authorized") is True and
            game_save.get("formatting_authorized") is False and
            game_save.get("arbitrary_paths_authorized") is False and
            game_save.get("hardware_acceptance") is False and
            game_save.get("device_identity_sha256") == [
                "c9004de451366bc54158d9d1f3504892c068153610a3f31093785827f1de380d",
                "cb175826408c181592f300640029fd45de66e3e38df9bd1f7d8a3c0b9f055e79",
            ] and
            game_save.get("filesystem_scope", "").startswith("/SAVES/"),
            "exact-unit journaled game-save authorization differs")
    save_seal = game_save.get("internal_nvs_scope", {})
    require(save_seal.get("authorized") is True and
            save_seal.get("device_identity_sha256") ==
            game_save.get("device_identity_sha256") and
            save_seal.get("partition") == "nvs" and
            save_seal.get("namespace") == "p4_save_seal" and
            set(save_seal.get("keys", {})) ==
            {"master_v1", "legacy_v1", "h<14hex>"} and
            "32-byte" in save_seal.get("keys", {}).get("master_v1", "") and
            "1176-byte" in
            save_seal.get("keys", {}).get("legacy_v1", "") and
            "at most 32" in
            save_seal.get("keys", {}).get("legacy_v1", "") and
            "88-byte" in
            save_seal.get("keys", {}).get("h<14hex>", "") and
            "object SHA-256" in
            save_seal.get("keys", {}).get("h<14hex>", "") and
            save_seal.get("legacy_marker_domain", "").startswith(
                "P4SAVE2-LEGACY-CLOSED") and
            save_seal.get("namespace_erase_authorized") is False and
            save_seal.get("partition_erase_or_format_authorized") is False and
            save_seal.get("efuse_write_authorized") is False and
            save_seal.get(
                "secure_boot_or_flash_encryption_change_authorized") is False,
            "exact-unit bounded save-seal NVS authorization differs")
    seal_source = (ROOT / "components/platform_save_seal/src/"
                   "platform_save_seal.c").read_text(encoding="utf-8")
    for token in (
        'SAVE_SEAL_NAMESPACE[] = "p4_save_seal"',
        'SAVE_SEAL_KEY[] = "master_v1"',
        'LEGACY_REGISTRY_KEY[] = "legacy_v1"',
        '"P4SAVE2-LEGACY-CLOSED"',
        "LEGACY_MARKER_MAX_ENTRIES = 32",
        "LEGACY_REGISTRY_FORMAT_VERSION = 2",
        "ANCHOR_FORMAT_VERSION = 1",
        "LEGACY_ENTRY_ANCHOR_EXPECTED",
        "LEGACY_ENTRY_BASELINE_REQUIRED",
        "platform_save_seal_legacy_is_closed",
        "platform_save_seal_close_legacy",
        "platform_save_seal_object_is_allowed",
        "platform_save_seal_object_sequence",
        "platform_save_seal_advance_object",
        "esp_fill_random", "nvs_get_blob", "nvs_set_blob", "nvs_commit",
    ):
        require(token in seal_source,
                f"save-seal implementation is missing {token!r}")
    for forbidden in (
        "nvs_flash_erase", "nvs_erase_all", "nvs_erase_key", "esp_efuse",
    ):
        require(forbidden not in seal_source,
                f"save-seal implementation exceeds authorization: {forbidden}")
    save_store_source = (ROOT / "components/p4_game_save/src/"
                         "store.c").read_text(encoding="utf-8")
    for token in (
        "legacy_policy.query", "legacy_policy.close",
        "legacy_policy.object_query", "legacy_policy.object_advance",
        "legacy_policy.object_sequence",
        "!object.info.authenticated", "close_legacy_window",
    ):
        require(token in save_store_source,
                f"save store downgrade gate is missing {token!r}")
    require(profile["usb_vbus_assessment"].get("device_mode_authorized") is True,
            "H2 sink/device authorization is missing")
    require(profile["usb_vbus_assessment"]["controller_host_power_ready"] is False,
            "USB host power gate changed")

    sdkconfig = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    ble_multiplayer_image = all(setting in sdkconfig for setting in (
        "#define CONFIG_BT_ENABLED 1",
        "#define CONFIG_BT_CONTROLLER_DISABLED 1",
        "#define CONFIG_BT_NIMBLE_ENABLED 1",
        "#define CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE 1",
        "#define CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI 1",
    ))
    if ble_multiplayer_image:
        ble_authorization = profile["peripheral_authorizations"][
            "multiplayer_ble"
        ]
        require(ble_authorization.get("authorized") is True and
                ble_authorization.get("wifi_data_plane_authorized") is False and
                len(ble_authorization.get("device_identity_sha256", [])) == 2,
                "BLE multiplayer exact-unit authorization is incomplete")
        for setting in (
            "#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 2",
            "#define CONFIG_BT_NIMBLE_MAX_BONDS 4",
            "#define CONFIG_BT_NIMBLE_MAX_CCCDS 16",
            "#define CONFIG_BT_NIMBLE_NVS_PERSIST 1",
        ):
            require(setting in sdkconfig,
                    f"BLE controller image is missing {setting}")
    usb_host_image = (
        "#define CONFIG_P4_WAVESHARE_H2_USB_HOST_MODE 1" in sdkconfig
    )
    force_full_speed_host = (
        "#define CONFIG_P4_WAVESHARE_H2_FORCE_FULL_SPEED_HOST 1" in
        sdkconfig
    )
    tinyusb_hs_host_image = usb_host_image and not force_full_speed_host
    retry_match = re.search(
        r"^#define CONFIG_P4_USB_HOST_EXT_PORT_ENUM_RETRY_ATTEMPTS (\d+)$",
        sdkconfig,
        re.MULTILINE,
    )
    retry_attempts = int(retry_match.group(1)) if retry_match else 0
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
    require("#define CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL 32768" in
            sdkconfig,
            "Waveshare image must retain a bootable 32 KiB internal/DMA reserve")
    if usb_host_image:
        require("#define CONFIG_TINYUSB_MSC_ENABLED 1" in sdkconfig,
                "controller-first image must link the USB Drive MSC app")
        require("#define CONFIG_P4_WAVESHARE_H2_RUNTIME_ROLE_SWITCH 1" in
                sdkconfig,
                "controller-first image is missing the guarded H2 role switch")
        require("#define CONFIG_USB_HOST_HUBS_SUPPORTED 1" in sdkconfig,
                "USB-host image is missing external hub support")
        require(tinyusb_hs_host_image,
                "controller-first image must use the pinned TinyUSB HS/TT host path")
        require("#define CONFIG_TINYUSB_DEBUG_LEVEL 0" in sdkconfig,
                "controller-first image must disable verbose TinyUSB tracing")
    else:
        require("#define CONFIG_TINYUSB_MSC_ENABLED 1" in sdkconfig,
                "H2 device image is missing TinyUSB MSC")

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
        "p4_multiplayer_registry",
        "platform_board", "platform_display", "platform_touch",
        "platform_console_settings",
        "platform_game_catalog", "platform_game_loader",
        "platform_game_storage", "platform_os_update",
        "espressif__esp_tinyusb", "espressif__tinyusb",
    }
    require(required_components <= components, "required component missing")
    require({"gamepad_core", "platform_gamepad", "platform_usb_host",
             "platform_gamepad_usb"}
            <= components,
            "role-selectable Waveshare input components are missing")
    signal_scan_image = {
        "p4_signal_scan", "platform_signal_scan"
    } <= components
    require(ble_multiplayer_image != signal_scan_image,
            "Waveshare image must select exactly one C6 radio consumer")
    require({"espressif__esp_hosted", "espressif__esp_wifi_remote"}
            <= components,
            "Waveshare image is missing the pinned C6 transport")
    if ble_multiplayer_image:
        require({"bt", "platform_ble_host", "platform_gamepad_ble",
                 "platform_multiplayer_ble", "platform_radio_hosted"}
                <= components,
                "Waveshare image is missing shared BLE gamepad/multiplayer")
    else:
        require({"p4_signal_scan", "platform_signal_scan",
                 "platform_radio_hosted"} <= components,
                "Waveshare image is missing the passive signal-scan path")
    if usb_host_image:
        require("p4_tinyusb_dual" in components,
                "controller-first image is missing the dual-role TinyUSB boundary")
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
    app_data = app.read_bytes()
    radio_markers = (
        (b"BLE multiplayer ready role=central mtu=%u route=%llu",
         b"BLE multiplayer ready role=peripheral mtu=%u")
        if ble_multiplayer_image else
        (b"P4_CONSOLE_OS SIGNAL_SCAN_START stage=post-ready mode=passive-only",
         b"C6 passive scanner ready")
    )
    for marker in radio_markers:
        require(marker in app_data,
                f"selected radio marker is missing: {marker!r}")
    if ble_multiplayer_image:
        for marker in (
            b"BLE_HOST_READY clients=%u bonding=persistent",
            b"BLE_GAMEPAD_READY name=%s reports=%u mtu=%u",
            b"BLE_GAMEPAD_SETUP_FAIL stage=%s rc=%d",
            b"P4_CONSOLE_OS BLE_LINK_BUDGET controller_links=1 "
            b"multiplayer_peer_links=1 host=shared "
            b"order=pair-controller-before-lobby",
            b"P4_CONSOLE_OS CONTROLLER_MAPPING_COMPLETE "
            b"result=%s persistent=%u",
            b"PAIR + MAP GAMEPADS",
            b"BLE PAD MODE",
        ):
            require(marker in app_data,
                    f"BLE controller marker is missing: {marker!r}")
        console_source = (APP / "main/console_os_main.c").read_text(
            encoding="utf-8"
        )
        require(re.search(
            r"(?s)\.id = CONSOLE_APP_CONTROLLERS,.*?"
            r"\.folder_path = \"\",.*?"
            r"\.page = CONSOLE_PAGE_CONTROLLERS",
            console_source,
        ) is not None,
                "Controllers app is not exposed on the main launcher")
        for token in (
            "platform_gamepad_get_raw_snapshot",
            "platform_console_settings_set_controller_mapping",
            "CONSOLE_ACTION_CONTROLLER_BLE_DISABLE",
            "CONSOLE_ACTION_CONTROLLER_MAPPING_START",
        ):
            require(token in console_source,
                    f"controller setup source is missing {token}")
        gamepad_ble_source = (
            ROOT / "components/platform_gamepad_ble/src/platform_gamepad_ble.c"
        ).read_text(encoding="utf-8")
        require(
            "static EXT_RAM_BSS_ATTR ble_hid_runtime_t s_ble;" in
            gamepad_ble_source,
            "BLE HID runtime must remain outside the internal DMA reserve",
        )
    cmake = (APP / "CMakeLists.txt").read_text(encoding="utf-8")
    version_match = re.search(r'set\(PROJECT_VER "([0-9]+\.[0-9]+\.[0-9]+)"\)',
                              cmake)
    require(version_match is not None,
            "firmware semantic version is missing")
    version_label = f"OS {version_match.group(1)}".encode("ascii")
    require(version_label in app_data,
            "visible shell version label is missing from the image")
    for forbidden in (
        b"TT pipe hub=",
        b"through HS hub transaction translator",
        b"Invalid transaction translator route",
    ):
        require(forbidden not in app_data,
                "application contains experimental USB TT scheduler code")
    if tinyusb_hs_host_image:
        for marker in (
            b"USB_HOST_READY controller=p4-hs stack=tinyusb root_speed=high",
            b"USB_HOST_ROOT_PORT_ENABLED stack=tinyusb root_speed=high hub_tt=enabled",
            b"GAMEPAD_USB_READY stack=tinyusb tier=generic-hid hub_tt=enabled",
            b"firmware_vbus_source=0",
            b"msc_storage=on-demand",
            b"MSC_STORAGE_READY allocation=on-demand",
        ):
            require(marker in app_data,
                    f"TinyUSB controller-first marker is missing: {marker!r}")
        ninja = (build / "build.ninja").read_text(encoding="utf-8")
        for relative in (
            "src/host/usbh.c",
            "src/host/hub.c",
            "src/class/hid/hid_host.c",
            "src/portable/synopsys/dwc2/hcd_dwc2.c",
        ):
            compile_lines = [
                line for line in ninja.splitlines()
                if line.startswith("build ") and
                f"espressif__tinyusb.dir/{relative}.obj" in line and
                ": C_COMPILER" in line
            ]
            require(len(compile_lines) == 1 and
                    f"managed_components/espressif__tinyusb/{relative}" in
                    compile_lines[0],
                    f"TinyUSB host source is not uniquely locked: {relative}")
        for source_name in (
            "platform_usb_host_tinyusb.c",
            "platform_gamepad_usb_tinyusb.c",
        ):
            require(ninja.count(source_name) >= 2,
                    f"controller-first image did not compile {source_name}")

    if usb_host_image and not tinyusb_hs_host_image:
        for marker in (
            b"msc_storage=on-demand",
            b"MSC_STORAGE_READY allocation=on-demand",
            b"BOOT_FRAME_RETRY",
            b"backlight_preserved=1",
        ):
            require(marker in app_data,
                    f"controller-first recovery marker is missing: {marker!r}")
        ninja = (build / "build.ninja").read_text(encoding="utf-8")
        ext_port_compile_lines = [
            line for line in ninja.splitlines()
            if line.startswith("build ") and "ext_port.c.obj" in line and
            ": C_COMPILER" in line
        ]
        hcd_compile_lines = [
            line for line in ninja.splitlines()
            if line.startswith("build ") and "hcd_dwc.c.obj" in line and
            ": C_COMPILER" in line
        ]
        hub_compile_lines = [
            line for line in ninja.splitlines()
            if line.startswith("build ") and "src/hub.c.obj" in line and
            ": C_COMPILER" in line
        ]
        require(len(hub_compile_lines) == 1 and
                "managed_components/espressif__usb/src/hub.c" in
                hub_compile_lines[0],
                "controller-first image changed the locked hub scheduler source")
        hub_source = APP / "managed_components/espressif__usb/src/hub.c"
        require(sha256(hub_source) == HUB_UPSTREAM_SHA256,
                "locked hub scheduler hash differs")
        upstream_ext_port = (
            APP / "managed_components/espressif__usb/src/ext_port.c"
        )
        require(upstream_ext_port.stat().st_size == EXT_PORT_UPSTREAM_BYTES and
                sha256(upstream_ext_port) == EXT_PORT_UPSTREAM_SHA256,
                "locked external-port source differs")
        upstream_hcd = (
            APP / "managed_components/espressif__usb/src/hcd_dwc.c"
        )
        require(upstream_hcd.stat().st_size == HCD_UPSTREAM_BYTES and
                sha256(upstream_hcd) == HCD_UPSTREAM_SHA256,
                "locked HCD source differs")
        generated_hcd = (
            build / "generated/espressif-usb-1.5.0/hcd_dwc.c"
        )
        require(generated_hcd.is_file() and
                generated_hcd.stat().st_size == HCD_FSLS_BYTES and
                sha256(generated_hcd) == HCD_FSLS_SHA256,
                "guarded HCD FS/LS overlay differs")
        check_hcd_overlay = subprocess.run(
            [sys.executable,
             str(ROOT / "scripts/generate-espressif-usb-hcd-fsls-overlay.py"),
             "--input", str(upstream_hcd),
             "--output", str(generated_hcd), "--check-output"],
            check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(check_hcd_overlay.returncode == 0,
                "HCD FS/LS overlay does not reproduce from locked input")
        require(len(hcd_compile_lines) == 1 and
                "generated/espressif-usb-1.5.0/hcd_dwc.c" in
                hcd_compile_lines[0] and
                "managed_components/espressif__usb/src/hcd_dwc.c" not in
                hcd_compile_lines[0],
                "controller-first image did not compile exactly one guarded hcd_dwc.c")
        for marker in (
            b"P4_HS_FSLS_ROOT_READY reset=reapplied",
            b"frame_interval=%u raw_speed=%u effective_speed=%u",
        ):
            require(marker in app_data,
                    f"guarded HCD marker is missing: {marker!r}")
        if retry_attempts == 0:
            require(len(ext_port_compile_lines) == 1 and
                    "managed_components/espressif__usb/src/ext_port.c" in
                    ext_port_compile_lines[0],
                    "stable image did not compile exactly one locked ext_port.c")
            for forbidden in (
                b"P4_EXT_PORT_ENUM_RETRY",
                b"P4_EXT_PORT_ENUM_RETRY_EXHAUSTED",
                b"P4_EXT_PORT_ENUM_RECOVERED",
            ):
                require(forbidden not in app_data,
                        f"retry marker remains in stable image: {forbidden!r}")
        else:
            generated = (
                build / "generated/espressif-usb-1.5.0/ext_port.c"
            )
            require(generated.is_file() and
                    generated.stat().st_size == EXT_PORT_SERIALIZED_BYTES and
                    sha256(generated) == EXT_PORT_SERIALIZED_SHA256,
                    "serialized external-port overlay differs")
            check_overlay = subprocess.run(
                [sys.executable,
                 str(ROOT / "scripts/generate-espressif-usb-ext-port-overlay.py"),
                 "--input", str(upstream_ext_port),
                 "--output", str(generated), "--check-output"],
                check=False, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            require(check_overlay.returncode == 0,
                    "serialized overlay does not reproduce from locked input")
            require(len(ext_port_compile_lines) == 1 and
                    "generated/espressif-usb-1.5.0/ext_port.c" in
                    ext_port_compile_lines[0] and
                    "managed_components/espressif__usb/src/ext_port.c" not in
                    ext_port_compile_lines[0],
                    "guarded image did not compile exactly one generated ext_port.c")
            for marker in (
                b"P4_EXT_PORT_ENUM_RETRY_SERIALIZED",
                b"P4_EXT_PORT_ENUM_RETRY_SUPPRESSED",
                b"P4_EXT_PORT_ENUM_RETRY_EXHAUSTED",
                b"P4_EXT_PORT_ENUM_RECOVERED",
                b"P4_CONSOLE_OS USB_ENUM_GUARD state=",
            ):
                require(marker in app_data,
                        f"guarded retry marker is missing: {marker!r}")
            require(b"P4_EXT_PORT_ENUM_RETRY Port" not in app_data,
                    "revoked 0.4.20 immediate retry marker returned")
    if firmware_only:
        print(json.dumps({
            "result": "waveshare-console-os-firmware-verified",
            "build": str(build),
            "radio": "ble-multiplayer" if ble_multiplayer_image
                     else "passive-signal-scan",
            "application": {
                "bytes": app.stat().st_size,
                "sha256": sha256(app),
            },
            "version": version_match.group(1),
            "hardware_tested": False,
        }, sort_keys=True))
        return

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
    save_mode = source.split(
        "static p4_game_save_storage_mode_t cartridge_save_storage_mode(void)",
        1,
    )[1].split("static void cartridge_save_worker", 1)[0]
    require("P4_GAME_SAVE_STORAGE_WRITABLE" in save_mode and
            "CONFIG_P4_BOARD_WAVESHARE" not in save_mode and
            "P4_GAME_SAVE_STORAGE_READ_ONLY" not in save_mode and
            "P4_GAME_SAVE_STORAGE_HOST_OWNED" in save_mode,
            "Waveshare save capability must be writable only under app FAT ownership")
    for token in (
        "platform_save_seal_load_key", "SAVE_SEAL_UNAVAILABLE",
        "platform_save_seal_legacy_is_closed",
        "platform_save_seal_close_legacy", "SAVE_LEGACY_MARKER",
        "platform_save_seal_object_is_allowed",
        "platform_save_seal_object_sequence",
        "platform_save_seal_advance_object", "SAVE_FRESHNESS",
        "protected_game_lineage_check", "PROTECTED_GAME_REJECTED",
        "p4_game_save_protection_clear", "SAVE_SEAL_READY",
    ):
        require(token in source,
                f"Console OS save seal integration is missing {token!r}")
    for token in (
        "present_boot_screen", "play_boot_chime",
        "CONSOLE_ACTION_COLOR_MODE_CHANGED", "cartridge_unlock_achievement",
        "platform_game_catalog_scan",
        '"microsd-games-directory" : "microsd-root-compat"',
        "static console_shell_t s_shell",
        "static platform_game_catalog_t s_catalog_staging",
        "P4_CONSOLE_OS BOOT_SCREEN status=visible",
        "P4_CONSOLE_OS STORAGE_INIT_BEGIN mode=background",
        "P4_CONSOLE_OS LOADING_SCREEN status=%s",
        "P4_CONSOLE_OS BOOT_POST status=%s",
        "P4_CONSOLE_OS BOOT_HDD status=%s",
        "P4_CONSOLE_OS BOOT_DIAL status=%s digits=614-276-3639",
        "P4_CONSOLE_OS BOOT_MODEM status=%s profile=v22bis-2400",
        "P4_CONSOLE_OS BOOT_MODEM_PHASE index=%u name=%s",
        "DIALING 614-276-3639",
        "V.22BIS 2400 BAUD",
        '"calling-tone", 1300U, 0U, 180U, 90U',
        '"answer-tone", 2100U, 0U, 260U, 30U',
        '"00-11-training", 1200U, 2400U, 140U, 24U',
        '"2400-bps-scrambled-ones", 1200U, 2400U, 260U, 24U',
        '"seek-1", 105U, 2700U, 34U, 72U',
        'elapsed_ms=%" PRIi64',
        "READING SD CARD",
        "P4_CONSOLE_OS MAIN_STACK stage=core-ready",
        "game_storage_init_worker",
        "wait_for_game_storage_with_boot_animation",
        "P4CART_SCAN_BEGIN", "P4CART_READY",
        "CONSOLE_APP_USB_DRIVE", "CONSOLE_PAGE_USB_DRIVE",
        "CONSOLE_APP_CONTROLLERS", "CONSOLE_PAGE_CONTROLLERS",
        "CONSOLE_ACTION_CONTROLLER_PAIR",
        "platform_gamepad_ble_connect_or_pair",
        "P4_CONSOLE_OS BLE_CONTROLLER_ACTION name=%s result=%s",
        "stop_usb_input_for_role_switch",
        "P4_CONSOLE_OS USB_ROLE_STOPPED",
        "p4_usb_ext_port_enum_retry_allowed",
        "p4_usb_hs_fsls_reapply_allowed",
        "begin_usb_enum_probe_or_suppress",
        "confirm_usb_enum_probe_after_stable_runtime",
        "P4_CONSOLE_OS USB_ENUM_GUARD state=%s",
        "present_interactive",
        "P4_CONSOLE_OS FRAME_ACK_MISSED action=continue",
        "result = present_interactive(shell);",
        "P4_CONSOLE_OS CARTRIDGE_FRAME_ACK_MISSED",
        "++context->display_ack_misses;",
        "display_ack_misses=%lu return=launcher",
    ):
        require(token in source, f"firmware source is missing {token}")
    for forbidden in (
        "recover_usb_input_after_cold_boot",
        "P4_CONSOLE_OS USB_ENUM_RECOVERY_BEGIN",
        "P4_CONSOLE_OS USB_ENUM_RECOVERY_SUCCESS",
        "P4_CONSOLE_OS USB_ENUM_RECOVERY_EXHAUSTED",
    ):
        require(forbidden not in source,
                f"obsolete app-level USB recovery remains in firmware source: {forbidden}")
    if tinyusb_hs_host_image:
        usb_host_source = (ROOT / "components/platform_usb_host/src/"
                           "platform_usb_host_tinyusb.c").read_text(
                               encoding="utf-8")
        gamepad_source = (ROOT / "components/platform_gamepad_usb/src/"
                          "platform_gamepad_usb_tinyusb.c").read_text(
                              encoding="utf-8")
        require("USB_PHY_SPEED_UNDEFINED" in usb_host_source and
                "TUSB_SPEED_AUTO" in usb_host_source and
                "PLATFORM_TUH_TASK_CORE = 1" in usb_host_source and
                "hub_tt=enabled" in usb_host_source and
                "firmware_vbus_source=0" in usb_host_source,
                "TinyUSB HS host policy differs")
        require("GAMEPAD_HID_MAX_DESCRIPTOR_BYTES" in gamepad_source and
                "GAMEPAD_HID_MAX_REPORT_BYTES" in gamepad_source and
                "disconnect_slot" in gamepad_source and
                "input=neutral" in gamepad_source,
                "TinyUSB HID bounds or disconnect neutralization differs")
    else:
        usb_host_source = (ROOT / "components/platform_usb_host/src/"
                           "platform_usb_host.c").read_text(encoding="utf-8")
        require("usb_dwc_ll_hcfg_set_fsls_supp_only(&USB_DWC_HS)" in
                usb_host_source and
                "USB_HOST_SPEED_POLICY root=full-speed-only" in usb_host_source and
                "reason=no-hs-hub-tt-scheduler" in usb_host_source,
                "Waveshare host speed policy no longer avoids the TT path")
    app_main = source.split("void app_main(void)", 1)[1]
    require(app_main.index("present_boot_screen(0U, \"STARTING...\")") <
            app_main.index("start_game_storage_initialization()") <
            app_main.index("play_boot_chime()"),
            "storage or boot dialing starts before the official boot frame")
    require(app_main.index("play_boot_chime()") <
            app_main.index("begin_usb_enum_probe_or_suppress()") <
            app_main.index("create_usb_input_or_continue()") <
            app_main.index("create_touch_or_continue()"),
            "USB recovery guard, controller host, or touch startup is misordered")
    require(app_main.index("create_touch_or_continue()") <
            app_main.index("wait_for_game_storage_with_boot_animation()") <
            app_main.index("present(shell)"),
            "interactive launcher does not wait for terminal storage state")
    settings_source = (ROOT / "components/platform_console_settings/src/"
                       "platform_console_settings.c").read_text(encoding="utf-8")
    settings_header = (ROOT / "components/platform_console_settings/include/"
                       "platform/console_settings.h").read_text(encoding="utf-8")
    require("PLATFORM_CONSOLE_BOOT_VOLUME_DEFAULT = 3" in settings_header and
            "PLATFORM_CONSOLE_GAME_VOLUME_DEFAULT = 3" in settings_header and
            'SETTINGS_NAMESPACE = "p4_console"' in settings_source and
            'VOLUME_POLICY_KEY = "volume_policy"' in settings_source and
            "VOLUME_POLICY_VERSION = 2" in settings_source and
            "migrate_volume_policy" in settings_source and
            'USB_ENUM_PROBE_KEY = "usb_enum_probe"' in settings_source and
            "nvs_commit(handle)" in settings_source and
            "platform_console_settings_begin_usb_enum_probe" in
                settings_source and
            "platform_console_settings_confirm_usb_enum_probe" in
                settings_source and
            "platform_console_settings_init(&s_console_settings)" in source and
            "CONSOLE_BOOT_POST_BEEP_MS = 220" in source and
            "CONSOLE_BOOT_POST_GAP_MS = 180" in source and
            "CONSOLE_BOOT_DTMF_TONE_MS = 160" in source and
            "CONSOLE_BOOT_DTMF_GAP_MS = 90" in source and
            "CONSOLE_BOOT_DTMF_DASH_MS = 220" in source and
            "digit_tone_ms=%u digit_gap_ms=%u group_gap_ms=%u" in source,
            "Console OS persistent audio panel or paced DTMF profile differs")
    require("s_console_settings.game_volume_step,\n        title,\n        multiplayer" in source,
            "touch Doom handoff does not receive the OS master volume")
    gamepad_core_source = (ROOT / "components/gamepad_core/src/"
                           "hid_gamepad.c").read_text(encoding="utf-8")
    for token in (
        "This controller reports its SNES face buttons in Y, B, A, X order",
        "GAMEPAD_BUTTON_WEST,",
        "GAMEPAD_BUTTON_EAST,",
        "GAMEPAD_BUTTON_SOUTH,",
        "GAMEPAD_BUTTON_NORTH,",
        "next.fields[8].map_target = GAMEPAD_HID_MAP_IGNORE;",
        "next.fields[9].map_target = GAMEPAD_HID_MAP_IGNORE;",
    ):
        require(token in gamepad_core_source,
                f"exact 0079:0011 SNES mapping is missing {token}")
    doom_source = (ROOT / "apps/doom_embedded_touch_audio/main/"
                   "doom_embedded_touch_audio_main.c").read_text(
                       encoding="utf-8")
    doom_gamepad_source = (
        ROOT / "apps/doom_embedded_gamepad_audio/main/"
        "doom_embedded_gamepad_audio_main.c"
    ).read_text(encoding="utf-8")
    console_main_cmake = (APP / "main/CMakeLists.txt").read_text(
        encoding="utf-8")
    for token in (
        "P4_DOOM_SHARED_GAMEPAD",
        "platform_gamepad_get_snapshot",
        "doom_gamepad_input_update",
        "service_gamepad();",
        "controller=shared-platform-gamepad",
    ):
        require(token in doom_source,
                f"Waveshare Doom shared controller path is missing {token}")
    for handoff_name, handoff_source in (
        ("touch-audio", doom_source),
        ("gamepad-audio", doom_gamepad_source),
    ):
        for token in (
            "bool allow_pwad",
            'memcmp(header, "PWAD", 4U)',
            "verify_readonly_vfs(wad_path, wad_size, chex)",
        ):
            require(
                token in handoff_source,
                f"Chex PWAD policy is missing from {handoff_name}: {token}",
            )
    require("doom_gamepad_input" in console_main_cmake,
            "Waveshare Console OS does not link Doom's normalized input adapter")
    require("CONSOLE_GAME_CATALOG_STACK_BYTES = 32 * 1024" in source and
            "GAME_CATALOG_SCAN_BEGIN mode=background" in source and
            "worker_low_water_bytes=%u" in source,
            "native game catalog worker stack regression is not guarded")
    catalog_worker = source[
        source.index("static void game_catalog_scan_worker(void *unused)"):
        source.index("static esp_err_t start_game_catalog_scan(void)")
    ]
    catalog_finish = source[
        source.index("static bool finish_game_catalog_scan(void)"):
        source.index("static void wait_for_game_catalog_with_boot_animation(void)")
    ]
    require("platform_os_update_inspect" not in catalog_worker and
            "platform_os_update_inspect(&s_update_staging)" in catalog_finish and
            "ota_inspect=foreground-internal" in source,
            "PSRAM catalog worker performs cache-disabling OTA inspection")
    require("CONSOLE_P4CART_SCAN_STACK_BYTES = 24 * 1024" in source and
            "worker_low_water_bytes=%u runtime=p4-lua-5.4-v1" in source,
            "legacy cart scan stack regression is not guarded")
    for token in (
        "_binary_bytebud_p4g_start",
        "platform_game_catalog_add_embedded_fallback",
        "embedded-default",
    ):
        require(token not in source,
                f"firmware source still supports embedded games: {token}")
    loader_source = (ROOT / "components/platform_game_loader/src/"
                     "platform_game_loader.c").read_text(encoding="utf-8")
    for token in (
        "ESP_ELFSYM_EXPORT(memcmp)",
        "cartridge_exit = elf.entry(1, arguments)",
        'failure_stage = "cartridge-entry"',
    ):
        require(token in loader_source,
                f"cartridge runtime diagnostics are missing {token}")
    require(
        "console_shell_show_home(shell);\n"
        "    const esp_err_t home_result = present_interactive(shell);" in source,
        "cartridge return must tolerate a recoverable Waveshare frame ack miss")
    require("RENAME_TO bytebud_p4g" not in cmake and
            "P4_DEFAULT_GAME_PACKAGE" not in cmake,
            "firmware build still embeds a default cartridge")
    ansi_source = (ROOT / "components/p4_ansi/src/ansi.c").read_text(
        encoding="utf-8")
    for token in (
        "const bool embolden",
        "cell->character >= UINT8_C(0x20)",
        "cell->character <= UINT8_C(0x7e)",
        "set = set ||",
    ):
        require(token in ansi_source,
                f"larger bold BBS font policy is missing {token}")
    if signal_scan_image:
        signal_scan_source = (ROOT / "components/platform_signal_scan/src/"
                              "platform_signal_scan.c").read_text(encoding="utf-8")
        for token in (
            "ssid_has_visible_name",
            ".show_hidden = false",
            "hidden_filtered=%u",
        ):
            require(token in signal_scan_source,
                    f"named-only signal scan policy is missing {token}")
        require(".show_hidden = true" not in signal_scan_source,
                "Waveshare scanner must not request hidden networks")
    require("console_shell_t shell;" not in source,
            "large shell state must not live on the main task stack")
    require(
        ".usb_drive_active =\n"
        "            s_game_storage_status.state == "
        "PLATFORM_GAME_STORAGE_USB_HOST ||\n"
        "            s_game_storage_status.state ==\n"
        "                PLATFORM_GAME_STORAGE_USB_FORMAT_REQUIRED," in source and
        ".usb_drive_active = s_game_storage_status.usb_driver_running" not in source,
        "USB Drive UI must follow storage ownership, not driver attachment state")
    storage_source = (ROOT / "components/platform_game_storage/src/"
                      "platform_game_storage.c").read_text(encoding="utf-8")
    write_policy = (ROOT / "components/platform_game_storage/src/"
                    "msc_write_policy.c").read_text(encoding="utf-8")
    for token in (
        "P4_GAME_STORAGE_USB_EXPORT",
        "tinyusb_msc_new_storage_sdmmc",
        "sdmmc_write_sectors",
        "sdmmc_read_sectors",
        "install_usb_driver",
        "P4_GAME_STORAGE RECOVERY_EXPORT",
        "GAME_STORAGE_SD_TRANSFER_BYTES = 4096",
        "platform_game_storage_set_usb_mode",
        "reason=host-not-ejected",
        ".auto_mount_off = 1U",
        "P4_GAME_STORAGE_RUNTIME_H2_SWITCH",
        "P4_GAME_STORAGE_EAGER_USB_STORAGE",
        "P4_GAME_STORAGE USB_DEVICE_DEFERRED",
        "MSC_STORAGE_READY allocation=on-demand",
        "runtime_create_usb_storage",
        "runtime_release_usb_storage",
        "sdmmc_host_deinit_slot(GAME_STORAGE_SD_SLOT)",
        "uninstall_usb_driver_if_running",
    ):
        require(token in storage_source,
                f"Waveshare MSC storage source is missing {token}")
    require("create_lazy_msc_storage" not in storage_source and
            "delete_lazy_msc_storage" not in storage_source and
            "result = sdmmc_host_deinit();" not in storage_source,
            "runtime USB switching must use the explicit on-demand helpers "
            "and never globally deinitialize the C6 SDIO host")
    board_defaults = (ROOT / "hardware/boards/"
                      "waveshare-esp32-p4-wifi6-touch-lcd-4.3/"
                      "sdkconfig.defaults").read_text(encoding="utf-8")
    require("CONFIG_TINYUSB_MSC_BUFSIZE=4096" in board_defaults,
            "Waveshare MSC buffer must fit the fragmented internal DMA heap")
    require("uint64_t byte_address" in storage_source and
            "uint64_t *out_address" in write_policy and
            "address > SIZE_MAX" not in write_policy,
            "Waveshare MSC addressing is not safe above 4 GB")
    require("10000U, 5000U, 1000U, 400U" in storage_source,
            "Waveshare SD clock fallback ladder changed")
    display_source = (ROOT / "components/platform_display/src/"
                      "platform_display.c").read_text(encoding="utf-8")
    require("if (s_pattern_active)" in display_source and
            "backlight_preserved=1" in display_source and
            "BOOT_FRAME_RETRY" in source,
            "Waveshare display timeout recovery contract changed")
    storage_cmake = (ROOT / "components/platform_game_storage/"
                     "CMakeLists.txt").read_text(encoding="utf-8")
    require("src/msc_write10_wrapper.c" in storage_cmake and
            "--wrap=tud_msc_write10_cb" in storage_cmake and
            "--wrap=tud_msc_start_stop_cb" in storage_cmake,
            "Waveshare MSC writes/ejects are not routed through the platform")
    installer = (ROOT / "scripts/install-olimex-sd-card.py").read_text(
        encoding="utf-8")
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    require("require_waveshare_h2_fat32" in installer and
            '"FilesystemType": "msdos"' in installer and
            '"Content": "DOS_FAT_32"' in installer and
            "--require-waveshare-h2-fat32" in makefile,
            "Waveshare installer no longer enforces H2 FAT32 media")
    package_builder = (ROOT / "scripts/build-game-package.py").read_text(
        encoding="utf-8")
    require("validate_elf_imports(output, compiler)" in package_builder and
            "ALLOWED_UNDEFINED_SYMBOLS" in package_builder,
            "P4G builder no longer rejects unsupported runtime imports")

    manifests = [
        read_json(path) for path in sorted((ROOT / "games").glob("*/game.json"))
        if read_json(path).get("enabled") is True
    ]
    reports = [
        verify_game(bundle / "GAMES" / manifest["package_file"], manifest)
        for manifest in manifests
    ]
    resource_reports = [
        verify_game_resource(
            bundle / "GAMES" / manifest["resource_file"], manifest
        )
        for manifest in manifests
        if isinstance(manifest.get("resource_file"), str)
    ]
    metadata = read_json(APP / "app-metadata.json")["native_game_api"]
    expected = metadata["seed_packages"]
    require([item["file"] for item in reports] == expected,
            "built cartridge list differs from app metadata")
    require([item["file"] for item in resource_reports] ==
            metadata.get("seed_resources", []),
            "built resource sidecar list differs from app metadata")
    legacy = read_json(APP / "app-metadata.json")["legacy_p4cart"]
    require(legacy.get("format") == "p4-cart-source-v1" and
            legacy.get("game_manager_visible") is True and
            legacy.get("runtime_implemented") is False and
            legacy.get("seed_cart") == str(P4CART_SEED),
            "legacy P4 Cart metadata differs")
    require(metadata.get("games_embedded_in_ota") is False and
            metadata.get("execution_source") ==
                "microSD /GAMES/*.P4G with optional same-name .P4R resources; root compatibility",
            "SD-only game execution policy differs")
    for report in reports:
        package = bundle / "GAMES" / str(report["file"])
        require(package.read_bytes() not in app_data,
                f"{package.name} leaked into the OTA application")
    for report in resource_reports:
        resource = bundle / "GAMES" / str(report["file"])
        require(resource.read_bytes() not in app_data,
                f"{resource.name} leaked into the OTA application")

    elf = build / str(project["app_elf"])
    compiler = pathlib.Path(str(project["c_compiler"]))
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols_result = subprocess.run(
        [str(nm), "-g", str(elf)], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(symbols_result.returncode == 0, "cannot inspect app ELF symbols")
    for symbol in (
        "_binary_bytebud_p4g_start",
        "_binary_bytebud_p4g_end",
        "platform_game_catalog_add_embedded_fallback",
    ):
        require(f" {symbol}\n" not in symbols_result.stdout,
                f"OTA ELF still exports embedded-game symbol {symbol}")
    update = verify_update(bundle / "UPDATE/P4UPDATE.P4U", app)
    p4cart = verify_p4cart(bundle / P4CART_SEED)

    print(json.dumps({
        "result": "waveshare-console-os-build-verified",
        "build": str(build),
        "h2_role": "controller-first-runtime-switch" if usb_host_image else "usb-device-msc",
        "application": {"bytes": app.stat().st_size, "sha256": sha256(app)},
        "logo_sha256": LOGO_SHA256,
        "game_execution_source": "microSD-only",
        "games_embedded_in_ota": False,
        "games": reports,
        "game_resources": resource_reports,
        "legacy_p4cart": p4cart,
        "update": update,
        "storage_policy": "app-owned/controller-host by default; USB Drive app exclusively switches H2 to MSC; return requires host eject or disconnect; firmware never formats",
        "hardware_tested": False,
    }, sort_keys=True))


if __name__ == "__main__":
    main()
