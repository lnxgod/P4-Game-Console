#!/usr/bin/env python3

"""Verify the exact reviewed storage diagnostic for build or app-flash use."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/storage_diag"
ELECTRICAL_REL = "hardware/evidence/elecrow-10.1-storage-path.json"
BUILD_EVIDENCE_REL = "test-runs/2026-08-12-storage-d1-build.json"
EXPECTED_INTERFACES = {
    "sdmmc_slot_0_1bit",
    "gpio43_sd_clk",
    "gpio44_sd_cmd",
    "gpio39_sd_d0",
}
EXPECTED_ABSENT = {
    "sd_d1_through_d7",
    "sd_card_detect",
    "sd_write_protect",
    "display",
    "touch",
    "audio",
    "camera",
    "wireless",
    "usb_host",
}
EXPECTED_SOURCE_INVENTORY = {
    "apps/storage_diag/CMakeLists.txt": "4c5ef28e086c5cacc700163e70f2a97230e908c107b18e031c738dbed9694e5c",
    "apps/storage_diag/main/CMakeLists.txt": "f70bbd90651d1e990f73a54a20b02b56b6f1310c6c9b8892ce9d62d977d82c6a",
    "apps/storage_diag/main/storage_diag_main.c": "9d261193581adcee3dba9fe78bf791b34550bf2aa9f7b484f617925a67dc9b6f",
    "apps/storage_diag/sdkconfig.defaults": "26f7106304e8cf35d77afa4c58fc2c3df6e4e0bd9df80a5973edb1567fa4e103",
    "components/platform_storage/CMakeLists.txt": "2d1c0f8a5550912a9b8af3318cd682c5ff4e62fb738882586faa2c0c72c6ed3b",
    "components/platform_storage/Kconfig": "b57a86f21e06f54574e0c1702064382b525b32c7d78298b8d51f029422d3fb0f",
    "components/platform_storage/include/platform/storage.h": "cfabf13d9c959549945e33d1fa44ab0e527d82258f3a692cf0f03a79c657dc42",
    "components/platform_storage/src/platform_storage.c": "5458ad657e3d5d0f9d448800cb4caa0ffde4041d57a3e6cf65b8df0d05fd7aed",
    "components/platform_storage/src/platform_storage_wad.c": "80e3b958ff767b0bdf80de777f5550098a19d5366963923d178fcd56e1c88ec4",
    "components/platform_storage/src/platform_storage_wad.h": "ac84f321b855d45b8a263596de0cfdbf958ac877775dddbff062367df8e9c3ff",
}


def fail(message: str) -> None:
    raise SystemExit(f"storage_diag build verification failed: {message}")


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


def strip_c_comments_and_strings(text: str) -> str:
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(" ", text)


def verify_source_scope() -> None:
    component_dir = ROOT / "components/platform_storage"
    sources = sorted(component_dir.rglob("*.c")) + sorted(component_dir.rglob("*.h"))
    sources += sorted((APP_DIR / "main").glob("*.c"))
    require(sources, "storage source files are missing")
    executable_text = "\n".join(strip_c_comments_and_strings(path.read_text()) for path in sources)
    forbidden = ("fwrite(", "unlink(", "remove(", "rename(", "mkdir(", "rmdir(")
    require(not any(token in executable_text for token in forbidden), "storage diagnostic contains file mutation")

    source = (component_dir / "src/platform_storage.c").read_text()
    required = (
        "#define STORAGE_SDMMC_SLOT SDMMC_HOST_SLOT_0",
        "#define STORAGE_SDMMC_FREQUENCY_KHZ 10000",
        "#define STORAGE_SDMMC_BUS_WIDTH 1",
        "#define STORAGE_SDMMC_CLK GPIO_NUM_43",
        "#define STORAGE_SDMMC_CMD GPIO_NUM_44",
        "#define STORAGE_SDMMC_D0 GPIO_NUM_39",
        ".format_if_mount_failed = false",
        "slot.d1 = GPIO_NUM_NC",
        "slot.d7 = GPIO_NUM_NC",
        "slot.cd = GPIO_NUM_NC",
        "slot.wp = GPIO_NUM_NC",
        "fopen(path, \"rb\")",
        "vTaskDelay(1U)",
    )
    require(all(token in source for token in required), "reviewed storage constants or safeguards changed")
    header = (component_dir / "include/platform/storage.h").read_text()
    require(
        '#define PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH "/sdcard/doom1.wad"' in header
        and '#define PLATFORM_STORAGE_FALLBACK_DOOM_WAD_PATH "/sdcard/doom/doom1.wad"' in header,
        "absolute WAD search order changed",
    )


def verify_source_inventory(build_evidence: dict) -> None:
    recorded = build_evidence.get("source_inventory")
    require(recorded == EXPECTED_SOURCE_INVENTORY, "recorded source inventory differs from verifier")
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = ROOT / relative
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source hash differs: {relative}")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-storage-diag.py <build-dir> <build-only|app-flash>")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "target must be build-only or app-flash")

    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "storage_diag", "wrong app metadata")
    require(metadata.get("evidence_class") == "build-tested", "metadata is not build-tested")
    require(metadata.get("authorization_evidence") == ELECTRICAL_REL, "wrong electrical evidence")
    require(metadata.get("build_evidence") == BUILD_EVIDENCE_REL, "wrong build evidence")
    require(metadata.get("game_data_embedded") is False, "metadata claims embedded game data")
    require(metadata.get("flash_authorized") is False, "legacy broad flash flag must remain false")
    require(metadata.get("flash_project_authorized") is False, "full-project flash must remain false")
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False, "build-only mode requires app flash to remain false")
    else:
        require(metadata.get("flash_app_authorized") is True, "app-only flash has not been explicitly authorized")
    require(set(metadata.get("hardware_interfaces", [])) == EXPECTED_INTERFACES, "metadata interface scope changed")
    require(EXPECTED_ABSENT <= set(metadata.get("hardware_interfaces_explicitly_absent", [])), "metadata denials changed")

    profile = load_json(ROOT / "hardware/board-profile.json")
    storage = profile.get("peripheral_authorizations", {}).get("storage", {})
    require(profile.get("pin_map_authorized") is False, "global pin map must remain locked")
    require(storage.get("authorized") is True, "scoped storage path is disabled")
    require(
        storage.get("scope") == "sdmmc_read_only_service_no_format_cross_revision_v1_0_through_v1_2",
        "profile storage scope changed",
    )
    require(storage.get("evidence") == ELECTRICAL_REL, "profile points to different storage evidence")
    require(storage.get("hardware_test") is None, "profile claims an unrecorded hardware test")

    electrical = load_json(ROOT / ELECTRICAL_REL)
    require(
        electrical.get("schema") == 1
        and electrical.get("classification") == "cross-revision-storage-path"
        and electrical.get("result") == "pass"
        and electrical.get("scope") == "sdmmc_read_only_service_no_format",
        "electrical evidence is not a passing scoped storage result",
    )
    require(electrical.get("authorization", {}).get("authorized") is True, "storage evidence is not authorized")
    require(
        electrical.get("official_source", {}).get("hardware_revisions_compared") == ["V1.0", "V1.1", "V1.2"],
        "published revision coverage changed",
    )

    build_evidence = load_json(ROOT / BUILD_EVIDENCE_REL)
    require(
        build_evidence.get("schema") == 1
        and build_evidence.get("classification") == "build-tested"
        and build_evidence.get("result") == "pass"
        and build_evidence.get("scope") == "storage-read-only",
        "reviewed build evidence is not a passing storage-only result",
    )
    require(build_evidence.get("authorization_evidence") == ELECTRICAL_REL, "build uses different electrical evidence")
    verify_source_inventory(build_evidence)

    lock = load_json(ROOT / "toolchain.lock.json")
    recorded_toolchain = build_evidence.get("toolchain", {})
    require(recorded_toolchain.get("esp_idf_version") == lock["esp_idf"]["version"], "IDF version differs from lock")
    require(recorded_toolchain.get("esp_idf_commit") == lock["esp_idf"]["git_commit"], "IDF commit differs from lock")
    target = lock["target"]
    recorded_build = build_evidence.get("build", {})
    require(recorded_build.get("target") == target["chip"], "recorded target differs from lock")
    require(recorded_build.get("minimum_revision_full") == target["min_revision_full"], "recorded minimum revision differs")
    require(recorded_build.get("maximum_revision_full") == target["max_revision_full"], "recorded maximum revision differs")
    require(
        recorded_build.get("reproducible_build") is True
        and recorded_build.get("compile_time_date_enabled") is False
        and recorded_build.get("independent_build_directories_compared", 0) >= 2
        and recorded_build.get("identical_binary") is True
        and recorded_build.get("identical_elf") is True,
        "reviewed evidence lacks two-build reproducibility proof",
    )
    require(recorded_build.get("scoped_storage_authorization_enabled") is True, "scoped Kconfig was not recorded")
    require(recorded_build.get("global_pin_map_authorized") is False, "build evidence claims a global pin map")

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True, "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True, "compile-time date is enabled")
    require(
        sdkconfig.get("PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
        "scoped storage Kconfig is disabled",
    )

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs from lock")
    require(int(description.get("min_rev")) == target["min_revision_full"], "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"], "generated maximum revision differs")

    flash_args = load_json(build_dir / "flasher_args.json")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000, "application offset is not 0x10000")
    binary = checked_build_path(build_dir, app.get("file"), "application binary")
    elf = checked_build_path(build_dir, description.get("app_elf"), "application ELF")

    manifest = load_json(ROOT / "hardware/backups/manifest.json")
    factory_apps = [
        entry for entry in manifest.get("factory_partition_table", [])
        if entry.get("type") == "app" and entry.get("subtype") == "factory"
    ]
    require(len(factory_apps) == 1 and int(factory_apps[0]["offset"], 0) == 0x10000, "saved factory app is ambiguous")
    require(factory_apps[0].get("size") == "11M", "saved factory app capacity changed")

    artifacts = build_evidence.get("artifacts", {})
    require(artifacts.get("saved_factory_app_partition_bytes") == 11 * 1024 * 1024, "wrong saved factory capacity")
    require(binary.stat().st_size < artifacts["saved_factory_app_partition_bytes"], "binary exceeds saved factory app capacity")
    require(binary.stat().st_size == artifacts.get("app_binary_bytes"), "binary size differs from reviewed artifact")
    require(sha256_file(binary) == artifacts.get("app_binary_sha256"), "binary hash differs from reviewed artifact")
    require(elf.stat().st_size == artifacts.get("elf_bytes"), "ELF size differs from reviewed artifact")
    require(sha256_file(elf) == artifacts.get("elf_sha256"), "ELF hash differs from reviewed artifact")

    wad = build_evidence.get("wad_policy", {})
    require(wad.get("bytes_committed") is False and wad.get("bytes_embedded") is False, "build evidence includes WAD bytes")
    require(
        wad.get("size_bytes") == 4196020
        and wad.get("sha256") == "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771",
        "reviewed WAD identity changed",
    )

    verify_source_scope()
    print(
        "storage_diag verification: PASS "
        f"mode={mode} offset=0x10000 bytes={binary.stat().st_size} "
        f"sha256={artifacts['app_binary_sha256']}"
    )


if __name__ == "__main__":
    main()
