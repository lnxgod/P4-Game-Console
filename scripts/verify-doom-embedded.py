#!/usr/bin/env python3

"""Fail-closed verification for the reviewed local embedded-WAD Doom image."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/doom_embedded"
LOCAL_WAD = ROOT / "local-data/doom/doom1.wad"
BUILD_EVIDENCE = ROOT / "test-runs/2026-08-12-doom-embedded-e1-build.json"
BUILD_EVIDENCE_SHA256 = "ad9c32d9087576ca6598b6a61bd27f41bd02f82bd3e5fdbc9e3e1ee64327ab3c"
EXPECTED_BINARY_BYTES = 4821648
EXPECTED_BINARY_SHA256 = "971aa57f154fdc7f3932d339b95a5dd62d659e72c11edb4cf448f412f3299f7e"
EXPECTED_ELF_BYTES = 12912728
EXPECTED_ELF_SHA256 = "3518df8ae9bb4c9b63e4304461a4344c0cfde6787041d5ad0b7fc359ad7fd47c"
EXPECTED_LOCK_SHA256 = "794b8d7a0b13cb2c810913c04f0a01adba406a72e4585a7f5d1ff1ea3524f0c4"
EXPECTED_DOOM_COMMIT = "dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284"
EXPECTED_DOOM_TREE = "413539bdaa1521af167d9b34e9db0cd193367624"
EXPECTED_DOOM_MANIFEST_SHA256 = "499fd1de6e8809099ba1ae5c75fb5eebde7a66f68950acfb9c72683c5279f2fe"
EXPECTED_WAD_BYTES = 4196020
EXPECTED_WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
EXPECTED_WAD_BINARY_OFFSET = 138356
EXPECTED_WAD_ELF_START = 0x40081C74
EXPECTED_WAD_ELF_END = 0x40482328
EXPECTED_APP_CAPACITY = 11 * 1024 * 1024
EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 24 * 1024, 0),
    ("phy_init", 1, 1, 0xF000, 4 * 1024, 0),
    ("factory", 0, 0, 0x10000, EXPECTED_APP_CAPACITY, 0),
    ("storage", 1, 0x82, 0xB10000, 4 * 1024 * 1024, 0),
]
EXPECTED_SOURCE_FILES = {
    "apps/doom_embedded/CMakeLists.txt",
    "apps/doom_embedded/main/CMakeLists.txt",
    "apps/doom_embedded/main/doom_embedded_main.c",
    "apps/doom_embedded/main/idf_component.yml",
    "apps/doom_embedded/sdkconfig.defaults",
    "apps/doom_embedded/partitions.csv",
    "apps/doom_embedded/dependencies.lock",
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
    "components/platform_readonly_blob/CMakeLists.txt",
    "components/platform_readonly_blob/include/platform/readonly_blob.h",
    "components/platform_readonly_blob/src/platform_readonly_blob_vfs.c",
    "components/platform_readonly_blob/src/readonly_blob_core.c",
    "components/platform_readonly_blob/src/readonly_blob_core.h",
    "components/platform_readonly_blob/tests/CMakeLists.txt",
    "components/platform_readonly_blob/tests/test_readonly_blob_core.c",
    "third_party/source-lock.json",
    "third_party/doomgeneric.git-tree",
    "third_party/game-data.json",
    "toolchain.lock.json",
}
EXPECTED_PROJECT_COMPILED_SOURCES = {
    "apps/doom_embedded/main/doom_embedded_main.c",
    "components/doom_video/src/doom_video_convert.c",
    "components/doom_video/src/doom_video_espidf.c",
    "components/platform_display/src/platform_display.c",
    "components/platform_display/src/platform_display_layout.c",
    "components/platform_readonly_blob/src/platform_readonly_blob_vfs.c",
    "components/platform_readonly_blob/src/readonly_blob_core.c",
}
EXPECTED_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
}
EXPECTED_ABSENT = {
    "gpio29",
    "gpio41",
    "sd_card",
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
    "platform_readonly_blob_register",
    "_binary_doom_shareware_wad_start",
    "_binary_doom_shareware_wad_end",
}
FORBIDDEN_LINKED_SYMBOLS = {
    "platform_storage_init",
    "platform_storage_get_card_info",
    "platform_storage_find_doom_shareware",
    "esp_vfs_fat_sdmmc_mount",
    "esp_vfs_fat_sdspi_mount",
    "sdmmc_card_init",
    "sdmmc_host_init",
    "sdspi_host_init",
    "usb_host_install",
    "hid_host_install",
    "platform_usb_host_start",
    "platform_gamepad_usb_start",
    "i2s_new_channel",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_embedded verification failed: {message}")


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
    try:
        cache = (build_dir / "CMakeCache.txt").read_text()
    except (OSError, UnicodeError) as error:
        fail(f"cannot read CMake cache: {error}")
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


def partition_entries(path: pathlib.Path) -> list[tuple[str, int, int, int, int, int]]:
    try:
        data = path.read_bytes()
    except OSError as error:
        fail(f"cannot read partition table: {error}")
    entries: list[tuple[str, int, int, int, int, int]] = []
    for offset in range(0, len(data), 32):
        entry = data[offset:offset + 32]
        if len(entry) != 32:
            break
        magic, part_type, subtype, part_offset, size, name, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        if magic == 0xEBEB:
            break
        require(magic == 0x50AA, f"invalid partition entry at byte {offset}")
        label = name.split(b"\0", 1)[0].decode("ascii", errors="strict")
        entries.append((label, part_type, subtype, part_offset, size, flags))
    return entries


def verify_metadata(mode: str) -> None:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "doom_embedded",
            "wrong app metadata")
    require(metadata.get("stage") == "E1-preflash-reviewed", "unexpected runtime stage")
    require(metadata.get("evidence_class") == "build-tested", "runtime is not build-tested")
    require(
        metadata.get("build_evidence")
        == "test-runs/2026-08-12-doom-embedded-e1-build.json",
        "wrong build evidence",
    )
    require(metadata.get("runtime_supported") is False,
            "preflash metadata must not claim runtime support")
    require(metadata.get("game_data_embedded") is True,
            "metadata no longer declares embedded game data")
    require(metadata.get("game_data_committed") is False,
            "metadata claims the WAD is committed")
    require(metadata.get("game_data_redistribution_authorized") is False,
            "metadata claims redistribution authorization")
    require(metadata.get("flash_authorized") is False,
            "legacy broad flash flag must remain false")
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
    require(set(metadata.get("hardware_interfaces_explicitly_absent", [])) == EXPECTED_ABSENT,
            "metadata out-of-scope interface set changed")


def verify_evidence() -> dict:
    require(sha256_file(BUILD_EVIDENCE) == BUILD_EVIDENCE_SHA256,
            "reviewed build-evidence file changed")
    evidence = load_json(BUILD_EVIDENCE)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested"
        and evidence.get("result") == "pass"
        and evidence.get("scope")
        == "SD-independent silent neutral-input Doom embedded-WAD fallback preflash review",
        "reviewed evidence is not the passing E1 build record",
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
        and build.get("identical_elf") is True
        and build.get("generated_flash_after_action") == "no_reset"
        and build.get("application_execution_deferred_until_exact_readback") is True
        and build.get("explicit_run_after_exact_readback") is True
        and build.get("application_offset") == "0x10000"
        and build.get("generated_app_partition_bytes") == EXPECTED_APP_CAPACITY
        and build.get("saved_factory_app_partition_bytes") == EXPECTED_APP_CAPACITY,
        "reviewed build/reproducibility properties changed",
    )
    game_data = evidence.get("game_data", {})
    require(
        game_data.get("size_bytes") == EXPECTED_WAD_BYTES
        and game_data.get("sha256") == EXPECTED_WAD_SHA256
        and game_data.get("wad_input_ignored") is True
        and game_data.get("wad_files_committed") is False
        and game_data.get("wad_bytes_embedded_in_local_artifact") is True
        and game_data.get("game_data_redistribution_authorized") is False
        and game_data.get("binary_occurrences") == 1
        and game_data.get("binary_occurrence_offset") == EXPECTED_WAD_BINARY_OFFSET
        and game_data.get("elf_symbol_span_bytes") == EXPECTED_WAD_BYTES,
        "reviewed local embedded-WAD policy changed",
    )
    safety = evidence.get("safety", {})
    require(
        safety.get("display_initialized_dark_before_wad_validation") is True
        and safety.get(
            "backlight_enabled_only_after_exact_embedded_wad_vfs_readback_and_first_black_frame"
        ) is True
        and safety.get("fatal_engine_exit_forces_backlight_dark") is True
        and safety.get("engine_screen_buffer_allocation_checked_before_use") is True
        and safety.get("frame_submit_failure_halts_dark") is True
        and safety.get("app_flash_holds_cpu_in_rom_loader_until_exact_readback") is True
        and safety.get("app_launch_occurs_exactly_once_after_verified_readback") is True
        and safety.get("wad_vfs") == "single-file immutable read-only"
        and safety.get("wad_vfs_write_callback_present") is False
        and safety.get("wad_vfs_bounds_checked") is True
        and safety.get("input") == "neutral"
        and safety.get("usb_host_initialized") is False
        and safety.get("platform_storage_service_compiled") is False
        and safety.get("sd_hardware_api_linked") is False,
        "reviewed runtime safety properties changed",
    )
    execution = evidence.get("execution", {})
    require(
        execution.get("firmware_flashed") is False
        and execution.get("firmware_executed") is False
        and execution.get("runtime_supported") is False,
        "build evidence makes an unsupported hardware claim",
    )
    return evidence


def verify_local_wad() -> bytes:
    require(LOCAL_WAD.is_file(), f"exact ignored local WAD is missing: {LOCAL_WAD}")
    require(LOCAL_WAD.stat().st_size == EXPECTED_WAD_BYTES, "local WAD byte count changed")
    require(sha256_file(LOCAL_WAD) == EXPECTED_WAD_SHA256, "local WAD SHA-256 changed")
    try:
        data = LOCAL_WAD.read_bytes()
    except OSError as error:
        fail(f"cannot read local WAD: {error}")
    require(data[:4] == b"IWAD" and len(data) >= 12, "local WAD header is invalid")
    lump_count, directory_offset = struct.unpack_from("<II", data, 4)
    require(
        lump_count > 0
        and directory_offset <= len(data)
        and lump_count * 16 <= len(data) - directory_offset,
        "local WAD directory is out of bounds",
    )
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", str(LOCAL_WAD.relative_to(ROOT))],
        cwd=ROOT,
        check=False,
    )
    require(ignored.returncode == 0, "local WAD is not ignored by Git")
    tracked = subprocess.run(
        ["git", "ls-files", "--error-unmatch", str(LOCAL_WAD.relative_to(ROOT))],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    require(tracked.returncode != 0, "local WAD is tracked by Git")
    for source_root in ("apps", "components", "docs", "hardware", "scripts", "test-runs", "third_party"):
        for candidate in (ROOT / source_root).rglob("*"):
            if candidate.is_file() and candidate.suffix.lower() == ".wad":
                fail(f"WAD file exists in a source/evidence tree: {candidate.relative_to(ROOT)}")
    return data


def verify_source_semantics() -> None:
    runtime = (APP_DIR / "main/doom_embedded_main.c").read_text()
    display_init = runtime.index("platform_display_init()")
    exit_register = runtime.index("I_AtExit(engine_exit_dark, true)")
    wad_validate = runtime.index("verify_embedded_wad(wad_start, wad_size)")
    vfs_register = runtime.index("platform_readonly_blob_register(&blob_config)")
    vfs_readback = runtime.index("verify_readonly_vfs()", vfs_register)
    video_init = runtime.index("doom_video_init()", vfs_readback)
    first_black = runtime.index("doom_video_submit_black", video_init)
    backlight_on = runtime.index("platform_display_set_brightness(25)", first_black)
    engine_start = runtime.index("doomgeneric_Create", backlight_on)
    require(
        display_init < exit_register < wad_validate < vfs_register < vfs_readback
        < video_init < first_black < backlight_on < engine_start,
        "fail-dark embedded-WAD startup order changed",
    )

    exit_callback = re.search(
        r"static void engine_exit_dark\(void\)\s*\{(?P<body>.*?)\n\}",
        runtime,
        re.DOTALL,
    )
    require(exit_callback is not None, "engine exit-dark callback is missing")
    require("platform_display_set_brightness(0)" in exit_callback.group("body"),
            "engine exit callback no longer forces darkness")
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
    dg_init = re.search(r"void DG_Init\(void\)\s*\{(?P<body>.*?)\n\}", runtime, re.DOTALL)
    require(dg_init is not None, "DG_Init is missing")
    require(
        "DG_ScreenBuffer == NULL" in dg_init.group("body")
        and 'halt_dark("engine-screen-buffer", ESP_ERR_NO_MEM)' in dg_init.group("body"),
        "engine framebuffer allocation is not checked fail-dark",
    )
    require(
        "EMBEDDED_WAD_BYTES ((size_t)4196020U)" in runtime
        and EXPECTED_WAD_SHA256 in runtime
        and 'memcmp(data, "IWAD", 4U)' in runtime
        and "mbedtls_sha256(data, size_bytes, digest, 0)" in runtime,
        "runtime no longer validates exact embedded WAD bytes",
    )
    require(
        "_binary_doom_shareware_wad_start" in runtime
        and "_binary_doom_shareware_wad_end" in runtime,
        "runtime embedded-WAD linker seam changed",
    )
    require('"-iwad"' in runtime and 'EMBEDDED_WAD_PATH' in runtime,
            "runtime no longer selects the embedded VFS WAD")
    require('"-gfxmode"' in runtime and '"rgba8888"' in runtime,
            "reviewed video mode changed")
    require('"-nosound"' in runtime and '"-nomusic"' in runtime,
            "silent runtime flags are missing")
    require("*pressed = 0" in runtime and "*key = 0" in runtime,
            "E1 input is no longer neutral")
    require("platform_storage" not in runtime and "sdmmc" not in runtime.lower(),
            "runtime source acquired an SD/storage dependency")

    defaults = (APP_DIR / "sdkconfig.defaults").read_text()
    require("CONFIG_ESPTOOLPY_AFTER_NORESET=y" in defaults,
            "app flash would start Doom before exact application readback")

    flash = (ROOT / "scripts/flash.sh").read_text()
    hook = re.search(
        r'if \[ "\$P4_APP" = doom_embedded \]; then\n(?P<body>.*?)\nfi',
        flash,
        re.DOTALL,
    )
    require(
        hook is not None
        and 'python3 "$P4_SCRIPT_DIR/verify-doom-embedded.py"' in hook.group("body")
        and '"$P4_BUILD_DIR" "$P4_FLASH_TARGET"' in hook.group("body"),
        "central app-flash path is not bound to this verifier",
    )
    deferred_membership = '[ "$P4_APP" = doom_embedded ] || \\'
    require(flash.count(deferred_membership) == 2,
            "Doom must be in both deferred readback and deferred launch branches")
    ordered_flash_tokens = (
        'P4_FLASH_OUTPUT=$(idf.py',
        'P4_READBACK_AFTER=no_reset',
        'if ! p4_verify_chunked_application_readback',
        '"$P4_BUILT_APP_PATH" "$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_BYTES"',
        'if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then',
        "Application readback verified:",
        'P4_RUN_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" run 2>&1)',
        "launched exactly once after successful application readback.",
        "p4_cleanup_flash_temps\ntrap - EXIT HUP INT TERM",
    )
    positions = [flash.find(token) for token in ordered_flash_tokens]
    require(
        all(position >= 0 for position in positions)
        and positions == sorted(positions)
        and '"$P4_PORT" "$P4_READBACK_AFTER" "$P4_READBACK_BAUDS"' in flash
        and 'P4_READBACK_CHUNK_BYTES=524288' in (
            ROOT / "scripts/lib/app-readback.sh"
        ).read_text(),
        "Doom execution is not deferred until after exact application readback",
    )

    main_cmake = (APP_DIR / "main/CMakeLists.txt").read_text()
    require(
        '"${CMAKE_CURRENT_LIST_DIR}/../../../local-data/doom/doom1.wad"' in main_cmake
        and "P4_EXPECTED_DOOM_WAD_BYTES 4196020" in main_cmake
        and EXPECTED_WAD_SHA256 in main_cmake
        and "target_add_binary_data(" in main_cmake
        and "BINARY" in main_cmake
        and "RENAME_TO doom_shareware_wad" in main_cmake,
        "build-time exact local-WAD gate or stable linker name changed",
    )
    require("platform_storage" not in main_cmake and "platform_readonly_blob" in main_cmake,
            "main component storage/VFS dependency changed")

    vfs = (ROOT / "components/platform_readonly_blob/src/platform_readonly_blob_vfs.c").read_text()
    core = (ROOT / "components/platform_readonly_blob/src/readonly_blob_core.c").read_text()
    header = (ROOT / "components/platform_readonly_blob/src/readonly_blob_core.h").read_text()
    tests = (ROOT / "components/platform_readonly_blob/tests/test_readonly_blob_core.c").read_text()
    require(
        "ESP_VFS_FLAG_CONTEXT_PTR" in vfs
        and "ESP_VFS_FLAG_READONLY_FS" in vfs
        and "ESP_VFS_FLAG_STATIC" in vfs
        and ".read_p = blob_read" in vfs
        and ".pread_p = blob_pread" in vfs
        and ".write_p" not in vfs,
        "VFS is no longer static/context-bound/read-only",
    )
    require(
        "(flags & O_ACCMODE) != O_RDONLY" in core
        and "O_CREAT | O_TRUNC | O_APPEND" in core
        and "errno = EROFS" in core
        and "core->size_bytes - core->position[fd]" in core
        and "decrement > base" in core,
        "read-only open or bounded read/seek checks changed",
    )
    require("READONLY_BLOB_MAX_OPEN_FILES = 8" in header,
            "read-only VFS descriptor bound changed")
    require(
        "INT64_MAX" in tests
        and "INT64_MIN" in tests
        and "errno == EMFILE" in tests
        and "errno == EROFS" in tests,
        "read-only VFS safety tests lost required cases",
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


def verify_saved_factory_layout() -> None:
    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    expected = [
        {"name": "nvs", "type": "data", "subtype": "nvs", "offset": "0x9000", "size": "24K"},
        {"name": "phy_init", "type": "data", "subtype": "phy", "offset": "0xf000", "size": "4K"},
        {"name": "factory", "type": "app", "subtype": "factory", "offset": "0x10000", "size": "11M"},
        {"name": "storage", "type": "data", "subtype": "spiffs", "offset": "0xb10000", "size": "4M"},
    ]
    require(manifest.get("factory_partition_table") == expected,
            "saved factory partition layout changed")


def verify_build_graph(
    build_dir: pathlib.Path,
    evidence: dict,
    wad_bytes: bytes,
) -> pathlib.Path:
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

    require(sha256_file(APP_DIR / "dependencies.lock") == EXPECTED_LOCK_SHA256,
            "dependency lock changed")
    lock_text = (APP_DIR / "dependencies.lock").read_text()
    require("version: 1.0.2" in lock_text, "EK79007 1.0.2 is not locked")
    require(
        "component_hash: 07c1afab7e9fd4dd2fd06ff9245e65327c5bbd5485efec199496e19a9304d47b"
        in lock_text,
        "EK79007 component hash changed",
    )

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
    require(
        sdkconfig.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdkconfig.get("ESPTOOLPY_AFTER_RESET") is not True
        and sdkconfig.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated build would start Doom before exact application readback",
    )
    require(sdkconfig.get("SPIRAM") is True and sdkconfig.get("SPIRAM_USE_MALLOC") is True,
            "PSRAM allocation support is disabled")
    require(sdkconfig.get("ESP_TASK_WDT_PANIC") is True,
            "DSI stall watchdog panic is disabled")
    require(sdkconfig.get("PARTITION_TABLE_CUSTOM") is True,
            "saved-capacity custom partition layout is disabled")
    require(sdkconfig.get("PARTITION_TABLE_CUSTOM_FILENAME") == "partitions.csv",
            "custom partition filename changed")
    require(sdkconfig.get("VFS_SUPPORT_DIR") is True,
            "stat support required by the read-only VFS is disabled")
    require(sdkconfig.get("PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
            "scoped display Kconfig is disabled")
    require(
        sdkconfig.get("PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is not True,
        "embedded fallback unexpectedly enabled the SD storage service",
    )

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs")
    require(int(description.get("min_rev")) == target["min_revision_full"],
            "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"],
            "generated maximum revision differs")
    require(description.get("git_revision") == lock["esp_idf"]["git_tag"],
            "generated IDF tag differs")
    build_components = set(description.get("build_components", []))
    require("platform_display" in build_components and "platform_readonly_blob" in build_components,
            "required platform services are absent")
    require("platform_storage" not in build_components,
            "platform storage service entered the embedded fallback build")

    flash_args = load_json(build_dir / "flasher_args.json")
    require(flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
            "generated flash after-action must hold Doom until readback passes")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000,
            "application offset is not 0x10000")
    binary = checked_build_path(build_dir, app.get("file"), "application binary")
    elf = checked_build_path(build_dir, description.get("app_elf"), "application ELF")
    require(binary.stat().st_size == EXPECTED_BINARY_BYTES, "application binary size changed")
    require(sha256_file(binary) == EXPECTED_BINARY_SHA256, "application binary hash changed")
    require(elf.stat().st_size == EXPECTED_ELF_BYTES, "application ELF size changed")
    require(sha256_file(elf) == EXPECTED_ELF_SHA256, "application ELF hash changed")
    require(binary.stat().st_size <= EXPECTED_APP_CAPACITY,
            "application exceeds the saved 11 MiB app partition")

    generated_partitions = partition_entries(build_dir / "partition_table/partition-table.bin")
    require(generated_partitions == EXPECTED_PARTITIONS,
            "generated partition table does not exactly mirror the saved factory layout")
    require(EXPECTED_APP_CAPACITY - binary.stat().st_size == 6712688,
            "reviewed app-partition free space changed")

    try:
        binary_bytes = binary.read_bytes()
    except OSError as error:
        fail(f"cannot inspect application binary: {error}")
    first = binary_bytes.find(wad_bytes)
    require(first == EXPECTED_WAD_BINARY_OFFSET,
            "exact WAD does not start at the reviewed binary offset")
    require(binary_bytes.find(wad_bytes, first + 1) == -1,
            "exact WAD appears more than once in the binary")

    build_ninja_text = (build_dir / "build.ninja").read_text()
    generated_wad_asm = build_dir / "doom1.wad.S"
    require(generated_wad_asm.is_file(), "generated WAD assembly source is missing")
    generated_prefix = generated_wad_asm.read_text(errors="strict")[:1000]
    require(
        str(LOCAL_WAD) in build_ninja_text
        and "doom1.wad.S" in build_ninja_text
        and str(LOCAL_WAD) in generated_prefix
        and "_binary_doom_shareware_wad_start" in generated_prefix,
        "build graph no longer has the one exact local-WAD input/stable symbol",
    )

    compile_commands_text = (build_dir / "compile_commands.json").read_text()
    compile_commands = json.loads(compile_commands_text)
    require(isinstance(compile_commands, list), "compile_commands is not an array")
    generated_entries = {
        pathlib.Path(entry["file"]).resolve()
        for entry in compile_commands
        if isinstance(entry, dict)
        and isinstance(entry.get("file"), str)
        and pathlib.Path(entry["file"]).name == "doom1.wad.S"
    }
    require(generated_entries == {generated_wad_asm.resolve()},
            "generated embedded-WAD assembly compile input changed")
    require("/components/platform_storage/" not in compile_commands_text,
            "platform storage source entered compile_commands")

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
        if relative.startswith((
            "apps/doom_embedded/main/",
            "components/doom_video/src/",
            "components/platform_display/src/",
            "components/platform_readonly_blob/src/",
        )):
            compiled_project.add(relative)
    require(compiled_project == EXPECTED_PROJECT_COMPILED_SOURCES,
            "project-owned compiled source set changed")

    archive = build_dir / "esp-idf/doom_engine/libdoom_engine.a"
    require(archive.is_file(), "doom engine archive is missing")
    members = set(command_output(
        [str(cache_tool(build_dir, "CMAKE_AR")), "t", str(archive)],
        "engine archive inspection",
    ).splitlines())
    expected_members = {f"{source}.obj" for source in selected}
    require(members == expected_members, "engine archive member set differs")
    map_path = build_dir / "p4_doom_embedded.map"
    require(map_path.is_file(), "linker map is missing")
    map_members = set(re.findall(
        r"esp-idf/doom_engine/libdoom_engine\.a\(([^)]+\.c\.obj)\)",
        map_path.read_text(errors="replace"),
    ))
    require(map_members == expected_members, "linker map engine coverage differs")
    require(
        "-Wl,--whole-archive  esp-idf/doom_engine/libdoom_engine.a  -Wl,--no-whole-archive"
        in build_ninja_text,
        "doom engine is not linked whole-archive",
    )

    symbols_text = command_output(
        [str(cache_tool(build_dir, "CMAKE_NM")), "-n", "-g", str(elf)],
        "ELF symbol inspection",
    )
    symbols = set(re.findall(r"^[0-9a-fA-F]+\s+[A-Za-z]\s+(\S+)$", symbols_text, re.MULTILINE))
    require(EXPECTED_LINKED_SYMBOLS <= symbols, "a required runtime symbol is absent")
    require(not (FORBIDDEN_LINKED_SYMBOLS & symbols),
            "an SD, USB-host, gamepad, or hardware-audio API reached the fallback ELF")
    symbol_values = {
        name: int(value, 16)
        for value, name in re.findall(
            r"^([0-9a-fA-F]+)\s+[A-Za-z]\s+(_binary_doom_shareware_wad_(?:start|end))$",
            symbols_text,
            re.MULTILINE,
        )
    }
    require(
        symbol_values.get("_binary_doom_shareware_wad_start") == EXPECTED_WAD_ELF_START
        and symbol_values.get("_binary_doom_shareware_wad_end") == EXPECTED_WAD_ELF_END
        and EXPECTED_WAD_ELF_END - EXPECTED_WAD_ELF_START == EXPECTED_WAD_BYTES,
        "embedded-WAD ELF linker span changed",
    )
    return binary


def verify_scoped_display_authorization() -> None:
    profile = load_json(ROOT / "hardware/board-profile.json")
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    display = profile.get("peripheral_authorizations", {}).get("display", {})
    require(
        display.get("authorized") is True
        and display.get("scope") == "display_only_cross_revision_v1_0_through_v1_2",
        "display-only authorization changed",
    )
    framebuffer_test_path = display.get("framebuffer_hardware_test")
    require(isinstance(framebuffer_test_path, str), "framebuffer hardware-test path is missing")
    framebuffer_test = load_json(ROOT / framebuffer_test_path)
    require(
        framebuffer_test.get("classification") == "hardware-tested"
        and framebuffer_test.get("result") == "pass"
        and framebuffer_test.get("firmware", {}).get("installed_app_readback_matches_binary") is True,
        "framebuffer prerequisite did not pass",
    )


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-doom-embedded.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"},
            "mode must be build-only or app-flash; full-project flash is prohibited")

    verify_metadata(mode)
    evidence = verify_evidence()
    wad_bytes = verify_local_wad()
    verify_source_semantics()
    verify_engine_provenance()
    verify_saved_factory_layout()
    verify_scoped_display_authorization()
    binary = verify_build_graph(build_dir, evidence, wad_bytes)
    print(
        "doom_embedded verification: PASS "
        f"mode={mode} offset=0x10000 bytes={binary.stat().st_size} "
        f"sha256={EXPECTED_BINARY_SHA256} engine_sources=80 "
        f"wad_embedded=true wad_bytes={EXPECTED_WAD_BYTES} wad_occurrences=1 "
        "sd=unused input=neutral audio=disabled"
    )


if __name__ == "__main__":
    main()
