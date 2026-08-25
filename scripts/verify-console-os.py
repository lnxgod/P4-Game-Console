#!/usr/bin/env python3
"""Fail-closed source and build verifier for Console OS game management."""

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
WAD_BYTES = 4_196_020
WAD_SHA256 = (
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
)
OTA0_BYTES = 0x370000
OTA1_BYTES = 0x380000
GAME_DATA_OFFSET = 0x710000
GAME_DATA_BYTES = 0x8F0000
P4G_HEADER_BYTES = 256
P4R_HEADER_BYTES = 128
P4U_HEADER_BYTES = 256
P4CART_SEED = pathlib.Path("P4/GAMES/BOUNCE-LAB.P4CART")


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


def c_string(field: bytes, label: str) -> str:
    require(b"\0" in field, f"{label} is not terminated")
    value, padding = field.split(b"\0", 1)
    require(value and not any(padding), f"{label} is empty or has dirty padding")
    try:
        return value.decode("ascii")
    except UnicodeDecodeError as error:
        fail(f"{label} is not ASCII: {error}")


def verify_game_package(path: pathlib.Path, manifest: dict) -> dict:
    data = path.read_bytes()
    require(P4G_HEADER_BYTES < len(data) <= 512 * 1024,
            f"{path.name} size is outside the package bound")
    require(data[:8] == b"P4GAME1\0", f"{path.name} has wrong magic")
    fields = struct.unpack_from("<9IHH", data, 8)
    (
        header_bytes, package_bytes, payload_offset, payload_bytes,
        format_version, api_version, launcher_id, required, optional,
        accent, flags,
    ) = fields
    require(header_bytes == payload_offset == P4G_HEADER_BYTES,
            f"{path.name} has wrong header layout")
    require(package_bytes == len(data) and
            payload_bytes == len(data) - payload_offset,
            f"{path.name} has wrong package lengths")
    require(format_version == api_version == 1,
            f"{path.name} has wrong format/API version")
    require(launcher_id == manifest["launcher_id"],
            f"{path.name} launcher ID differs from its manifest")
    require((required & 1) != 0 and required & optional == 0,
            f"{path.name} capability masks are invalid")
    require(accent == int(manifest["accent_rgb565"], 16),
            f"{path.name} accent differs from its manifest")
    profile_flag, profile_bytes = expected_multiplayer_extension(
        manifest,
        set(manifest["required_capabilities"]) |
        set(manifest["optional_capabilities"]),
    )
    require(flags == (1 | profile_flag),
            f"{path.name} disclosure/profile flags differ")
    payload = data[payload_offset:]
    require(hashlib.sha256(payload).digest() == data[48:80],
            f"{path.name} payload digest differs")
    require(payload[:4] == b"\x7fELF" and payload[4:7] == b"\x01\x01\x01",
            f"{path.name} payload is not ELF32 little-endian")
    require(c_string(data[80:128], "game id") == manifest["id"],
            f"{path.name} game ID differs")
    require(c_string(data[128:144], "game title") == manifest["title"],
            f"{path.name} title differs")
    require(c_string(data[176:208], "game folder") == manifest["folder"],
            f"{path.name} folder differs")
    require(c_string(data[208:224], "game version") == manifest["version"],
            f"{path.name} version differs")
    require(data[240:256] == profile_bytes,
            f"{path.name} multiplayer profile differs from its manifest")
    return {
        "file": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
    }


def verify_game_resource(path: pathlib.Path, manifest: dict) -> dict:
    data = path.read_bytes()
    require(P4R_HEADER_BYTES < len(data) <= 8 * 1024 * 1024,
            f"{path.name} size is outside the resource bound")
    require(data[:8] == b"P4RES01\0", f"{path.name} has wrong magic")
    header, package, offset, payload, version, flags = struct.unpack_from(
        "<6I", data, 8
    )
    require(header == offset == P4R_HEADER_BYTES and package == len(data) and
            payload == len(data) - offset,
            f"{path.name} has wrong resource layout")
    require(version == 1 and flags == 0,
            f"{path.name} format version or flags differ")
    require(hashlib.sha256(data[offset:]).digest() == data[32:64],
            f"{path.name} payload digest differs")
    require(c_string(data[64:112], "resource game id") == manifest["id"],
            f"{path.name} game ID differs")
    require(not any(data[112:128]), f"{path.name} reserved bytes are dirty")
    return {
        "file": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "payload_sha256": hashlib.sha256(data[offset:]).hexdigest(),
    }


def verify_os_update(path: pathlib.Path, app_binary: pathlib.Path) -> dict:
    data = path.read_bytes()
    image = app_binary.read_bytes()
    require(data[:8] == b"P4OSUP1\0", "P4UPDATE.P4U has wrong magic")
    fields = struct.unpack_from("<6I", data, 8)
    header, package, offset, payload, version, flags = fields
    require(header == offset == P4U_HEADER_BYTES and package == len(data),
            "P4UPDATE.P4U layout differs")
    require(payload == len(image) and data[offset:] == image,
            "P4UPDATE.P4U payload differs from the app binary")
    require(version == 1 and flags == 0,
            "P4UPDATE.P4U format version or flags differ")
    require(hashlib.sha256(image).digest() == data[32:64],
            "P4UPDATE.P4U payload digest differs")
    package_version = c_string(data[64:96], "OS update version")
    build = c_string(data[96:160], "OS update build")
    require(c_string(data[160:176], "OS update target") == "esp32p4",
            "P4UPDATE.P4U has wrong target")
    require(not any(data[176:256]), "P4UPDATE.P4U reserved bytes are dirty")
    return {
        "file": path.name,
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "image_sha256": hashlib.sha256(image).hexdigest(),
        "version": package_version,
        "build": build,
    }


def verify_p4cart(path: pathlib.Path) -> dict:
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
                f"seed P4 Cart is not the deterministic Bounce Lab package: "
                f"{packed.stderr.strip()}")
    return {"file": str(P4CART_SEED), "bytes": path.stat().st_size,
            "sha256": sha256(path)}


def main() -> None:
    build = (
        pathlib.Path(sys.argv[1]).resolve()
        if len(sys.argv) > 1 else APP / "build"
    )
    require(build.is_dir(), f"missing build directory: {build}")

    toolchain = read_json(ROOT / "toolchain.lock.json")
    require(toolchain["esp_idf"]["version"] == "5.5.3",
            "unexpected ESP-IDF lock")
    require(toolchain["target"] == {
        "chip": "esp32p4",
        "silicon_family": "revision_1_x",
        "min_revision_full": 100,
        "max_revision_full": 199,
    }, "unexpected target lock")
    dependency_lock = (APP / "dependencies.lock").read_text(encoding="utf-8")
    for component, version in (
        ("espressif/esp_tinyusb", "2.0.1"),
        ("espressif/elf_loader", "1.3.1"),
        ("idf", "5.5.3"),
    ):
        require(re.search(
            rf"(?ms)^  {re.escape(component)}:.*?^    version: "
            rf"{re.escape(version)}$", dependency_lock) is not None,
            f"dependency lock is missing {component} {version}")

    sdkconfig = (build / "config/sdkconfig.h").read_text(encoding="utf-8")
    for setting in (
        "CONFIG_FATFS_SECTOR_512",
        "CONFIG_WL_SECTOR_SIZE_512",
        "CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE",
        "CONFIG_ELF_LOADER",
        "CONFIG_ELF_LOADER_LOAD_PSRAM",
        "CONFIG_ELF_LOADER_LIBC_SYMBOLS",
    ):
        require(f"#define {setting} 1" in sdkconfig,
                f"build is missing {setting}")
    require("CONFIG_ELF_LOADER_ESPIDF_SYMBOLS" not in sdkconfig,
            "cartridges must not resolve arbitrary ESP-IDF symbols")
    require("CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT" not in sdkconfig,
            "shared-object-to-shared-object linking must remain disabled")

    metadata = read_json(APP / "app-metadata.json")
    require(metadata.get("stage") ==
                "sd-only-game-catalog-p4cart-ota-launcher-candidate",
            "unexpected app stage")
    for key in (
        "runtime_supported", "top_level_runtime_authorized",
        "flash_authorized", "flash_app_authorized",
        "flash_project_authorized", "hardware_access_allowed",
        "game_data_embedded", "game_data_committed",
        "game_data_redistribution_authorized",
    ):
        require(metadata.get(key) is False, f"metadata must deny {key}")
    native = metadata.get("native_game_api", {})
    require(native.get("format") == "p4-native-elf-v1" and
            native.get("api_version") == 1 and
            native.get("native_code_is_security_sandboxed") is False and
            native.get("games_embedded_in_ota") is False,
            "native cartridge metadata differs")
    expected_seed_packages = native.get("seed_packages")
    require(isinstance(expected_seed_packages, list) and
            len(expected_seed_packages) >= 2 and
            all(isinstance(name, str) and name.endswith(".P4G")
                for name in expected_seed_packages),
            "seed package metadata differs")
    legacy = metadata.get("legacy_p4cart", {})
    require(legacy.get("format") == "p4-cart-source-v1" and
            legacy.get("game_manager_visible") is True and
            legacy.get("runtime_implemented") is False and
            legacy.get("seed_cart") == str(P4CART_SEED),
            "legacy P4 Cart metadata differs")
    shell = metadata.get("shell", {})
    require(shell.get("dynamic_executable_loading") is True and
            "game-manager" in shell.get("built_in_apps", []),
            "Game Manager shell metadata differs")
    storage = metadata.get("game_storage", {})
    require(storage.get("partition_offset") == "0x710000" and
            storage.get("partition_bytes") == GAME_DATA_BYTES and
            storage.get("runtime_format_allowed") is False and
            storage.get("hardware_tested") is False,
            "persistent storage metadata differs")
    update = metadata.get("os_update", {})
    require(update.get("target") == "inactive OTA slot only" and
            update.get("rollback_until_first_ready_frame") is True and
            update.get("preserves_game_data") is True,
            "OTA update metadata differs")

    manifest_check = subprocess.run(
        [sys.executable, str(ROOT / "scripts/generate-game-registry.py"),
         "--games-root", str(ROOT / "games"), "--check"],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(manifest_check.returncode == 0,
            f"invalid game manifests: {manifest_check.stderr.strip()}")

    wad = ROOT / "local-data/doom/doom1.wad"
    require(wad.is_file() and wad.stat().st_size == WAD_BYTES and
            sha256(wad) == WAD_SHA256, "local Doom shareware WAD differs")
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", str(wad)], cwd=ROOT, check=False)
    require(ignored.returncode == 0, "local Doom WAD is not ignored")

    project = read_json(build / "project_description.json")
    require(project.get("project_name") == "p4_console_os" and
            project.get("target") == "esp32p4" and
            project.get("min_rev") == "100" and
            project.get("max_rev") == "199",
            "project target or revision bounds differ")
    components = set(project.get("build_components", []))
    required_components = {
        "console_shell", "p4_game_api", "p4_game_package",
        "p4_content_catalog", "p4_multiplayer",
        "p4_multiplayer_registry", "p4_os_update_package",
        "platform_game_catalog",
        "platform_game_loader", "platform_game_storage",
        "platform_os_update", "platform_display", "platform_touch",
        "platform_readonly_blob", "fatfs", "wear_levelling",
    }
    require(required_components <= components, "required component missing")
    require(any(name.endswith("elf_loader") for name in components),
            "pinned ELF loader component missing")
    require("maze_chase" not in components and
            "space_invaders" not in components,
            "sample games were linked back into the OS image")
    forbidden_components = {
        "doom_gamepad_input", "platform_gamepad_usb", "platform_usb_host",
        "usb_host_hid", "espressif__usb_host_hid", "espressif__usb",
    }
    require(not (forbidden_components & components),
            "forbidden USB-host component linked")

    flasher = read_json(build / "flasher_args.json")
    require(flasher.get("flash_settings") == {
        "flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m",
    }, "unexpected flash geometry")
    expected_flash_files = {
        "0x2000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": "ota_data_initial.bin",
        "0x20000": "p4_console_os.bin",
        "0x710000": "game_data.bin",
    }
    require(flasher.get("flash_files") == expected_flash_files,
            "full-project flash map differs")

    partition_tool = (
        pathlib.Path(str(project["idf_path"])) /
        "components/partition_table/gen_esp32part.py"
    )
    partition_result = subprocess.run(
        [sys.executable, str(partition_tool),
         str(build / "partition_table/partition-table.bin")],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(partition_result.returncode == 0,
            f"partition binary cannot be decoded: {partition_result.stderr}")
    partition_text = partition_result.stdout
    for row in (
        "otadata,data,ota,0x10000,8K,",
        "ota_0,app,ota_0,0x20000,3520K,",
        "ota_1,app,ota_1,0x390000,3584K,",
        "game_data,data,fat,0x710000,9152K,",
    ):
        require(row in partition_text, f"partition table is missing {row}")
    require(0x390000 + OTA1_BYTES == GAME_DATA_OFFSET,
            "OTA slots no longer end at the persistent-data boundary")

    app_binary = build / str(project["app_bin"])
    app_elf = build / str(project["app_elf"])
    require(app_binary.is_file() and app_elf.is_file(),
            "missing app build artifacts")
    require(app_binary.stat().st_size <= OTA0_BYTES,
            "app does not fit the smaller OTA slot")
    os_update = verify_os_update(build / "P4UPDATE.P4U", app_binary)
    p4cart = verify_p4cart(build / "game-storage-seed" / P4CART_SEED)

    package_reports: list[dict] = []
    manifests: list[dict] = []
    for path in sorted((ROOT / "games").glob("*/game.json")):
        manifest = read_json(path)
        if manifest.get("enabled") is True:
            manifests.append(manifest)
            package_reports.append(verify_game_package(
                build / "game-storage-seed/GAMES" /
                manifest["package_file"],
                manifest,
            ))
    expected_seed_resources = sorted(
        manifest["resource_file"] for manifest in manifests
        if isinstance(manifest.get("resource_file"), str)
    )
    resource_reports = [
        verify_game_resource(
            build / "game-storage-seed/GAMES" / manifest["resource_file"],
            manifest,
        )
        for manifest in manifests
        if isinstance(manifest.get("resource_file"), str)
    ]
    require([item["file"] for item in resource_reports] ==
            expected_seed_resources,
            "built seed resource set differs")
    require([item["file"] for item in package_reports] ==
            expected_seed_packages,
            "built seed cartridge set differs")
    app_binary_data = app_binary.read_bytes()
    for manifest in manifests:
        package = build / "game-storage-seed/GAMES" / manifest["package_file"]
        require(package.read_bytes() not in app_binary_data,
                f"{package.name} leaked into the OTA application")
    parser_test = ROOT / "build-host/p4_game_package/tests/test_p4_game_package"
    if parser_test.is_file():
        parsed = subprocess.run(
            [str(parser_test),
             str(build / "game-storage-seed/GAMES/MAZE.P4G"),
             str(build / "game-storage-seed/GAMES/INVADERS.P4G")],
            cwd=ROOT, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(parsed.returncode == 0,
                f"native package parser rejected real cartridges: "
                f"{parsed.stderr.strip()}")

    game_image = build / "game_data.bin"
    require(game_image.is_file() and
            game_image.stat().st_size == GAME_DATA_BYTES,
            "generated game-data image has wrong size")
    game_image_bytes = game_image.read_bytes()
    require(int.from_bytes(game_image_bytes[4096 + 11:4096 + 13],
                           "little") == 512,
            "generated FAT uses the wrong logical sector size")
    normalized = subprocess.run(
        [sys.executable, str(ROOT / "scripts/normalize-game-storage-image.py"),
         str(game_image), "--check"], cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(normalized.returncode == 0,
            f"game-data image is not normalized: {normalized.stderr.strip()}")
    fat_parser = (
        pathlib.Path(str(project["idf_path"])) /
        "components/fatfs/fatfsparse.py"
    )
    cmake_cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    python_match = re.search(r"(?m)^PYTHON:[^=]+=(.+)$", cmake_cache)
    require(python_match is not None, "build does not record pinned Python")
    idf_python = python_match.group(1)
    with tempfile.TemporaryDirectory(prefix="p4-game-data-") as temporary:
        extracted = subprocess.run(
            [idf_python, str(fat_parser), str(game_image),
             "--wl-layer", "enabled"],
            cwd=temporary, check=False, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        require(extracted.returncode == 0,
                f"cannot parse game-data image: {extracted.stderr.strip()}")
        root = pathlib.Path(temporary)
        volume_roots = [path for path in root.rglob("P4 GAMES") if path.is_dir()]
        require(len(volume_roots) == 1, "generated FAT has wrong volume label")
        volume = volume_roots[0]
        require(sorted(item.name for item in volume.iterdir()) ==
                sorted(["DOOM1.WAD", "README.TXT", "GAMES", "UPDATE",
                        "P4"]),
                "generated FAT root contents differ")
        require((volume / "GAMES").is_dir() and
                sorted(item.name for item in (volume / "GAMES").iterdir()) ==
                sorted(expected_seed_packages + expected_seed_resources),
                "generated FAT GAMES contents differ")
        require((volume / P4CART_SEED).read_bytes() ==
                (build / "game-storage-seed" / P4CART_SEED).read_bytes(),
                "generated FAT contains the wrong P4 Cart seed")
        require((volume / "UPDATE").is_dir() and
                not any((volume / "UPDATE").iterdir()),
                "generated FAT UPDATE directory differs")
        require((volume / "DOOM1.WAD").stat().st_size == WAD_BYTES and
                sha256(volume / "DOOM1.WAD") == WAD_SHA256,
                "generated FAT contains wrong Doom WAD")
        require((volume / "README.TXT").read_bytes() ==
                (APP / "game-storage/README.TXT").read_bytes(),
                "generated FAT README differs")
        for manifest in manifests:
            name = manifest["package_file"]
            require((volume / "GAMES" / name).read_bytes() ==
                    (build / "game-storage-seed/GAMES" / name).read_bytes(),
                    f"generated FAT contains wrong {name}")
            resource_name = manifest.get("resource_file")
            if isinstance(resource_name, str):
                require((volume / "GAMES" / resource_name).read_bytes() ==
                        (build / "game-storage-seed/GAMES" /
                         resource_name).read_bytes(),
                        f"generated FAT contains wrong {resource_name}")

    compiler = pathlib.Path(str(project["c_compiler"]))
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols_result = subprocess.run(
        [str(nm), "-g", str(app_elf)], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(symbols_result.returncode == 0, "cannot inspect app ELF symbols")
    symbols = symbols_result.stdout
    for symbol in (
        "app_main", "console_os_launch_doom", "platform_game_catalog_scan",
        "p4_content_catalog_scan",
        "p4_mp_session_init",
        "platform_game_loader_run", "p4_game_package_parse",
        "p4_os_update_package_parse", "platform_os_update_install",
        "esp_elf_relocate", "esp_ota_set_boot_partition",
        "platform_game_storage_stream_update_file_exclusive",
        "__wrap_tud_msc_write10_cb",
        "tinyusb_driver_install", "tinyusb_msc_new_storage_spiflash",
    ):
        require(f" {symbol}\n" in symbols, f"missing ELF symbol {symbol}")
    multiplayer_archive = (
        build / "esp-idf/p4_multiplayer/libp4_multiplayer.a"
    )
    archive_symbols_result = subprocess.run(
        [str(nm), "-g", str(multiplayer_archive)], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(archive_symbols_result.returncode == 0,
            "cannot inspect multiplayer archive symbols")
    for symbol in (
        "p4_mp_packet_decode", "p4_mp_stream_consume",
        "p4_mp_session_init",
    ):
        require(f" {symbol}\n" in archive_symbols_result.stdout,
                f"multiplayer archive is missing {symbol}")
    for symbol in (
        "p4_maze_chase_game", "p4_space_invaders_game",
        "_binary_doom_shareware_wad_start", "_binary_bytebud_p4g_start",
        "platform_game_catalog_add_embedded_fallback", "usb_host_install",
        "hid_host_install", "platform_usb_host_start",
        "esp_vfs_fat_sdmmc_mount", "es8311_codec_new",
    ):
        require(f" {symbol}\n" not in symbols,
                f"forbidden ELF symbol {symbol}")

    objdump = compiler.with_name(
        compiler.name.removesuffix("gcc") + "objdump")
    disassembly_result = subprocess.run(
        [str(objdump), "-d", str(app_elf)], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    require(disassembly_result.returncode == 0,
            "cannot disassemble app ELF")
    require(re.search(
                r"\b(jal|j)\s+[0-9a-f]+\s+<__wrap_tud_msc_write10_cb>",
                disassembly_result.stdout) is not None,
            "TinyUSB WRITE(10) does not call the verified platform wrapper")

    shell_main = (APP / "main/console_os_main.c").read_text(encoding="utf-8")
    require("CONSOLE_PAGE_GAMES" in shell_main and
            "native_format=p4-native-elf-v1" in shell_main and
            "platform_game_loader_run" in shell_main and
            "P4CART_SCAN_BEGIN" in shell_main and
            "P4CART_READY" in shell_main,
            "Game Manager/cartridge route is absent from Console OS")
    for token in (
        "CONSOLE_APP_FILES", "CONSOLE_APP_GAMES",
        "CONSOLE_APP_ACHIEVEMENTS", "CONSOLE_APP_MULTIPLAYER",
        "CONSOLE_APP_SAVES", "CONSOLE_APP_TERMINAL",
        "p4_mp_session_init(&s_multiplayer_session)",
        "CONSOLE_BOOT_DTMF_TONE_MS = 160",
        "CONSOLE_BOOT_DTMF_GAP_MS = 90",
        "CONSOLE_BOOT_DTMF_DASH_MS = 220",
        "digit_tone_ms=%u digit_gap_ms=%u group_gap_ms=%u",
    ):
        require(token in shell_main,
                f"10 in Console OS feature parity is missing {token}")
    shell_source = (
        ROOT / "components/console_shell/src/console_shell.c"
    ).read_text(encoding="utf-8")
    for token in (
        "draw_multiplayer", "draw_saves", "draw_files",
        "P4_TERMINAL_COMMAND_SSH",
        "WIRED STREAM CORE READY", "HOST RELAY REQUIRED",
    ):
        require(token in shell_source,
                f"10 in system-app surface is missing {token}")
    app_main_source = shell_main[shell_main.index("void app_main") :]
    require_order(app_main_source, [
        "result = present_boot_screen(0U, \"STARTING...\");",
        "platform_display_set_brightness(CONSOLE_BACKLIGHT_PERCENT)",
        "start_game_storage_initialization()",
        "wait_for_game_storage_with_boot_animation()",
        "result = present(shell);",
        "platform_os_update_mark_running_valid(",
    ], "first-frame OTA confirmation")
    update_source = (
        ROOT / "components/platform_os_update/src/platform_os_update.c"
    ).read_text(encoding="utf-8")
    require_order(update_source, [
        "platform_game_storage_stream_update_file_exclusive(",
        "esp_ota_end(stream.handle)",
        "esp_ota_set_boot_partition(target)",
    ], "OTA commit")
    storage_source = (
        ROOT / "components/platform_game_storage/src/platform_game_storage.c"
    ).read_text(encoding="utf-8")
    write_policy_header = (
        ROOT / "components/platform_game_storage/src/msc_write_policy.h"
    ).read_text(encoding="utf-8")
    write_policy_source = (
        ROOT / "components/platform_game_storage/src/msc_write_policy.c"
    ).read_text(encoding="utf-8")
    require("uint64_t *out_address" in write_policy_header and
            "uint64_t byte_address" in storage_source and
            "address > SIZE_MAX" not in write_policy_source,
            "USB MSC block addressing is not 64-bit safe")
    require_order(storage_source, [
        "msc_write_policy_validate(",
        "wl_erase_range(s_wl_handle, address, size_bytes)",
        "wl_write(s_wl_handle, address, data, size_bytes)",
        "wl_read(",
        "memcmp(data, s_hash_buffer, size_bytes)",
    ], "verified USB block write")
    require_order(storage_source[
        storage_source.index(
            "static esp_err_t stream_regular_file_exclusive("):
        storage_source.index(
            "esp_err_t platform_game_storage_lock_for_game(")
    ], [
        "s_maintenance = true",
        "tinyusb_driver_uninstall()",
        "fopen(path, \"rb\")",
        "install_usb_driver()",
        "s_maintenance = false",
    ], "exclusive update stream")

    report = {
        "result": "console-os-game-manager-build-verified-not-hardware-tested",
        "target": "esp32p4-revision-1.x",
        "idf": "5.5.3",
        "app": {
            "path": str(app_binary.relative_to(ROOT)),
            "bytes": app_binary.stat().st_size,
            "sha256": sha256(app_binary),
            "smallest_ota_free_bytes": OTA0_BYTES - app_binary.stat().st_size,
        },
        "os_update": os_update,
        "legacy_p4cart": p4cart,
        "game_packages": package_reports,
        "game_resources": resource_reports,
        "game_data": {
            "path": str(game_image.relative_to(ROOT)),
            "bytes": GAME_DATA_BYTES,
            "sha256": sha256(game_image),
            "offset": hex(GAME_DATA_OFFSET),
        },
        "hardware_execution_authorized": False,
        "feature_parity": {
            "system_apps": [
                "file-manager", "game-manager", "achievements",
                "multiplayer", "save-manager", "terminal-ssh-ui",
            ],
            "multiplayer_core_linked": True,
            "physical_transport": "host-relay-pending",
        },
    }
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
