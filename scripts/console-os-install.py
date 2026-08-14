#!/usr/bin/env python3

"""Exact-unit Console OS installer using the frozen crash-safe E6 transport.

The transport is reused only for its retained-UART identity, preimage, write,
readback, launch, and automatic-restore state machine. Console-specific trust
and startup acceptance remain in this wrapper and its frozen capture module.
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
TRANSPORT_PATH = SCRIPT_DIR / "doom-e6-install.py"
CAPTURE_PATH = SCRIPT_DIR / "capture-console-os-runtime.py"
AUTH_PATH = ROOT / "hardware/evidence/console-os-exact-unit-authorization.json"
BUILD_EVIDENCE_PATH = ROOT / "test-runs/2026-08-14-console-os-exact-unit-build.json"
RELEASE_PATH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-exact-unit-audio-release.json"
RECOVERY_CLOSURE_PATH = ROOT / "hardware/evidence/doom-e6-recovery-closure-20260814.json"
ARTIFACT_PATH = ROOT / "apps/console_os/build/p4_console_os.bin"
EXPECTED_TRANSPORT_SHA256 = (
    "f5d2255f37ca9373fdaf42ebe711968c31dc51b3dbbc2f7e501a235fbb546471"
)
EXPECTED_CAPTURE_SHA256 = (
    "cdb2c124f0f1e58aaaac7f13d416eff00cb044cc0f13232afa8537d8645aa571"
)
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_JSON_BYTES = 4 * 1024 * 1024


class InstallError(RuntimeError):
    """The Console OS exact-unit install contract was not satisfied."""


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


def _load_transport() -> Any:
    return _load_exact_module(
        TRANSPORT_PATH, EXPECTED_TRANSPORT_SHA256,
        "console_os_frozen_recovery_transport",
    )


def _load_capture() -> Any:
    return _load_exact_module(
        CAPTURE_PATH, EXPECTED_CAPTURE_SHA256,
        "console_os_frozen_startup_capture",
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


def _validate_recovery_closure(record: Mapping[str, Any], transport: Any) -> None:
    ledger = record.get("closed_ledger")
    if not (
        record.get("schema") == 1
        and record.get("result") == "exact-e5-restored-and-launched"
        and record.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and record.get("phase") == "restored-e5-launched"
        and record.get("restore_required") is False
        and record.get("restoration_verified") is True
        and record.get("restored_span_bytes") == 4_931_584
        and record.get("restored_span_sha256")
            == "b87b93073b51d14e308fce2d5f2c4e490234101d2f4a536fec1bea0036559477"
        and isinstance(ledger, Mapping)
    ):
        raise InstallError("predecessor recovery closure differs")
    path = transport._validate_bound_repository_file(ledger, "closed recovery ledger")
    state = json.loads(path.read_text(encoding="utf-8"))
    if not (
        state.get("phase") == "restored-e5-launched"
        and state.get("restore_required") is False
        and state.get("restoration_verified") is True
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
    ):
        raise InstallError("closed recovery ledger no longer proves restoration")


def _contract_from_authorization(
    authorization: pathlib.Path, authorization_sha256: str, transport: Any,
) -> Any:
    if authorization.resolve(strict=True) != AUTH_PATH.resolve(strict=True):
        raise InstallError("authorization path differs")
    auth = _read_json(
        authorization, "Console OS exact-unit authorization",
        authorization_sha256,
    )
    artifact = auth.get("exact_artifact")
    if not (
        auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-console-os-mvp"
        and auth.get("device_binding") == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4",
            "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
        }
        and auth.get("operator_direction") == "go"
        and isinstance(artifact, Mapping)
    ):
        raise InstallError("authorization scope or exact-unit binding differs")
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
        and contract.artifact_bytes > 0
        and contract.mutation_span_bytes == 4_931_584
        and contract.mutation_span_bytes >= contract.artifact_bytes
        and contract.mutation_span_bytes % transport.WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes
            == ((contract.artifact_bytes + transport.WRITE_BLOCK_BYTES - 1)
                // transport.WRITE_BLOCK_BYTES) * transport.WRITE_BLOCK_BYTES
        and artifact.get("mutation_tail_byte") == "ff"
    ):
        raise InstallError("artifact mutation span differs")

    build = _bound_json(
        auth.get("build_evidence"), BUILD_EVIDENCE_PATH, "build evidence"
    )
    release = _bound_json(
        auth.get("exact_unit_audio_release"), RELEASE_PATH,
        "exact-unit audio release",
    )
    closure = _bound_json(
        auth.get("predecessor_recovery_closure"), RECOVERY_CLOSURE_PATH,
        "predecessor recovery closure",
    )
    transport._validate_exact_unit_audio_release(release)
    _validate_recovery_closure(closure, transport)
    if not (
        build.get("result")
            == "console-os-exact-unit-candidate-build-verified"
        and build.get("exact_artifact") == dict(artifact)
        and build.get("installer_transport", {}).get("sha256")
            == EXPECTED_TRANSPORT_SHA256
        and build.get("startup_capture", {}).get("sha256")
            == EXPECTED_CAPTURE_SHA256
    ):
        raise InstallError("build evidence does not bind the exact artifact route")
    inventory = build.get("source_inventory")
    required = {
        "apps/console_os/main/console_os_main.c",
        "apps/console_os/main/CMakeLists.txt",
        "apps/console_os/dependencies.lock",
        "components/console_shell/include/console/shell.h",
        "components/console_shell/src/console_shell.c",
        "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c",
        "apps/doom_embedded_touch_audio/components/platform_audio/src/platform_audio_adapter.c",
        "apps/doom/components/doom_audio/src/doom_music_synth.c",
        "components/platform_audio_factory/src/platform_audio_factory.c",
        "scripts/console-os-install.py",
        "scripts/capture-console-os-runtime.py",
        "scripts/doom-e6-install.py",
        "scripts/doom-e5-install.py",
        "scripts/gamepad-diag-restore.py",
        "scripts/verify-console-os.py",
        "toolchain.lock.json",
        "hardware/board-profile.json",
    }
    if not isinstance(inventory, Mapping) or not required <= set(inventory):
        raise InstallError("build evidence omits critical source inventory")
    transport._validate_inventory(inventory)

    execution = auth.get("execution_contract")
    if execution != {
        "same_uart_handle": True,
        "live_full_span_preimage_required": True,
        "app_partition_only": True,
        "retained_launcher_capture_required": True,
        "automatic_restore_on_capture_failure": True,
        "shell_audio_hardware_calls": 0,
        "doom_audio_deferred_until_user_handoff": True,
        "usb_runtime": False,
        "recovery_directory": "hardware/local-state/console-os-install-20260814",
    }:
        raise InstallError("execution contract differs")
    return contract


def install_from_trust_anchor(
    *, port: str, artifact: pathlib.Path, authorization: pathlib.Path,
    authorization_sha256: str, recovery_directory: pathlib.Path,
    capture_seconds: float = 30.0,
) -> dict[str, Any]:
    """Install only when called by the separately frozen issuance route."""

    transport = _load_transport()
    capture = _load_capture()
    contract = _contract_from_authorization(
        authorization, authorization_sha256, transport
    )
    if artifact.resolve(strict=True) != ARTIFACT_PATH.resolve(strict=True):
        raise InstallError("artifact path differs from the exact build output")
    transport._read_sealed_artifact(artifact, contract)
    expected_recovery = (
        ROOT / "hardware/local-state/console-os-install-20260814"
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
        raise InstallError("Console OS transaction ended without a result")
    return {
        **result,
        "app": "console_os",
        "startup_classification": "launcher-only-amp-not-energized",
        "transport_core": "frozen-doom-e6-retained-uart-recovery-state-machine",
    }


def recover(port: str, recovery_directory: pathlib.Path) -> dict[str, Any]:
    """Restore the sealed E5 preimage after an interrupted Console OS write."""

    transport = _load_transport()
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
