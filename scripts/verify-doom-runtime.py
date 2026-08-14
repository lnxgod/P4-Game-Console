#!/usr/bin/env python3

"""Fail-closed verification for the reviewed silent Doom D1 runtime."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/doom_runtime"
BUILD_EVIDENCE = ROOT / "test-runs/2026-08-12-doom-d1-build.json"
BUILD_EVIDENCE_SHA256 = "f9b42245b4dea2d4629e479f835db825fd2eb64a720171b7ed6791be31526302"
EXPECTED_BINARY_BYTES = 704720
EXPECTED_BINARY_SHA256 = "97236ee252e4dc4373abed8368d1d27ec1892a7f569618c4b938b4117df93b42"
EXPECTED_ELF_BYTES = 9479332
EXPECTED_ELF_SHA256 = "1e08102140e342dbb18b5940395bf845782b16b734ddc96a80003589dd4ba4d1"
EXPECTED_LOCK_SHA256 = "a444148460bc071992c1463794413b62b56f62bfe74bcef9154748cc0804fdd2"
EXPECTED_DOOM_COMMIT = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
EXPECTED_DOOM_TREE = "413539bdaa1521af167d9b34e9db0cd193367624"
EXPECTED_DOOM_MANIFEST_SHA256 = "499fd1de6e8809099ba1ae5c75fb5eebde7a66f68950acfb9c72683c5279f2fe"
EXPECTED_WAD_BYTES = 4196020
EXPECTED_WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
EXPECTED_SOURCE_FILES = {
    "apps/doom_runtime/CMakeLists.txt",
    "apps/doom_runtime/main/CMakeLists.txt",
    "apps/doom_runtime/main/doom_runtime_main.c",
    "apps/doom_runtime/main/idf_component.yml",
    "apps/doom_runtime/sdkconfig.defaults",
    "apps/doom_runtime/dependencies.lock",
    "apps/doom/components/doom_engine/CMakeLists.txt",
    "components/doom_video/CMakeLists.txt",
    "components/doom_video/include/doom/video.h",
    "components/doom_video/include/doom/video_convert.h",
    "components/doom_video/src/doom_video_convert.c",
    "components/doom_video/src/doom_video_espidf.c",
    "components/doom_video/tests/CMakeLists.txt",
    "components/doom_video/tests/test_doom_video.c",
    "components/doom_video/tests/test_pinned_doom_rgb565.c",
    "components/platform_display/CMakeLists.txt",
    "components/platform_display/Kconfig",
    "components/platform_display/include/platform/display.h",
    "components/platform_display/src/platform_display.c",
    "components/platform_display/src/platform_display_layout.c",
    "components/platform_display/src/platform_display_layout.h",
    "components/platform_display/tests/CMakeLists.txt",
    "components/platform_display/tests/test_platform_display_layout.c",
    "components/platform_storage/CMakeLists.txt",
    "components/platform_storage/Kconfig",
    "components/platform_storage/include/platform/storage.h",
    "components/platform_storage/src/platform_storage.c",
    "components/platform_storage/src/platform_storage_wad.c",
    "components/platform_storage/src/platform_storage_wad.h",
    "components/platform_storage/tests/CMakeLists.txt",
    "components/platform_storage/tests/test_platform_storage_wad.c",
    "third_party/source-lock.json",
    "third_party/doomgeneric.git-tree",
    "third_party/game-data.json",
    "toolchain.lock.json",
}
EXPECTED_PROJECT_COMPILED_SOURCES = {
    "apps/doom_runtime/main/doom_runtime_main.c",
    "components/doom_video/src/doom_video_convert.c",
    "components/doom_video/src/doom_video_espidf.c",
    "components/platform_display/src/platform_display.c",
    "components/platform_display/src/platform_display_layout.c",
    "components/platform_storage/src/platform_storage.c",
    "components/platform_storage/src/platform_storage_wad.c",
}
EXPECTED_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
    "sdmmc_slot_0_1bit_gpio43_gpio44_gpio39",
}
EXPECTED_ABSENT = {
    "gpio29",
    "gpio41",
    "touch",
    "audio",
    "camera",
    "wireless",
    "usb_host",
}
EXPECTED_LINKED_SYMBOLS = {
    "app_main",
    "doomgeneric_Create",
    "doomgeneric_Tick",
    "DG_Init",
    "DG_DrawFrame",
    "DG_GetKey",
    "doom_video_convert_xrgb8888_to_rgb565",
    "doom_video_init",
    "doom_video_submit_black",
    "doom_video_submit_xrgb8888",
    "platform_display_init",
    "platform_display_set_brightness",
    "platform_display_submit_rgb565",
    "platform_storage_init",
    "platform_storage_get_card_info",
    "platform_storage_find_doom_shareware",
}
FORBIDDEN_LINKED_SYMBOLS = {
    "usb_host_install",
    "hid_host_install",
    "i2s_new_channel",
    "i2c_new_master_bus",
    "platform_usb_host_start",
    "platform_gamepad_usb_start",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_runtime verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        fail(f"cannot hash {path}: {error}")
    return digest.hexdigest()


def checked_build_path(build_dir: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    candidate = (build_dir / relative).resolve()
    require(candidate.is_relative_to(build_dir), f"{label} path leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def cache_tool(build_dir: pathlib.Path, variable: str) -> pathlib.Path:
    cache = (build_dir / "CMakeCache.txt").read_text()
    match = re.search(rf"^{re.escape(variable)}:FILEPATH=(.+)$", cache, re.MULTILINE)
    require(match is not None, f"{variable} is absent from CMakeCache")
    tool = pathlib.Path(match.group(1))
    require(tool.is_file(), f"configured tool is missing: {tool}")
    return tool


def command_output(arguments: list[str], label: str) -> str:
    try:
        result = subprocess.run(
            arguments,
            check=True,
            capture_output=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"{label} failed: {error}")
    return result.stdout


def generated_factory_partition_bytes(path: pathlib.Path) -> int:
    data = path.read_bytes()
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic, part_type, subtype, part_offset, size, name, _flags = struct.unpack(
            "<HBBII16sI", entry
        )
        if magic == 0xEBEB:
            break
        if magic != 0x50AA:
            continue
        label = name.split(b"\0", 1)[0].decode("ascii", errors="strict")
        if part_type == 0 and subtype == 0 and label == "factory":
            require(part_offset == 0x10000, "generated factory offset is not 0x10000")
            return size
    fail("generated partition table has no factory application")


def verify_metadata(mode: str) -> dict:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "doom_runtime",
            "wrong app metadata")
    require(metadata.get("stage") == "D1-preflash-reviewed", "unexpected runtime stage")
    require(metadata.get("evidence_class") == "build-tested", "runtime is not build-tested")
    require(metadata.get("build_evidence") == "test-runs/2026-08-12-doom-d1-build.json",
            "wrong build evidence")
    require(metadata.get("runtime_supported") is False,
            "preflash metadata must not claim runtime support")
    require(metadata.get("game_data_embedded") is False, "metadata claims embedded game data")
    require(metadata.get("flash_authorized") is False, "legacy broad flash flag must remain false")
    require(metadata.get("flash_project_authorized") is False,
            "full-project flash must remain false")
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False,
                "build-only mode requires app flash to remain false")
    else:
        require(metadata.get("flash_app_authorized") is True,
                "app-only flash has not been explicitly authorized")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES,
            "metadata interface scope changed")
    require(EXPECTED_ABSENT <= set(metadata.get("hardware_interfaces_explicitly_absent", [])),
            "metadata no longer denies every out-of-scope interface")
    return metadata


def verify_evidence() -> dict:
    require(sha256_file(BUILD_EVIDENCE) == BUILD_EVIDENCE_SHA256,
            "reviewed build-evidence file changed")
    evidence = load_json(BUILD_EVIDENCE)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested"
        and evidence.get("result") == "pass"
        and evidence.get("scope") == "silent neutral-input Doom runtime preflash review",
        "reviewed evidence is not the passing D1 build record",
    )
    source_inventory = evidence.get("source_inventory")
    require(isinstance(source_inventory, dict), "source inventory is missing")
    require(set(source_inventory) == EXPECTED_SOURCE_FILES, "reviewed source set changed")
    for relative, expected_hash in source_inventory.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT), f"source path leaves repository: {relative}")
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source changed: {relative}")

    artifacts = evidence.get("artifacts", {})
    require(
        artifacts.get("app_binary_bytes") == EXPECTED_BINARY_BYTES
        and artifacts.get("app_binary_sha256") == EXPECTED_BINARY_SHA256
        and artifacts.get("elf_bytes") == EXPECTED_ELF_BYTES
        and artifacts.get("elf_sha256") == EXPECTED_ELF_SHA256,
        "reviewed artifact identity changed",
    )
    engine = evidence.get("engine", {})
    require(
        engine.get("commit") == EXPECTED_DOOM_COMMIT
        and engine.get("tree") == EXPECTED_DOOM_TREE
        and engine.get("vendor_manifest_sha256") == EXPECTED_DOOM_MANIFEST_SHA256
        and engine.get("vendor_files_verified") == 205
        and engine.get("vendor_files_modified") is False
        and engine.get("selected_sources") == 80
        and engine.get("compiled_sources") == 80
        and engine.get("archive_members") == 80
        and engine.get("map_members") == 80
        and engine.get("whole_archive") is True,
        "reviewed engine provenance or link coverage changed",
    )
    build = evidence.get("build", {})
    require(
        build.get("reproducible_build") is True
        and build.get("compile_time_date_enabled") is False
        and build.get("independent_build_directories_compared", 0) >= 2
        and build.get("canonical_build_matches") is True
        and build.get("identical_binary") is True
        and build.get("identical_elf") is True,
        "reviewed evidence lacks three-build reproducibility",
    )
    game_data = evidence.get("game_data", {})
    require(
        game_data.get("size_bytes") == EXPECTED_WAD_BYTES
        and game_data.get("sha256") == EXPECTED_WAD_SHA256
        and game_data.get("wad_files_committed") is False
        and game_data.get("wad_bytes_embedded") is False
        and game_data.get("build_graph_contains_wad_input") is False,
        "reviewed external-WAD policy changed",
    )
    safety = evidence.get("safety", {})
    require(
        safety.get("display_initialized_dark_before_storage") is True
        and safety.get("backlight_enabled_only_after_exact_wad_and_first_black_frame") is True
        and safety.get("fatal_engine_exit_forces_backlight_dark") is True
        and safety.get("engine_screen_buffer_allocation_checked_before_use") is True
        and safety.get("frame_submit_failure_halts_dark") is True
        and safety.get("input") == "neutral"
        and safety.get("usb_host_initialized") is False,
        "reviewed runtime safety properties changed",
    )
    return evidence


def verify_source_semantics() -> None:
    runtime = (APP_DIR / "main/doom_runtime_main.c").read_text()
    display_init = runtime.index("platform_display_init()")
    exit_register = runtime.index("I_AtExit(engine_exit_dark, true)")
    storage_init = runtime.index("platform_storage_init()")
    wad_validate = runtime.index("platform_storage_find_doom_shareware")
    first_black = runtime.index("doom_video_submit_black")
    backlight_on = runtime.index("platform_display_set_brightness(25)")
    engine_start = runtime.index("doomgeneric_Create")
    require(
        display_init < exit_register < storage_init < wad_validate
        < first_black < backlight_on < engine_start,
        "fail-dark initialization/startup order changed",
    )

    exit_callback = re.search(
        r"static void engine_exit_dark\(void\)\s*\{(?P<body>.*?)\n\}",
        runtime,
        re.DOTALL,
    )
    require(exit_callback is not None, "engine exit-dark callback is missing")
    require("platform_display_set_brightness(0)" in exit_callback.group("body"),
            "engine exit callback no longer forces darkness")
    dg_init = re.search(r"void DG_Init\(void\)\s*\{(?P<body>.*?)\n\}", runtime, re.DOTALL)
    require(dg_init is not None, "DG_Init is missing")
    require(
        "DG_ScreenBuffer == NULL" in dg_init.group("body")
        and 'halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM)' in dg_init.group("body"),
        "engine framebuffer allocation is not checked fail-dark",
    )
    halt = re.search(
        r"static void halt_dark\([^)]*\)\s*\{(?P<body>.*?)\n\}",
        runtime,
        re.DOTALL,
    )
    require(halt is not None, "halt-dark function is missing")
    require(
        "platform_display_set_brightness(0)" in halt.group("body")
        and "for (;;)" in halt.group("body"),
        "halt path no longer stays dark and stopped",
    )
    require('"-gfxmode"' in runtime and '"rgba8888"' in runtime,
            "reviewed video mode changed")
    require('"-nosound"' in runtime and '"-nomusic"' in runtime,
            "silent runtime flags are missing")
    require("*pressed = 0" in runtime and "*key = 0" in runtime,
            "D1 input is no longer neutral")

    cmake_text = "\n".join(
        (ROOT / relative).read_text()
        for relative in EXPECTED_SOURCE_FILES
        if relative.endswith("CMakeLists.txt")
    )
    require(
        all(token not in cmake_text for token in ("target_add_binary_data", "EMBED_FILES", "EMBED_TXTFILES")),
        "a reviewed CMake file can embed external bytes",
    )


def verify_engine_provenance() -> None:
    result = subprocess.run(
        [str(ROOT / "scripts/doom/verify-doomgeneric.sh")],
        cwd=ROOT,
        capture_output=True,
        encoding="utf-8",
    )
    if result.returncode != 0:
        fail(f"doomgeneric provenance check failed: {result.stderr.strip()}")
    require(
        f"commit={EXPECTED_DOOM_COMMIT} tree={EXPECTED_DOOM_TREE} files=205" in result.stdout,
        "doomgeneric provenance marker changed",
    )


def verify_build_graph(build_dir: pathlib.Path, evidence: dict) -> tuple[pathlib.Path, pathlib.Path]:
    lock = load_json(ROOT / "toolchain.lock.json")
    target = lock["target"]
    recorded_build = evidence["build"]
    recorded_toolchain = evidence["toolchain"]
    require(recorded_toolchain.get("esp_idf_version") == lock["esp_idf"]["version"],
            "evidence IDF version differs from lock")
    require(recorded_toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"],
            "evidence IDF commit differs from lock")
    require(recorded_build.get("target") == target["chip"], "evidence target differs from lock")
    require(recorded_build.get("minimum_revision_full") == target["min_revision_full"],
            "evidence minimum revision differs from lock")
    require(recorded_build.get("maximum_revision_full") == target["max_revision_full"],
            "evidence maximum revision differs from lock")

    dependencies = evidence["dependencies"]
    require(sha256_file(APP_DIR / "dependencies.lock") == EXPECTED_LOCK_SHA256,
            "dependency lock changed")
    require(dependencies.get("lock_file_sha256") == EXPECTED_LOCK_SHA256,
            "evidence dependency-lock hash changed")
    lock_text = (APP_DIR / "dependencies.lock").read_text()
    require("version: 1.0.2" in lock_text, "EK79007 1.0.2 is not locked")
    require("component_hash: 07c1afab7e9fd4dd2fd06ff9245e65327c5bbd5485efec199496e19a9304d47b" in lock_text,
            "EK79007 component hash changed")

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True,
            "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True,
            "compile-time date is enabled")
    require(sdkconfig.get("ESP32P4_SELECTS_REV_LESS_V3") is True,
            "revision-1.x silicon family is not selected")
    require(sdkconfig.get("ESP32P4_REV_MIN_100") is True,
            "minimum silicon revision is not v1.0")
    require(sdkconfig.get("ESPTOOLPY_FLASHSIZE_16MB") is True,
            "build flash size is not 16 MiB")
    require(sdkconfig.get("SPIRAM") is True and sdkconfig.get("SPIRAM_USE_MALLOC") is True,
            "PSRAM allocation support is disabled")
    require(sdkconfig.get("ESP_TASK_WDT_PANIC") is True,
            "DSI stall watchdog panic is disabled")
    require(sdkconfig.get("PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
            "scoped display Kconfig is disabled")
    require(sdkconfig.get("PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
            "scoped storage Kconfig is disabled")

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs")
    require(int(description.get("min_rev")) == target["min_revision_full"],
            "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"],
            "generated maximum revision differs")
    require(description.get("git_revision") == lock["esp_idf"]["git_tag"],
            "generated IDF tag differs")

    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000,
            "application offset is not 0x10000")
    binary = checked_build_path(build_dir, app.get("file"), "application binary")
    elf = checked_build_path(build_dir, description.get("app_elf"), "application ELF")
    require(binary.stat().st_size == EXPECTED_BINARY_BYTES, "application binary size changed")
    require(sha256_file(binary) == EXPECTED_BINARY_SHA256, "application binary hash changed")
    require(elf.stat().st_size == EXPECTED_ELF_BYTES, "application ELF size changed")
    require(sha256_file(elf) == EXPECTED_ELF_SHA256, "application ELF hash changed")

    generated_capacity = generated_factory_partition_bytes(
        build_dir / "partition_table/partition-table.bin"
    )
    require(generated_capacity == evidence["build"]["generated_app_partition_bytes"] == 1048576,
            "generated application capacity changed")
    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    factory = [
        entry for entry in manifest.get("factory_partition_table", [])
        if entry.get("type") == "app" and entry.get("subtype") == "factory"
    ]
    require(len(factory) == 1 and int(factory[0]["offset"], 0) == 0x10000,
            "saved factory application is ambiguous")
    require(factory[0].get("size") == "11M", "saved factory capacity changed")
    require(binary.stat().st_size <= generated_capacity and binary.stat().st_size <= 11 * 1024 * 1024,
            "application exceeds an applicable partition capacity")

    compile_commands_text = (build_dir / "compile_commands.json").read_text()
    build_ninja_text = (build_dir / "build.ninja").read_text()
    require(".wad" not in (compile_commands_text + build_ninja_text).lower(),
            "WAD path appears in the build graph")
    compile_commands = json.loads(compile_commands_text)
    require(isinstance(compile_commands, list), "compile_commands is not an array")

    engine_cmake = (ROOT / "apps/doom/components/doom_engine/CMakeLists.txt").read_text()
    selected = re.findall(r"\$\{P4_DOOMGENERIC_DIR\}/([A-Za-z0-9_]+\.c)", engine_cmake)
    require(len(selected) == len(set(selected)) == 80, "selected engine source set changed")
    compiled_engine = {
        pathlib.Path(entry["file"]).name
        for entry in compile_commands
        if isinstance(entry, dict)
        and isinstance(entry.get("file"), str)
        and "/third_party/doomgeneric/doomgeneric/" in entry["file"]
    }
    require(compiled_engine == set(selected), "compiled engine source set differs")

    compiled_project: set[str] = set()
    for entry in compile_commands:
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            continue
        source = pathlib.Path(entry["file"]).resolve()
        if not source.is_relative_to(ROOT):
            continue
        relative = source.relative_to(ROOT).as_posix()
        if relative.startswith(("apps/doom_runtime/main/", "components/doom_video/src/",
                                "components/platform_display/src/", "components/platform_storage/src/")):
            compiled_project.add(relative)
    require(compiled_project == EXPECTED_PROJECT_COMPILED_SOURCES,
            "project-owned compiled source set changed")

    archive = build_dir / "esp-idf/doom_engine/libdoom_engine.a"
    require(archive.is_file(), "doom engine archive is missing")
    members = set(command_output([str(cache_tool(build_dir, "CMAKE_AR")), "t", str(archive)],
                                 "engine archive inspection").splitlines())
    expected_members = {f"{source}.obj" for source in selected}
    require(members == expected_members, "engine archive member set differs")
    map_members = set(re.findall(
        r"esp-idf/doom_engine/libdoom_engine\.a\(([^)]+\.c\.obj)\)",
        (build_dir / "p4_doom_runtime.map").read_text(errors="replace"),
    ))
    require(map_members == expected_members, "linker map engine coverage differs")
    require(
        "-Wl,--whole-archive  esp-idf/doom_engine/libdoom_engine.a  -Wl,--no-whole-archive"
        in build_ninja_text,
        "doom engine is not linked whole-archive",
    )

    symbols_text = command_output(
        [str(cache_tool(build_dir, "CMAKE_NM")), "-g", str(elf)],
        "ELF symbol inspection",
    )
    symbols = set(re.findall(r"^[0-9a-fA-F]+\s+[A-Za-z]\s+(\S+)$", symbols_text, re.MULTILINE))
    require(EXPECTED_LINKED_SYMBOLS <= symbols, "a required runtime symbol is absent")
    require(not (FORBIDDEN_LINKED_SYMBOLS & symbols),
            "USB or audio initialization code reached the silent runtime ELF")
    require(re.search(r"\b_binary_.*(?:wad|doom1)", symbols_text, re.IGNORECASE) is None,
            "ELF contains an embedded-WAD linker symbol")
    return binary, elf


def verify_scoped_authorizations(mode: str) -> None:
    profile = load_json(ROOT / "hardware/board-profile.json")
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    authorizations = profile.get("peripheral_authorizations", {})
    display = authorizations.get("display", {})
    storage = authorizations.get("storage", {})
    require(
        display.get("authorized") is True
        and display.get("scope") == "display_only_cross_revision_v1_0_through_v1_2",
        "display-only authorization changed",
    )
    require(
        storage.get("authorized") is True
        and storage.get("scope") == "sdmmc_read_only_service_no_format_cross_revision_v1_0_through_v1_2",
        "storage-only authorization changed",
    )
    require(authorizations.get("audio", {}).get("authorized") is False,
            "silent D1 review cannot proceed after an unreviewed audio authorization")

    framebuffer_test_path = display.get("framebuffer_hardware_test")
    require(isinstance(framebuffer_test_path, str), "framebuffer hardware-test path is missing")
    framebuffer_test = load_json(ROOT / framebuffer_test_path)
    require(
        framebuffer_test.get("classification") == "hardware-tested"
        and framebuffer_test.get("result") == "pass"
        and framebuffer_test.get("firmware", {}).get("installed_app_readback_matches_binary") is True,
        "framebuffer prerequisite did not pass",
    )

    if mode != "app-flash":
        return
    storage_test_path = storage.get("hardware_test")
    require(isinstance(storage_test_path, str) and storage_test_path,
            "storage prerequisite has no passing hardware-test path")
    storage_test = load_json(ROOT / storage_test_path)
    require(
        storage_test.get("classification") == "hardware-tested"
        and storage_test.get("result") == "pass",
        "storage prerequisite did not pass",
    )
    checks = storage_test.get("checks", {})
    require(
        checks.get("mount_passed") is True
        and checks.get("wad_verified") is True
        and checks.get("wad_identity") == "doom-shareware-1.9"
        and checks.get("wad_bytes") == EXPECTED_WAD_BYTES
        and checks.get("wad_sha256") == EXPECTED_WAD_SHA256
        and checks.get("installed_app_readback_matches_binary") is True
        and checks.get("failure_markers_observed") is False,
        "storage hardware evidence lacks exact mount/WAD/readback acceptance",
    )


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-doom-runtime.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "mode must be build-only or app-flash")

    verify_metadata(mode)
    evidence = verify_evidence()
    verify_source_semantics()
    verify_engine_provenance()
    verify_scoped_authorizations(mode)
    binary, _elf = verify_build_graph(build_dir, evidence)
    print(
        "doom_runtime verification: PASS "
        f"mode={mode} offset=0x10000 bytes={binary.stat().st_size} "
        f"sha256={EXPECTED_BINARY_SHA256} engine_sources=80 wad_embedded=false "
        "input=neutral audio=disabled"
    )


if __name__ == "__main__":
    main()
