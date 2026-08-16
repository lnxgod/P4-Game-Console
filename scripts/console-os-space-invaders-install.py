#!/usr/bin/env python3

"""Exact-unit installer for the frozen Console OS Space Invaders artifact.

Installation is intentionally unavailable from this script's public CLI.  The
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
BASE_INSTALLER_PATH = SCRIPT_DIR / "console-os-install.py"
PREDECESSOR_INSTALLER_PATH = SCRIPT_DIR / "console-os-game-api-v1-install.py"
AUTH_PATH = ROOT / "hardware/evidence/console-os-space-invaders-exact-unit-authorization.json"
BUILD_PATH = ROOT / "test-runs/2026-08-14-console-os-space-invaders-build.json"
AUDIO_PATH = ROOT / "hardware/evidence/console-os-space-invaders-exact-unit-audio-release.json"
PRIOR_AUDIO_PATH = ROOT / "hardware/evidence/console-os-game-api-v1-exact-unit-audio-release.json"
JEDEC_PATH = ROOT / "hardware/evidence/console-os-space-invaders-jedec-normalization.json"
PRIOR_JEDEC_PATH = ROOT / "hardware/evidence/console-os-game-api-v1-jedec-normalization.json"
PREDECESSOR_PATH = ROOT / "hardware/test-runs/2026-08-14-console-os-game-api-v1-install.json"
ATTEMPT1_PATH = ROOT / "hardware/test-runs/2026-08-14-console-os-space-invaders-install-attempt1.json"
ATTEMPT2_PATH = ROOT / "hardware/test-runs/2026-08-14-console-os-space-invaders-install-attempt2.json"
ARTIFACT_PATH = ROOT / "apps/console_os/build-space-invaders/p4_console_os.bin"
RECOVERY_PATH = ROOT / "hardware/local-state/console-os-space-invaders-install-20260814-attempt3"

EXPECTED_BASE_SHA256 = "0887dbcc43d79f48e06628eca21c518974ef900ff8f8cb025d63bb2bbf44c491"
EXPECTED_PREDECESSOR_INSTALLER_SHA256 = "406d0b6da57029890f2e4f92bb6d6aed32e3fc2e0f4c81ee7ddbddc48e08ff86"
EXPECTED_TRANSPORT_SHA256 = "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
EXPECTED_CAPTURE_SHA256 = "cdb2c124f0f1e58aaaac7f13d416eff00cb044cc0f13232afa8537d8645aa571"
EXPECTED_DEVICE_SHA256 = "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
EXPECTED_BUILD_SHA256 = "1ba05b3eed3913470e3e4812130ec7d2fdd81b1b6108faea45c7d68fde720537"
EXPECTED_AUDIO_SHA256 = "a9320abb67c5cab2f11aeb69994fbc45c1474b4f9871ff46de86da3d5c678d1f"
EXPECTED_PRIOR_AUDIO_SHA256 = "c01638869e7859fc509cbde001f0dcf02ef875778d4d19e0af832f2709242722"
EXPECTED_JEDEC_SHA256 = "ad5eb401db052ff3945edfd4c05ae2b9862b33fe041bf41b99b696e52f1508b8"
EXPECTED_PRIOR_JEDEC_SHA256 = "041a521af4050c1dfdd944adb64dae8ffa5f340d8b0a2ff7255dfe47b7fd33e9"
EXPECTED_PREDECESSOR_RECORD_SHA256 = "4b4f803d6567da035734c7fd505448a646778f8ad75db03440f6de6c400bf0e5"

PREDECESSOR_BYTES = 4_941_776
PREDECESSOR_SHA256 = "f4426c370b9f251aa5a3ae5049b99a4c068067390c351dc680d1b7d36492be36"
PREDECESSOR_PADDED_BYTES = 4_947_968
PREDECESSOR_PADDED_SHA256 = "23ec0f631b86bf3b8bbd44e1fc791d2fcf699de9a36b6d6c6c8075c4ea996092"

ORIGINAL_CAPTURE_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=5 "
    b"surface=rgb565-320x200 touch=gt911 audio=safe-handoff-only "
    b"execution=guarded-exact-unit-candidate"
)
SUCCESSOR_CAPTURE_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=7 "
    b"surface=rgb565-320x200 touch=gt911 "
    b"native_game_api=1 native_format=p4-native-static-v1 "
    b"execution=build-candidate"
)
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024


class InstallError(RuntimeError):
    """The exact Space Invaders install contract was not satisfied."""


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
    if not (
        record.get("schema") == 1
        and record.get("result") == "pass-exact-install-launcher-and-maze-operator-confirmed"
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
        and transaction.get("restore_required") is False
        and transaction.get("restore_attempt_count") == 0
        and isinstance(startup, Mapping)
        and startup.get("result") == "pass"
        and startup.get("amplifier_energized") is False
        and startup.get("doom_handoffs") == 0
    ):
        raise InstallError("installed six-app predecessor evidence differs")


def _validate_audio(release: Mapping[str, Any], artifact: Mapping[str, Any]) -> None:
    unit = dict(_exact_unit())
    expected_artifact = {
        "offset": artifact.get("offset"),
        "app_binary_bytes": artifact.get("app_binary_bytes"),
        "app_binary_sha256": artifact.get("app_binary_sha256"),
        "mutation_span_bytes": artifact.get("mutation_span_bytes"),
        "mutation_tail_byte": "ff",
    }
    runtime = release.get("authorized_audio_runtime")
    safety = release.get("execution_safety")
    if not (
        release.get("schema") == 1
        and release.get("active") is True
        and release.get("scope") == "one-device-console-os-space-invaders-factory-audio"
        and release.get("exact_unit") == unit
        and release.get("exact_artifact") == expected_artifact
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
        and safety == {
            "app_partition_only": True,
            "same_uart_handle": True,
            "live_full_successor_span_preimage_required": True,
            "exact_readback_required": True,
            "automatic_predecessor_restore_on_startup_rejection": True,
            "startup_acceptance_keeps_amplifier_off": True,
            "manual_space_invaders_audio_acceptance_after_launcher_boot": True,
        }
    ):
        raise InstallError("Space Invaders exact-unit audio release differs")
    prior = _bound_json(release.get("prior_audio_release"), PRIOR_AUDIO_PATH, "prior audio release")
    if not (
        hashlib.sha256(_read_regular(PRIOR_AUDIO_PATH, "prior audio release", maximum=MAX_JSON_BYTES)).hexdigest()
        == EXPECTED_PRIOR_AUDIO_SHA256
        and prior.get("schema") == 1
        and prior.get("active") is True
        and prior.get("scope") == "one-device-console-os-game-api-v1-factory-audio"
        and prior.get("exact_unit") == unit
        and prior.get("exact_artifact", {}).get("app_binary_sha256") == PREDECESSOR_SHA256
    ):
        raise InstallError("prior exact-unit audio release differs")


def _validate_jedec(record: Mapping[str, Any], artifact: Mapping[str, Any]) -> None:
    prior_binding = record.get("prior_exact_unit_evidence")
    contract = record.get("normalization_contract")
    if not (
        record.get("schema") == 1
        and record.get("active") is True
        and record.get("result") == "release-observed-0xab-high-byte-with-exact-low24-before-flash-begin"
        and record.get("scope") == "console-os-space-invaders-exact-unit-install-boundaries"
        and record.get("exact_unit") == {k: v for k, v in _exact_unit().items() if k != "psram_bytes"}
        and record.get("exact_artifact") == {
            "offset": artifact.get("offset"),
            "app_binary_bytes": artifact.get("app_binary_bytes"),
            "app_binary_sha256": artifact.get("app_binary_sha256"),
            "mutation_span_bytes": artifact.get("mutation_span_bytes"),
        }
        and isinstance(prior_binding, Mapping)
        and prior_binding.get("sha256") == EXPECTED_PRIOR_JEDEC_SHA256
        and prior_binding.get("observed_write_boundary_raw") == "0xfc1840c8"
        and prior_binding.get("observed_prelaunch_raw") == "0xff1840c8"
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
        raise InstallError("Space Invaders JEDEC binding differs")
    prior = _bound_json(prior_binding, PRIOR_JEDEC_PATH, "prior JEDEC evidence")
    if not (
        prior.get("result") == "release-observed-0xfc-high-byte-with-exact-low24"
        and prior.get("exact_unit", {}).get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and prior.get("normalization_contract", {}).get("required_low_24_bits") == "0x1840c8"
        and prior.get("normalization_contract", {}).get("scope_is_cross_unit") is False
    ):
        raise InstallError("prior exact-unit JEDEC evidence differs")
    observation_binding = record.get("current_exact_observation")
    attempt = _bound_json(observation_binding, ATTEMPT1_PATH, "preceding fail-closed attempt")
    if not (
        isinstance(observation_binding, Mapping)
        and observation_binding.get("stage") == "e6-write-boundary-before-flash-begin"
        and observation_binding.get("raw_value") == "0xab1840c8"
        and observation_binding.get("canonical_low24") == "0x1840c8"
        and observation_binding.get("writes_attempted") == 0
        and observation_binding.get("restore_required") is False
        and attempt.get("result") == "fail-closed-before-flash-begin-new-unused-rdid-container-byte"
        and attempt.get("hardware", {}).get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and attempt.get("artifact", {}).get("sha256") == "9596617fd86d9cd9abd12a539329c6df2d06e696ca4de6814daa644e56090303"
        and attempt.get("transaction", {}).get("install_write_attempt_count") == 0
        and attempt.get("transaction", {}).get("restore_required") is False
        and attempt.get("jedec_observation", {}).get("write_boundary_raw") == "0xab1840c8"
    ):
        raise InstallError("preceding fail-closed RDID observation differs")


def _validate_build(build: Mapping[str, Any], artifact: Mapping[str, Any], transport: Any) -> None:
    exact = build.get("exact_artifact")
    game = build.get("game")
    hardware = build.get("hardware_status")
    if not (
        build.get("schema") == 1
        and build.get("result") == "space-invaders-host-and-console-os-build-verified-not-hardware-tested"
        and isinstance(exact, Mapping)
        and exact.get("build_directory") == "apps/console_os/build-space-invaders"
        and exact.get("offset") == artifact.get("offset")
        and exact.get("app_binary_bytes") == artifact.get("app_binary_bytes")
        and exact.get("app_binary_sha256") == artifact.get("app_binary_sha256")
        and exact.get("elf_bytes") == artifact.get("elf_bytes")
        and exact.get("elf_sha256") == artifact.get("elf_sha256")
        and exact.get("bootloader_sha256") == artifact.get("bootloader_sha256")
        and exact.get("partition_table_sha256") == artifact.get("partition_table_sha256")
        and exact.get("mutation_span_bytes") == artifact.get("mutation_span_bytes")
        and isinstance(game, Mapping)
        and game.get("id") == "org.p4console.space-invaders"
        and game.get("format") == "p4-native-static-v1"
        and game.get("api_version") == 1
        and game.get("implementation") == "original-clean-room-code-rendered-geometry-only"
        and game.get("third_party_arcade_rom_sprite_font_sound_map_or_artwork") is False
        and isinstance(hardware, Mapping)
        and hardware.get("hardware_execution_authorized") is False
        and hardware.get("flash_authorized") is False
        and hardware.get("flash_attempted") is False
        and hardware.get("application_bytes_written") == 0
    ):
        raise InstallError("Space Invaders build evidence differs")
    inventory = build.get("source_inventory")
    provenance = build.get("post_build_provenance_checks")
    required = {
        "games/space_invaders/CMakeLists.txt",
        "games/space_invaders/game.json",
        "games/space_invaders/include/p4_games/space_invaders.h",
        "games/space_invaders/src/space_invaders.c",
        "games/space_invaders/src/space_invaders_internal.h",
        "games/space_invaders/tests/test_space_invaders.c",
        "games/space_invaders/tools/render_preview.c",
        "toolchain.lock.json",
    }
    if not isinstance(inventory, Mapping) or set(inventory) != required:
        raise InstallError("Space Invaders build source inventory differs")
    build_inventory = dict(inventory)
    if build_inventory.pop("games/space_invaders/game.json") != "062551e4bc772f90f9715db961511166873b91a647dcba6dc2c87aefae1069d1":
        raise InstallError("recorded build-time game manifest digest differs")
    transport._validate_inventory(build_inventory)

    correction = provenance.get("transport_mutation_span_correction") if isinstance(provenance, Mapping) else None
    live = provenance.get("live_manifest_after_build") if isinstance(provenance, Mapping) else None
    if not (
        isinstance(correction, Mapping)
        and correction.get("originally_recorded_rounding") == "4-KiB"
        and correction.get("required_frozen_transport_rounding") == "16-KiB"
        and correction.get("corrected_mutation_span_bytes") == 4_964_352
        and correction.get("corrected_mutation_span_sha256") == "8a4c7be1bd6dab0fbc24d16217c8d44f18e6ec4aca3a3eb5b7bc3259b25b1ffe"
        and correction.get("artifact_bytes_changed") is False
        and correction.get("hardware_writes_before_correction") == 0
        and isinstance(live, Mapping)
        and live.get("path") == "games/space_invaders/game.json"
        and live.get("sha256") == "00b75e6cc88db45d05a1640ac540879ed93ab9ba09cd80e6b6f9cb3cb0ea4106"
        and live.get("byte_identical_to_recorded_build_input") is False
        and live.get("generated_registry_path") == "apps/console_os/build-space-invaders/esp-idf/main/p4_game_registry/p4_game_registry.c"
        and live.get("generated_registry_sha256") == "1e728c42b7ffaddcfa739c0253673854f81bbf2905f4f570d778781e37a53b16"
        and live.get("generated_registry_contains_space_invaders_entry") is True
        and live.get("exact_binary_contains_id_title_and_subtitle") is True
        and live.get("rebuild_performed") is False
    ):
        raise InstallError("post-build provenance correction differs")
    manifest_payload = _read_regular(
        ROOT / "games/space_invaders/game.json", "live Space Invaders manifest",
        maximum=MAX_JSON_BYTES,
    )
    if hashlib.sha256(manifest_payload).hexdigest() != live.get("sha256"):
        raise InstallError("live Space Invaders manifest changed")
    manifest = json.loads(manifest_payload.decode("utf-8", "strict"))
    if not (
        isinstance(manifest, Mapping)
        and manifest.get("schema") == 1
        and manifest.get("format") == "p4-native-static-v1"
        and manifest.get("api_version") == 1
        and manifest.get("component") == "space_invaders"
        and manifest.get("entry_symbol") == "p4_space_invaders_game"
        and manifest.get("launcher_id") == 101
        and manifest.get("id") == "org.p4console.space-invaders"
        and manifest.get("title") == "SPACE INVADERS"
        and manifest.get("subtitle") == "DEFEND THE P4"
        and manifest.get("enabled") is True
    ):
        raise InstallError("live Space Invaders manifest semantics differ")
    registry_payload = _read_regular(
        ROOT / str(live.get("generated_registry_path")),
        "frozen generated game registry", maximum=MAX_SOURCE_BYTES,
    )
    if (
        hashlib.sha256(registry_payload).hexdigest() != live.get("generated_registry_sha256")
        or b"extern const p4_game_descriptor_t p4_space_invaders_game;" not in registry_payload
        or b"&p4_space_invaders_game," not in registry_payload
        or b"const size_t p4_generated_game_count = 2U;" not in registry_payload
    ):
        raise InstallError("frozen generated game registry differs")


def _contract_from_authorization(
    authorization: pathlib.Path, authorization_sha256: str, transport: Any,
) -> Any:
    if authorization.resolve(strict=True) != AUTH_PATH.resolve(strict=True):
        raise InstallError("authorization path differs")
    auth = _read_json(authorization, "Space Invaders exact-unit authorization", authorization_sha256)
    artifact = auth.get("exact_artifact")
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-console-os-space-invaders"
        and auth.get("operator_direction") == "push-this-game"
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
        and contract.artifact_bytes == 4_948_512
        and contract.artifact_sha256 == "9596617fd86d9cd9abd12a539329c6df2d06e696ca4de6814daa644e56090303"
        and contract.mutation_span_bytes == 4_964_352
        and contract.mutation_span_bytes >= PREDECESSOR_PADDED_BYTES
        and contract.mutation_span_bytes % transport.WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes == ((contract.artifact_bytes + transport.WRITE_BLOCK_BYTES - 1) // transport.WRITE_BLOCK_BYTES) * transport.WRITE_BLOCK_BYTES
        and artifact.get("mutation_span_sha256") == "8a4c7be1bd6dab0fbc24d16217c8d44f18e6ec4aca3a3eb5b7bc3259b25b1ffe"
        and artifact.get("mutation_tail_byte") == "ff"
        and artifact.get("contains_ignored_shareware_wad") is True
        and artifact.get("redistribution_authorized") is False
    ):
        raise InstallError("exact Space Invaders artifact geometry differs")

    predecessor_binding = auth.get("installed_predecessor")
    predecessor = _bound_json(predecessor_binding, PREDECESSOR_PATH, "installed predecessor")
    build = _bound_json(auth.get("build_evidence"), BUILD_PATH, "build evidence")
    audio = _bound_json(auth.get("exact_unit_audio_release"), AUDIO_PATH, "audio release")
    jedec = _bound_json(auth.get("jedec_normalization"), JEDEC_PATH, "JEDEC binding")
    preceding_bindings = auth.get("preceding_fail_closed_attempts")
    if not isinstance(preceding_bindings, list) or len(preceding_bindings) != 2:
        raise InstallError("authorization preceding-attempt bindings differ")
    preceding1 = _bound_json(preceding_bindings[0], ATTEMPT1_PATH, "preceding fail-closed attempt 1")
    preceding2 = _bound_json(preceding_bindings[1], ATTEMPT2_PATH, "preceding fail-closed attempt 2")
    _validate_predecessor(predecessor)
    _validate_build(build, artifact, transport)
    _validate_audio(audio, artifact)
    _validate_jedec(jedec, artifact)
    if not (
        preceding1.get("result") == "fail-closed-before-flash-begin-new-unused-rdid-container-byte"
        and preceding2.get("result") == "fail-closed-before-write-boundary-usb-serial-disconnected-during-second-live-verification"
        and all(item.get("writes_attempted") == 0 for item in preceding_bindings)
        and all(item.get("restore_required") is False for item in preceding_bindings)
        and preceding2.get("transaction", {}).get("install_write_attempt_count") == 0
        and preceding2.get("transaction", {}).get("restore_required") is False
    ):
        raise InstallError("authorization preceding-attempt binding differs")
    if audio.get("installed_predecessor") != dict(predecessor_binding):
        raise InstallError("audio release predecessor binding differs")

    predecessor_installer = _read_regular(
        PREDECESSOR_INSTALLER_PATH.resolve(strict=True), "predecessor installer",
        maximum=MAX_SOURCE_BYTES, required_mode=0o755,
    )
    if hashlib.sha256(predecessor_installer).hexdigest() != EXPECTED_PREDECESSOR_INSTALLER_SHA256:
        raise InstallError("predecessor installer bytes changed")
    if auth.get("frozen_primitives") != {
        "base_installer_sha256": EXPECTED_BASE_SHA256,
        "predecessor_installer_sha256": EXPECTED_PREDECESSOR_INSTALLER_SHA256,
        "transport_sha256": EXPECTED_TRANSPORT_SHA256,
        "startup_capture_sha256": EXPECTED_CAPTURE_SHA256,
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
        "recovery_directory": "hardware/local-state/console-os-space-invaders-install-20260814-attempt3",
    }:
        raise InstallError("execution contract differs")
    return contract


def _configure_transport(transport: Any) -> None:
    if (
        transport.INSTALLED_E5_BYTES,
        transport.INSTALLED_E5_SHA256,
        transport.INSTALLED_E5_PADDED_SPAN_BYTES,
        transport.INSTALLED_E5_PADDED_SPAN_SHA256,
    ) != (
        4_898_400,
        "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8",
        4_898_816,
        "9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9",
    ):
        raise InstallError("frozen transport predecessor constants differ")
    if transport.E5.EXPECTED_FLASH_JEDEC_LOW24 != 0x1840C8 or transport.E5.ALLOWED_FLASH_JEDEC_HIGH8 != {0x00, 0xFF}:
        raise InstallError("frozen transport JEDEC constants differ")
    transport.INSTALLED_E5_BYTES = PREDECESSOR_BYTES
    transport.INSTALLED_E5_SHA256 = PREDECESSOR_SHA256
    transport.INSTALLED_E5_PADDED_SPAN_BYTES = PREDECESSOR_PADDED_BYTES
    transport.INSTALLED_E5_PADDED_SPAN_SHA256 = PREDECESSOR_PADDED_SHA256
    transport.E5.ALLOWED_FLASH_JEDEC_HIGH8 = {0x00, 0xAB, 0xFC, 0xFF}


def _configure_capture(capture: Any) -> None:
    if (
        capture.START != ORIGINAL_CAPTURE_START
        or not isinstance(capture.FIXED, tuple)
        or not capture.FIXED
        or capture.FIXED[0] != ("start", ORIGINAL_CAPTURE_START)
    ):
        raise InstallError("frozen Console OS capture signature differs")
    capture.START = SUCCESSOR_CAPTURE_START
    capture.FIXED = (("start", SUCCESSOR_CAPTURE_START),) + capture.FIXED[1:]


def _load_primitives() -> tuple[Any, Any, Any]:
    base = _load_module(BASE_INSTALLER_PATH, EXPECTED_BASE_SHA256, "space_invaders_frozen_base")
    transport = base._load_transport()
    capture = base._load_capture()
    _configure_transport(transport)
    _configure_capture(capture)
    return base, transport, capture


def install_from_trust_anchor(
    *, port: str, artifact: pathlib.Path, authorization: pathlib.Path,
    authorization_sha256: str, recovery_directory: pathlib.Path,
    capture_seconds: float = 30.0,
) -> dict[str, Any]:
    _base, transport, capture = _load_primitives()
    contract = _contract_from_authorization(authorization, authorization_sha256, transport)
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
        raise InstallError("Space Invaders transaction ended without a result")
    return {
        **result,
        "app": "console_os",
        "native_game_api": 1,
        "launcher_entries": 7,
        "new_game": "org.p4console.space-invaders",
        "startup_classification": "launcher-seven-apps-amp-not-energized",
        "transport_core": "frozen-doom-e6-retained-uart-recovery-state-machine",
        "restorable_predecessor": "console-os-game-api-v1",
    }


def recover(port: str, recovery_directory: pathlib.Path) -> dict[str, Any]:
    _base, transport, _capture = _load_primitives()
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
