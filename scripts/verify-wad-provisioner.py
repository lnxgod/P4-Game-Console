#!/usr/bin/env python3

"""Fail-closed configure, build, and app-flash checks for the local WAD provisioner."""

from __future__ import annotations

import csv
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/wad_provisioner"
LOCAL_WAD_REL = "local-data/doom/doom1.wad"
PARTITIONS_REL = "apps/wad_provisioner/partitions.csv"
ELECTRICAL_REL = "hardware/evidence/elecrow-10.1-storage-path.json"
FORMAT_HARDWARE_TEST_REL = "hardware/test-runs/2026-08-12-sd-format-diag-d1.json"
BUILD_EVIDENCE_REL = "test-runs/2026-08-12-wad-provisioner-d1-build.json"
EXPECTED_WAD_BYTES = 4_196_020
EXPECTED_WAD_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
EXPECTED_APP_OFFSET = 0x10000
EXPECTED_SOURCE_INVENTORY = {
    "apps/wad_provisioner/CMakeLists.txt": "69acf4e8f6c6082be2c07f8e812552c16f1ea7c263bde518d8b9023ac5ef8e65",
    "apps/wad_provisioner/main/CMakeLists.txt": "dc00a0dada1b23e5211e949d68c02b8e543a55f937d0bf23e5a4533c8374c6dc",
    "apps/wad_provisioner/main/wad_provisioner_main.c": "873091670b9a96b98a67b7546be0f283daf131df87afde1140fac3de44142881",
    "apps/wad_provisioner/partitions.csv": "99c9a328dae43e6b6346bcf08b67c9114bfa3813398295f53753b49c1c6eafe1",
    "apps/wad_provisioner/sdkconfig.defaults": "dc95b3a8fc3bc7642b992d3847961026769840b3c85dd2cbafd7c9cdb3dc41d1",
    "components/platform_storage/CMakeLists.txt": "2d1c0f8a5550912a9b8af3318cd682c5ff4e62fb738882586faa2c0c72c6ed3b",
    "components/platform_storage/Kconfig": "b57a86f21e06f54574e0c1702064382b525b32c7d78298b8d51f029422d3fb0f",
    "components/platform_storage/include/platform/storage.h": "14232433d13029884a8f2a2c8b216bec06333ad0c9ea905e27f40aa610bec6be",
    "components/platform_storage/src/platform_storage.c": "6c6499974ccf6eeee09b81fba8a5b49e15f2ecdb15cb6b90c5543f7ade3733b9",
    "components/platform_storage/src/platform_storage_wad.c": "80e3b958ff767b0bdf80de777f5550098a19d5366963923d178fcd56e1c88ec4",
    "components/platform_storage/src/platform_storage_wad.h": "ac84f321b855d45b8a263596de0cfdbf958ac877775dddbff062367df8e9c3ff",
}


def fail(message: str) -> None:
    raise SystemExit(f"wad_provisioner verification failed: {message}")


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
    number = int(match.group(1), 0)
    multiplier = {"": 1, "k": 1024, "m": 1024 * 1024}[match.group(2).lower()]
    return number * multiplier


def factory_partition_from_reviewed_source() -> tuple[int, int]:
    # The committed build layout is the recovery source, independent of any
    # captured firmware. The flash route checks the actual live partition.
    partition = factory_partition_from_csv(ROOT / "apps/wad_provisioner/partitions.csv")
    require(partition == (0x10000, 11 * 1024 * 1024), "reviewed factory app boundary changed")
    return partition


def factory_partition_from_csv(path: pathlib.Path) -> tuple[int, int]:
    try:
        with path.open(newline="") as source:
            rows = [
                row
                for row in csv.reader(line for line in source if not line.lstrip().startswith("#"))
                if row
            ]
    except OSError as error:
        fail(f"cannot read partition CSV: {error}")
    matches = [
        row
        for row in rows
        if len(row) >= 5 and row[1].strip() == "app" and row[2].strip() == "factory"
    ]
    require(len(matches) == 1, "provisioner partition CSV must contain one factory app")
    return int(matches[0][3].strip(), 0), parse_size(matches[0][4])


def git_output(arguments: list[str]) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            ["git", "-C", str(ROOT), *arguments],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError as error:
        fail(f"git policy check could not run: {error}")


def verify_local_distribution_policy(wad_path: pathlib.Path) -> None:
    expected = (ROOT / LOCAL_WAD_REL).resolve()
    require(wad_path == expected, f"authorized WAD path must be {expected}")
    ignored = git_output(["check-ignore", "--quiet", "--", LOCAL_WAD_REL])
    require(ignored.returncode == 0, "local WAD is not ignored by Git")
    tracked = git_output(["ls-files"])
    require(tracked.returncode == 0, "could not inspect tracked-file policy")
    tracked_wads = [line for line in tracked.stdout.splitlines() if pathlib.PurePosixPath(line).suffix.lower() == ".wad"]
    # Explicit owner-authorized original map pack; never allow an IWAD here.
    allowed = {"game-data/pure-hades/v0.6/PUREHADES.WAD":
               "ab027fbeebe20787214a3bc1239bba73271030dc284cd108c20ae8bc73752fc8"}
    for name in tracked_wads:
        require(name in allowed, f"Unauthorized tracked WAD: {name}")
        require(sha256_file(ROOT / name) == allowed[name],
                f"Published Pure Hades identity changed: {name}")


def verify_input(wad_path: pathlib.Path, partitions_path: pathlib.Path) -> None:
    require(partitions_path == (ROOT / PARTITIONS_REL).resolve(), "authorized partition CSV path changed")
    verify_local_distribution_policy(wad_path)
    require(wad_path.is_file(), f"local WAD is missing: {wad_path}")
    require(wad_path.stat().st_size == EXPECTED_WAD_BYTES, "local WAD byte count differs")
    require(sha256_file(wad_path) == EXPECTED_WAD_SHA256, "local WAD SHA-256 differs")
    reviewed_partition = factory_partition_from_reviewed_source()
    csv_partition = factory_partition_from_csv(partitions_path)
    require(csv_partition == reviewed_partition, "custom factory partition differs from reviewed source layout")
    require(csv_partition[0] == EXPECTED_APP_OFFSET, "factory app offset is not 0x10000")
    require(EXPECTED_WAD_BYTES < csv_partition[1], "WAD alone cannot fit in factory app partition")
    print(
        "WAD provisioner configure gate: PASS "
        f"wad_bytes={EXPECTED_WAD_BYTES} wad_sha256={EXPECTED_WAD_SHA256} "
        f"app_offset=0x{csv_partition[0]:x} app_capacity={csv_partition[1]}"
    )


def checked_build_file(build_dir: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    path = (build_dir / relative).resolve()
    require(path.is_relative_to(build_dir), f"{label} leaves build directory")
    require(path.is_file(), f"missing {label}: {path}")
    return path


def elf_nm(description: dict) -> str:
    compiler = description.get("c_compiler")
    candidates: list[pathlib.Path] = []
    if isinstance(compiler, str) and compiler:
        candidates.append(pathlib.Path(compiler).with_name("riscv32-esp-elf-nm"))
    discovered = shutil.which("riscv32-esp-elf-nm")
    if discovered is not None:
        candidates.append(pathlib.Path(discovered))
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    fail("riscv32-esp-elf-nm was not found beside the recorded compiler or on PATH")


def strip_c_comments_and_strings(text: str) -> str:
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(" ", text)


def verify_source_inventory(build_evidence: dict) -> None:
    recorded = build_evidence.get("source_inventory")
    require(recorded == EXPECTED_SOURCE_INVENTORY, "recorded source inventory differs from verifier")
    for relative, expected_hash in EXPECTED_SOURCE_INVENTORY.items():
        path = ROOT / relative
        require(path.is_file(), f"reviewed source is missing: {relative}")
        require(sha256_file(path) == expected_hash, f"reviewed source hash differs: {relative}")


def verify_source_policy() -> None:
    source = (APP_DIR / "main/wad_provisioner_main.c").read_text()
    required = (
        "O_WRONLY | O_CREAT | O_EXCL",
        "fsync(descriptor)",
        "platform_storage_inspect_wad(PROVISION_TEMP_PATH",
        "path_absent(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH",
        "rename(PROVISION_TEMP_PATH, PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH)",
        "platform_storage_inspect_wad(PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH",
        "vTaskDelay(1U)",
        'expected_name = "DOOM1.WAD"',
        'expected_name = "P4WAD.TMP"',
        "opendir(PLATFORM_STORAGE_MOUNT_POINT)",
        "readdir(root)",
        "strcasecmp(entry->d_name, expected_name)",
    )
    require(all(token in source for token in required), "provisioning safeguards changed")
    executable = strip_c_comments_and_strings(source)
    require(executable.count("rename(") == 1, "provisioner must contain exactly one rename")
    forbidden = (
        "O_TRUNC",
        "unlink(",
        "remove(",
        "rmdir(",
        "mkdir(",
        "ftruncate(",
        "esp_vfs_fat_sdcard_format(",
        "esp_vfs_fat_sdcard_format_cfg(",
        "f_mkfs(",
        "f_fdisk(",
    )
    require(not any(token in executable for token in forbidden), "provisioner contains overwrite, delete, or format code")

    storage_source = (ROOT / "components/platform_storage/src/platform_storage.c").read_text()
    require(".format_if_mount_failed = false" in storage_source, "storage mount may format on failure")
    require("#define STORAGE_SDMMC_FREQUENCY_KHZ 1000" in storage_source,
            "storage is not using the connected unit's proven 1 MHz operating point")
    storage_header = (ROOT / "components/platform_storage/include/platform/storage.h").read_text()
    require(
        '#define PLATFORM_STORAGE_PRIMARY_DOOM_WAD_PATH "/sdcard/DOOM1.WAD"' in storage_header
        and '#define PLATFORM_STORAGE_FALLBACK_DOOM_WAD_PATH "/sdcard/DOOM/DOOM1.WAD"' in storage_header,
        "runtime WAD paths are not uppercase 8.3-compatible",
    )

    defaults = (APP_DIR / "sdkconfig.defaults").read_text()
    require("CONFIG_ESPTOOLPY_AFTER_NORESET=y" in defaults,
            "provisioner would start before exact application readback")

    cmake = (APP_DIR / "CMakeLists.txt").read_text()
    require(
        "target_add_binary_data(" in cmake
        and 'RENAME_TO p4_provision_wad' in cmake
        and "verify-wad-provisioner.py" in cmake,
        "local binary-data/configure gate changed",
    )

    flash_script = (ROOT / "scripts/flash.sh").read_text()
    require(
        'if [ "$P4_APP" = wad_provisioner ]; then' in flash_script
        and '"$P4_SCRIPT_DIR/verify-wad-provisioner.py"' in flash_script
        and '"$P4_BUILD_DIR" "$P4_FLASH_TARGET"' in flash_script,
        "central flash path is not bound to the provisioner verifier",
    )
    deferred_apps = (
        'if [ "$P4_APP" = sd_format_diag ] || \\\n'
        '   [ "$P4_APP" = wad_provisioner ] || \\\n'
        '   [ "$P4_APP" = doom_embedded ] || \\\n'
        '   [ "$P4_APP" = audio_diag ]; then'
    )
    require(flash_script.count(deferred_apps) == 2,
            "provisioner is not in both deferred readback and launch scopes")
    require(
        'P4_RESUME_READBACK=false' in flash_script
        and '--resume-readback) P4_RESUME_READBACK=true; shift ;;' in flash_script
        and '[ "$P4_RESUME_READBACK" != app-flash ]' not in flash_script
        and '[ "$P4_FLASH_TARGET" != app-flash ]' in flash_script
        and '[ "$P4_APP" != wad_provisioner ]' in flash_script,
        "readback-only resume scope changed",
    )
    require(
        'P4_PROBE_AFTER=no_reset' in flash_script
        and 'p4_read_device_identity_hash "$P4_PORT" "$P4_PROBE_AFTER"' in flash_script
        and '--after "$P4_PROBE_AFTER" flash_id' in flash_script
        and '--after "$P4_PROBE_AFTER" get_security_info' in flash_script,
        "resume preflight could launch the installed provisioner before readback",
    )
    ordered_flash_tokens = (
        'if [ "$P4_RESUME_READBACK" = true ]; then\n'
        "    printf 'Resume mode: application write skipped; verifying existing flash before deferred launch.",
        'else\n    P4_FLASH_OUTPUT=$(idf.py',
        deferred_apps,
        'P4_READBACK_AFTER=no_reset',
        "P4_READBACK_BAUDS='460800 230400 115200'",
        "P4_READBACK_BAUDS='921600 460800 230400'",
        'if ! p4_verify_chunked_application_readback',
        '"$P4_BUILT_APP_PATH" "$P4_BUILT_APP_OFFSET" "$P4_BUILT_APP_BYTES"',
        'image was not launched.',
        'if [ "$P4_READBACK_HASH" != "$P4_BUILT_APP_HASH" ]; then',
        'Ordered application readback SHA-256 mismatch; image was not launched.',
        'Application readback verified:',
        deferred_apps,
        'P4_RUN_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" run 2>&1)',
        'Verified %s launched exactly once after successful application readback.',
    )
    cursor = 0
    for token in ordered_flash_tokens:
        position = flash_script.find(token, cursor)
        require(position >= 0, "provisioner launch is not deferred until after exact readback")
        cursor = position + len(token)
    readback = (ROOT / "scripts/lib/app-readback.sh").read_text()
    helper = (ROOT / "scripts/verify-readback-chunks.py").read_text()
    require(
        "P4_READBACK_CHUNK_BYTES=524288" in readback
        and "for P4_READBACK_BAUD in $P4_READBACK_BAUDS; do" in readback
        and "read_flash \\" in readback
        and '"$P4_READBACK_THIS_OFFSET" "$P4_READBACK_THIS_BYTES"' in readback
        and 'mv "$P4_READBACK_ATTEMPT" "$P4_READBACK_CANONICAL"' in readback
        and 'verify-readback-chunks.py" chunk' in readback
        and 'verify-readback-chunks.py" aggregate' in readback
        and 'actual_names != expected_names' in helper
        and 'for index in range(args.chunks)' in helper,
        "central readback is not bounded, per-chunk verified, and ordered",
    )


def verify_metadata(mode: str) -> dict:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "wad_provisioner", "wrong app metadata")
    require(metadata.get("evidence_class") == "build-tested", "metadata is not build-tested")
    require(metadata.get("authorization_evidence") == ELECTRICAL_REL, "wrong electrical evidence")
    require(metadata.get("build_evidence") == BUILD_EVIDENCE_REL, "wrong build evidence")
    require(metadata.get("runtime_supported") is True, "runtime support policy changed")
    require(metadata.get("flash_authorized") is False, "legacy broad flash flag must remain false")
    require(metadata.get("flash_project_authorized") is False, "full-project flash must remain false")
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False, "build-only mode requires app flash to remain false")
    else:
        require(metadata.get("flash_app_authorized") is True, "app-only flash has not been explicitly authorized")
    require(
        metadata.get("game_data_embedded") is True
        and metadata.get("redistributable") is False
        and metadata.get("local_artifact_only") is True,
        "local WAD distribution policy changed",
    )
    mutation = metadata.get("storage_mutation_policy", {})
    require(
        mutation.get("format") is False
        and mutation.get("overwrite") is False
        and mutation.get("delete") is False
        and mutation.get("target") == "/sdcard/DOOM1.WAD"
        and mutation.get("temporary_path") == "/sdcard/P4WAD.TMP",
        "storage mutation policy changed",
    )
    require(
        "not-power-fail-atomic" in " ".join(mutation.get("write_sequence", []))
        and "not power-fail atomic" in str(mutation.get("power_failure_policy", "")),
        "FAT rename power-failure limitation is not explicit",
    )
    return metadata


def verify_connected_storage_operating_point() -> None:
    electrical = load_json(ROOT / ELECTRICAL_REL)
    operating = electrical.get("connected_unit_operating_point", {})
    require(
        operating.get("hardware_test") == FORMAT_HARDWARE_TEST_REL
        and operating.get("configured_maximum_clock_khz") == 1000
        and operating.get("measured_real_frequency_khz") == 1000
        and operating.get("bus_width") == 1
        and operating.get("result") == "card-init-raw-layout-and-nonformatting-fat32-remount-pass"
        and operating.get("runtime_filename_mode") == "uppercase-8.3-compatible",
        "connected-unit storage operating point changed",
    )
    hardware_test = load_json(ROOT / FORMAT_HARDWARE_TEST_REL)
    fat32 = hardware_test.get("fat32", {})
    require(
        hardware_test.get("result") == "fat32-format-pass-file-probe-name-fail"
        and hardware_test.get("card", {}).get("real_frequency_khz") == 1000
        and fat32.get("partition_result") == "FR_OK"
        and fat32.get("mkfs_result") == "FR_OK"
        and fat32.get("remount") == "ESP_OK"
        and hardware_test.get("file_probe", {}).get("errno") == 22,
        "connected-unit FAT32/remount evidence changed",
    )


def verify_build(
    build_dir: pathlib.Path,
    mode: str,
    wad_path: pathlib.Path,
    partitions_path: pathlib.Path,
) -> None:
    verify_input(wad_path, partitions_path)
    verify_metadata(mode)
    verify_connected_storage_operating_point()
    verify_source_policy()

    build_evidence = load_json(ROOT / BUILD_EVIDENCE_REL)
    require(
        build_evidence.get("schema") == 1
        and build_evidence.get("classification") == "build-tested"
        and build_evidence.get("result") == "pass"
        and build_evidence.get("scope") == "one-shot-local-shareware-wad-provisioning",
        "reviewed build evidence is not a passing provisioner result",
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
        and recorded_build.get("fresh_independent_build_directories_compared", 0) >= 1
        and recorded_build.get("build_directories_compared", 0) >= 2
        and recorded_build.get("committed_defaults_only_build_compared") is True
        and recorded_build.get("identical_binary") is True
        and recorded_build.get("identical_elf") is True
        and recorded_build.get("storage_frequency_khz") == 1000
        and recorded_build.get("runtime_filename_mode") == "uppercase-8.3-compatible"
        and recorded_build.get("flash_after_action") == "no_reset_until_exact_readback_then_explicit_run",
        "reviewed evidence lacks independent defaults-only reproducibility proof",
    )

    sdkconfig = load_json(build_dir / "config/sdkconfig.json")
    require(sdkconfig.get("APP_REPRODUCIBLE_BUILD") is True, "generated build is not reproducible")
    require(sdkconfig.get("APP_COMPILE_TIME_DATE") is not True, "compile-time date is enabled")
    require(sdkconfig.get("PARTITION_TABLE_CUSTOM") is True, "custom partition table is disabled")
    require(sdkconfig.get("PARTITION_TABLE_CUSTOM_FILENAME") == "partitions.csv", "partition filename changed")
    require(sdkconfig.get("ESPTOOLPY_FLASHSIZE_16MB") is True, "built flash size differs")
    require(
        sdkconfig.get("ESPTOOLPY_AFTER_NORESET") is True
        and sdkconfig.get("ESPTOOLPY_AFTER") == "no_reset",
        "generated provisioner would start before exact application readback",
    )
    require(sdkconfig.get("FATFS_LFN_NONE") is True,
            "reviewed runtime filename-mode assumption changed")
    require(
        sdkconfig.get("PLATFORM_STORAGE_ELECROW_10_1_CROSS_REVISION_AUTHORIZED") is True,
        "scoped storage authorization is disabled",
    )

    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == target["chip"], "generated target differs from lock")
    require(int(description.get("min_rev")) == target["min_revision_full"], "generated minimum revision differs")
    require(int(description.get("max_rev")) == target["max_revision_full"], "generated maximum revision differs")

    flash_args = load_json(build_dir / "flasher_args.json")
    require(flash_args.get("extra_esptool_args", {}).get("after") == "no_reset",
            "flash after-action must preserve deferred provisioner launch")
    app = flash_args.get("app", {})
    require(int(str(app.get("offset")), 0) == EXPECTED_APP_OFFSET, "built app offset differs")
    binary = checked_build_file(build_dir, app.get("file"), "application binary")
    elf = checked_build_file(build_dir, description.get("app_elf"), "application ELF")
    _, capacity = factory_partition_from_reviewed_source()
    artifacts = build_evidence.get("artifacts", {})
    require(artifacts.get("application_offset") == EXPECTED_APP_OFFSET, "recorded app offset differs")
    require(artifacts.get("saved_factory_app_partition_bytes") == capacity, "recorded app capacity differs")
    require(binary.stat().st_size <= capacity, "application binary exceeds saved factory app partition")
    require(binary.stat().st_size == artifacts.get("app_binary_bytes"), "binary size differs from reviewed artifact")
    require(sha256_file(binary) == artifacts.get("app_binary_sha256"), "binary hash differs from reviewed artifact")
    require(elf.stat().st_size == artifacts.get("elf_bytes"), "ELF size differs from reviewed artifact")
    require(sha256_file(elf) == artifacts.get("elf_sha256"), "ELF hash differs from reviewed artifact")

    wad_bytes = wad_path.read_bytes()
    binary_bytes = binary.read_bytes()
    occurrence_count = binary_bytes.count(wad_bytes)
    occurrence_offset = binary_bytes.find(wad_bytes)
    embedded = build_evidence.get("embedded_wad", {})
    require(occurrence_count == 1, "exact WAD bytes must occur once in the application binary")
    require(embedded.get("binary_occurrences") == 1, "recorded WAD occurrence count differs")
    require(embedded.get("binary_offset") == occurrence_offset, "recorded WAD binary offset differs")
    require(embedded.get("symbol_span_bytes") == EXPECTED_WAD_BYTES, "recorded WAD symbol span differs")
    require(
        embedded.get("size_bytes") == EXPECTED_WAD_BYTES
        and embedded.get("sha256") == EXPECTED_WAD_SHA256,
        "recorded embedded WAD identity differs",
    )

    symbol_output = subprocess.run(
        [elf_nm(description), "-n", str(elf)], check=True, capture_output=True, text=True
    ).stdout
    starts = re.findall(
        r"^([0-9a-fA-F]+)\s+\w\s+_binary_p4_provision_wad_start$",
        symbol_output,
        re.MULTILINE,
    )
    ends = re.findall(
        r"^([0-9a-fA-F]+)\s+\w\s+_binary_p4_provision_wad_end$",
        symbol_output,
        re.MULTILINE,
    )
    require(len(starts) == 1 and len(ends) == 1, "embedded WAD boundary symbols are missing or duplicated")
    require(int(ends[0], 16) - int(starts[0], 16) == EXPECTED_WAD_BYTES, "embedded WAD span differs")

    policy = build_evidence.get("policy", {})
    require(
        policy.get("wad_tracked") is False
        and policy.get("wad_ignored") is True
        and policy.get("redistributable") is False
        and policy.get("full_project_flash_authorized") is False
        and policy.get("format") is False
        and policy.get("overwrite") is False
        and policy.get("delete") is False
        and policy.get("fat_rename_power_fail_atomic") is False,
        "reviewed local/distribution/mutation policy differs",
    )
    require(
        policy.get("target_path") == "/sdcard/DOOM1.WAD"
        and policy.get("temporary_path") == "/sdcard/P4WAD.TMP"
        and policy.get("runtime_filename_mode") == "uppercase-8.3-compatible"
        and policy.get("storage_frequency_khz") == 1000
        and policy.get("flash_after_action") == "no_reset_until_exact_readback_then_explicit_run"
        and policy.get("readback_chunk_bytes_max") == 524288
        and policy.get("readback_baud_retry_sequence") == [460800, 230400, 115200]
        and policy.get("small_readback_baud_retry_sequence") == [921600, 460800, 230400]
        and policy.get("resume_readback_baud_retry_sequence") == [460800, 230400, 115200]
        and policy.get("resume_readback_rewrites_application") is False,
        "reviewed path, clock, or deferred-launch policy differs",
    )
    print(
        "WAD provisioner verification: PASS "
        f"mode={mode} offset=0x{EXPECTED_APP_OFFSET:x} app_bytes={binary.stat().st_size} "
        f"app_sha256={artifacts['app_binary_sha256']} wad_occurrences={occurrence_count}"
    )


def main() -> None:
    if len(sys.argv) == 4 and sys.argv[1] == "--configure":
        verify_input(pathlib.Path(sys.argv[2]).resolve(), pathlib.Path(sys.argv[3]).resolve())
        return
    if len(sys.argv) != 5:
        fail(
            "usage: verify-wad-provisioner.py --configure <wad> <partitions> OR "
            "verify-wad-provisioner.py <build-dir> <build-only|app-flash> <wad> <partitions>"
        )
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), f"build directory is missing: {build_dir}")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "target must be build-only or app-flash")
    verify_build(
        build_dir,
        mode,
        pathlib.Path(sys.argv[3]).resolve(),
        pathlib.Path(sys.argv[4]).resolve(),
    )


if __name__ == "__main__":
    main()
