#!/usr/bin/env python3

"""Exact fail-closed verifier for the dormant-USB E3 Doom install image."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/doom_embedded_gamepad"
LOCAL_WAD = ROOT / "local-data/doom/doom1.wad"
EVIDENCE_REL = "test-runs/2026-08-13-doom-embedded-gamepad-e3-build.json"
EVIDENCE = ROOT / EVIDENCE_REL
EVIDENCE_SHA256 = "8dbf0c25616850f551a719d7eee5e5448f9baa8b9450bd03f77425371b18677c"
AUTH_REL = "hardware/evidence/doom-embedded-gamepad-e3-one-shot-authorization.json"
AUTH = ROOT / AUTH_REL
RUNTIME_EVIDENCE_REL = "hardware/test-runs/2026-08-13-doom-embedded-gamepad-e3.json"
RUNTIME_EVIDENCE = ROOT / RUNTIME_EVIDENCE_REL
AUTH_ID = "doom-embedded-gamepad-e3-one-shot-authorization-2026-08-13"
INACTIVE_CLASS = "user-requested-doom-e3-app-only-one-shot-inactive"
ACTIVE_CLASS = "user-requested-doom-e3-app-only-one-shot-active"
CONSUMED_CLASS = "user-requested-doom-e3-app-only-one-shot-consumed-revoked"
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
EXPECTED_OFFSET = "0x10000"
EXPECTED_BINARY_BYTES = 4927824
EXPECTED_BINARY_SHA256 = (
    "32814a97b72b3b227e2ace02393222178ceebf02d18a4f0b35bead1f4584ea24"
)
EXPECTED_ELF_BYTES = 14004520
EXPECTED_ELF_SHA256 = (
    "97d0bc767d6394de9d4f9a95191fd1e3e1fa2f1e0615da8c24b9aee8e2632f36"
)
EXPECTED_BOOTLOADER_BYTES = 22912
EXPECTED_BOOTLOADER_SHA256 = (
    "fcb629f826b8cd79fb21533b8691650a775804f337db37451b03127e36a32ca9"
)
EXPECTED_PARTITION_BYTES = 3072
EXPECTED_PARTITION_SHA256 = (
    "5b5bfa656e96706d5144b352bf9294ab455a7e1e49136ea5521cf15902a9e433"
)
EXPECTED_LOCK_SHA256 = (
    "de169c6847730a99d6006199cfbe32a18d64a5a8957559b30c2dfd4bbf83039a"
)
EXPECTED_WAD_BYTES = 4196020
EXPECTED_WAD_SHA256 = (
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
)
EXPECTED_WAD_OFFSET = 155744
EXPECTED_APP_CAPACITY = 11 * 1024 * 1024
EXPECTED_PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 24 * 1024, 0),
    ("phy_init", 1, 1, 0xF000, 4 * 1024, 0),
    ("factory", 0, 0, 0x10000, EXPECTED_APP_CAPACITY, 0),
    ("storage", 1, 0x82, 0xB10000, 4 * 1024 * 1024, 0),
]
EXPECTED_RUNTIME_INTERFACES = {
    "mipi_dsi_bus_0",
    "internal_ldo_3_2500mv",
    "internal_ldo_4_3300mv",
    "gpio31_backlight_pwm",
}
REQUIRED_SYMBOLS = {
    "app_main",
    "doomgeneric_Create",
    "doomgeneric_Tick",
    "DG_GetKey",
    "doom_video_init",
    "doom_video_submit_black",
    "doom_video_submit_xrgb8888",
    "platform_display_init",
    "platform_display_set_brightness",
    "platform_readonly_blob_register",
    "platform_usb_host_start",
    "platform_usb_host_enable_root_port",
    "platform_usb_host_quiesce",
    "platform_usb_host_stop",
    "platform_gamepad_usb_start",
    "platform_gamepad_usb_get_snapshot",
    "platform_gamepad_usb_stop",
    "usb_host_install",
    "usb_host_lib_handle_events",
    "hid_host_install",
    "s_usb_runtime_authorization_gate",
    "s_build_authorization_gate",
    "_binary_doom_shareware_wad_start",
    "_binary_doom_shareware_wad_end",
}
FORBIDDEN_SYMBOLS = {
    "platform_audio_start",
    "platform_audio_write",
    "platform_storage_init",
    "esp_vfs_fat_sdmmc_mount",
    "sdmmc_card_init",
}


def fail(message: str) -> None:
    raise SystemExit(f"doom_embedded_gamepad verification failed: {message}")


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


def exact_file(path: pathlib.Path, size: int, digest: str, label: str) -> None:
    require(path.is_file(), f"missing {label}: {path}")
    require(path.stat().st_size == size, f"wrong {label} byte count")
    require(sha256_file(path) == digest, f"wrong {label} SHA-256")


def checked_build_file(build_dir: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label} path")
    path = (build_dir / relative).resolve()
    require(path.is_relative_to(build_dir), f"{label} leaves build directory")
    require(path.is_file(), f"missing {label}: {path}")
    return path


def cache_tool(build_dir: pathlib.Path, variable: str) -> pathlib.Path:
    try:
        cache = (build_dir / "CMakeCache.txt").read_text()
    except (OSError, UnicodeError) as error:
        fail(f"cannot read CMake cache: {error}")
    match = re.search(rf"^{re.escape(variable)}:FILEPATH=(.+)$", cache, re.MULTILINE)
    require(match is not None, f"{variable} missing from CMake cache")
    path = pathlib.Path(match.group(1))
    require(path.is_file(), f"configured tool is missing: {path}")
    return path


def output(arguments: list[str], label: str) -> str:
    try:
        result = subprocess.run(
            arguments, check=True, capture_output=True, encoding="utf-8"
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"{label} failed: {error}")
    return result.stdout


def partition_entries(path: pathlib.Path) -> list[tuple[str, int, int, int, int, int]]:
    data = path.read_bytes()
    entries: list[tuple[str, int, int, int, int, int]] = []
    for offset in range(0, len(data), 32):
        entry = data[offset : offset + 32]
        if len(entry) != 32:
            break
        magic, kind, subtype, part_offset, size, raw_name, flags = struct.unpack(
            "<HBBII16sI", entry
        )
        if magic == 0xEBEB:
            break
        require(magic == 0x50AA, f"invalid partition entry at byte {offset}")
        name = raw_name.split(b"\0", 1)[0].decode("ascii")
        entries.append((name, kind, subtype, part_offset, size, flags))
    return entries


def verify_policy(mode: str, metadata: dict, auth: dict) -> None:
    require(mode in {"build-only", "app-flash"}, "mode must be build-only or app-flash")
    require(metadata.get("schema") == 1, "metadata schema changed")
    require(metadata.get("app") == "doom_embedded_gamepad", "wrong app metadata")
    require(
        metadata.get("build_evidence") == EVIDENCE_REL,
        "metadata build evidence changed",
    )
    require(metadata.get("runtime_supported") is False, "runtime claim is premature")
    require(metadata.get("flash_authorized") is False, "broad flash must remain false")
    require(
        metadata.get("flash_project_authorized") is False,
        "full-project flash must remain false",
    )
    require(metadata.get("fixture_authorized") is False, "USB fixture became authorized")
    require(metadata.get("fixture_evidence") is None, "USB fixture evidence appeared")
    require(
        metadata.get("one_shot_authorization_evidence") == AUTH_REL,
        "metadata one-shot record changed",
    )
    features = metadata.get("runtime_features")
    require(
        features
        == {
            "display": True,
            "immutable_embedded_wad": True,
            "doom": True,
            "silent": True,
            "neutral_input": True,
            "native_usb_gamepad": False,
            "audio": False,
        },
        "runtime feature boundary changed",
    )
    require(
        set(metadata.get("runtime_hardware_interfaces", []))
        == EXPECTED_RUNTIME_INTERFACES,
        "runtime hardware interface scope changed",
    )

    exact = auth.get("exact_artifact")
    binding = auth.get("device_binding")
    scope = auth.get("scope")
    execution = auth.get("execution_contract")
    require(auth.get("schema") == 1 and auth.get("id") == AUTH_ID, "wrong authorization")
    require(
        exact
        == {
            "offset": EXPECTED_OFFSET,
            "bytes": EXPECTED_BINARY_BYTES,
            "sha256": EXPECTED_BINARY_SHA256,
            "build_evidence": EVIDENCE_REL,
        },
        "authorization exact artifact changed",
    )
    require(
        isinstance(binding, dict)
        and binding.get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and binding.get("flash_bytes") == 16777216
        and binding.get("chip") == "ESP32-P4"
        and binding.get("chip_revision") == "v1.3",
        "authorization device binding changed",
    )
    require(
        isinstance(scope, dict)
        and scope.get("app") == "doom_embedded_gamepad"
        and scope.get("app_partition_only") is True
        and scope.get("full_project_flash") is False
        and scope.get("bootloader_write") is False
        and scope.get("partition_table_write") is False
        and scope.get("data_partition_write") is False
        and scope.get("erase_flash") is False
        and scope.get("display_wad_doom_runtime") is True
        and scope.get("audio_runtime") is False
        and scope.get("usb_runtime") is False
        and scope.get("usb_fixture_authorized") is False
        and scope.get("controller_acceptance_run") is False,
        "authorization scope widened",
    )
    require(
        isinstance(execution, dict)
        and execution.get("maximum_terminal_attempts") == 1
        and execution.get("reservation_is_consumed_on_success_or_failure") is True
        and execution.get("rebuild_after_verification") is False
        and execution.get("write_after_action") == "no_reset"
        and execution.get("readback") == "ordered-512KiB-max-chunks-no-reset"
        and execution.get("explicit_run_after_exact_readback") is True
        and execution.get("explicit_run_count") == 1,
        "one-shot execution contract changed",
    )
    if mode == "build-only":
        require(metadata.get("flash_app_authorized") is False, "inactive app flash enabled")
        require(
            metadata.get("one_shot_authorization_active") is False,
            "inactive metadata one-shot flag enabled",
        )
        require(auth.get("active") is False, "inactive one-shot record became active")
        if auth.get("consumed") is True:
            require(
                auth.get("classification") == CONSUMED_CLASS
                and auth.get("terminal_outcome") == "completed"
                and auth.get("consumed_at_utc") == "2026-08-13T15:54:00Z",
                "consumed one-shot record changed",
            )
            ledger = auth.get("durable_ledger")
            require(
                ledger
                == {
                    "path": "hardware/local-state/doom-embedded-gamepad-e3-one-shot.json",
                    "bytes": 554,
                    "sha256": "33da02f3130d81a82601f3134a2ed37a3cdade8e2e5f8ef408e8ab109869d19e",
                    "status": "completed",
                    "terminal": True,
                },
                "consumed durable ledger binding changed",
            )
            require(
                metadata.get("runtime_evidence") == RUNTIME_EVIDENCE_REL
                and auth.get("runtime_evidence") == RUNTIME_EVIDENCE_REL,
                "post-run evidence binding changed",
            )
            runtime = load_json(RUNTIME_EVIDENCE)
            install = runtime.get("installation", {})
            runtime_auth = runtime.get("authorization", {})
            runtime_policy = runtime.get("runtime", {})
            require(
                runtime.get("classification")
                == "hardware-install-readback-pass-one-launch-runtime-unobserved"
                and runtime.get("result")
                == "pass-exact-app-install-and-launch-runtime-unobserved"
                and install.get("app_only") is True
                and install.get("exact_chunked_host_readback_verified") is True
                and install.get("readback_chunks") == 10
                and install.get("readback_bytes") == EXPECTED_BINARY_BYTES
                and install.get("readback_sha256") == EXPECTED_BINARY_SHA256
                and install.get("launch_count") == 1
                and runtime_auth.get("executed_active_record_bytes") == 2846
                and runtime_auth.get("executed_active_record_sha256")
                == "86bef6280b81432f11f14069a2eb566e265c191ee20daff403f05bbea75fe3c3"
                and runtime_auth.get("revoked_record_bytes") == 3310
                and runtime_auth.get("revoked_record_sha256")
                == "bcadcf3a66a8fcc38dc1455dc11aa83662f62cf93f0f105bb5d484696ad16801"
                and runtime_auth.get("ledger_sha256")
                == "33da02f3130d81a82601f3134a2ed37a3cdade8e2e5f8ef408e8ab109869d19e"
                and runtime_auth.get("revoked_after_completion") is True
                and runtime_auth.get("active") is False
                and runtime_policy.get("serial_runtime_observed") is False
                and runtime_policy.get("visual_runtime_observed") is False
                and runtime_policy.get("runtime_supported") is False
                and runtime_policy.get("usb_runtime_authorized") is False
                and runtime_policy.get("usb_fixture_authorized") is False,
                "post-run installation evidence changed",
            )
        else:
            require(
                auth.get("classification") == INACTIVE_CLASS,
                "inactive one-shot record changed",
            )
    else:
        require(auth.get("consumed") is False, "authorization is already consumed")
        require(metadata.get("flash_app_authorized") is True, "app-only flash is inactive")
        require(
            metadata.get("one_shot_authorization_active") is True,
            "metadata one-shot flag is inactive",
        )
        require(
            auth.get("classification") == ACTIVE_CLASS and auth.get("active") is True,
            "exact E3 one-shot is not active",
        )


def verify_evidence() -> dict:
    require(sha256_file(EVIDENCE) == EVIDENCE_SHA256, "reviewed build evidence changed")
    evidence = load_json(EVIDENCE)
    require(
        evidence.get("schema") == 1
        and evidence.get("classification")
        == "build-tested-install-gate-prepared-runtime-usb-denied"
        and evidence.get("result") == "pass-build-and-reproducibility-no-hardware-access",
        "build evidence classification changed",
    )
    inventory = evidence.get("source_inventory")
    require(isinstance(inventory, dict) and len(inventory) >= 50, "source inventory missing")
    for relative, digest in inventory.items():
        require(isinstance(relative, str) and isinstance(digest, str), "bad source inventory")
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT) and path.is_file(), f"missing source: {relative}")
        require(sha256_file(path) == digest, f"reviewed source changed: {relative}")
    build = evidence.get("build", {})
    require(
        build.get("reproducible_build") is True
        and build.get("clean_builds_compared") == 3
        and build.get("independent_build_directories_compared") == 2
        and build.get("canonical_build_also_compared") is True
        and build.get("identical_binary") is True
        and build.get("identical_elf") is True
        and build.get("identical_bootloader") is True
        and build.get("identical_partition_table") is True
        and build.get("generated_flash_after_action") == "no_reset"
        and build.get("target") == "esp32p4"
        and build.get("minimum_revision_full") == 100
        and build.get("maximum_revision_full") == 199
        and build.get("application_offset") == EXPECTED_OFFSET
        and build.get("application_partition_bytes") == EXPECTED_APP_CAPACITY
        and build.get("usb_fixture_authorization_enabled") is False,
        "reproducible build properties changed",
    )
    artifacts = evidence.get("artifacts", {})
    require(
        artifacts.get("app_binary_bytes") == EXPECTED_BINARY_BYTES
        and artifacts.get("app_binary_sha256") == EXPECTED_BINARY_SHA256
        and artifacts.get("elf_bytes") == EXPECTED_ELF_BYTES
        and artifacts.get("elf_sha256") == EXPECTED_ELF_SHA256
        and artifacts.get("bootloader_bytes") == EXPECTED_BOOTLOADER_BYTES
        and artifacts.get("bootloader_sha256") == EXPECTED_BOOTLOADER_SHA256
        and artifacts.get("partition_table_bytes") == EXPECTED_PARTITION_BYTES
        and artifacts.get("partition_table_sha256") == EXPECTED_PARTITION_SHA256,
        "reviewed artifact identities changed",
    )
    identities = evidence.get("reproducibility", {}).get("identities")
    require(isinstance(identities, list) and len(identities) == 3, "three builds missing")
    require(
        [item.get("label") for item in identities]
        == ["canonical", "independent-a", "independent-b"],
        "build labels changed",
    )
    for identity in identities:
        require(
            identity.get("app_binary_bytes") == EXPECTED_BINARY_BYTES
            and identity.get("app_binary_sha256") == EXPECTED_BINARY_SHA256
            and identity.get("elf_bytes") == EXPECTED_ELF_BYTES
            and identity.get("elf_sha256") == EXPECTED_ELF_SHA256,
            "one reproducibility identity differs",
        )
    audit = evidence.get("source_and_link_audit", {})
    require(
        audit.get("post_link_audit") == "pass"
        and audit.get("app_usb_gate_value") == 0
        and audit.get("platform_usb_gate_value") == 0
        and audit.get("usb_dormant_path_linked") is True
        and audit.get("usb_calls_on_zero_gate_path") == 0
        and audit.get("input") == "neutral"
        and audit.get("audio") == "disabled",
        "dormant USB link policy changed",
    )
    install = evidence.get("install_gate", {})
    require(
        install.get("prepared") is True
        and install.get("active") is False
        and install.get("app_only") is True
        and install.get("broad_flash") is False
        and install.get("full_project_flash") is False
        and install.get("exact_artifact_sealed_after_verifier") is True
        and install.get("rebuild_after_verifier") is False
        and install.get("write_after_action") == "no_reset"
        and install.get("chunked_readback_after_action") == "no_reset"
        and install.get("explicit_run_count") == 1,
        "prepared install gate changed",
    )
    return evidence


def verify_build(build_dir: pathlib.Path) -> None:
    flasher = load_json(build_dir / "flasher_args.json")
    app = flasher.get("app")
    require(isinstance(app, dict), "flasher app entry missing")
    require(app.get("offset") == EXPECTED_OFFSET, "application offset changed")
    binary = checked_build_file(build_dir, app.get("file"), "application binary")
    description = load_json(build_dir / "project_description.json")
    require(description.get("target") == "esp32p4", "wrong target")
    require(int(description.get("min_rev")) == 100, "wrong minimum silicon revision")
    require(int(description.get("max_rev")) == 199, "wrong maximum silicon revision")
    elf = checked_build_file(build_dir, description.get("app_elf"), "application ELF")
    bootloader = build_dir / "bootloader/bootloader.bin"
    partitions = build_dir / "partition_table/partition-table.bin"
    exact_file(binary, EXPECTED_BINARY_BYTES, EXPECTED_BINARY_SHA256, "application binary")
    exact_file(elf, EXPECTED_ELF_BYTES, EXPECTED_ELF_SHA256, "application ELF")
    exact_file(
        bootloader, EXPECTED_BOOTLOADER_BYTES, EXPECTED_BOOTLOADER_SHA256, "bootloader"
    )
    exact_file(
        partitions,
        EXPECTED_PARTITION_BYTES,
        EXPECTED_PARTITION_SHA256,
        "partition table",
    )
    require(partition_entries(partitions) == EXPECTED_PARTITIONS, "partition layout changed")
    require(
        sha256_file(APP_DIR / "dependencies.lock") == EXPECTED_LOCK_SHA256,
        "component lock changed",
    )

    wad = LOCAL_WAD.read_bytes()
    image = binary.read_bytes()
    require(len(wad) == EXPECTED_WAD_BYTES, "local WAD byte count changed")
    require(hashlib.sha256(wad).hexdigest() == EXPECTED_WAD_SHA256, "local WAD changed")
    require(image.count(wad) == 1 and image.find(wad) == EXPECTED_WAD_OFFSET, "WAD embed changed")

    sdkconfig = (build_dir / "config/sdkconfig.h").read_text()
    require("#define CONFIG_PLATFORM_DISPLAY_ELECROW_10_1_CROSS_REVISION_AUTHORIZED 1" in sdkconfig,
            "display authorization is absent")
    require("CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED" not in sdkconfig,
            "USB fixture authorization is enabled")
    require("#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1" not in sdkconfig,
            "unexpected rollback policy")
    require("#define CONFIG_APP_COMPILE_TIME_DATE 1" not in sdkconfig,
            "compile-time date broke reproducibility")

    nm = cache_tool(build_dir, "CMAKE_NM")
    symbols_text = output([str(nm), "-a", str(elf)], "symbol audit")
    symbols = {line.split()[-1] for line in symbols_text.splitlines() if line.split()}
    require(REQUIRED_SYMBOLS <= symbols, "required Doom/native-gamepad symbol missing")
    require(not (FORBIDDEN_SYMBOLS & symbols), "forbidden audio/storage symbol linked")
    expected_addresses = {
        "s_usb_runtime_authorization_gate": "40095f90",
        "s_build_authorization_gate": "40496c7c",
        "_binary_doom_shareware_wad_start": "40096060",
        "_binary_doom_shareware_wad_end": "40496714",
    }
    for name, address in expected_addresses.items():
        require(
            re.search(rf"(?m)^{address}\s+\S\s+{re.escape(name)}$", symbols_text) is not None,
            f"{name} address changed",
        )
    objdump = cache_tool(build_dir, "CMAKE_OBJDUMP")
    for address, label in ((0x40095F90, "app USB gate"), (0x40496C7C, "platform USB gate")):
        dump = output(
            [
                str(objdump),
                "-s",
                f"--start-address=0x{address:x}",
                f"--stop-address=0x{address + 1:x}",
                str(elf),
            ],
            label,
        )
        require(re.search(rf"(?m)^\s*{address:x}\s+00\s", dump) is not None, f"{label} is nonzero")


def verify_flash_route() -> None:
    flash = (ROOT / "scripts/flash.sh").read_text()
    required = [
        'P4_APP" = doom_embedded_gamepad',
        'verify-doom-embedded-gamepad.py',
        'P4_E3_EXPECTED_BYTES=4927824',
        'P4_E3_EXPECTED_HASH=32814a97b72b3b227e2ace02393222178ceebf02d18a4f0b35bead1f4584ea24',
        'P4_VERIFIED_DIRECT_WRITE=true',
        '--after no_reset write_flash',
        'p4_verify_chunked_application_readback',
        "doom-e3-one-shot-state.py",
        "Verified %s launched exactly once after successful application readback.",
    ]
    for text in required:
        require(text in flash, f"central flash route missing {text!r}")
    verifier_pos = flash.index('verify-doom-embedded-gamepad.py')
    seal_pos = flash.index('P4_E3_EXPECTED_BYTES=4927824', verifier_pos)
    write_pos = flash.index('--after no_reset write_flash', seal_pos)
    readback_pos = flash.index('p4_verify_chunked_application_readback', write_pos)
    run_pos = flash.index('esptool.py --chip esp32p4 --port "$P4_PORT" run', readback_pos)
    require(verifier_pos < seal_pos < write_pos < readback_pos < run_pos,
            "verify/seal/write/readback/run order changed")
    between = flash[verifier_pos:write_pos]
    require('"$P4_SCRIPT_DIR/build.sh"' not in between, "firmware rebuild occurs after verifier")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-doom-embedded-gamepad.py BUILD_DIR build-only|app-flash")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), "build directory is missing")
    mode = sys.argv[2]
    metadata = load_json(APP_DIR / "app-metadata.json")
    auth = load_json(AUTH)
    verify_policy(mode, metadata, auth)
    verify_evidence()
    verify_build(build_dir)
    verify_flash_route()
    print(
        "doom_embedded_gamepad verification PASS: "
        f"mode={mode} bytes={EXPECTED_BINARY_BYTES} sha256={EXPECTED_BINARY_SHA256} "
        "usb_runtime=false usb_fixture=false app_gate=0 platform_gate=0"
    )


if __name__ == "__main__":
    main()
