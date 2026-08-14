#!/usr/bin/env python3

"""Fail-closed verifier for the inactive native USB gamepad diagnostic."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/gamepad_diag"
EVIDENCE = ROOT / "test-runs/2026-08-13-gamepad-d1-build.json"
AUTHORIZATION = ROOT / "hardware/evidence/gamepad-diag-d1-one-shot-authorization.json"
APP_BYTES = 301328
APP_SHA256 = "ad197d70eca589cb104bb654e5d35178dbb2cefd8fa60cdd184cd8806b1350fb"
ELF_BYTES = 5691060
ELF_SHA256 = "c50117429eae1a58ae3578670dfe32d55fd1dfcdf463f68df981079ba64446ae"
BOOT_BYTES = 22912
BOOT_SHA256 = "fcb629f826b8cd79fb21533b8691650a775804f337db37451b03127e36a32ca9"
PARTITION_BYTES = 3072
PARTITION_SHA256 = "7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820"
SDKCONFIG_BYTES = 52561
SDKCONFIG_SHA256 = "810cb16f847eaaced62fff044b815668ac1de2154a00154f9c8ad79661c0722a"
DESCRIPTOR_SHA256 = "05a151c932362fee13503905962880ce75212bbf8c43c95701527b8290833162"
AUTH_ID = "gamepad-diag-d1-one-shot-authorization-2026-08-13"
SOURCE_HASHES = {
    "apps/gamepad_diag/CMakeLists.txt": "1d68c48fcd063a28aff81648cccea4452d7a6b7e6fe425f6fd1ab031044d32be",
    "apps/gamepad_diag/main/CMakeLists.txt": "40106ed7d5c710cc546f4e3049c31a77a2b47159b3f7b762c086dea3c9d0232e",
    "apps/gamepad_diag/main/gamepad_diag_main.c": "2ba408eac595f03b7e3ead1856d4594ef51487ccc91ff5416b64cff4d2f5ea37",
    "apps/gamepad_diag/main/gamepad_diag_arm_model.c": "0c9d5ec128485e397ed0a8ea2bd98a61df75a0675124b8cb01625be0f191f7c1",
    "apps/gamepad_diag/main/gamepad_diag_arm_model.h": "47fb11c75c0d863c47676186bc7340cf927dd73907a8a5be5006ea7033a885dd",
    "apps/gamepad_diag/README.md": "99e806bf95827889349704ea496f303e0d201e2181a6d4d6d776b3682aa12d27",
    "apps/gamepad_diag/tests/CMakeLists.txt": "922f2af9d943ae05224a5550566ec2b143942efc95f8cf201114c4b6208c69b3",
    "apps/gamepad_diag/tests/test_gamepad_diag_arm_model.c": "d25bebfb322dfbc5f94db48c6f5a1d3f9f3057f09a193763957b01e0d0d15250",
    "components/platform_usb_host/cmake/verify_native_gamepad_link.cmake": "b04e48c62f74ef354cec72e86dbdeeae08d88656a65e073fbb253bc4cc903081",
    "Makefile": "bedf9624e33692991d7a7ed5c084452b7613d0267f457bcc1a9124bcbfa63ae2",
}


def fail(message: str) -> None:
    raise SystemExit(f"gamepad_diag verification failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: pathlib.Path) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        fail(f"cannot load {path}: {error}")
    require(isinstance(value, dict), f"{path} must contain an object")
    return value


def child(build: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label}")
    path = (build / relative).resolve()
    require(path.is_relative_to(build) and path.is_file(), f"unsafe or missing {label}")
    return path


def verify_source() -> None:
    for relative, expected in SOURCE_HASHES.items():
        path = ROOT / relative
        require(path.is_file() and sha256_file(path) == expected,
                f"frozen runtime source changed: {relative}")
    main = (APP_DIR / "main/gamepad_diag_main.c").read_text()
    arm_header = (APP_DIR / "main/gamepad_diag_arm_model.h").read_text()
    cmake = (APP_DIR / "main/CMakeLists.txt").read_text()
    required = (
        "GAMEPAD_D1_WAIT_ARM", "GAMEPAD_D1_ARM_ACCEPTED",
        "mbedtls_ct_memcmp", "mbedtls_platform_zeroize",
        "GAMEPAD_D1_HALT stage=host-arm", "root_data_port_enabled=0",
        "platform_usb_host_start(&FIXTURE_EVIDENCE)",
        "platform_gamepad_usb_start()", "platform_usb_host_enable_root_port()",
        DESCRIPTOR_SHA256,
    )
    require(all(token in main for token in required), "host-arm/runtime source contract changed")
    require("P4_GAMEPAD_D1_ARM " in arm_header and AUTH_ID in arm_header
            and "GAMEPAD_DIAG_ARM_LINE_BYTES 132U" in arm_header,
            "exact ARM frame grammar changed")
    app_main = main[main.index("void app_main(void)") :]
    order = [
        app_main.index("require_host_arm()"),
        app_main.index("platform_usb_host_start(&FIXTURE_EVIDENCE)"),
        app_main.index("platform_gamepad_usb_start()"),
        app_main.index("platform_usb_host_enable_root_port()"),
    ]
    require(order == sorted(order) and len(set(order)) == len(order),
            "host ARM -> host -> HID -> root order changed")
    require_arm = main[main.index("static void __attribute__((noinline)) require_host_arm"):
                       main.index("typedef struct", main.index("static void __attribute__((noinline)) require_host_arm"))]
    require("GAMEPAD_D1_ARM_ACCEPTED" in require_arm,
            "ARM acceptance proof moved outside the dominant helper")
    pre_arm = app_main[: order[0]]
    require("platform_usb_host_start" not in pre_arm
            and "platform_gamepad_usb_start" not in pre_arm
            and "platform_usb_host_enable_root_port" not in pre_arm,
            "USB can start before exact ARM acceptance")
    require("P4_GAMEPAD_DIAG_ZERO_ARM_TOKEN_SHA256" in cmake
            and "Refusing an arm-token digest in an electrically inactive" in cmake,
            "inactive all-zero ARM digest gate changed")


def verify_inactive_policy(mode: str) -> None:
    metadata = load_json(APP_DIR / "app-metadata.json")
    authorization = load_json(AUTHORIZATION)
    profile = load_json(ROOT / "hardware/board-profile.json")
    for flag in (
        "runtime_supported", "one_shot_authorization_active",
        "usb_runtime_authorized", "flash_authorized", "flash_app_authorized",
        "flash_project_authorized", "fixture_authorized",
    ):
        require(metadata.get(flag) is False, f"metadata {flag} must remain false")
    require(authorization.get("active") is False
            and authorization.get("issuable") is False
            and authorization.get("compiled_gate_state") == {
                "app_runtime_authorization_gate": 0,
                "platform_build_authorization_gate": 0,
            }, "authorization is not the exact inactive record")
    require(profile.get("pin_map_authorized") is False
            and profile.get("peripheral_authorizations", {}).get("usb_host") is None,
            "board profile unexpectedly authorizes USB Host")
    if mode != "build-only":
        fail("app-flash stays disabled until physical evidence, scoped board profile, "
             "private token-bound active artifact, and active three-build evidence exist")


def verify_evidence() -> None:
    evidence = load_json(EVIDENCE)
    require(evidence.get("schema") == 1, "build evidence schema changed")
    require(evidence.get("execution", {}).get("hardware_accessed") is False
            and evidence.get("execution", {}).get("firmware_flashed") is False
            and evidence.get("execution", {}).get("firmware_executed") is False,
            "build evidence claims physical execution")
    artifacts = evidence.get("artifacts", {})
    require(artifacts.get("app_binary_bytes") == APP_BYTES
            and artifacts.get("app_binary_sha256") == APP_SHA256
            and artifacts.get("elf_bytes") == ELF_BYTES
            and artifacts.get("elf_sha256") == ELF_SHA256,
            "build evidence does not bind the frozen runtime")
    source_audit = evidence.get("source_and_link_audit", {})
    require(
        evidence.get("resolved_active_config") is None
        and source_audit.get("compiled_fixture_post_link_verified") is False
        and source_audit.get("compiled_fixture_binding") is None
        and source_audit.get("compiled_arm_token_post_link_verified") is False
        and source_audit.get("compiled_arm_token_sha256") is None,
        "inactive build evidence must not claim a compiled physical fixture",
    )
    future = evidence.get("future_active_evidence_requirements", {})
    require(
        future.get("resolved_sdkconfig_path_and_sha256_required") is True
        and future.get("compiled_fixture_post_link_verification_required") is True
        and future.get("compiled_arm_token_post_link_verification_required") is True
        and future.get("private_arm_token_digest_must_match_each_of_three_identical_builds") is True
        and future.get("hardware_accessed_during_build_evidence_must_remain_false") is True
        and future.get("recovery_gate_inventory_revalidated_before_uart_reset") is True,
        "future active fixture/recovery evidence contract is incomplete",
    )
    restore = evidence.get("restore_contract", {})
    require(
        restore.get("flash_sector_bytes") == 4096
        and restore.get("install_transport_block_bytes") == 16384
        and restore.get("restore_write_block_bytes") == 4096
        and restore.get("artifact_bytes") == APP_BYTES
        and restore.get("derived_mutation_span_bytes") == 311296
        and restore.get("derived_mutation_span") == "[0x10000,0x5c000)",
        "build evidence has the wrong bounded restoration geometry",
    )
    gate_inventory = evidence.get("gate_inventory")
    required_gate_files = {
        "scripts/gamepad-diag-one-shot-state.py",
        "scripts/gamepad-diag-capture.py",
        "scripts/gamepad-diag-restore.py",
        "scripts/gamepad-diag-install.py",
        "scripts/verify-gamepad-diag.py",
        "scripts/flash.sh",
        "scripts/lib/app-readback.sh",
        "scripts/tests/test-gamepad-diag-gate.py",
        "scripts/tests/test-gamepad-diag-state-contract.py",
        "scripts/tests/test-gamepad-diag-restore.py",
        "scripts/tests/test-gamepad-diag-install.py",
        "scripts/tests/test-gamepad-diag-flash-route.py",
        "scripts/tests/test-app-readback.sh",
        "scripts/tests/fixtures/esptool.py",
        "scripts/check.sh",
    }
    require(
        isinstance(gate_inventory, dict)
        and set(gate_inventory) == required_gate_files,
        "build evidence omits or expands the exact inactive gate inventory",
    )
    for relative, expected in gate_inventory.items():
        path = (ROOT / relative).resolve()
        require(
            path.is_relative_to(ROOT)
            and path.is_file()
            and re.fullmatch(r"[0-9a-f]{64}", str(expected)) is not None
            and sha256_file(path) == expected,
            f"frozen inactive gate source changed: {relative}",
        )


def verify_build(build: pathlib.Path) -> None:
    flasher = load_json(build / "flasher_args.json")
    app = flasher.get("app", {})
    require(int(str(app.get("offset")), 0) == 0x10000, "app offset changed")
    binary = child(build, app.get("file"), "app binary")
    description = load_json(build / "project_description.json")
    require(description.get("project_name") == "p4_gamepad_diag"
            and description.get("target") == "esp32p4", "wrong project/target")
    elf = child(build, description.get("app_elf"), "app ELF")
    checks = (
        (binary, APP_BYTES, APP_SHA256, "app"),
        (elf, ELF_BYTES, ELF_SHA256, "ELF"),
        (build / "bootloader/bootloader.bin", BOOT_BYTES, BOOT_SHA256, "bootloader"),
        (build / "partition_table/partition-table.bin", PARTITION_BYTES,
         PARTITION_SHA256, "build partition table"),
        (APP_DIR / "sdkconfig", SDKCONFIG_BYTES, SDKCONFIG_SHA256, "sdkconfig"),
    )
    for path, size, digest, label in checks:
        require(path.is_file() and path.stat().st_size == size
                and sha256_file(path) == digest, f"{label} differs from freeze")
    sdkconfig = (APP_DIR / "sdkconfig").read_text()
    require("# CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED is not set" in sdkconfig,
            "inactive fixture gate changed")
    data = binary.read_bytes()
    require(data.count(("0" * 64).encode()) == 1,
            "inactive impossible ARM digest is missing or duplicated")
    require(data.count(bytes.fromhex(DESCRIPTOR_SHA256)) >= 2,
            "exact descriptor profile is not retained in binary")
    map_text = (build / "p4_gamepad_diag.map").read_text()
    for symbol in (
        "s_runtime_authorization_gate", "s_build_authorization_gate",
        "s_arm_token_sha256_hex", "EXPECTED_DESCRIPTOR_SHA256",
        "usb_host_install", "hid_host_install", "platform_usb_host_enable_root_port",
        "__wrap_usb_host_device_open",
    ):
        require(symbol in map_text, f"native USB graph symbol missing: {symbol}")


def main() -> None:
    if len(sys.argv) != 3 or sys.argv[2] not in {"build-only", "app-flash"}:
        fail("usage: verify-gamepad-diag.py BUILD_DIR build-only|app-flash")
    build = pathlib.Path(sys.argv[1]).resolve()
    require(build.is_dir(), "build directory is missing")
    mode = sys.argv[2]
    verify_source()
    verify_inactive_policy(mode)
    verify_evidence()
    verify_build(build)
    print(f"gamepad_diag exact inactive {mode} verification PASS")


if __name__ == "__main__":
    main()
