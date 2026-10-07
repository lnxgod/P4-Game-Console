#!/usr/bin/env python3
"""Fail-closed exact-image verifier for the D2.4 level diagnostic."""

from __future__ import annotations

import hashlib
import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_DIR = ROOT / "apps/audio_level_diag"
BUILD_EVIDENCE = ROOT / "test-runs/2026-08-13-audio-d24-build.json"
AUTHORIZATION = ROOT / "hardware/evidence/audio-level-diag-d24-one-shot-authorization.json"
APP_OFFSET = 0x10000
APP_BYTES = 196176
APP_SHA256 = "2932520cc5dc7511b5032007a30931fc1cc59cb664966717ed01512636d7d37a"
ELF_BYTES = 4415256
ELF_SHA256 = "0779157d094076b759db30f822af72e2dc11aa23ba98501b15f76e3270f34958"
BOOT_BYTES = 22912
BOOT_SHA256 = "fcb629f826b8cd79fb21533b8691650a775804f337db37451b03127e36a32ca9"
PARTITION_BYTES = 3072
PARTITION_SHA256 = "7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820"
SDKCONFIG_BYTES = 56948
SDKCONFIG_SHA256 = "4166510055452822ee64209439d626068acbfe20a9bd98d655f4a98e1016779b"
SOURCE_HASHES = {
    "apps/audio_level_diag/CMakeLists.txt": "93706f0b5913650b59eb00631f0702dd0fde9b5d7c7541b12d0abac6d9d66b54",
    "apps/audio_level_diag/README.md": "9605856739e8c20fc6ac80b41f17bde793e709891ba40252ee5b3c7a906e84ab",
    "apps/audio_level_diag/main/CMakeLists.txt": "fe69baf6b6129d88abf7695fd3859f163b34ef3c8dfb53fa1406665287fde857",
    "apps/audio_level_diag/main/audio_level_diag_main.c": "0556c35f49e796a457e1e173183f450724aca335eedd8f4a19ca11d2a70c81da",
    "apps/audio_level_diag/main/idf_component.yml": "12caf5b7509c0f6af70ad4324ee29db8268f678022ef5da49835678f9bc48bac",
    "apps/audio_level_diag/sdkconfig.defaults": "0733bc1cb50a16dbbee12ad062fc4c9757f17216dc8691f6df49b1619363198e",
    "apps/audio_level_diag/dependencies.lock": "e6ae2189715ed9d6a07ffda8eca0c5830dc92206611d37682b16ba78df6e72fc",
}
HOST_TOOL_PATHS = {
    "scripts/verify-audio-level-diag.py",
    "scripts/flash-audio-level-diag.sh",
    "scripts/capture-audio-level-diag.py",
    "scripts/d24_capture_transport.py",
    "scripts/analyze-audio-level-tone.py",
    "scripts/tests/test-d24-gate.py",
    "scripts/lib/app-readback.sh",
    "scripts/lib/project-env.sh",
    "scripts/build.sh",
    "scripts/verify-readback-chunks.py",
    "scripts/verify-live-app-layout.py",
    "scripts/verify-metadata.py",
    "scripts/verify-env.sh",
    "toolchain.lock.json",
}
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(f"audio_level_diag verification failed: {message}")


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


def checked_child(directory: pathlib.Path, relative: object, label: str) -> pathlib.Path:
    require(isinstance(relative, str) and relative, f"missing {label}")
    candidate = (directory / relative).resolve()
    require(candidate.is_relative_to(directory), f"{label} leaves build directory")
    require(candidate.is_file(), f"missing {label}: {candidate}")
    return candidate


def verify_source() -> None:
    for relative, digest in SOURCE_HASHES.items():
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT) and path.is_file(), f"missing source {relative}")
        require(sha256_file(path) == digest, f"frozen source changed: {relative}")
    source = (APP_DIR / "main/audio_level_diag_main.c").read_text()
    required = (
        "D24_TONE_PEAK 4096U",
        "D24_TONE_MS 400U",
        "D24_FADE_MS 80U",
        "D24_DMA_DESCRIPTOR_COUNT 6U",
        "D24_DMA_FRAMES_PER_DESCRIPTOR 256U",
        "D24_WRITE_TIMEOUT_MS 100U",
        "P4_AUDIO_D24_ARM ",
        "audio-level-diag-d24-one-shot-authorization-2026-08-13",
        "ff9175223ffd30ffa0fbf940aa5c705d40a7fd1f6d6bbf9adec6a95d22edb3f3",
        "sizeof(s_host_arm_line) - 1U == 136U",
        "state=zero-data-clocks-running",
        "direct_amp_vdd5v_unswitched=1",
    )
    require(all(token in source for token in required), "D2.4 source contract changed")
    order = [
        source.index("result = create_zeroed_i2s();", source.index("void app_main")),
        source.index("result = acquire_board_power();", source.index("void app_main")),
        source.index("P4_AUDIO D2.4 CAPTURE_ARM", source.index("void app_main")),
        source.index("P4_AUDIO D2.4 TONE_BEGIN", source.index("void app_main")),
        source.index("result = write_zero_ring_and_drain();", source.index("void app_main")),
        source.index("result = release_i2s();", source.index("void app_main")),
        source.index("result = release_board_power();", source.index("void app_main")),
    ]
    require(order == sorted(order) and len(set(order)) == len(order),
            "zero/power/tone/cleanup order changed")
    cleanup = source[source.index("static esp_err_t cleanup_runtime"):
                     source.index("static void halt_safe")]
    require(cleanup.index("force_analog_u4_shutdown") < cleanup.index("write_zero_ring_and_drain")
            < cleanup.index("release_i2s") < cleanup.index("release_board_power"),
            "cleanup direct-path order changed")
    require("if (result != ESP_OK) {\n        return result;" not in cleanup,
            "unrelated U4 failure can skip direct-path cleanup")


def verify_metadata(mode: str) -> None:
    metadata = load_json(APP_DIR / "app-metadata.json")
    require(metadata.get("schema") == 1 and metadata.get("app") == "audio_level_diag",
            "wrong metadata")
    for flag in ("runtime_supported", "flash_authorized", "flash_app_authorized",
                 "flash_project_authorized"):
        require(metadata.get(flag) is False, f"metadata flag {flag} must remain false")
    require(metadata.get("runtime_evidence") is None, "metadata claims runtime evidence")
    require(metadata.get("purpose") ==
            "Run one host-armed, synchronized 440 Hz direct-I2S diagnostic at a 4096 PCM peak without changing the reusable Doom audio backend.",
            "diagnostic isolation changed")


def verify_authorization(mode: str) -> None:
    authorization = load_json(AUTHORIZATION)
    require(authorization.get("schema") == 1
            and authorization.get("id")
            == "audio-level-diag-d24-one-shot-authorization-2026-08-13",
            "wrong D2.4 authorization")
    require(authorization.get("exact_artifact") == {
        "offset": "0x10000", "bytes": APP_BYTES, "sha256": APP_SHA256,
    }, "authorization artifact changed")
    boundary = authorization.get("exception_boundary", {})
    require(boundary.get("metadata_flags_remain_false") is True
            and boundary.get("connected_unit_only") is True
            and boundary.get("app_partition_only") is True
            and boundary.get("full_project_flash_authorized") is False
            and boundary.get("doom_composite_authorized") is False
            and boundary.get("reusable_platform_audio_authorized") is False,
            "authorization escaped its narrow exception boundary")
    tooling = authorization.get("exact_host_tooling")
    require(isinstance(tooling, dict), "authorization host-tool inventory must be an object")
    # Dated evidence may record this old prerequisite; it never participates
    # in current flash authorization or file validation.
    tooling = {relative: digest for relative, digest in tooling.items()
               if relative != "hardware/backups/manifest.json"}
    require(set(tooling) == HOST_TOOL_PATHS,
            "authorization host-tool inventory is incomplete or overbroad")
    for relative, expected_hash in tooling.items():
        require(isinstance(expected_hash, str)
                and SHA256_PATTERN.fullmatch(expected_hash) is not None,
                f"invalid authorized tool hash: {relative}")
        path = (ROOT / relative).resolve()
        require(path.is_relative_to(ROOT) and path.is_file() and not path.is_symlink(),
                f"authorized tool path is unsafe: {relative}")
        require(sha256_file(path) == expected_hash,
                f"authorized host tool changed: {relative}")
    if mode == "build-only":
        require(authorization.get("classification")
                == "user-requested-bounded-diagnostic-one-shot-inactive"
                and authorization.get("result")
                == "prepared-inactive-pending-independent-final-gate-audit"
                and boundary.get("active") is False,
                "build-only authorization must remain inactive")
    else:
        require(authorization.get("classification")
                == "user-requested-bounded-diagnostic-one-shot-active"
                and authorization.get("result")
                == "authorized-exact-app-preflash-reviewed"
                and boundary.get("active") is True,
                "D2.4 one-shot exception is not independently activated")


def verify_evidence() -> None:
    evidence = load_json(BUILD_EVIDENCE)
    require(evidence.get("schema") == 1
            and evidence.get("classification") == "build-tested"
            and evidence.get("result") == "pass-build-only-runtime-denied",
            "build evidence classification changed")
    artifacts = evidence.get("artifacts", {})
    require(artifacts == {
        "app_binary_bytes": APP_BYTES,
        "app_binary_sha256": APP_SHA256,
        "elf_bytes": ELF_BYTES,
        "elf_sha256": ELF_SHA256,
        "bootloader_bytes": BOOT_BYTES,
        "bootloader_sha256": BOOT_SHA256,
        "partition_table_bytes": PARTITION_BYTES,
        "partition_table_sha256": PARTITION_SHA256,
        "sdkconfig_bytes": SDKCONFIG_BYTES,
        "sdkconfig_sha256": SDKCONFIG_SHA256,
    }, "build evidence artifact identity changed")
    require(evidence.get("source_inventory") == SOURCE_HASHES,
            "build evidence source inventory changed")
    reproducibility = evidence.get("reproducibility", {})
    identities = reproducibility.get("identities", [])
    require(reproducibility.get("clean_builds_compared") == 3
            and reproducibility.get("identical_application_binary") is True
            and reproducibility.get("identical_application_elf") is True
            and [item.get("label") for item in identities]
            == ["canonical", "independent-a", "independent-b"]
            and all(item.get("app_binary_bytes") == APP_BYTES
                    and item.get("app_binary_sha256") == APP_SHA256
                    and item.get("elf_bytes") == ELF_BYTES
                    and item.get("elf_sha256") == ELF_SHA256
                    for item in identities),
            "three-build reproducibility record changed")
    require(evidence.get("runtime", {}).get("authorized") is False
            and evidence.get("runtime", {}).get("hardware_accessed") is False
            and evidence.get("runtime", {}).get("executed") is False,
            "build evidence claims runtime")


def verify_build(build_dir: pathlib.Path) -> tuple[pathlib.Path, pathlib.Path]:
    flasher = load_json(build_dir / "flasher_args.json")
    app = flasher.get("app", {})
    require(int(str(app.get("offset")), 0) == APP_OFFSET, "app offset changed")
    binary = checked_child(build_dir, app.get("file"), "app binary")
    description = load_json(build_dir / "project_description.json")
    require(description.get("project_name") == "p4_audio_level_diag"
            and description.get("target") == "esp32p4", "wrong project/target")
    elf = checked_child(build_dir, description.get("app_elf"), "app ELF")
    require(binary.stat().st_size == APP_BYTES and sha256_file(binary) == APP_SHA256,
            "app binary differs from freeze")
    require(elf.stat().st_size == ELF_BYTES and sha256_file(elf) == ELF_SHA256,
            "app ELF differs from freeze")
    sdkconfig = (build_dir.parent / "sdkconfig").resolve()
    require(sdkconfig.parent == build_dir.parent.resolve()
            and sdkconfig.is_file() and not sdkconfig.is_symlink(),
            "generated sdkconfig is not the exact build sibling")
    require(sdkconfig.stat().st_size == SDKCONFIG_BYTES
            and sha256_file(sdkconfig) == SDKCONFIG_SHA256,
            "generated sdkconfig differs from freeze")
    sdk = sdkconfig.read_text()
    require('CONFIG_IDF_TARGET="esp32p4"' in sdk
            and "CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y" in sdk
            and "CONFIG_ESP32P4_REV_MIN_100=y" in sdk
            and "CONFIG_ESPTOOLPY_AFTER_NORESET=y" in sdk,
            "target/revision/no-reset config changed")
    return binary, elf


def verify_isolated_flash_route() -> None:
    path = ROOT / "scripts/flash-audio-level-diag.sh"
    require(path.is_file(), "isolated D2.4 flash route missing")
    source = path.read_text()
    required = (
        "verify-audio-level-diag.py",
        "audio-level-diag-d24-one-shot-authorization.json",
        "capture-audio-level-diag.py",
        "hardware/local-state/audio-level-d24.json",
        "--after no_reset write_flash",
        "p4_verify_chunked_application_readback",
        "emit-receipt",
        "remains stopped in the ROM loader",
    )
    require(all(token in source for token in required), "isolated flash route incomplete")
    require("esptool.py --chip esp32p4 --port \"$P4_PORT\" run" not in source,
            "isolated flash route can directly run D2.4")
    require(source.count("preflight-host --port") == 1
            and 'while [ "$P4_PREFLIGHT_RUN" -le 3 ]' in source,
            "isolated route lacks exactly configured three host preflights")
    verifier_call = 'python3 "$P4_VERIFIER" "$P4_BUILD_DIR" app-flash'
    require(source.count(verifier_call) == 3
            and source.count('"$P4_SCRIPT_DIR/build.sh" "$P4_APP"') == 1,
            "isolated route requires one build and three exact verifier checks")
    first_verifier = source.index(verifier_call)
    second_verifier = source.index(verifier_call, first_verifier + 1)
    third_verifier = source.index(verifier_call, second_verifier + 1)
    ordered = [
        source.index('"$P4_SCRIPT_DIR/build.sh" "$P4_APP"'),
        first_verifier,
        'chmod 400 "$P4_SNAPSHOT"',
        "preflight-host --port", "check-issuable", '"$P4_CAPTURE" reserve',
        "P4_PROBE=$(esptool.py",
        second_verifier,
        "--after no_reset write_flash",
        "p4_verify_chunked_application_readback",
        third_verifier,
        '"$P4_CAPTURE" emit-receipt',
        "remains stopped in the ROM loader",
    ]
    ordered = [source.index(item) if isinstance(item, str) else item for item in ordered]
    require(ordered == sorted(ordered) and len(set(ordered)) == len(ordered),
            "isolated route order changed")
    require(source.count(
        verifier_call + '\npython3 "$P4_CAPTURE" emit-receipt'
    ) == 1,
            "final active verifier is not immediately before receipt emission")


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify-audio-level-diag.py <build-dir> <build-only|app-flash>")
    mode = sys.argv[2]
    require(mode in {"build-only", "app-flash"}, "full-project flash is prohibited")
    build_dir = pathlib.Path(sys.argv[1]).resolve()
    require(build_dir.is_dir(), "build directory missing")
    verify_source()
    verify_metadata(mode)
    verify_authorization(mode)
    verify_evidence()
    binary, _ = verify_build(build_dir)
    verify_isolated_flash_route()
    print("audio_level_diag verification: PASS "
          f"mode={mode} offset=0x{APP_OFFSET:x} bytes={binary.stat().st_size} "
          f"sha256={sha256_file(binary)} "
          f"one_shot_app_flash_authorization={'true' if mode == 'app-flash' else 'false'} "
          "reusable_runtime_authorization=false")


if __name__ == "__main__":
    main()
