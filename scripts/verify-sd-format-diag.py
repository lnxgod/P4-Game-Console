#!/usr/bin/env python3

"""Fail-closed exact-image gate for the one-shot destructive J5 SD formatter."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/sd_format_diag"
AUTHORIZATION_REL = "hardware/evidence/sd-format-diag-one-shot-authorization.json"
ELECTRICAL_REL = "hardware/evidence/elecrow-10.1-storage-path.json"
BUILD_EVIDENCE_REL = "test-runs/2026-08-12-sd-format-diag-d1-build.json"
EXPECTED_APP_OFFSET = 0x10000
EXPECTED_MAIN_TASK_STACK_BYTES = 16384
EXPECTED_INTERFACES = {
    "sdmmc_slot_0_1bit_sdr",
    "gpio43_sd_clk",
    "gpio44_sd_cmd",
    "gpio39_sd_d0",
    "clock_khz_1000",
    "micro_sd_connector_j5",
}
EXPECTED_ABSENT = {
    "sd_d1_through_d7",
    "sd_card_detect",
    "sd_write_protect",
    "sd_ddr",
    "display",
    "touch",
    "audio",
    "camera",
    "wireless",
    "usb_host",
}
EXPECTED_SOURCE_INVENTORY = {
    "apps/sd_format_diag/CMakeLists.txt": "de29c227615d30b274a5c280a1c6c38b0765c3d09fc25e6bf815875d416ab7c5",
    "apps/sd_format_diag/main/CMakeLists.txt": "717dfd9e252ce99ef709dbf6a11dce7286188c78ec7911aedd68854a7892d976",
    "apps/sd_format_diag/main/Kconfig.projbuild": "0e179becc83e292a93ed4a8e657cd4378c8c49875788dab9a31881ed0653bab9",
    "apps/sd_format_diag/main/sd_format_diag_main.c": "9693472b99f454ee560ba7b83c5f5eca98dc693b9f3c8a24a61ad4d1fa2d84ac",
    "apps/sd_format_diag/sdkconfig.defaults": "7a82d1fb447d47ad30cb666ea0d03fa4be4bd2dbc55148659fff5f0dafe686b2",
}


def fail(message: str) -> None:
    raise SystemExit(f"sd_format_diag verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain a JSON object")
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


def parse_size(value: str) -> int:
    match = re.fullmatch(r"\s*(0x[0-9a-fA-F]+|[0-9]+)\s*([KkMm]?)\s*", value)
    require(match is not None, f"invalid size: {value!r}")
    multiplier = {"": 1, "k": 1024, "m": 1024 * 1024}[match.group(2).lower()]
    return int(match.group(1), 0) * multiplier


def checked_build_file(build_dir: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    path = (build_dir / relative).resolve()
    require(path.is_relative_to(build_dir), f"{label} leaves build directory")
    require(path.is_file(), f"missing {label}: {path}")
    return path


def verify_source_inventory(evidence: dict) -> None:
    require(evidence.get("source_inventory") == EXPECTED_SOURCE_INVENTORY,
            "recorded source inventory differs from verifier")
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = ROOT / relative
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source hash differs: {relative}")


def verify_source_policy() -> None:
    source = (APP_DIR / "main/sd_format_diag_main.c").read_text()
    required = (
        "#define FORMAT_SDMMC_SLOT SDMMC_HOST_SLOT_0",
        "#define FORMAT_SDMMC_BUS_WIDTH 1",
        "#define FORMAT_SDMMC_CLK GPIO_NUM_43",
        "#define FORMAT_SDMMC_CMD GPIO_NUM_44",
        "#define FORMAT_SDMMC_D0 GPIO_NUM_39",
        "#define FORMAT_FREQUENCY_KHZ 1000",
        "host.flags = SDMMC_HOST_FLAG_1BIT | SDMMC_HOST_FLAG_DEINIT_ARG",
        "slot.d1 = GPIO_NUM_NC",
        "slot.d7 = GPIO_NUM_NC",
        "slot.cd = GPIO_NUM_NC",
        "slot.wp = GPIO_NUM_NC",
        "const LBA_t partitions[] = {100U, 0U, 0U, 0U}",
        "f_fdisk(physical_drive, partitions, work)",
        ".fmt = FM_FAT32",
        ".n_fat = 2U",
        "f_mkfs(drive, &options, work, FORMAT_WORK_BUFFER_BYTES)",
        "*out_filesystem_type != FS_FAT32",
        "partition_end != (uint64_t)card->csd.capacity",
        "total_sectors != partition_sectors",
        "hidden_sectors != partition_lba",
        "sectors_per_cluster != FORMAT_ALLOCATION_UNIT_BYTES / 512U",
        "cluster_count < 65525U",
        "FATFS *filesystem = heap_caps_calloc(",
        "const FRESULT unmount_result = f_mount(NULL, drive, 0U)",
        "heap_caps_free(filesystem)",
        "sdmmc_card_t *raw_card = heap_caps_calloc(",
        "heap_caps_free(raw_card)",
        "heap_caps_malloc(",
        "fflush(file)",
        "fsync(fileno(file))",
        "unlink(FORMAT_VERIFY_PATH)",
        "verify_file_absent()",
        "REMOVE_PERSISTENCE_PASS absent_after_remount=true",
        "COMPLETE filesystem=FAT32 remount=pass write_read=pass",
    )
    require(all(token in source for token in required), "reviewed destructive safeguards changed")
    require("esp_vfs_fat_sdcard_format_cfg(" not in source, "unsafe mounted-card format helper is present")
    require("esp_vfs_fat_sdcard_format(" not in source, "unsafe mounted-card format helper is present")
    require("FATFS filesystem" not in source, "large FatFs object returned to formatter stack")
    require("sdmmc_card_t raw_card" not in source, "large SD card object returned to app_main stack")
    require(source.count("verify_write_read_remove(expected_sha256)") == 2,
            "formatter must perform two file verification cycles")

    defaults = (APP_DIR / "sdkconfig.defaults").read_text()
    require("CONFIG_SD_FORMAT_DIAG_DESTRUCTIVE_AUTHORIZED=y" in defaults,
            "destructive compile gate is disabled in reviewed defaults")
    require("CONFIG_ESPTOOLPY_AFTER_NORESET=y" in defaults,
            "flash would reset and start the formatter before application readback")
    require(
        f"CONFIG_ESP_MAIN_TASK_STACK_SIZE={EXPECTED_MAIN_TASK_STACK_BYTES}" in defaults,
        "reviewed defaults do not reserve a safe formatter main-task stack",
    )
    require((APP_DIR / "main/Kconfig.projbuild").is_file(), "destructive Kconfig is not component-scoped")
    require(not (APP_DIR / "Kconfig.projbuild").exists(), "stale root Kconfig would not be loaded")

    flash = (ROOT / "scripts/flash.sh").read_text()
    require(
        'if [ "$P4_APP" = sd_format_diag ]; then' in flash
        and '"$P4_SCRIPT_DIR/verify-sd-format-diag.py"' in flash
        and '"$P4_BUILD_DIR" "$P4_FLASH_TARGET"' in flash,
        "central flash path is not bound to the formatter verifier",
    )
    ordered_flash_tokens = (
        'P4_READBACK_AFTER=no_reset',
        'if ! p4_verify_chunked_application_readback',
        '"$P4_BUILT_APP_PATH" "$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_BYTES"',
        'if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then',
        "Application readback verified:",
        'P4_RUN_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" run 2>&1)',
        "launched exactly once after successful application readback.",
    )
    positions = [flash.find(token) for token in ordered_flash_tokens]
    require(
        all(position >= 0 for position in positions)
        and positions == sorted(positions)
        and '"$P4_PORT" "$P4_READBACK_AFTER" "$P4_READBACK_BAUDS"' in flash
        and 'P4_READBACK_CHUNK_BYTES=524288' in (
            ROOT / "scripts/lib/app-readback.sh"
        ).read_text(),
        "formatter execution is not deferred until after exact application readback",
    )


def verify_metadata(mode: str) -> None:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "sd_format_diag", "wrong app metadata")
    require(metadata.get("evidence_class") == "build-tested", "metadata is not build-tested")
    require(metadata.get("authorization_evidence") == AUTHORIZATION_REL, "wrong one-shot authorization")
    require(metadata.get("electrical_evidence") == ELECTRICAL_REL, "wrong electrical evidence")
    require(metadata.get("build_evidence") == BUILD_EVIDENCE_REL, "wrong build evidence")
    require(metadata.get("runtime_supported") is True, "runtime-supported policy changed")
    require(metadata.get("game_data_embedded") is False, "formatter embeds game data")
    require(metadata.get("flash_authorized") is False, "legacy broad flash must remain false")
    require(metadata.get("flash_project_authorized") is False, "full-project flash must remain false")
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False,
                "build-only verification requires app-only flash false")
    else:
        require(metadata.get("flash_app_authorized") is True,
                "app-only flash has not been explicitly authorized")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES,
            "metadata interface scope changed")
    require(EXPECTED_ABSENT <= set(metadata.get("hardware_interfaces_explicitly_absent", [])),
            "metadata interface denials changed")
    mutation = metadata.get("storage_mutation_policy", {})
    require(
        mutation.get("scope") == "inserted_j5_micro_sd_only"
        and mutation.get("destructive") is True
        and mutation.get("repartition") is True
        and mutation.get("format") == "FAT32"
        and mutation.get("verify_file_write_read_remove") is True
        and mutation.get("remount_verify") is True
        and mutation.get("external_spi_flash_mutation") is False,
        "metadata mutation boundary changed",
    )


def verify_authorization() -> None:
    authorization = load_json(ROOT / AUTHORIZATION_REL)
    require(
        authorization.get("schema") == 1
        and authorization.get("classification") == "user-authorized-destructive-one-shot"
        and authorization.get("result") == "authorized-pending-exact-app-preflash-review"
        and authorization.get("scope") == "inserted_j5_micro_sd_only",
        "one-shot destructive authorization changed",
    )
    inherited = authorization.get("inherited_electrical_path", {})
    require(
        inherited.get("evidence") == ELECTRICAL_REL
        and inherited.get("interface") == "SDMMC0"
        and inherited.get("connector") == "J5"
        and inherited.get("bus_width") == 1
        and inherited.get("maximum_clock_khz") == 1000
        and inherited.get("gpio") == {"clock": 43, "command": 44, "data0": 39},
        "one-shot electrical scope changed",
    )
    boundary = authorization.get("exception_boundary", {})
    require(
        boundary.get("supersedes_read_only_policy_for_this_app_once") is True
        and boundary.get("reusable_platform_storage_policy_unchanged") is True
        and boundary.get("external_spi_flash_data_partition_mutation") is False
        and boundary.get("application_flash_scope") == "factory_app_partition_only_at_0x10000"
        and boundary.get("full_project_flash_authorized") is False,
        "one-shot exception boundary changed",
    )

    electrical = load_json(ROOT / ELECTRICAL_REL)
    invariant = electrical.get("cross_revision_invariants", {}).get("controller", {})
    require(
        electrical.get("classification") == "cross-revision-storage-path"
        and electrical.get("result") == "pass"
        and invariant.get("host") == "SDMMC"
        and invariant.get("slot") == 0
        and invariant.get("bus_width") == 1
        and invariant.get("maximum_clock_khz") == 10000,
        "underlying electrical path changed",
    )


def verify_build(build_dir: pathlib.Path, mode: str) -> None:
    verify_metadata(mode)
    verify_authorization()
    verify_source_policy()

    evidence = load_json(ROOT / BUILD_EVIDENCE_REL)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification") == "build-tested"
        and evidence.get("result") == "pass"
        and evidence.get("scope") == "one-shot-destructive-j5-sd-fat32-format",
        "build evidence is not the passing formatter record",
    )
    require(evidence.get("authorization_evidence") == AUTHORIZATION_REL, "build authorization changed")
    require(evidence.get("electrical_evidence") == ELECTRICAL_REL, "build electrical evidence changed")
    verify_source_inventory(evidence)

    lock = load_json(ROOT / "toolchain.lock.json")
    toolchain = evidence.get("toolchain", {})
    require(toolchain.get("esp_idf_version") == lock["esp_idf"]["version"], "IDF version differs")
    require(toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"], "IDF commit differs")
    target = lock["target"]
    recorded = evidence.get("build", {})
    require(
        recorded.get("target") == target["chip"]
        and recorded.get("minimum_revision_full") == target["min_revision_full"]
        and recorded.get("maximum_revision_full") == target["max_revision_full"],
        "recorded target or revision differs from lock",
    )
    require(
        recorded.get("reproducible_build") is True
        and recorded.get("compile_time_date_enabled") is False
        and recorded.get("fresh_independent_build_directories_compared", 0) >= 2
        and recorded.get("committed_defaults_only_build_compared") is True
        and recorded.get("identical_binary") is True
        and recorded.get("identical_elf") is True
        and recorded.get("destructive_formatter_kconfig_enabled") is True
        and recorded.get("main_task_stack_bytes") == EXPECTED_MAIN_TASK_STACK_BYTES
        and recorded.get("app_main_stack_frame_bytes") == 352
        and recorded.get("large_formatter_state_heap_allocated") is True
        and recorded.get("global_pin_map_authorized") is False,
        "reproducibility or authorization proof changed",
    )

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True, "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True, "compile-time date is enabled")
    require(sdkconfig.get("SD_FORMAT_DIAG_DESTRUCTIVE_AUTHORIZED") is True,
            "generated build omits destructive formatter")
    require(
        sdkconfig.get("ESP_MAIN_TASK_STACK_SIZE") == EXPECTED_MAIN_TASK_STACK_BYTES,
        "generated formatter main-task stack is not the reviewed safe size",
    )
    require(
        sdkconfig.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdkconfig.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated build would launch the formatter before readback",
    )
    require(sdkconfig.get("ESPTOOLPY_FLASHSIZE_16MB") is True, "generated flash size changed")

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs")
    require(int(description.get("min_rev")) == target["min_revision_full"], "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"], "generated maximum revision differs")

    flash_args = load_json(build_dir / "flasher_args.json")
    require(flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
            "flash after-action must be no_reset so readback precedes formatter execution")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == EXPECTED_APP_OFFSET, "application offset differs")
    binary = checked_build_file(build_dir, app.get("file"), "application binary")
    elf = checked_build_file(build_dir, description.get("app_elf"), "application ELF")

    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    factory_apps = [
        item for item in manifest.get("factory_partition_table", [])
        if item.get("type") == "app" and item.get("subtype") == "factory"
    ]
    require(len(factory_apps) == 1, "saved factory application is ambiguous")
    capacity = parse_size(factory_apps[0]["size"])
    require(int(factory_apps[0]["offset"], 0) == EXPECTED_APP_OFFSET, "saved app offset differs")

    artifacts = evidence.get("artifacts", {})
    require(artifacts.get("application_offset") == EXPECTED_APP_OFFSET, "recorded app offset differs")
    require(artifacts.get("saved_factory_app_partition_bytes") == capacity, "recorded app capacity differs")
    require(binary.stat().st_size <= capacity, "application exceeds saved factory app partition")
    require(binary.stat().st_size == artifacts.get("app_binary_bytes"), "binary size differs")
    require(sha256_file(binary) == artifacts.get("app_binary_sha256"), "binary hash differs")
    require(elf.stat().st_size == artifacts.get("elf_bytes"), "ELF size differs")
    require(sha256_file(elf) == artifacts.get("elf_sha256"), "ELF hash differs")

    policy = evidence.get("policy", {})
    require(
        policy.get("user_destructive_authorization_recorded") is True
        and policy.get("reusable_platform_storage_read_only_policy_unchanged") is True
        and policy.get("app_flash_authorized") is False
        and policy.get("full_project_flash_authorized") is False
        and policy.get("hardware_accessed_during_build") is False
        and policy.get("hardware_execution_recorded") is False,
        "recorded preflash policy changed",
    )
    print(
        "sd_format_diag verification: PASS "
        f"mode={mode} offset=0x{EXPECTED_APP_OFFSET:x} bytes={binary.stat().st_size} "
        f"sha256={artifacts['app_binary_sha256']} target=inserted_j5_micro_sd"
    )


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-sd-format-diag.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "target must be build-only or app-flash")
    verify_build(build_dir, mode)


if __name__ == "__main__":
    main()
