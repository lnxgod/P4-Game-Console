#!/usr/bin/env python3

"""Exact-unit installer for the frozen badge-free Console OS folder image.

Installation is intentionally unavailable from this script's public CLI. The
separately issued outer route supplies the immutable authorization digest.
Recovery remains directly callable after an interrupted successor write.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import signal
import stat
import sys
import types
from typing import Any, Mapping


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
ROOT = SCRIPT_DIR.parent.resolve(strict=True)
PREDECESSOR_INSTALLER_PATH = SCRIPT_DIR / "console-os-space-invaders-install.py"
AUTH_PATH = ROOT / "hardware/evidence/console-os-folder-clean-exact-unit-authorization.json"
BUILD_PATH = ROOT / "test-runs/2026-08-14-console-os-folder-clean-build.json"
AUDIO_PATH = ROOT / "hardware/evidence/console-os-folder-clean-exact-unit-audio-release.json"
PRIOR_AUDIO_PATH = ROOT / "hardware/evidence/console-os-space-invaders-exact-unit-audio-release.json"
JEDEC_PATH = ROOT / "hardware/evidence/console-os-folder-clean-jedec-normalization.json"
PRIOR_JEDEC_PATH = ROOT / "hardware/evidence/console-os-space-invaders-jedec-normalization.json"
PREDECESSOR_PATH = ROOT / "hardware/test-runs/2026-08-14-console-os-space-invaders-install.json"
PREDECESSOR_LEDGER_PATH = ROOT / "hardware/local-state/console-os-space-invaders-install-20260814-attempt3/e6-install-ledger.json"
ARTIFACT_PATH = ROOT / "apps/console_os/build-folder-clean/p4_console_os.bin"
ELF_PATH = ROOT / "apps/console_os/build-folder-clean/p4_console_os.elf"
BOOTLOADER_PATH = ROOT / "apps/console_os/build-folder-clean/bootloader/bootloader.bin"
PARTITION_PATH = ROOT / "apps/console_os/build-folder-clean/partition_table/partition-table.bin"
REGISTRY_PATH = ROOT / "apps/console_os/build-folder-clean/esp-idf/main/p4_game_registry/p4_game_registry.c"
RECOVERY_PATH = ROOT / "hardware/local-state/console-os-folder-clean-install-20260814"

EXPECTED_PREDECESSOR_INSTALLER_SHA256 = "2166d7997fe49003d0d17a389373e68818bd3abd4536c274488c7c6bf54cbc27"
EXPECTED_DEVICE_SHA256 = "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
EXPECTED_BUILD_SHA256 = "c98c28ea3124beed854efcb96f2172a463c937f852373d56cd2f793ba607d40c"
EXPECTED_AUDIO_SHA256 = "e5f8cf589518766320de4dc50f0de9e74f8e2591de36e43bd0c1c5a1e65e7c0e"
EXPECTED_PRIOR_AUDIO_SHA256 = "a9320abb67c5cab2f11aeb69994fbc45c1474b4f9871ff46de86da3d5c678d1f"
EXPECTED_JEDEC_SHA256 = "180b7303752f9407ffeb44124730d8d97ccbfba671120d8c6dda85d10253ce4c"
EXPECTED_PRIOR_JEDEC_SHA256 = "ad5eb401db052ff3945edfd4c05ae2b9862b33fe041bf41b99b696e52f1508b8"
EXPECTED_PREDECESSOR_RECORD_SHA256 = "d88bcb6023730d2fe70f0b6f9f28c3cd15762d3c79983d358536fbbd3e90f4e8"
EXPECTED_PREDECESSOR_LEDGER_SHA256 = "efa9983ac3d376443253a5a6d235529db5cc2dcb5d5bd6182264199091f026e1"

PREDECESSOR_BYTES = 4_948_512
PREDECESSOR_SHA256 = "9596617fd86d9cd9abd12a539329c6df2d06e696ca4de6814daa644e56090303"
PREDECESSOR_PADDED_BYTES = 4_964_352
PREDECESSOR_PADDED_SHA256 = "8a4c7be1bd6dab0fbc24d16217c8d44f18e6ec4aca3a3eb5b7bc3259b25b1ffe"

SUCCESSOR_BYTES = 4_951_552
SUCCESSOR_SHA256 = "636ee38221197c0c7b02c5d04ea19ddfac0a3c6dbd9b2719d8bb21dfb1658a7d"
SUCCESSOR_PADDED_BYTES = 4_964_352
SUCCESSOR_PADDED_SHA256 = "ad863f0b1b8377692a22168cad5601b3f3f49fa5b7ab126b12d4df98c83a8f96"
SUCCESSOR_CAPTURE_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=7 "
    b"surface=rgb565-320x200 touch=gt911 "
    b"native_game_api=1 native_format=p4-native-static-v1 "
    b"execution=build-candidate"
)
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024
MAX_BUILD_OUTPUT_BYTES = 32 * 1024 * 1024


class InstallError(RuntimeError):
    """The exact badge-free folder install contract was not satisfied."""


def _digest(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise InstallError(f"{label} digest has invalid shape")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise InstallError(f"{label} digest has invalid shape") from error
    return value


def _read_regular(
    path: pathlib.Path, label: str, *, maximum: int,
    required_mode: int | None = None,
) -> bytes:
    before = path.lstat()
    mode = stat.S_IMODE(before.st_mode)
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or not 0 < before.st_size <= maximum
        or (required_mode is not None and mode != required_mode)
        or (required_mode is None and mode & 0o022)
    ):
        raise InstallError(f"{label} metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino, opened.st_size)
            != (before.st_dev, before.st_ino, before.st_size)
            or stat.S_IMODE(opened.st_mode) != mode
        ):
            raise InstallError(f"{label} changed while opening")
        payload = bytearray()
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError(f"{label} was truncated")
            payload.extend(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError(f"{label} grew while reading")
        after = os.fstat(descriptor)
        if (after.st_dev, after.st_ino, after.st_size) != (
            opened.st_dev, opened.st_ino, opened.st_size
        ):
            raise InstallError(f"{label} changed while reading")
        return bytes(payload)
    finally:
        os.close(descriptor)


def _read_json(path: pathlib.Path, label: str, expected_sha256: str) -> dict[str, Any]:
    payload = _read_regular(path, label, maximum=MAX_JSON_BYTES)
    if hashlib.sha256(payload).hexdigest() != _digest(expected_sha256, label):
        raise InstallError(f"{label} changed")
    try:
        value = json.loads(payload.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise InstallError(f"{label} is not valid UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise InstallError(f"{label} is not a JSON object")
    return value


def _bound_json(
    binding: Mapping[str, Any], exact_path: pathlib.Path, label: str,
) -> dict[str, Any]:
    if not isinstance(binding, Mapping):
        raise InstallError(f"{label} binding is missing")
    relative = binding.get("path")
    expected = binding.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise InstallError(f"{label} binding is malformed")
    path = (ROOT / relative).resolve(strict=True)
    if path != exact_path.resolve(strict=True):
        raise InstallError(f"{label} path differs")
    return _read_json(path, label, expected)


def _load_module(path: pathlib.Path, expected_sha256: str, name: str) -> Any:
    payload = _read_regular(
        path.resolve(strict=True), name, maximum=MAX_SOURCE_BYTES,
        required_mode=0o755,
    )
    if hashlib.sha256(payload).hexdigest() != expected_sha256:
        raise InstallError(f"{name} bytes changed")
    module = types.ModuleType(name)
    module.__file__ = str(path)
    module.__package__ = ""
    sys.modules[name] = module
    try:
        exec(compile(payload, str(path), "exec"), module.__dict__)
    except BaseException:
        sys.modules.pop(name, None)
        raise
    return module


def _exact_unit() -> dict[str, Any]:
    return {
        "identity_sha256": EXPECTED_DEVICE_SHA256,
        "chip": "ESP32-P4",
        "chip_revision": "v1.3",
        "flash_bytes": 16_777_216,
        "psram_bytes": 33_554_432,
        "raw_identity_stored": False,
    }


def _validate_predecessor(record: Mapping[str, Any]) -> None:
    hardware = record.get("hardware")
    artifact = record.get("artifact")
    transaction = record.get("transaction")
    startup = record.get("retained_uart_startup")
    ledger = transaction.get("ledger") if isinstance(transaction, Mapping) else None
    if not (
        record.get("schema") == 1
        and record.get("result") == "pass-exact-install-seven-app-launcher-gameplay-acceptance-pending"
        and isinstance(hardware, Mapping)
        and hardware.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and hardware.get("chip") == "ESP32-P4"
        and hardware.get("chip_revision") == "v1.3"
        and hardware.get("flash_bytes") == 16_777_216
        and hardware.get("psram_bytes") == 33_554_432
        and isinstance(artifact, Mapping)
        and artifact.get("offset") == "0x10000"
        and artifact.get("bytes") == PREDECESSOR_BYTES
        and artifact.get("sha256") == PREDECESSOR_SHA256
        and artifact.get("mutation_span_bytes") == PREDECESSOR_PADDED_BYTES
        and artifact.get("mutation_span_sha256") == PREDECESSOR_PADDED_SHA256
        and artifact.get("readback_sha256") == PREDECESSOR_PADDED_SHA256
        and artifact.get("app_partition_only") is True
        and isinstance(transaction, Mapping)
        and transaction.get("same_uart_handle") is True
        and transaction.get("exclusive_uart_open_count") == 1
        and transaction.get("uart_reopen_count") == 0
        and transaction.get("install_write_attempt_count") == 1
        and transaction.get("application_launch_count") == 1
        and transaction.get("restore_required") is False
        and transaction.get("restore_attempt_count") == 0
        and isinstance(ledger, Mapping)
        and ledger.get("path") == str(PREDECESSOR_LEDGER_PATH.relative_to(ROOT))
        and ledger.get("sha256") == EXPECTED_PREDECESSOR_LEDGER_SHA256
        and ledger.get("phase") == "e6-runtime-accepted"
        and isinstance(startup, Mapping)
        and startup.get("result") == "pass"
        and startup.get("startup_signature") == "apps=7 native_game_api=1 native_format=p4-native-static-v1"
        and startup.get("amplifier_energized") is False
        and startup.get("doom_handoffs") == 0
    ):
        raise InstallError("installed flat seven-app predecessor evidence differs")
    completed = _read_json(
        PREDECESSOR_LEDGER_PATH, "completed predecessor ledger",
        EXPECTED_PREDECESSOR_LEDGER_SHA256,
    )
    if not (
        completed.get("phase") == "e6-runtime-accepted"
        and completed.get("restore_required") is False
        and completed.get("install_write_attempt_count") == 1
        and completed.get("application_launch_count") == 1
    ):
        raise InstallError("completed predecessor ledger is not terminal and clean")


def _validate_build(build: Mapping[str, Any], artifact: Mapping[str, Any], transport: Any) -> None:
    exact = build.get("exact_artifact")
    launcher = build.get("launcher_model")
    hardware = build.get("hardware_status")
    registry = build.get("generated_registry")
    if not (
        build.get("schema") == 1
        and build.get("result") == "console-os-folder-clean-host-and-esp32p4-build-verified-not-hardware-tested"
        and isinstance(exact, Mapping)
        and exact.get("build_directory") == "apps/console_os/build-folder-clean"
        and exact.get("build_directory_git_ignored") is True
        and exact.get("offset") == artifact.get("offset")
        and exact.get("app_binary_bytes") == artifact.get("app_binary_bytes")
        and exact.get("app_binary_sha256") == artifact.get("app_binary_sha256")
        and exact.get("elf_bytes") == artifact.get("elf_bytes")
        and exact.get("elf_sha256") == artifact.get("elf_sha256")
        and exact.get("bootloader_sha256") == artifact.get("bootloader_sha256")
        and exact.get("partition_table_sha256") == artifact.get("partition_table_sha256")
        and exact.get("mutation_span_bytes") == artifact.get("mutation_span_bytes")
        and exact.get("mutation_span_sha256") == artifact.get("mutation_span_sha256")
        and isinstance(launcher, Mapping)
        and launcher.get("registered_app_count") == 7
        and launcher.get("root_tiles") == ["ALL PROGRAMS", "GAMES", "SYSTEM"]
        and launcher.get("game_type_folders") == ["ACTION", "ARCADE"]
        and launcher.get("kid_facing_capability_badges_visible") is False
        and launcher.get("capability_metadata_retained_internally") is True
        and isinstance(hardware, Mapping)
        and hardware.get("hardware_execution_authorized") is False
        and hardware.get("flash_authorized") is False
        and hardware.get("flash_attempted") is False
        and hardware.get("application_bytes_written") == 0
        and isinstance(registry, Mapping)
        and registry.get("path") == str(REGISTRY_PATH.relative_to(ROOT))
        and registry.get("sha256") == "a19da762c90bf7801af9ce759bec6ed1ea16b5f13af99fab822cabbc8ae83f06"
        and registry.get("registered_game_count") == 2
        and registry.get("parallel_folder_table_present") is True
    ):
        raise InstallError("badge-free folder build evidence differs")
    inventory = build.get("source_inventory")
    if not isinstance(inventory, Mapping) or len(inventory) != 8:
        raise InstallError("badge-free folder source inventory differs")
    transport._validate_inventory(inventory)
    registry_payload = _read_regular(
        REGISTRY_PATH, "frozen generated game registry", maximum=MAX_SOURCE_BYTES,
    )
    if (
        hashlib.sha256(registry_payload).hexdigest() != registry.get("sha256")
        or b"const size_t p4_generated_game_count = 2U;" not in registry_payload
        or b'"GAMES/ARCADE"' not in registry_payload
    ):
        raise InstallError("frozen generated folder registry differs")


def _validate_audio(release: Mapping[str, Any], artifact: Mapping[str, Any]) -> None:
    runtime = release.get("authorized_audio_runtime")
    safety = release.get("execution_safety")
    if not (
        release.get("schema") == 1
        and release.get("active") is True
        and release.get("scope") == "one-device-console-os-folder-clean-factory-audio"
        and release.get("exact_unit") == _exact_unit()
        and release.get("exact_artifact") == {
            "offset": artifact.get("offset"),
            "app_binary_bytes": artifact.get("app_binary_bytes"),
            "app_binary_sha256": artifact.get("app_binary_sha256"),
            "mutation_span_bytes": artifact.get("mutation_span_bytes"),
            "mutation_tail_byte": "ff",
        }
        and release.get("operator_risk_acceptance") == {
            "unresolved_amplifier_topology_risk_accepted": True,
            "gpio24_pdm_clock_may_feed_codec_mclk": True,
            "population_or_continuity_proof_available": False,
            "connected_unit_only": True,
            "reusable_authorization": False,
            "cross_unit_authorization": False,
            "prior_exact_unit_doom_sfx_and_music_audible": True,
        }
        and isinstance(runtime, Mapping)
        and runtime.get("launcher_audio_hardware_calls") == 0
        and runtime.get("launcher_amplifier_energized") is False
        and runtime.get("native_game_audio_deferred_until_game_launch") is True
        and runtime.get("backend") == "complete-pinned-factory-initializer"
        and runtime.get("sample_rate_hz") == 16_000
        and runtime.get("format") == "signed-pcm16-stereo"
        and runtime.get("pdm_clock_gpio") == 24
        and runtime.get("lrclk_gpio") == 21
        and runtime.get("bclk_gpio") == 22
        and runtime.get("dout_gpio") == 23
        and runtime.get("amplifier_shutdown_gpio") == 30
        and runtime.get("amplifier_enable_level") == 0
        and runtime.get("external_codec_i2c_transactions") == 0
        and runtime.get("backend_volume_step") == "6/10"
        and isinstance(safety, Mapping)
        and safety.get("app_partition_only") is True
        and safety.get("same_uart_handle") is True
        and safety.get("live_full_successor_span_preimage_required") is True
        and safety.get("exact_readback_required") is True
        and safety.get("automatic_predecessor_restore_on_startup_rejection") is True
        and safety.get("startup_acceptance_keeps_amplifier_off") is True
    ):
        raise InstallError("badge-free folder exact-unit audio release differs")
    prior = _bound_json(release.get("prior_audio_release"), PRIOR_AUDIO_PATH, "prior audio release")
    if not (
        release.get("prior_audio_release", {}).get("sha256") == EXPECTED_PRIOR_AUDIO_SHA256
        and prior.get("schema") == 1
        and prior.get("active") is True
        and prior.get("exact_unit") == _exact_unit()
        and prior.get("exact_artifact", {}).get("app_binary_sha256") == PREDECESSOR_SHA256
    ):
        raise InstallError("prior exact-unit audio release differs")


def _validate_jedec(record: Mapping[str, Any], artifact: Mapping[str, Any]) -> None:
    contract = record.get("normalization_contract")
    observation = record.get("installed_predecessor_observation")
    if not (
        record.get("schema") == 1
        and record.get("active") is True
        and record.get("result") == "release-prior-success-bound-to-new-exact-successor-before-flash"
        and record.get("scope") == "console-os-folder-clean-exact-unit-install-boundaries"
        and record.get("exact_unit") == {k: v for k, v in _exact_unit().items() if k != "psram_bytes"}
        and record.get("exact_artifact") == {
            "offset": artifact.get("offset"),
            "app_binary_bytes": artifact.get("app_binary_bytes"),
            "app_binary_sha256": artifact.get("app_binary_sha256"),
            "mutation_span_bytes": artifact.get("mutation_span_bytes"),
        }
        and isinstance(observation, Mapping)
        and observation.get("path") == str(PREDECESSOR_PATH.relative_to(ROOT))
        and observation.get("sha256") == EXPECTED_PREDECESSOR_RECORD_SHA256
        and observation.get("post_stub_initial_raw") == "0x001840c8"
        and observation.get("write_boundary_raw") == "0xab1840c8"
        and observation.get("prelaunch_raw") == "0xff1840c8"
        and observation.get("canonical_low24") == "0x1840c8"
        and observation.get("restore_required") is False
        and contract == {
            "accepted_high_bytes": ["0x00", "0xab", "0xfc", "0xff"],
            "required_low_24_bits": "0x1840c8",
            "wrong_low_24_bits_fail_closed": True,
            "all_other_high_bytes_fail_closed": True,
            "exact_device_identity_still_required": True,
            "exact_16_mib_flash_geometry_still_required": True,
            "validated_partition_table_still_required": True,
            "sealed_live_preimage_still_required": True,
            "scope_is_cross_unit": False,
            "scope_is_reusable": False,
        }
    ):
        raise InstallError("badge-free folder JEDEC binding differs")
    prior = _bound_json(
        record.get("prior_exact_unit_evidence"), PRIOR_JEDEC_PATH,
        "prior JEDEC evidence",
    )
    if not (
        record.get("prior_exact_unit_evidence", {}).get("sha256") == EXPECTED_PRIOR_JEDEC_SHA256
        and prior.get("active") is True
        and prior.get("exact_unit", {}).get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and prior.get("normalization_contract", {}).get("required_low_24_bits") == "0x1840c8"
        and prior.get("normalization_contract", {}).get("scope_is_cross_unit") is False
    ):
        raise InstallError("prior exact-unit JEDEC evidence differs")


def _validate_build_outputs(artifact: Mapping[str, Any]) -> None:
    for path, label, size_key, hash_key in (
        (ELF_PATH, "frozen ELF", "elf_bytes", "elf_sha256"),
        (BOOTLOADER_PATH, "frozen bootloader", "bootloader_bytes", "bootloader_sha256"),
        (PARTITION_PATH, "frozen partition table", "partition_table_bytes", "partition_table_sha256"),
    ):
        payload = _read_regular(path, label, maximum=MAX_BUILD_OUTPUT_BYTES)
        if len(payload) != artifact.get(size_key) or hashlib.sha256(payload).hexdigest() != artifact.get(hash_key):
            raise InstallError(f"{label} identity differs")


def _contract_from_authorization(
    authorization: pathlib.Path, authorization_sha256: str, transport: Any,
) -> Any:
    if authorization.resolve(strict=True) != AUTH_PATH.resolve(strict=True):
        raise InstallError("authorization path differs")
    auth = _read_json(authorization, "badge-free folder exact-unit authorization", authorization_sha256)
    artifact = auth.get("exact_artifact")
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-console-os-folder-clean"
        and auth.get("operator_direction") == "flash-the-folder-image-without-developer-capability-badges"
        and auth.get("device_binding") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
        }
        and isinstance(artifact, Mapping)
    ):
        raise InstallError("authorization scope or device binding differs")
    try:
        contract = transport.ArtifactContract(
            int(str(artifact.get("offset")), 0),
            int(artifact.get("app_binary_bytes")),
            _digest(artifact.get("app_binary_sha256"), "artifact"),
            int(artifact.get("mutation_span_bytes")),
        )
    except (TypeError, ValueError) as error:
        raise InstallError("artifact geometry is malformed") from error
    if not (
        contract.offset == 0x10000
        and contract.artifact_bytes == SUCCESSOR_BYTES
        and contract.artifact_sha256 == SUCCESSOR_SHA256
        and contract.mutation_span_bytes == SUCCESSOR_PADDED_BYTES
        and contract.mutation_span_bytes >= PREDECESSOR_PADDED_BYTES
        and contract.mutation_span_bytes % transport.WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes == (
            (contract.artifact_bytes + transport.WRITE_BLOCK_BYTES - 1)
            // transport.WRITE_BLOCK_BYTES
        ) * transport.WRITE_BLOCK_BYTES
        and artifact.get("mutation_span_sha256") == SUCCESSOR_PADDED_SHA256
        and artifact.get("mutation_tail_byte") == "ff"
        and artifact.get("contains_ignored_shareware_wad") is True
        and artifact.get("redistribution_authorized") is False
    ):
        raise InstallError("exact badge-free folder artifact geometry differs")

    predecessor_binding = auth.get("installed_predecessor")
    predecessor = _bound_json(predecessor_binding, PREDECESSOR_PATH, "installed predecessor")
    build = _bound_json(auth.get("build_evidence"), BUILD_PATH, "build evidence")
    audio = _bound_json(auth.get("exact_unit_audio_release"), AUDIO_PATH, "audio release")
    jedec = _bound_json(auth.get("jedec_normalization"), JEDEC_PATH, "JEDEC binding")
    if not (
        auth.get("build_evidence", {}).get("sha256") == EXPECTED_BUILD_SHA256
        and auth.get("exact_unit_audio_release", {}).get("sha256") == EXPECTED_AUDIO_SHA256
        and auth.get("jedec_normalization", {}).get("sha256") == EXPECTED_JEDEC_SHA256
        and predecessor_binding.get("sha256") == EXPECTED_PREDECESSOR_RECORD_SHA256
    ):
        raise InstallError("authorization evidence digest differs")
    _validate_predecessor(predecessor)
    _validate_build(build, artifact, transport)
    _validate_audio(audio, artifact)
    _validate_jedec(jedec, artifact)
    _validate_build_outputs(artifact)

    if auth.get("launcher_contract") != {
        "registered_app_count": 7,
        "root_tiles": ["ALL PROGRAMS", "GAMES", "SYSTEM"],
        "game_type_folders": ["ACTION", "ARCADE"],
        "kid_facing_capability_badges_visible": False,
        "capability_metadata_retained_internally": True,
    }:
        raise InstallError("launcher contract differs")
    if auth.get("frozen_primitives") != {
        "predecessor_installer_sha256": EXPECTED_PREDECESSOR_INSTALLER_SHA256,
        "base_installer_sha256": "0887dbcc43d79f48e06628eca21c518974ef900ff8f8cb025d63bb2bbf44c491",
        "transport_sha256": "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471",
        "startup_capture_sha256": "cdb2c124f0f1e58aaaac7f13d416eff00cb044cc0f13232afa8537d8645aa571",
        "startup_signature": SUCCESSOR_CAPTURE_START.decode("ascii"),
        "accepted_rdid_high_bytes": ["0x00", "0xab", "0xfc", "0xff"],
    }:
        raise InstallError("frozen primitive binding differs")
    if auth.get("execution_contract") != {
        "same_uart_handle": True,
        "live_full_span_preimage_required": True,
        "app_partition_only": True,
        "retained_launcher_capture_required": True,
        "automatic_restore_on_capture_failure": True,
        "launcher_audio_hardware_calls": 0,
        "native_game_audio_deferred_until_user_launch": True,
        "usb_runtime": False,
        "recovery_directory": "hardware/local-state/console-os-folder-clean-install-20260814",
    }:
        raise InstallError("execution contract differs")
    return contract


def _load_primitives() -> tuple[Any, Any, Any]:
    predecessor = _load_module(
        PREDECESSOR_INSTALLER_PATH,
        EXPECTED_PREDECESSOR_INSTALLER_SHA256,
        "folder_clean_frozen_predecessor",
    )
    _base, transport, capture = predecessor._load_primitives()
    if (
        transport.INSTALLED_E5_BYTES,
        transport.INSTALLED_E5_SHA256,
        transport.INSTALLED_E5_PADDED_SPAN_BYTES,
        transport.INSTALLED_E5_PADDED_SPAN_SHA256,
    ) != (
        4_941_776,
        "f4426c370b9f251aa5a3ae5049b99a4c068067390c351dc680d1b7d36492be36",
        4_947_968,
        "23ec0f631b86bf3b8bbd44e1fc791d2fcf699de9a36b6d6c6c8075c4ea996092",
    ):
        raise InstallError("frozen predecessor transport constants differ")
    if (
        transport.E5.EXPECTED_FLASH_JEDEC_LOW24 != 0x1840C8
        or transport.E5.ALLOWED_FLASH_JEDEC_HIGH8 != {0x00, 0xAB, 0xFC, 0xFF}
        or capture.START != SUCCESSOR_CAPTURE_START
        or capture.FIXED[0] != ("start", SUCCESSOR_CAPTURE_START)
    ):
        raise InstallError("frozen predecessor runtime contract differs")
    transport.INSTALLED_E5_BYTES = PREDECESSOR_BYTES
    transport.INSTALLED_E5_SHA256 = PREDECESSOR_SHA256
    transport.INSTALLED_E5_PADDED_SPAN_BYTES = PREDECESSOR_PADDED_BYTES
    transport.INSTALLED_E5_PADDED_SPAN_SHA256 = PREDECESSOR_PADDED_SHA256
    return predecessor, transport, capture


def install_from_trust_anchor(
    *, port: str, artifact: pathlib.Path, authorization: pathlib.Path,
    authorization_sha256: str, recovery_directory: pathlib.Path,
    capture_seconds: float = 30.0,
) -> dict[str, Any]:
    _predecessor, transport, capture = _load_primitives()
    contract = _contract_from_authorization(
        authorization, authorization_sha256, transport,
    )
    if artifact.resolve(strict=True) != ARTIFACT_PATH.resolve(strict=True):
        raise InstallError("artifact path differs from the frozen build output")
    transport._read_sealed_artifact(artifact, contract)
    if recovery_directory.resolve(strict=True) != RECOVERY_PATH.resolve():
        raise InstallError("recovery directory differs from authorization")
    transport._owned_directory(recovery_directory, "recovery directory")

    previous = transport._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = transport.E5._production_runtime()
        device = transport.E5.open_serial_once(port)
        result = transport.install_same_handle(
            device=device,
            artifact_path=artifact,
            contract=contract,
            recovery_directory=recovery_directory,
            runtime=runtime,
            capture_module=capture,
            capture_seconds=capture_seconds,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                transport.E5._set_controls_false(device)
                device.close()
                if getattr(device, "is_open", False):
                    raise InstallError("exclusive UART did not close")
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise InstallError("badge-free folder transaction ended without a result")
    return {
        **result,
        "app": "console_os",
        "native_game_api": 1,
        "registered_apps": 7,
        "root_tiles": ["ALL PROGRAMS", "GAMES", "SYSTEM"],
        "game_type_folders": ["ACTION", "ARCADE"],
        "kid_facing_capability_badges_visible": False,
        "startup_classification": "launcher-seven-apps-amp-not-energized",
        "transport_core": "frozen-doom-e6-retained-uart-recovery-state-machine",
        "restorable_predecessor": "console-os-space-invaders-flat-launcher",
    }


def recover(port: str, recovery_directory: pathlib.Path) -> dict[str, Any]:
    _predecessor, transport, _capture = _load_primitives()
    if recovery_directory.resolve(strict=True) != RECOVERY_PATH.resolve():
        raise InstallError("recovery directory differs from authorization")
    previous = transport._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = transport.E5._production_runtime()
        device = transport.E5.open_serial_once(port)
        result = transport.recover_same_handle(
            device=device, recovery_directory=recovery_directory, runtime=runtime,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                transport.E5._set_controls_false(device)
                device.close()
            except BaseException as close_error:
                if error is None:
                    error = close_error
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    if error is not None:
        raise error
    if result is None:
        raise InstallError("recovery ended without a result")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("recover",))
    parser.add_argument("--port", required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    args = parser.parse_args()
    result = recover(args.port, args.recovery_directory)
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
