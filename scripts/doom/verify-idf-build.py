#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Verify the ESP32-P4 Doom build-only link contract and its artifacts."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
MAP_MEMBER_RE = re.compile(r"libdoom_engine\.a\(([^)]+)\)")
SOURCE_RE = re.compile(r'"\$\{P4_DOOMGENERIC_DIR\}/([^"/]+\.c)"')


def fail(message: str) -> "None":
    raise SystemExit(f"P4_DOOM_D05 VERIFY FAIL reason={message}")


def load_object(path: pathlib.Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"invalid-json path={path} detail={error}")
    if not isinstance(value, dict):
        fail(f"expected-json-object path={path}")
    return value


def require_file(path: pathlib.Path) -> None:
    if not path.is_file():
        fail(f"missing-file path={path}")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def archive_members(archive: pathlib.Path, cmake_cache: pathlib.Path) -> set[str]:
    cache = cmake_cache.read_text(encoding="utf-8")
    match = re.search(r"^CMAKE_AR:FILEPATH=(.+)$", cache, flags=re.MULTILINE)
    archive_tool = match.group(1) if match else shutil.which("ar")
    if archive_tool is None:
        fail("archive-tool-not-found")
    try:
        result = subprocess.run(
            [archive_tool, "t", str(archive)],
            check=True,
            capture_output=True,
            encoding="utf-8",
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"archive-inspection-failed detail={error}")
    members = {line.strip() for line in result.stdout.splitlines() if line.strip()}
    if not members:
        fail("engine-archive-empty")
    return members


def main() -> None:
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: verify-idf-build.py PROJECT_ROOT [EXPECTED-EVIDENCE.json]")

    root = pathlib.Path(sys.argv[1]).resolve()
    app = root / "apps" / "doom"
    build = app / "build"
    metadata_path = app / "app-metadata.json"
    component_path = app / "components" / "doom_engine" / "CMakeLists.txt"
    sdkconfig_path = app / "sdkconfig"
    description_path = build / "project_description.json"
    compile_commands_path = build / "compile_commands.json"
    build_ninja_path = build / "build.ninja"
    map_path = build / "p4_doom_build_probe.map"
    elf_path = build / "p4_doom_build_probe.elf"
    binary_path = build / "p4_doom_build_probe.bin"
    archive_path = build / "esp-idf" / "doom_engine" / "libdoom_engine.a"
    cache_path = build / "CMakeCache.txt"

    for path in (
        metadata_path,
        component_path,
        sdkconfig_path,
        description_path,
        compile_commands_path,
        build_ninja_path,
        map_path,
        elf_path,
        binary_path,
        archive_path,
        cache_path,
    ):
        require_file(path)

    metadata = load_object(metadata_path)
    if metadata.get("app") != "doom" or metadata.get("stage") != "D0.5":
        fail("unexpected-app-metadata")
    for key in (
        "runtime_supported",
        "flash_authorized",
        "flash_app_authorized",
        "flash_project_authorized",
        "game_data_embedded",
    ):
        if metadata.get(key) is not False:
            fail(f"metadata-key-must-be-false key={key}")
    if metadata.get("hardware_interfaces") != []:
        fail("hardware-interfaces-must-be-empty")

    description = load_object(description_path)
    lock = load_object(root / "toolchain.lock.json")
    source_lock = load_object(root / "third_party" / "source-lock.json")
    target_lock = lock.get("target")
    idf_lock = lock.get("esp_idf")
    if not isinstance(target_lock, dict):
        fail("toolchain-target-missing")
    if not isinstance(idf_lock, dict):
        fail("toolchain-idf-missing")
    expected_target = target_lock.get("chip")
    expected_minimum = target_lock.get("min_revision_full")
    expected_maximum = target_lock.get("max_revision_full")
    try:
        actual_minimum = int(description.get("min_rev"))
        actual_maximum = int(description.get("max_rev"))
    except (TypeError, ValueError):
        fail("invalid-build-revision-bounds")
    if (
        description.get("target") != expected_target
        or actual_minimum != expected_minimum
        or actual_maximum != expected_maximum
    ):
        fail("build-target-or-revision-mismatch")
    if description.get("git_revision") != idf_lock.get("git_tag"):
        fail("build-idf-tag-mismatch")
    sdkconfig_text = sdkconfig_path.read_text(encoding="utf-8")
    if "CONFIG_APP_REPRODUCIBLE_BUILD=y" not in sdkconfig_text.splitlines():
        fail("reproducible-build-config-missing")

    component_text = component_path.read_text(encoding="utf-8")
    selected_sources = SOURCE_RE.findall(component_text)
    if len(selected_sources) != len(set(selected_sources)) or not selected_sources:
        fail("selected-source-list-empty-or-duplicated")
    selected_members = {f"{source}.obj" for source in selected_sources}

    compile_commands_text = compile_commands_path.read_text(encoding="utf-8")
    build_ninja_text = build_ninja_path.read_text(encoding="utf-8")
    if ".wad" in (compile_commands_text + build_ninja_text).lower():
        fail("wad-path-present-in-build-graph")

    compile_commands = json.loads(compile_commands_text)
    if not isinstance(compile_commands, list):
        fail("compile-commands-not-an-array")
    compiled_sources = {
        pathlib.Path(entry["file"]).name
        for entry in compile_commands
        if isinstance(entry, dict)
        and isinstance(entry.get("file"), str)
        and "/third_party/doomgeneric/doomgeneric/" in entry["file"]
    }
    if compiled_sources != set(selected_sources):
        fail("compiled-source-set-mismatch")

    members = archive_members(archive_path, cache_path)
    if members != selected_members:
        fail("engine-archive-member-set-mismatch")

    map_members = set(MAP_MEMBER_RE.findall(map_path.read_text(encoding="utf-8")))
    if map_members != selected_members:
        fail("whole-archive-map-coverage-mismatch")

    link_line = next(
        (
            line
            for line in build_ninja_text.splitlines()
            if line.startswith("  LINK_LIBRARIES =")
        ),
        "",
    )
    whole_archive_fragment = (
        "-Wl,--whole-archive  esp-idf/doom_engine/libdoom_engine.a  "
        "-Wl,--no-whole-archive"
    )
    if whole_archive_fragment not in link_line:
        fail("whole-archive-link-flags-missing")

    binary_size = binary_path.stat().st_size
    binary_sha256 = sha256_file(binary_path)
    elf_size = elf_path.stat().st_size
    elf_sha256 = sha256_file(elf_path)
    if SHA256_RE.fullmatch(binary_sha256) is None or SHA256_RE.fullmatch(elf_sha256) is None:
        fail("artifact-hash-invalid")

    if len(sys.argv) == 3:
        evidence = load_object(pathlib.Path(sys.argv[2]).resolve())
        artifacts = evidence.get("artifacts")
        link = evidence.get("link")
        build_evidence = evidence.get("build")
        engine_evidence = evidence.get("engine")
        toolchain_evidence = evidence.get("toolchain")
        execution_evidence = evidence.get("execution")
        game_data_evidence = evidence.get("game_data")
        source_entries = source_lock.get("sources")
        if not all(
            isinstance(value, dict)
            for value in (
                artifacts,
                link,
                build_evidence,
                engine_evidence,
                toolchain_evidence,
                execution_evidence,
                game_data_evidence,
                source_entries,
            )
        ):
            fail("evidence-or-lock-object-missing")
        doomgeneric_lock = source_entries.get("doomgeneric")
        if not isinstance(doomgeneric_lock, dict):
            fail("doomgeneric-source-lock-missing")
        if evidence.get("classification") != "build-tested" or evidence.get("result") != "pass":
            fail("evidence-classification-or-result-invalid")
        if (
            engine_evidence.get("commit") != doomgeneric_lock.get("commit")
            or engine_evidence.get("tree") != doomgeneric_lock.get("tree")
            or engine_evidence.get("vendor_files_modified") is not False
        ):
            fail("engine-evidence-mismatch")
        if (
            toolchain_evidence.get("esp_idf_version") != idf_lock.get("version")
            or toolchain_evidence.get("esp_idf_commit") != idf_lock.get("git_commit")
        ):
            fail("toolchain-evidence-mismatch")
        if any(
            execution_evidence.get(key) is not False
            for key in ("firmware_flashed", "firmware_executed", "runtime_supported")
        ):
            fail("execution-evidence-must-remain-false")
        if any(
            game_data_evidence.get(key) is not False
            for key in ("wad_files_committed", "wad_bytes_embedded")
        ):
            fail("game-data-evidence-must-remain-false")
        if (
            artifacts.get("app_binary_bytes") != binary_size
            or artifacts.get("app_binary_sha256") != binary_sha256
            or artifacts.get("elf_bytes") != elf_size
            or artifacts.get("elf_sha256") != elf_sha256
        ):
            fail("artifact-evidence-mismatch")
        if (
            link.get("selected_engine_sources") != len(selected_sources)
            or link.get("archive_members") != len(members)
            or link.get("map_members") != len(map_members)
            or link.get("whole_archive") is not True
        ):
            fail("link-evidence-mismatch")
        if (
            build_evidence.get("target") != description.get("target")
            or build_evidence.get("minimum_revision_full") != actual_minimum
            or build_evidence.get("maximum_revision_full") != actual_maximum
            or build_evidence.get("reproducible_build") is not True
            or build_evidence.get("independent_build_directories_compared") != 2
            or build_evidence.get("identical_binary") is not True
            or build_evidence.get("identical_elf") is not True
        ):
            fail("target-evidence-mismatch")

    print(
        "P4_DOOM_D05 VERIFY PASS "
        f"target={description.get('target')} revisions={actual_minimum}..{actual_maximum} "
        f"sources={len(selected_sources)} archive_members={len(members)} "
        f"map_members={len(map_members)} whole_archive=true "
        f"bin_bytes={binary_size} bin_sha256={binary_sha256} "
        f"elf_bytes={elf_size} elf_sha256={elf_sha256}"
    )


if __name__ == "__main__":
    main()
