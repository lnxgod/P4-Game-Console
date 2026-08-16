#!/usr/bin/env python3

"""Exact-unit installer for the Console OS native Game API v1 artifact.

This successor keeps the previously reviewed retained-UART transaction engine
byte-for-byte. It replaces only that engine's installed-predecessor binding and
the Console OS startup signature, after validating the new exact authorization.
The public CLI exposes recovery only; installation requires the separately
issued outer route.
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
AUTH_PATH = (
    ROOT / "hardware/evidence/console-os-game-api-v1-exact-unit-authorization.json"
)
BUILD_EVIDENCE_PATH = (
    ROOT / "test-runs/2026-08-14-console-os-game-api-v1-build.json"
)
AUDIO_RELEASE_PATH = (
    ROOT / "hardware/evidence/console-os-game-api-v1-exact-unit-audio-release.json"
)
PRIOR_AUDIO_RELEASE_PATH = (
    ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-exact-unit-audio-release.json"
)
PREDECESSOR_RECORD_PATH = (
    ROOT / "hardware/test-runs/2026-08-14-console-os-mvp-install.json"
)
JEDEC_NORMALIZATION_PATH = (
    ROOT / "hardware/evidence/console-os-game-api-v1-jedec-normalization.json"
)
ARTIFACT_PATH = ROOT / "apps/console_os/build/p4_console_os.bin"
EXPECTED_BASE_INSTALLER_SHA256 = (
    "0887dbcc43d79f48e06628eca21c518974ef900ff8f8cb025d63bb2bbf44c491"
)
EXPECTED_TRANSPORT_SHA256 = (
    "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
)
EXPECTED_CAPTURE_SHA256 = (
    "cdb2c124f0f1e58aaaac7f13d416eff00cb044cc0f13232afa8537d8645aa571"
)
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
PREDECESSOR_BYTES = 4_928_880
PREDECESSOR_SHA256 = (
    "5c4cdc811fa0da1ba21c72dae0bdf5862f02ecdd6d2c468b9075d8c530d58957"
)
PREDECESSOR_PADDED_BYTES = 4_931_584
PREDECESSOR_PADDED_SHA256 = (
    "ccc6b5f89d2a1990f9f11ee376939aba675952ffe54c4835ba775a9e45bb7be2"
)
ORIGINAL_CAPTURE_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=5 "
    b"surface=rgb565-320x200 touch=gt911 audio=safe-handoff-only "
    b"execution=guarded-exact-unit-candidate"
)
GAME_API_V1_CAPTURE_START = (
    b"P4_CONSOLE_OS START shell=freertos-native apps=6 "
    b"surface=rgb565-320x200 touch=gt911 "
    b"native_game_api=1 native_format=p4-native-static-v1 "
    b"execution=build-candidate"
)
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024


class InstallError(RuntimeError):
    """The exact Console OS Game API v1 install contract was not satisfied."""


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
        or before.st_size <= 0
        or before.st_size > maximum
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
        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError(f"{label} was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError(f"{label} grew while reading")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size)
            != (opened.st_dev, opened.st_ino, opened.st_size)
            or stat.S_IMODE(after.st_mode) != mode
        ):
            raise InstallError(f"{label} changed while reading")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _read_json(
    path: pathlib.Path, label: str, expected_sha256: str,
) -> dict[str, Any]:
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


def _load_exact_module(
    path: pathlib.Path, expected_sha256: str, name: str,
) -> Any:
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


def _load_base() -> Any:
    return _load_exact_module(
        BASE_INSTALLER_PATH, EXPECTED_BASE_INSTALLER_SHA256,
        "console_os_game_api_v1_frozen_base",
    )


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


def _validate_predecessor(record: Mapping[str, Any]) -> None:
    hardware = record.get("hardware")
    artifact = record.get("artifact")
    transaction = record.get("transaction")
    retained = record.get("retained_uart_acceptance")
    if not (
        record.get("schema") == 1
        and record.get("result")
            == "pass-console-launcher-runtime-doom-handoff-and-acoustic-confirmation-pending"
        and isinstance(hardware, Mapping)
        and hardware.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and hardware.get("chip") == "ESP32-P4"
        and hardware.get("chip_revision") == "v1.3"
        and hardware.get("flash_bytes") == 16_777_216
        and isinstance(artifact, Mapping)
        and artifact.get("offset") == "0x10000"
        and artifact.get("bytes") == PREDECESSOR_BYTES
        and artifact.get("sha256") == PREDECESSOR_SHA256
        and artifact.get("mutation_span_bytes") == PREDECESSOR_PADDED_BYTES
        and artifact.get("mutation_span_sha256") == PREDECESSOR_PADDED_SHA256
        and artifact.get("readback_sha256") == PREDECESSOR_PADDED_SHA256
        and isinstance(transaction, Mapping)
        and transaction.get("same_uart_handle") is True
        and transaction.get("app_partition_only") is True
        and transaction.get("restore_required") is False
        and transaction.get("rollback_performed") is False
        and isinstance(retained, Mapping)
        and retained.get("result") == "pass"
        and retained.get("amplifier_energized") is False
    ):
        raise InstallError("installed predecessor evidence differs")


def _validate_audio_release(
    release: Mapping[str, Any], artifact: Mapping[str, Any],
    predecessor_binding: Mapping[str, Any], transport: Any,
) -> None:
    risk = release.get("operator_risk_acceptance")
    runtime = release.get("authorized_audio_runtime")
    safety = release.get("execution_safety")
    if not (
        release.get("schema") == 1
        and release.get("active") is True
        and release.get("scope")
            == "one-device-console-os-game-api-v1-factory-audio"
        and release.get("exact_unit") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
            "psram_bytes": 33_554_432,
            "raw_identity_stored": False,
        }
        and release.get("exact_artifact") == {
            "offset": artifact.get("offset"),
            "app_binary_bytes": artifact.get("app_binary_bytes"),
            "app_binary_sha256": artifact.get("app_binary_sha256"),
            "mutation_span_bytes": artifact.get("mutation_span_bytes"),
            "mutation_tail_byte": artifact.get("mutation_tail_byte"),
        }
        and risk == {
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
            "manual_maze_audio_acceptance_after_launcher_boot": True,
        }
        and release.get("installed_predecessor") == dict(predecessor_binding)
    ):
        raise InstallError("Game API v1 exact-unit audio release differs")
    prior = _bound_json(
        release.get("prior_audio_release"), PRIOR_AUDIO_RELEASE_PATH,
        "prior exact-unit audio release",
    )
    transport._validate_exact_unit_audio_release(prior)


def _validate_build(
    build: Mapping[str, Any], artifact: Mapping[str, Any], transport: Any,
) -> None:
    status = build.get("hardware_status")
    native = build.get("native_game_contract")
    prototype = build.get("prototype_game")
    if not (
        build.get("schema") == 1
        and build.get("result")
            == "console-os-native-game-api-v1-build-verified-not-hardware-tested"
        and build.get("exact_artifact") == dict(artifact)
        and isinstance(status, Mapping)
        and status.get("hardware_execution_authorized") is False
        and status.get("flash_authorized") is False
        and status.get("flash_attempted") is False
        and status.get("application_bytes_written") == 0
        and isinstance(native, Mapping)
        and native.get("format") == "p4-native-static-v1"
        and native.get("api_version") == 1
        and native.get("audio_backend_volume_step") == "6/10"
        and native.get("return_to_launcher") is True
        and isinstance(prototype, Mapping)
        and prototype.get("id") == "org.p4console.maze-chase"
        and prototype.get("implementation")
            == "original-clean-room-code-rendered-shapes-only"
    ):
        raise InstallError("Game API v1 build evidence differs")
    inventory = build.get("source_inventory")
    required = {
        "apps/console_os/main/console_os_main.c",
        "components/console_shell/src/console_shell.c",
        "components/p4_game_api/include/p4/game.h",
        "components/p4_game_api/src/audio_mixer.c",
        "components/p4_game_api/src/game_runtime.c",
        "components/p4_game_platform/src/audio_session.c",
        "games/maze_chase/game.json",
        "games/maze_chase/src/maze_chase.c",
        "scripts/generate-game-registry.py",
        "scripts/verify-console-os.py",
        "toolchain.lock.json",
    }
    if not isinstance(inventory, Mapping) or not required <= set(inventory):
        raise InstallError("Game API v1 build omits critical source inventory")
    transport._validate_inventory(inventory)


def _validate_jedec_normalization(record: Mapping[str, Any]) -> None:
    runtime = record.get("pinned_runtime")
    semantics = record.get("source_semantics")
    normalization = record.get("normalization_contract")
    observations = record.get("exact_observations")
    if not (
        record.get("schema") == 1
        and record.get("active") is True
        and record.get("result")
            == "release-observed-0xfc-high-byte-with-exact-low24"
        and record.get("scope")
            == "console-os-game-api-v1-exact-unit-install-boundaries"
        and record.get("exact_unit") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
            "raw_identity_stored": False,
        }
        and runtime == {
            "esptool_version": "4.12.0",
            "loader_sha256":
                "6c5f0c4a9d2047adb1c9164aee66208018789a0f6531922204f6b280168c80e8",
            "commands_sha256":
                "d9786dda365985a97504153b5a45bebf55bdf5b8272bf16891ca682b47e2c3af",
            "esp32p4_target_sha256":
                "0319aa7ee6e2e45c3ceb2e3424fb98a2ae9734873a2206767085abf75e74bf37",
            "legacy_rev1_stub_sha256":
                "3c0f27938055192977123cd4b503bf27d1676c59a7fd7e3f93de8e95b0cf63bf",
        }
        and isinstance(semantics, Mapping)
        and semantics.get("flash_id_command") == "0x9f"
        and semantics.get("requested_reply_bits") == 24
        and semantics.get("returned_container_bits") == 32
        and semantics.get("returned_value_postmasked_by_loader") is False
        and semantics.get("high_byte_is_part_of_requested_rdid") is False
        and semantics.get("canonical_exact_identity") == "0x1840c8"
        and normalization == {
            "accepted_high_bytes": ["0x00", "0xfc", "0xff"],
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
        and isinstance(observations, list)
        and len(observations) == 2
        and all(
            isinstance(item, Mapping)
            and item.get("stage") == "e6-write-boundary"
            and item.get("raw_value") == "0xfc1840c8"
            and item.get("canonical_low24") == "0x1840c8"
            and item.get("writes_attempted") == 0
            for item in observations
        )
    ):
        raise InstallError("exact-unit JEDEC normalization evidence differs")


def _contract_from_authorization(
    authorization: pathlib.Path, authorization_sha256: str, transport: Any,
) -> Any:
    if authorization.resolve(strict=True) != AUTH_PATH.resolve(strict=True):
        raise InstallError("authorization path differs")
    auth = _read_json(
        authorization, "Console OS Game API v1 exact-unit authorization",
        authorization_sha256,
    )
    artifact = auth.get("exact_artifact")
    predecessor_binding = auth.get("installed_predecessor")
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-console-os-game-api-v1"
        and auth.get("operator_direction") == "show-it-on-the-tablet"
        and auth.get("device_binding") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
        }
        and isinstance(artifact, Mapping)
        and isinstance(predecessor_binding, Mapping)
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
        and contract.artifact_bytes == 4_941_776
        and contract.artifact_sha256
            == "f4426c370b9f251aa5a3ae5049b99a4c068067390c351dc680d1b7d36492be36"
        and contract.mutation_span_bytes == 4_947_968
        and contract.mutation_span_bytes >= PREDECESSOR_PADDED_BYTES
        and contract.mutation_span_bytes % transport.WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes
            == ((contract.artifact_bytes + transport.WRITE_BLOCK_BYTES - 1)
                // transport.WRITE_BLOCK_BYTES) * transport.WRITE_BLOCK_BYTES
        and artifact.get("mutation_tail_byte") == "ff"
    ):
        raise InstallError("exact Game API v1 artifact geometry differs")

    build = _bound_json(
        auth.get("build_evidence"), BUILD_EVIDENCE_PATH, "build evidence"
    )
    predecessor = _bound_json(
        predecessor_binding, PREDECESSOR_RECORD_PATH, "installed predecessor"
    )
    release = _bound_json(
        auth.get("exact_unit_audio_release"), AUDIO_RELEASE_PATH,
        "Game API v1 exact-unit audio release",
    )
    jedec = _bound_json(
        auth.get("jedec_normalization"), JEDEC_NORMALIZATION_PATH,
        "Game API v1 exact-unit JEDEC normalization",
    )
    _validate_predecessor(predecessor)
    _validate_audio_release(release, artifact, predecessor_binding, transport)
    _validate_build(build, artifact, transport)
    _validate_jedec_normalization(jedec)

    if auth.get("frozen_primitives") != {
        "base_installer_sha256": EXPECTED_BASE_INSTALLER_SHA256,
        "transport_sha256": EXPECTED_TRANSPORT_SHA256,
        "startup_capture_sha256": EXPECTED_CAPTURE_SHA256,
        "startup_signature": GAME_API_V1_CAPTURE_START.decode("ascii"),
        "accepted_rdid_high_bytes": ["0x00", "0xfc", "0xff"],
    }:
        raise InstallError("frozen install primitive binding differs")
    if auth.get("execution_contract") != {
        "same_uart_handle": True,
        "live_full_span_preimage_required": True,
        "app_partition_only": True,
        "retained_launcher_capture_required": True,
        "automatic_restore_on_capture_failure": True,
        "launcher_audio_hardware_calls": 0,
        "native_game_audio_deferred_until_user_launch": True,
        "usb_runtime": False,
        "recovery_directory":
            "hardware/local-state/console-os-game-api-v1-install-20260814",
    }:
        raise InstallError("execution contract differs")
    return contract


def _configure_transport(transport: Any) -> None:
    original = (
        transport.INSTALLED_E5_BYTES,
        transport.INSTALLED_E5_SHA256,
        transport.INSTALLED_E5_PADDED_SPAN_BYTES,
        transport.INSTALLED_E5_PADDED_SPAN_SHA256,
    )
    if original != (
        4_898_400,
        "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8",
        4_898_816,
        "9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9",
    ):
        raise InstallError("frozen transport predecessor constants differ")
    if (
        transport.E5.EXPECTED_FLASH_JEDEC_LOW24 != 0x1840C8
        or transport.E5.ALLOWED_FLASH_JEDEC_HIGH8 != {0x00, 0xFF}
    ):
        raise InstallError("frozen transport JEDEC constants differ")
    transport.INSTALLED_E5_BYTES = PREDECESSOR_BYTES
    transport.INSTALLED_E5_SHA256 = PREDECESSOR_SHA256
    transport.INSTALLED_E5_PADDED_SPAN_BYTES = PREDECESSOR_PADDED_BYTES
    transport.INSTALLED_E5_PADDED_SPAN_SHA256 = PREDECESSOR_PADDED_SHA256
    transport.E5.ALLOWED_FLASH_JEDEC_HIGH8 = {0x00, 0xFC, 0xFF}


def _configure_capture(capture: Any) -> None:
    if (
        capture.START != ORIGINAL_CAPTURE_START
        or not isinstance(capture.FIXED, tuple)
        or not capture.FIXED
        or capture.FIXED[0] != ("start", ORIGINAL_CAPTURE_START)
    ):
        raise InstallError("frozen Console OS capture signature differs")
    capture.START = GAME_API_V1_CAPTURE_START
    capture.FIXED = (("start", GAME_API_V1_CAPTURE_START),) + capture.FIXED[1:]


def install_from_trust_anchor(
    *, port: str, artifact: pathlib.Path, authorization: pathlib.Path,
    authorization_sha256: str, recovery_directory: pathlib.Path,
    capture_seconds: float = 30.0,
) -> dict[str, Any]:
    """Install only when called by the separately frozen issuance route."""

    base = _load_base()
    transport = base._load_transport()
    capture = base._load_capture()
    contract = _contract_from_authorization(
        authorization, authorization_sha256, transport
    )
    _configure_transport(transport)
    _configure_capture(capture)
    if artifact.resolve(strict=True) != ARTIFACT_PATH.resolve(strict=True):
        raise InstallError("artifact path differs from the exact build output")
    transport._read_sealed_artifact(artifact, contract)
    expected_recovery = (
        ROOT / "hardware/local-state/console-os-game-api-v1-install-20260814"
    ).resolve()
    if recovery_directory.resolve(strict=True) != expected_recovery:
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
        raise InstallError("Game API v1 transaction ended without a result")
    return {
        **result,
        "app": "console_os",
        "native_game_api": 1,
        "startup_classification": "launcher-game-api-v1-amp-not-energized",
        "transport_core": "frozen-doom-e6-retained-uart-recovery-state-machine",
        "restorable_predecessor": "console-os-mvp",
    }


def recover(port: str, recovery_directory: pathlib.Path) -> dict[str, Any]:
    """Restore the sealed Console OS MVP preimage after an interrupted write."""

    base = _load_base()
    transport = base._load_transport()
    _configure_transport(transport)
    expected_recovery = (
        ROOT / "hardware/local-state/console-os-game-api-v1-install-20260814"
    ).resolve()
    if recovery_directory.resolve(strict=True) != expected_recovery:
        raise InstallError("recovery directory differs from authorization")
    previous = transport._arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        runtime = transport.E5._production_runtime()
        device = transport.E5.open_serial_once(port)
        result = transport.recover_same_handle(
            device=device, recovery_directory=recovery_directory,
            runtime=runtime,
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
