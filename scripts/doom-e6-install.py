#!/usr/bin/env python3

"""Crash-recoverable same-UART installer for the E6 factory-audio Doom image.

This route is intentionally inactive until an exact E6 authorization hash is
issued.  Its transaction retains one exclusive UART from loader entry through
live partition/preimage capture, the only write, complete padded-span
readback, the only application launch, and the strict startup capture.  A
durable ledger enters ``restore_required`` immediately before ``flash_begin``.
Any caught failure after that transition restores the exact sealed live
preimage and launches the previously installed E5 touch-only image.  The
``recover`` command performs that same restore after an uncatchable crash.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.machinery
import importlib.util
import json
import os
import pathlib
import signal
import stat
import sys
import types
from typing import Any, Mapping, NamedTuple


SCRIPT_PATH = pathlib.Path(__file__).resolve(strict=True)
SCRIPT_DIR = SCRIPT_PATH.parent
ROOT = SCRIPT_DIR.parent.resolve(strict=True)
E5_INSTALL_PATH = SCRIPT_DIR / "doom-e5-install.py"
CAPTURE_PATH = SCRIPT_DIR / "capture-doom-e6-runtime.py"
AUTH_PATH = ROOT / "hardware/evidence/doom-embedded-touch-audio-e6-factory-audio-authorization.json"
EXPECTED_DEVICE_SHA256 = "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
EXPECTED_OFFSET = 0x10000
WRITE_BLOCK_BYTES = 0x4000
RESTORE_BLOCK_BYTES = 0x1000
MAX_READBACK_CHUNK_BYTES = 512 * 1024
MAX_JSON_RECORD_BYTES = 2 * 1024 * 1024
MAX_BOUND_FILE_BYTES = 64 * 1024 * 1024
MAX_STUB_JSON_BYTES = 1024 * 1024
LEDGER_NAME = "e6-install-ledger.json"
PREIMAGE_NAME = "live-preimage.bin"
PARTITION_TABLE_NAME = "live-partition-table.bin"
CAPTURE_RAW_NAME = "e6-startup.raw"
CAPTURE_SUMMARY_NAME = "e6-startup.json"
INSTALLED_E5_BYTES = 4_898_400
INSTALLED_E5_SHA256 = "68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8"
INSTALLED_E5_PADDED_SPAN_BYTES = 4_898_816
INSTALLED_E5_PADDED_SPAN_SHA256 = "9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9"
EXPECTED_BOOTSTRAP_HELPER_SHA256 = {
    "doom-e5-install.py": "cd84e329fa00fcab7726e45ba81bc94d4c50de188ab64087e180d40a9f565008",
    "gamepad-diag-restore.py": "5be16e889116cc8a9e9009d730c2d0bcd3438ac07ead2d726ac1a346a5f0e244",
    "capture-doom-e6-runtime.py": "b8832bda1292f708d26b274a53a69a4b766b58dce18951f83f995655e5e0a2e5",
}


class InstallError(RuntimeError):
    """The E6 install/recovery contract was not satisfied."""


class ArtifactContract(NamedTuple):
    offset: int
    artifact_bytes: int
    artifact_sha256: str
    mutation_span_bytes: int


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_regular_bytes(
    path: pathlib.Path, label: str, *, maximum_bytes: int,
    required_mode: int | None = None,
) -> bytes:
    """Read one exact owned regular-file object through one no-follow fd."""

    try:
        before = path.lstat()
    except OSError as error:
        raise InstallError(f"cannot stat {label}: {error}") from error
    mode = stat.S_IMODE(before.st_mode)
    if (
        not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode)
        or before.st_uid != os.getuid()
        or (required_mode is not None and mode != required_mode)
        or (required_mode is None and mode & 0o022)
        or before.st_size <= 0 or before.st_size > maximum_bytes
    ):
        raise InstallError(f"{label} metadata or bounded size is invalid")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino)
            or opened.st_uid != before.st_uid
            or opened.st_size != before.st_size
            or stat.S_IMODE(opened.st_mode) != mode
            or not stat.S_ISREG(opened.st_mode)
        ):
            raise InstallError(f"{label} changed while opening")
        chunks: list[bytes] = []
        remaining = opened.st_size
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError(f"{label} was truncated while reading")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError(f"{label} grew while reading")
        after = os.fstat(descriptor)
        if (
            (after.st_dev, after.st_ino, after.st_size, stat.S_IMODE(after.st_mode))
            != (opened.st_dev, opened.st_ino, opened.st_size, mode)
        ):
            raise InstallError(f"{label} changed while reading")
        return b"".join(chunks)
    finally:
        os.close(descriptor)


def _read_json_record(
    path: pathlib.Path, label: str, *, expected_sha256: str | None = None,
    required_mode: int | None = None,
) -> dict[str, Any]:
    payload = _read_regular_bytes(
        path, label, maximum_bytes=MAX_JSON_RECORD_BYTES,
        required_mode=required_mode,
    )
    if expected_sha256 is not None and _sha256_bytes(payload) != _validate_digest(
        expected_sha256, label
    ):
        raise InstallError(f"{label} changed")
    try:
        value = json.loads(payload.decode("utf-8", "strict"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise InstallError(f"{label} is not canonical UTF-8 JSON") from error
    if not isinstance(value, dict):
        raise InstallError(f"{label} is not a JSON object")
    return value


def _read_exact_helper_bytes(
    path: pathlib.Path, name: str, expected_sha256: str,
) -> bytes:
    resolved = path.resolve(strict=True)
    if resolved.parent != SCRIPT_DIR or resolved.name != name:
        raise InstallError(f"bootstrap helper path changed: {name}")
    payload = _read_regular_bytes(
        resolved, f"bootstrap helper {name}", maximum_bytes=MAX_JSON_RECORD_BYTES,
        required_mode=0o755,
    )
    if _sha256_bytes(payload) != expected_sha256:
        raise InstallError(f"bootstrap helper bytes changed: {name}")
    return payload


def _exec_exact_module(payload: bytes, path: pathlib.Path, name: str) -> Any:
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


class _ExactSourceLoader:
    def __init__(self, payload: bytes, path: pathlib.Path) -> None:
        self.payload = payload
        self.path = path

    def create_module(self, _spec: Any) -> None:
        return None

    def exec_module(self, module: Any) -> None:
        module.__file__ = str(self.path)
        exec(compile(self.payload, str(self.path), "exec"), module.__dict__)


def _load_exact_bootstrap_modules() -> tuple[Any, Any, Any]:
    restore_path = SCRIPT_DIR / "gamepad-diag-restore.py"
    e5_payload = _read_exact_helper_bytes(
        E5_INSTALL_PATH, "doom-e5-install.py",
        EXPECTED_BOOTSTRAP_HELPER_SHA256["doom-e5-install.py"],
    )
    restore_payload = _read_exact_helper_bytes(
        restore_path, "gamepad-diag-restore.py",
        EXPECTED_BOOTSTRAP_HELPER_SHA256["gamepad-diag-restore.py"],
    )
    capture_payload = _read_exact_helper_bytes(
        CAPTURE_PATH, "capture-doom-e6-runtime.py",
        EXPECTED_BOOTSTRAP_HELPER_SHA256["capture-doom-e6-runtime.py"],
    )

    # Frozen E5 asks importlib to load its restore dependency by path. Intercept
    # that exact request with a loader backed by the already validated bytes;
    # no helper pathname is reopened between validation and code execution.
    original_spec = importlib.util.spec_from_file_location

    def exact_spec(name: str, location: Any, *args: Any, **kwargs: Any) -> Any:
        try:
            requested = pathlib.Path(location).resolve(strict=True)
        except (OSError, TypeError, ValueError):
            return original_spec(name, location, *args, **kwargs)
        if requested == restore_path.resolve(strict=True):
            return importlib.machinery.ModuleSpec(
                name, _ExactSourceLoader(restore_payload, restore_path),
                origin=str(restore_path),
            )
        return original_spec(name, location, *args, **kwargs)

    importlib.util.spec_from_file_location = exact_spec
    try:
        e5 = _exec_exact_module(
            e5_payload, E5_INSTALL_PATH, "doom_e6_e5_primitives"
        )
    finally:
        importlib.util.spec_from_file_location = original_spec
    restore = e5.RESTORE
    if pathlib.Path(restore.__file__).resolve(strict=True) != restore_path.resolve(strict=True):
        raise InstallError("exact restore module origin changed during bootstrap")
    capture = _exec_exact_module(
        capture_payload, CAPTURE_PATH, "doom_e6_capture"
    )
    return e5, restore, capture


E5, RESTORE, CAPTURE = _load_exact_bootstrap_modules()


def _owned_directory(path: pathlib.Path, label: str) -> os.stat_result:
    info = path.lstat()
    if (
        not stat.S_ISDIR(info.st_mode) or stat.S_ISLNK(info.st_mode)
        or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o700
    ):
        raise InstallError(f"{label} must be an owned exact 0700 directory")
    return info


def _fsync_directory(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def _write_new_private(
    path: pathlib.Path, payload: bytes, mode: int = 0o600,
) -> dict[str, Any]:
    _owned_directory(path.parent, "private output parent")
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        mode,
    )
    binding: dict[str, Any]
    try:
        written = 0
        while written < len(payload):
            count = os.write(descriptor, payload[written:])
            if count <= 0:
                raise InstallError("private file write made no progress")
            written += count
        os.fsync(descriptor)
        opened = os.fstat(descriptor)
        if stat.S_IMODE(opened.st_mode) != mode or opened.st_size != len(payload):
            raise InstallError("private file metadata differs after write")
        binding = {
            "path": str(path.resolve(strict=True)),
            "device": opened.st_dev,
            "inode": opened.st_ino,
            "mode": mode,
            "bytes": len(payload),
            "sha256": _sha256_bytes(payload),
        }
    finally:
        os.close(descriptor)
    _fsync_directory(path.parent)
    return binding


def _atomic_ledger(path: pathlib.Path, state: Mapping[str, Any]) -> None:
    _owned_directory(path.parent, "recovery directory")
    payload = (json.dumps(dict(state), indent=2, sort_keys=True) + "\n").encode()
    temporary = path.parent / f".{path.name}.next"
    if temporary.exists() or temporary.is_symlink():
        raise InstallError("stale ledger replacement exists")
    descriptor = os.open(
        temporary,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
        0o600,
    )
    try:
        written = 0
        while written < len(payload):
            count = os.write(descriptor, payload[written:])
            if count <= 0:
                raise InstallError("ledger write made no progress")
            written += count
        os.fsync(descriptor)
    except BaseException:
        os.close(descriptor)
        temporary.unlink(missing_ok=True)
        _fsync_directory(path.parent)
        raise
    else:
        os.close(descriptor)
    os.replace(temporary, path)
    os.chmod(path, 0o600, follow_symlinks=False)
    _fsync_directory(path.parent)


def _load_ledger(path: pathlib.Path) -> dict[str, Any]:
    value = _read_json_record(
        path, "ledger", required_mode=0o600
    )
    if value.get("schema") != 1:
        raise InstallError("ledger schema is invalid")
    return value


def _discard_uncommitted_ledger_replacement(
    path: pathlib.Path, committed: Mapping[str, Any],
) -> bool:
    """Discard only a safely identified, never-committed atomic temp file.

    A fsynced ``.next`` file is not durable ledger state until ``os.replace``
    commits it.  Recovery may therefore remove it only when the canonical
    ledger already conservatively requires restoration.  Anything other than
    the fixed, owned 0600 regular child remains a hard failure.
    """

    temporary = path.parent / f".{path.name}.next"
    try:
        info = temporary.lstat()
    except FileNotFoundError:
        return False
    if committed.get("restore_required") is not True:
        raise InstallError(
            "uncommitted ledger replacement exists without a restore-required ledger"
        )
    if (
        not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)
        or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o600
    ):
        raise InstallError("uncommitted ledger replacement is not an owned 0600 file")
    descriptor = os.open(
        temporary, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    )
    try:
        opened = os.fstat(descriptor)
        if (
            (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino)
            or not stat.S_ISREG(opened.st_mode)
            or stat.S_IMODE(opened.st_mode) != 0o600
        ):
            raise InstallError("uncommitted ledger replacement changed while opening")
    finally:
        os.close(descriptor)
    temporary.unlink()
    _fsync_directory(path.parent)
    return True


def _transition(path: pathlib.Path, state: dict[str, Any], phase: str, **updates: Any) -> None:
    replacement = dict(state)
    replacement.update(updates)
    replacement["phase"] = phase
    replacement["transition_count"] = int(state.get("transition_count", 0)) + 1
    blocked: set[signal.Signals] = set()
    if hasattr(signal, "pthread_sigmask"):
        blocked = {signal.SIGHUP, signal.SIGINT, signal.SIGTERM}
        signal.pthread_sigmask(signal.SIG_BLOCK, blocked)
    try:
        _atomic_ledger(path, replacement)
        # Durable publication and the in-process mirror are one signal-atomic
        # transition. Without this, a signal after replace/fsync could let a
        # stale mirror overwrite the newly journaled capture or restore truth.
        state.clear()
        state.update(replacement)
    except BaseException:
        # If replace committed but publication was interrupted by an injected
        # non-signal exception, recover the canonical bytes before callers
        # make another safety transition.
        try:
            canonical = _load_ledger(path)
        except BaseException:
            raise
        state.clear()
        state.update(canonical)
        raise
    finally:
        if blocked:
            signal.pthread_sigmask(signal.SIG_UNBLOCK, blocked)


def _validate_digest(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise InstallError(f"{label} digest has invalid shape")
    try:
        bytes.fromhex(value)
    except ValueError as error:
        raise InstallError(f"{label} digest has invalid shape") from error
    return value


def _validate_evidence_artifact_contract(
    authorized: Mapping[str, Any], evidence: Mapping[str, Any],
) -> None:
    recorded = evidence.get("exact_artifact")
    fields = (
        "offset", "app_binary_bytes", "app_binary_sha256",
        "mutation_span_bytes", "mutation_tail_byte",
    )
    if not isinstance(recorded, Mapping) or any(
        authorized.get(field) != recorded.get(field) for field in fields
    ):
        raise InstallError("authorization artifact differs from bound build evidence")
    try:
        artifact_bytes = int(recorded["app_binary_bytes"])
        mutation_span_bytes = int(recorded["mutation_span_bytes"])
    except (KeyError, TypeError, ValueError) as error:
        raise InstallError("build evidence artifact geometry is malformed") from error
    if not (
        recorded.get("offset") == "0x10000"
        and mutation_span_bytes
            == ((artifact_bytes + WRITE_BLOCK_BYTES - 1)
                // WRITE_BLOCK_BYTES) * WRITE_BLOCK_BYTES
        and recorded.get("mutation_tail_byte") == "ff"
    ):
        raise InstallError("build evidence artifact span is not deterministic")


def _validate_exact_unit_audio_release(record: Mapping[str, Any]) -> None:
    """Require the narrow owner-accepted E6 factory-audio exception."""

    exact_unit = record.get("exact_unit")
    risk = record.get("operator_risk_acceptance")
    attestation = record.get("operator_attestation")
    factory = record.get("pinned_factory_source")
    initializer = record.get("complete_factory_initializer")
    prior = record.get("prior_nondamaging_run")
    rollback = record.get("rollback_identity")
    if not (
        record.get("schema") == 1
        and record.get("active") is True
        and record.get("classification")
            == "exact-unit-operator-accepted-factory-audio-release"
        and record.get("scope") == "one-device-e6-complete-factory-audio-init"
        and exact_unit == {
            "identity_sha256": EXPECTED_DEVICE_SHA256,
            "chip": "ESP32-P4", "chip_revision": "v1.3",
            "flash_bytes": 16_777_216,
        }
        and risk == {
            "unresolved_amplifier_topology_risk_accepted": True,
            "gpio24_pdm_clock_may_feed_codec_mclk": True,
            "population_or_continuity_proof_available": False,
            "connected_unit_only": True,
            "reusable_authorization": False,
            "cross_unit_authorization": False,
            "acoustic_output_not_yet_proven": True,
        }
        and attestation == {
            "exact_connected_unit_prior_run_no_damage_observed": True,
            "factory_image_speaker_was_audible_to_operator": True,
            "direction": "replay-complete-pinned-factory-hardware-initializer",
            "accepts_unresolved_topology_risk_for_this_unit": True,
        }
        and factory == {
            "commit": "c5a437311b951aaa9d17115bf420877a8f1f7b83",
            "archive_sha256": "73b32c4d4dc89cc0b091388d6a7862d827d6548412717bcb1f36f7adb8da2e28",
            "board_source_sha256": "f0aa354307710744f37d57b8ea23942b13d6ae38c26a98ad118c606ae5b11b69",
            "codec_control_source_sha256": "f80ee68cd9e079725e9ea68d218b04c06438a86a91988e98750dfbc4b319ee18",
        }
        and initializer == {
            "pdm_rx_controller": 0, "pdm_clock_gpio": 24,
            "pdm_clock_hz": 1_024_000, "pdm_data_input_gpio": 26,
            "pdm_samples_consumed": False,
            "speaker_tx_controller": 1, "sample_rate_hz": 16_000,
            "format": "signed-pcm16-stereo", "lrclk_gpio": 21,
            "bclk_gpio": 22, "dout_gpio": 23, "tx_mclk": "unused",
            "amplifier_shutdown_gpio": 30, "amplifier_enable_level": 0,
            "external_codec_i2c_transactions": 0, "usb_runtime": False,
        }
        and isinstance(prior, Mapping)
        and prior.get("record_result") == "fail-no-microphone-detected-tone"
        and prior.get("serial_result") == "pass"
        and prior.get("microphone_result") == "fail"
        and prior.get("connected_unit_speaker_path_accepted") is False
        and prior.get("path")
            == "hardware/test-runs/2026-08-13-audio-direct-d23-attempt2.json"
        and prior.get("sha256")
            == "74568d7b388dd437a9dd2a0a672bb635b343329b60baa0778c7740f87350925b"
        and rollback == {
            "offset": "0x10000",
            "e5_artifact_bytes": INSTALLED_E5_BYTES,
            "e5_artifact_sha256": INSTALLED_E5_SHA256,
            "e5_padded_span_bytes": INSTALLED_E5_PADDED_SPAN_BYTES,
            "e5_padded_span_sha256": INSTALLED_E5_PADDED_SPAN_SHA256,
        }
    ):
        raise InstallError("exact-unit audio release semantics differ")
    _validate_bound_repository_file(prior, "prior nondamaging audio run")


def _validate_rollback_bundle(record: Mapping[str, Any]) -> None:
    identity = record.get("installed_identity")
    requirement = record.get("future_install_requirement")
    if not (
        record.get("schema") == "p4-doom-e6-rollback-bundle-v1"
        and requirement == {
            "minimum_preimage_bytes": INSTALLED_E5_PADDED_SPAN_BYTES,
            "preimage_rule": "ceil(successor_artifact_bytes/16384)*16384",
            "restore_rule": "restore and exact-readback the entire sealed live successor mutation span after any post-mutation failure",
            "same_handle_live_preimage_required": True,
        }
        and isinstance(identity, Mapping)
        and identity.get("app") == "doom_embedded_touch_audio"
        and identity.get("stage") == "E5-touch-only-persistent"
        and identity.get("offset") == "0x10000"
        and identity.get("artifact_prefix_bytes") == INSTALLED_E5_BYTES
        and identity.get("padded_span_bytes") == INSTALLED_E5_PADDED_SPAN_BYTES
        and identity.get("padded_tail_bytes")
            == INSTALLED_E5_PADDED_SPAN_BYTES - INSTALLED_E5_BYTES
        and identity.get("padded_tail_value") == "0xff"
        and identity.get("stub_write_block_bytes") == WRITE_BLOCK_BYTES
    ):
        raise InstallError("installed predecessor rollback semantics differ")
    artifact = identity.get("artifact")
    padded = identity.get("padded_span")
    expected_artifact = {
        "path": "test-runs/doom-e6-rollback-2026-08-13/installed-e5-exact.bin",
        "mode": "0400", "size_bytes": INSTALLED_E5_BYTES,
        "sha256": INSTALLED_E5_SHA256, "regular": True, "symlink": False,
    }
    expected_padded = {
        "path": "test-runs/doom-e6-rollback-2026-08-13/installed-e5-span-4898816.bin",
        "mode": "0400", "size_bytes": INSTALLED_E5_PADDED_SPAN_BYTES,
        "sha256": INSTALLED_E5_PADDED_SPAN_SHA256,
        "regular": True, "symlink": False,
    }
    if not isinstance(artifact, Mapping) or not isinstance(padded, Mapping) or any(
        artifact.get(key) != value for key, value in expected_artifact.items()
    ) or any(padded.get(key) != value for key, value in expected_padded.items()):
        raise InstallError("installed predecessor file bindings differ")
    artifact_path = (ROOT / str(artifact["path"])).resolve(strict=True)
    padded_path = (ROOT / str(padded["path"])).resolve(strict=True)
    artifact_bytes = _read_regular_bytes(
        artifact_path, "offline E5 artifact", maximum_bytes=INSTALLED_E5_BYTES,
        required_mode=0o400,
    )
    padded_bytes = _read_regular_bytes(
        padded_path, "offline E5 padded span",
        maximum_bytes=INSTALLED_E5_PADDED_SPAN_BYTES, required_mode=0o400,
    )
    if not (
        len(artifact_bytes) == INSTALLED_E5_BYTES
        and _sha256_bytes(artifact_bytes) == INSTALLED_E5_SHA256
        and len(padded_bytes) == INSTALLED_E5_PADDED_SPAN_BYTES
        and _sha256_bytes(padded_bytes) == INSTALLED_E5_PADDED_SPAN_SHA256
        and padded_bytes[:INSTALLED_E5_BYTES] == artifact_bytes
        and padded_bytes[INSTALLED_E5_BYTES:] == b"\xff" * (
            INSTALLED_E5_PADDED_SPAN_BYTES - INSTALLED_E5_BYTES
        )
    ):
        raise InstallError("offline E5 predecessor bytes differ")


def _contract_from_authorization(
    path: pathlib.Path, expected_sha256: str,
) -> tuple[ArtifactContract, dict[str, Any]]:
    """Validate the exact authorization selected by the outer trust anchor.

    The installer deliberately does not embed this digest: authorization binds
    build evidence, whose inventory binds the installer.  The reviewed outer
    route supplies the final authorization digest after those earlier nodes
    are frozen, keeping the trust graph acyclic.
    """

    if path.resolve(strict=True) != AUTH_PATH:
        raise InstallError("authorization path is not the exact E6 record")
    expected_sha256 = _validate_digest(
        expected_sha256, "outer authorization trust anchor"
    )
    auth = _read_json_record(
        path, "exact E6 authorization", expected_sha256=expected_sha256
    )
    artifact = auth.get("exact_artifact") if isinstance(auth, dict) else None
    if not (
        isinstance(auth, dict) and auth.get("schema") == 1
        and auth.get("active") is True
        and auth.get("scope") == "exact-unit-e6-factory-audio"
        and auth.get("gates") == {"composite": 1, "touch": 1, "audio": 1}
        and auth.get("device_binding", {}).get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and isinstance(artifact, dict)
    ):
        raise InstallError("E6 authorization scope, device, or gates differ")
    contract = ArtifactContract(
        offset=int(str(artifact.get("offset")), 0),
        artifact_bytes=int(artifact.get("app_binary_bytes")),
        artifact_sha256=_validate_digest(
            artifact.get("app_binary_sha256"), "artifact"
        ),
        mutation_span_bytes=int(artifact.get("mutation_span_bytes")),
    )
    if not (
        contract.offset == EXPECTED_OFFSET
        and contract.artifact_bytes > 0
        and contract.mutation_span_bytes >= INSTALLED_E5_PADDED_SPAN_BYTES
        and contract.mutation_span_bytes >= contract.artifact_bytes
        and contract.mutation_span_bytes % WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes
            == ((contract.artifact_bytes + WRITE_BLOCK_BYTES - 1)
                // WRITE_BLOCK_BYTES) * WRITE_BLOCK_BYTES
        and artifact.get("mutation_tail_byte") == "ff"
    ):
        raise InstallError("E6 authorization artifact geometry differs")
    evidence_binding = auth.get("build_evidence")
    release_binding = auth.get("exact_unit_audio_release")
    rollback_binding = auth.get("installed_predecessor_rollback")
    if not (
        isinstance(evidence_binding, dict)
        and isinstance(release_binding, dict)
        and isinstance(rollback_binding, dict)
        and auth.get("execution_contract", {}).get("same_uart_handle") is True
        and auth.get("execution_contract", {}).get("live_full_span_preimage_required") is True
        and auth.get("execution_contract", {}).get("retained_startup_capture_required") is True
        and auth.get("execution_contract", {}).get("automatic_restore_on_capture_failure") is True
        and auth.get("execution_contract", {}).get("usb_runtime") is False
    ):
        raise InstallError("E6 authorization omits release/rollback/execution bindings")
    evidence_path, evidence = _read_bound_repository_json(
        evidence_binding, "build evidence"
    )
    del evidence_path
    _release_path, release = _read_bound_repository_json(
        release_binding, "exact-unit audio release"
    )
    _rollback_path, rollback = _read_bound_repository_json(
        rollback_binding, "installed predecessor rollback"
    )
    _validate_exact_unit_audio_release(release)
    _validate_rollback_bundle(rollback)
    _validate_evidence_artifact_contract(artifact, evidence)
    inventory = evidence.get("source_inventory") if isinstance(evidence, dict) else None
    required = {
        "apps/doom_embedded_touch_audio/main/doom_embedded_touch_audio_main.c",
        "apps/doom_embedded_touch_audio/main/runtime_gate.c",
        "apps/doom_embedded_touch_audio/main/audio_lifecycle.c",
        "apps/doom_embedded_touch_audio/components/platform_audio/src/platform_audio_adapter.c",
        "components/platform_audio_factory/include/platform_audio_factory/audio.h",
        "components/platform_audio_factory/src/platform_audio_factory.c",
        "hardware/board-profile.json",
        "scripts/capture-doom-e6-runtime.py",
        "scripts/doom-e6-install.py",
        "scripts/tests/test-doom-e6-install.py",
        "scripts/tests/test-doom-e6-runtime-capture.py",
        "scripts/verify-doom-embedded-touch-audio.py",
        ".agents/skills/develop-esp32-p4-platform/references/elecrow-10-in-variant.md",
    }
    if not isinstance(inventory, dict) or not required <= set(inventory):
        raise InstallError("E6 build evidence omits critical source inventory")
    _validate_inventory(inventory)
    return contract, auth


def _validate_bound_repository_file(binding: Mapping[str, Any], label: str) -> pathlib.Path:
    if not isinstance(binding, Mapping):
        raise InstallError(f"{label} binding is missing")
    relative = binding.get("path")
    expected = binding.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise InstallError(f"{label} binding is malformed")
    path = (ROOT / relative).resolve(strict=True)
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise InstallError(f"{label} leaves the repository")
    payload = _read_regular_bytes(
        path, label, maximum_bytes=MAX_BOUND_FILE_BYTES
    )
    if _sha256_bytes(payload) != _validate_digest(expected, label):
        raise InstallError(f"{label} changed")
    return path


def _read_bound_repository_json(
    binding: Mapping[str, Any], label: str,
) -> tuple[pathlib.Path, dict[str, Any]]:
    if not isinstance(binding, Mapping):
        raise InstallError(f"{label} binding is missing")
    relative = binding.get("path")
    expected = binding.get("sha256")
    if not isinstance(relative, str) or not isinstance(expected, str):
        raise InstallError(f"{label} binding is malformed")
    path = (ROOT / relative).resolve(strict=True)
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise InstallError(f"{label} leaves the repository")
    return path, _read_json_record(
        path, label, expected_sha256=expected
    )


def _validate_inventory(inventory: Mapping[str, Any]) -> None:
    for relative, expected in inventory.items():
        _validate_bound_repository_file(
            {"path": relative, "sha256": expected},
            f"frozen source {relative}",
        )


def _validate_recovery_inventory(state: Mapping[str, Any]) -> None:
    inventory = state.get("recovery_inventory")
    if not isinstance(inventory, Mapping):
        raise InstallError("recovery ledger omits frozen helper inventory")
    required = {
        "scripts/doom-e6-install.py", "scripts/capture-doom-e6-runtime.py",
        "scripts/doom-e5-install.py", "scripts/gamepad-diag-restore.py",
    }
    if not required <= set(inventory):
        raise InstallError("recovery inventory omits a required helper")
    _validate_inventory(inventory)


def _read_sealed_artifact(path: pathlib.Path, contract: ArtifactContract) -> bytes:
    info = path.lstat()
    if (
        not stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode)
        or info.st_uid != os.getuid() or stat.S_IMODE(info.st_mode) != 0o400
        or info.st_size != contract.artifact_bytes
    ):
        raise InstallError("sealed E6 artifact metadata differs")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        opened = os.fstat(descriptor)
        if (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino):
            raise InstallError("sealed E6 artifact changed while opening")
        chunks: list[bytes] = []
        remaining = contract.artifact_bytes
        while remaining:
            chunk = os.read(descriptor, min(1024 * 1024, remaining))
            if not chunk:
                raise InstallError("sealed E6 artifact was truncated")
            chunks.append(chunk)
            remaining -= len(chunk)
        if os.read(descriptor, 1):
            raise InstallError("sealed E6 artifact grew while reading")
    finally:
        os.close(descriptor)
    payload = b"".join(chunks)
    if _sha256_bytes(payload) != contract.artifact_sha256:
        raise InstallError("sealed E6 artifact hash differs")
    return payload


def _write_span(stub: Any, device: Any, handle: tuple[int, int, int, int, int],
                offset: int, payload: bytes, block_bytes: int) -> None:
    if len(payload) == 0 or len(payload) % block_bytes or offset % block_bytes:
        raise InstallError("write span is not exact block geometry")
    original_write_size = getattr(stub, "FLASH_WRITE_SIZE", None)
    if original_write_size != WRITE_BLOCK_BYTES or block_bytes not in {
        WRITE_BLOCK_BYTES, RESTORE_BLOCK_BYTES,
    }:
        raise InstallError("pinned stub write size or requested geometry changed")
    try:
        stub.FLASH_WRITE_SIZE = block_bytes
        if getattr(stub, "FLASH_WRITE_SIZE", None) != block_bytes:
            raise InstallError("stub refused the exact write packet size")
        blocks = stub.flash_begin(len(payload), offset, encrypted_write=False)
        if blocks != len(payload) // block_bytes:
            raise InstallError("stub returned an unexpected write block count")
        for sequence in range(blocks):
            E5._assert_same_handle(device, handle, stub)
            block = payload[sequence * block_bytes:(sequence + 1) * block_bytes]
            stub.flash_block(block, sequence, encrypted=False)
            E5._assert_same_handle(device, handle, stub)
        stub.flash_finish(reboot=False)
        E5._assert_same_handle(device, handle, stub)
    finally:
        stub.FLASH_WRITE_SIZE = original_write_size
        if getattr(stub, "FLASH_WRITE_SIZE", None) != WRITE_BLOCK_BYTES:
            raise InstallError("stub write size did not return to pinned 16 KiB")


def _read_exact(stub: Any, device: Any,
                handle: tuple[int, int, int, int, int], offset: int,
                byte_count: int, chunk_bytes: int = MAX_READBACK_CHUNK_BYTES) -> bytes:
    return E5._read_flash_exact(
        stub, device, handle, offset, byte_count, chunk_bytes
    )


def _file_binding(path: pathlib.Path, expected_bytes: int, label: str) -> dict[str, Any]:
    return RESTORE.file_binding(path, expected_bytes, label)


def _seal_live_snapshot(directory: pathlib.Path, filename: str, payload: bytes,
                        label: str) -> dict[str, Any]:
    return RESTORE.seal_snapshot(
        directory, filename, payload, expected_bytes=len(payload), label=label
    )


def _table_binding(directory: pathlib.Path, table: bytes,
                   contract: ArtifactContract, runtime: Any) -> dict[str, Any]:
    binding = _seal_live_snapshot(
        directory, PARTITION_TABLE_NAME, table, "live partition table"
    )
    binding.update(RESTORE.parse_partition_table(
        table, mutation_offset=contract.offset,
        mutation_bytes=contract.mutation_span_bytes,
    ))
    binding["restore_runtime"] = dict(runtime.binding)
    binding["restore_tool"] = {
        "path": str(E5.RESTORE_PATH.resolve(strict=True)),
        "sha256": _sha256_file(E5.RESTORE_PATH),
    }
    return binding


def _preimage_binding(directory: pathlib.Path, preimage: bytes,
                      contract: ArtifactContract) -> dict[str, Any]:
    binding = _seal_live_snapshot(
        directory, PREIMAGE_NAME, preimage, "E6 live restore preimage"
    )
    binding.update({
        "offset": contract.offset,
        "mutation_span_bytes": contract.mutation_span_bytes,
        "sector_bytes": RESTORE_BLOCK_BYTES,
        "install_write_block_bytes": WRITE_BLOCK_BYTES,
        "installed_e5_prefix_bytes": INSTALLED_E5_BYTES,
        "installed_e5_prefix_sha256": INSTALLED_E5_SHA256,
        "installed_e5_padded_span_bytes": INSTALLED_E5_PADDED_SPAN_BYTES,
        "installed_e5_padded_span_sha256": INSTALLED_E5_PADDED_SPAN_SHA256,
    })
    return binding


def _validate_sealed_recovery_contract(
    state: Mapping[str, Any], runtime: Any,
) -> tuple[ArtifactContract, bytes, bytes]:
    """Validate the canonical restore target and reread both sealed snapshots."""

    try:
        contract = ArtifactContract(
            int(state["offset"]), int(state["artifact_bytes"]),
            _validate_digest(state["artifact_sha256"], "ledger artifact"),
            int(state["mutation_span_bytes"]),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise InstallError("recovery ledger geometry is malformed") from error
    if not (
        state.get("schema") == 1
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and contract.offset == EXPECTED_OFFSET
        and 0 < contract.artifact_bytes <= contract.mutation_span_bytes
        and contract.mutation_span_bytes >= INSTALLED_E5_PADDED_SPAN_BYTES
        and contract.mutation_span_bytes % WRITE_BLOCK_BYTES == 0
        and contract.mutation_span_bytes
            == ((contract.artifact_bytes + WRITE_BLOCK_BYTES - 1)
                // WRITE_BLOCK_BYTES) * WRITE_BLOCK_BYTES
    ):
        raise InstallError("recovery ledger target is not the canonical E6 app span")
    preimage_binding = state.get("preimage_binding")
    table_binding = state.get("partition_table_binding")
    if not isinstance(preimage_binding, Mapping) or not isinstance(table_binding, Mapping):
        raise InstallError("recovery ledger omits sealed snapshot bindings")
    if not (
        preimage_binding.get("offset") == contract.offset
        and preimage_binding.get("bytes") == contract.mutation_span_bytes
        and preimage_binding.get("mutation_span_bytes") == contract.mutation_span_bytes
        and preimage_binding.get("sector_bytes") == RESTORE_BLOCK_BYTES
        and preimage_binding.get("install_write_block_bytes") == WRITE_BLOCK_BYTES
        and preimage_binding.get("installed_e5_prefix_bytes") == INSTALLED_E5_BYTES
        and preimage_binding.get("installed_e5_prefix_sha256") == INSTALLED_E5_SHA256
        and preimage_binding.get("installed_e5_padded_span_bytes")
            == INSTALLED_E5_PADDED_SPAN_BYTES
        and preimage_binding.get("installed_e5_padded_span_sha256")
            == INSTALLED_E5_PADDED_SPAN_SHA256
    ):
        raise InstallError("sealed preimage geometry or E5 binding differs")
    RESTORE.validate_partition_binding(
        table_binding, expected_offset=contract.offset,
        expected_span=contract.mutation_span_bytes, runtime=runtime,
    )
    _preimage_path, preimage = RESTORE._read_bound_payload(
        preimage_binding, "E6 live restore preimage"
    )
    _table_path, table = RESTORE._read_bound_payload(
        table_binding, "live partition table"
    )
    if (
        len(preimage) != contract.mutation_span_bytes
        or _sha256_bytes(preimage[:INSTALLED_E5_BYTES]) != INSTALLED_E5_SHA256
        or _sha256_bytes(preimage[:INSTALLED_E5_PADDED_SPAN_BYTES])
            != INSTALLED_E5_PADDED_SPAN_SHA256
    ):
        raise InstallError("sealed live preimage is not the exact installed E5 padded span")
    RESTORE.parse_partition_table(
        table, mutation_offset=contract.offset,
        mutation_bytes=contract.mutation_span_bytes,
    )
    return contract, table, preimage


def _load_stub(device: Any, handle: tuple[int, int, int, int, int],
               runtime: Any) -> Any:
    runtime.loader_reset_class(device).reset()
    E5._set_controls_false(device)
    E5._assert_same_handle(device, handle)
    rom = runtime.rom_class(device, 115200, False)
    rom.connect("no_reset", attempts=1, warnings=False)
    E5._assert_same_handle(device, handle, rom)
    RESTORE._validate_live_rom(rom, device, EXPECTED_DEVICE_SHA256)
    stub_binding = runtime.binding.get("legacy_rev1_stub")
    if not isinstance(stub_binding, Mapping):
        raise InstallError("pinned ESP32-P4 rev1 stub binding is missing")
    stub_path = pathlib.Path(str(stub_binding.get("path")))
    if (
        not stub_path.is_absolute()
        or stub_binding.get("sha256") != RESTORE.LEGACY_REV1_STUB_SHA256
    ):
        raise InstallError("pinned ESP32-P4 rev1 stub binding changed")
    stub_payload = _read_regular_bytes(
        stub_path, "pinned ESP32-P4 rev1 stub JSON",
        maximum_bytes=MAX_STUB_JSON_BYTES,
    )
    if _sha256_bytes(stub_payload) != RESTORE.LEGACY_REV1_STUB_SHA256:
        raise InstallError("pinned ESP32-P4 rev1 stub changed")
    stub = rom.run_stub(_decode_legacy_stub_payload(stub_payload))
    E5._assert_same_handle(device, handle, stub)
    if not getattr(stub, "IS_STUB", False):
        raise InstallError("RAM stub did not remain active")
    E5._prepare_exact_flash_after_stub(stub)
    E5._assert_same_handle(device, handle, stub)
    return stub


def _decode_legacy_stub_payload(payload: bytes) -> Any:
    """Decode the already hash-validated stub bytes without reopening a path."""

    try:
        value = json.loads(payload.decode("utf-8", "strict"))
        if not isinstance(value, dict):
            raise TypeError("stub root is not an object")
        image = types.SimpleNamespace(
            text=base64.b64decode(value["text"], validate=True),
            text_start=int(value["text_start"]),
            entry=int(value["entry"]),
            data=base64.b64decode(value["data"], validate=True),
            data_start=int(value["data_start"]),
            bss_start=int(value["bss_start"]),
        )
    except (UnicodeError, ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise InstallError("cannot parse pinned ESP32-P4 rev1 stub bytes") from error
    addresses = (image.text_start, image.entry, image.data_start, image.bss_start)
    if (
        not image.text or not image.data
        or any(isinstance(address, bool) or not 0 <= address <= 0xFFFFFFFF
               for address in addresses)
    ):
        raise InstallError("pinned ESP32-P4 rev1 stub fields are invalid")
    return image


def _durable_restore_mark(ledger_path: pathlib.Path, state: dict[str, Any]) -> None:
    _transition(
        ledger_path, state, "restore-write-attempted",
        restore_required=True,
        restore_attempt_count=int(state.get("restore_attempt_count", 0)) + 1,
        restore_write_attempt_count=int(state.get("restore_write_attempt_count", 0)) + 1,
    )


def _restore_e5_same_handle(device: Any, runtime: Any,
                            ledger_path: pathlib.Path,
                            state: dict[str, Any]) -> dict[str, Any]:
    """Restore the exact live preimage, verify it, then relaunch E5 once."""

    contract, table, preimage = _validate_sealed_recovery_contract(state, runtime)
    handle = E5._handle_binding(device)
    _transition(
        ledger_path, state, "restore-entered", restore_required=True,
        restore_attempt_count=int(state.get("restore_attempt_count", 0)) + 1,
    )
    stub = _load_stub(device, handle, runtime)
    if _read_exact(
        stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
        RESTORE.PARTITION_TABLE_BYTES,
    ) != table:
        raise InstallError("live partition table changed before E5 restoration")
    RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
    E5._fresh_exact_flash_id(stub, "e5-restore-write-boundary")
    stub.flash_set_parameters(RESTORE.FLASH_BYTES)
    E5._assert_same_handle(device, handle, stub)
    _transition(
        ledger_path, state, "restore-write-attempted",
        restore_required=True,
        restore_write_attempt_count=int(state.get("restore_write_attempt_count", 0)) + 1,
    )
    _write_span(
        stub, device, handle, contract.offset, preimage, RESTORE_BLOCK_BYTES
    )
    restored = _read_exact(
        stub, device, handle, contract.offset, contract.mutation_span_bytes
    )
    if restored != preimage:
        raise InstallError("E5 restoration readback differs from sealed preimage")
    if _read_exact(
        stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
        RESTORE.PARTITION_TABLE_BYTES,
    ) != table:
        raise InstallError("partition table changed during E5 restoration")
    RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
    E5._fresh_exact_flash_id(stub, "e5-restore-prelaunch")
    E5._set_controls_false(device)
    runtime.launch_reset_class(device, uses_usb=False).reset()
    E5._set_controls_false(device)
    E5._assert_same_handle(device, handle)
    result = {
        "bytes": len(restored),
        "sha256": _sha256_bytes(restored),
        "same_uart_handle": True,
        "download_reset_count": 1,
        "restore_write_count": 1,
        "e5_application_launch_count": 1,
        "e5_artifact_bytes": INSTALLED_E5_BYTES,
        "e5_artifact_sha256": INSTALLED_E5_SHA256,
        "e5_padded_span_bytes": INSTALLED_E5_PADDED_SPAN_BYTES,
        "e5_padded_span_sha256": INSTALLED_E5_PADDED_SPAN_SHA256,
        "arm_transmitted_bytes": 0,
    }
    _transition(
        ledger_path, state, "restored-e5-launched",
        restore_required=False, restoration_verified=True,
        restore_result=result,
    )
    return result


def _persist_capture_raw(directory: pathlib.Path, raw: bytes) -> dict[str, Any]:
    raw_path = directory / CAPTURE_RAW_NAME
    return _write_new_private(raw_path, raw)


def _persist_capture_summary(
    directory: pathlib.Path, summary: Mapping[str, Any],
) -> dict[str, Any]:
    summary_path = directory / CAPTURE_SUMMARY_NAME
    summary_payload = (
        json.dumps(dict(summary), indent=2, sort_keys=True) + "\n"
    ).encode()
    return _write_new_private(summary_path, summary_payload)


def _bounded_failure(error: BaseException) -> str:
    text = f"{type(error).__name__}: {error}".replace("\x00", "?")
    return text[:1024]


def install_same_handle(
    *, device: Any, artifact_path: pathlib.Path, contract: ArtifactContract,
    recovery_directory: pathlib.Path, runtime: Any,
    capture_module: Any = CAPTURE, capture_seconds: float = 45.0,
    readback_chunk_bytes: int = MAX_READBACK_CHUNK_BYTES,
) -> dict[str, Any]:
    """Install, read back, launch, and accept E6 on one exclusive descriptor."""

    _owned_directory(recovery_directory, "recovery directory")
    ledger_path = recovery_directory / LEDGER_NAME
    for name in (
        LEDGER_NAME, PREIMAGE_NAME, PARTITION_TABLE_NAME,
        CAPTURE_RAW_NAME, CAPTURE_SUMMARY_NAME,
    ):
        candidate = recovery_directory / name
        if candidate.exists() or candidate.is_symlink():
            raise InstallError(f"recovery child already exists: {name}")
    if not 1 <= readback_chunk_bytes <= MAX_READBACK_CHUNK_BYTES:
        raise InstallError("readback chunk size is outside 1..512 KiB")
    artifact = _read_sealed_artifact(artifact_path, contract)
    install_span = artifact + b"\xff" * (
        contract.mutation_span_bytes - contract.artifact_bytes
    )
    handle = E5._handle_binding(device)
    state: dict[str, Any] = {
        "schema": 1,
        "phase": "reserved",
        "transition_count": 0,
        "restore_required": False,
        "device_identity_sha256": EXPECTED_DEVICE_SHA256,
        "offset": contract.offset,
        "artifact_bytes": contract.artifact_bytes,
        "artifact_sha256": contract.artifact_sha256,
        "mutation_span_bytes": contract.mutation_span_bytes,
        "mutation_span_sha256": _sha256_bytes(install_span),
        "install_write_attempt_count": 0,
        "restore_attempt_count": 0,
        "restore_write_attempt_count": 0,
        "application_launch_count": 0,
        "recovery_inventory": {
            str(path.relative_to(ROOT)): _sha256_file(path)
            for path in (
                SCRIPT_PATH, CAPTURE_PATH.resolve(strict=True),
                E5_INSTALL_PATH.resolve(strict=True),
                E5.RESTORE_PATH.resolve(strict=True),
            )
        },
    }
    _atomic_ledger(ledger_path, state)
    write_marked = False
    try:
        stub = _load_stub(device, handle, runtime)
        table = _read_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        )
        RESTORE.parse_partition_table(
            table, mutation_offset=contract.offset,
            mutation_bytes=contract.mutation_span_bytes,
        )
        live_preimage = _read_exact(
            stub, device, handle, contract.offset,
            contract.mutation_span_bytes, readback_chunk_bytes,
        )
        if (
            len(live_preimage) < INSTALLED_E5_PADDED_SPAN_BYTES
            or _sha256_bytes(live_preimage[:INSTALLED_E5_BYTES]) != INSTALLED_E5_SHA256
            or _sha256_bytes(live_preimage[:INSTALLED_E5_PADDED_SPAN_BYTES])
                != INSTALLED_E5_PADDED_SPAN_SHA256
        ):
            raise InstallError(
                "live rollback span does not contain the exact installed E5 padded span"
            )
        table_binding = _table_binding(
            recovery_directory, table, contract, runtime
        )
        preimage_binding = _preimage_binding(
            recovery_directory, live_preimage, contract
        )
        _transition(
            ledger_path, state, "preimage-sealed",
            partition_table_binding=table_binding,
            preimage_binding=preimage_binding,
            restore_required=False,
        )

        # Reopen and validate the durable recovery material before the final
        # live-device boundary.  The identity/security/RDID checks below then
        # remain immediately adjacent to the durable transition and first
        # flash command.
        sealed_contract, sealed_table, sealed_preimage = (
            _validate_sealed_recovery_contract(state, runtime)
        )
        if (
            sealed_contract != contract
            or sealed_table != table
            or sealed_preimage != live_preimage
        ):
            raise InstallError("sealed recovery snapshots differ before E6 mutation")
        if _read_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        ) != table:
            raise InstallError("partition table changed before E6 mutation")
        if _read_exact(
            stub, device, handle, contract.offset,
            contract.mutation_span_bytes, readback_chunk_bytes,
        ) != live_preimage:
            raise InstallError("live app span changed before E6 mutation")
        if _read_sealed_artifact(artifact_path, contract) != artifact:
            raise InstallError("sealed E6 artifact changed before mutation")
        RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
        E5._fresh_exact_flash_id(stub, "e6-write-boundary")
        stub.flash_set_parameters(RESTORE.FLASH_BYTES)
        E5._assert_same_handle(device, handle, stub)
        _transition(
            ledger_path, state, "install-write-attempted",
            restore_required=True,
            install_write_attempt_count=1,
        )
        write_marked = True
        _write_span(
            stub, device, handle, contract.offset, install_span,
            WRITE_BLOCK_BYTES,
        )
        readback = _read_exact(
            stub, device, handle, contract.offset,
            contract.mutation_span_bytes, readback_chunk_bytes,
        )
        if readback != install_span:
            raise InstallError("E6 full padded mutation-span readback differs")
        if _read_exact(
            stub, device, handle, RESTORE.PARTITION_TABLE_OFFSET,
            RESTORE.PARTITION_TABLE_BYTES,
        ) != table:
            raise InstallError("partition table changed during E6 app-only write")
        RESTORE._validate_live_rom(stub, device, EXPECTED_DEVICE_SHA256)
        E5._fresh_exact_flash_id(stub, "e6-prelaunch")
        E5._set_controls_false(device)
        _transition(
            ledger_path, state, "e6-launch-attempted",
            restore_required=True, application_launch_count=1,
        )
        runtime.launch_reset_class(device, uses_usb=False).reset()
        E5._set_controls_false(device)
        E5._assert_same_handle(device, handle)

        capture_error: BaseException | None = None
        try:
            raw, summary = capture_module.capture_open_handle(
                device, capture_seconds, min_stats=2
            )
        except BaseException as error:
            capture_error = error
            raw = getattr(error, "partial_raw", b"")
            summary = getattr(error, "failure_summary", None)
            if not isinstance(raw, bytes) or len(raw) > CAPTURE.MAX_TRANSCRIPT_BYTES:
                raw = b""
            if not isinstance(summary, Mapping):
                summary = {
                    "schema": 1,
                    "result": "fail",
                    "capture_incomplete": True,
                    "capture_error_reason": _bounded_failure(error),
                }

        # Journal the durable raw binding before summary serialization/write.
        # A read error, overflow, signal, or summary persistence failure must
        # leave the rejected transcript attributable after automatic restore.
        raw_binding = _persist_capture_raw(recovery_directory, raw)
        _transition(
            ledger_path, state, "e6-capture-raw-recorded",
            restore_required=True,
            startup_capture={"raw": raw_binding},
            startup_capture_result="pending",
            startup_capture_rejection_reason=(
                _bounded_failure(capture_error) if capture_error else None
            ),
        )
        summary_binding = _persist_capture_summary(
            recovery_directory, summary
        )
        capture_binding = {
            "raw": raw_binding,
            "summary": summary_binding,
        }
        capture_result = summary.get("result")
        rejection_reason = (
            None if capture_result == "pass" and capture_error is None
            else (
                _bounded_failure(capture_error) if capture_error is not None
                else "retained-uart-startup-summary-result-not-pass"
            )
        )
        _transition(
            ledger_path, state, "e6-capture-recorded",
            restore_required=True,
            startup_capture=capture_binding,
            startup_capture_result=capture_result,
            startup_capture_rejection_reason=rejection_reason,
        )
        if capture_error is not None:
            raise InstallError(
                "E6 retained-UART startup capture failed: "
                f"{_bounded_failure(capture_error)}"
            ) from capture_error
        if capture_result != "pass":
            raise InstallError("E6 retained-UART startup acceptance failed")
        result = {
            "offset": contract.offset,
            "artifact_bytes": contract.artifact_bytes,
            "artifact_sha256": contract.artifact_sha256,
            "mutation_span_bytes": contract.mutation_span_bytes,
            "mutation_span_sha256": _sha256_bytes(install_span),
            "readback_sha256": _sha256_bytes(readback),
            "readback_chunks": (
                contract.mutation_span_bytes + readback_chunk_bytes - 1
            ) // readback_chunk_bytes,
            "same_uart_handle": True,
            "exclusive_uart_open_count": 1,
            "uart_reopen_count": 0,
            "loader_entry_reset_count": 1,
            "application_launch_count": 1,
            "startup_capture": capture_binding,
            "restore_required": False,
            "audio_gate": 1,
            "usb_runtime": False,
        }
        _transition(
            ledger_path, state, "e6-runtime-accepted",
            restore_required=False, install_result=result,
        )
        return result
    except BaseException as primary:
        if write_marked or state.get("restore_required") is True:
            primary_failure = _bounded_failure(primary)
            try:
                _transition(
                    ledger_path, state, "e6-rollback-requested",
                    restore_required=True,
                    primary_failure=primary_failure,
                )
            except BaseException as journal_error:
                # Restoration remains the safety priority.  Carry the bounded
                # cause into the next durable restore transition even if this
                # dedicated journal update itself failed.
                state["primary_failure"] = primary_failure
                state["primary_failure_journal_error"] = _bounded_failure(
                    journal_error
                )
            try:
                _restore_e5_same_handle(
                    device, runtime, ledger_path, state
                )
            except BaseException as restore_error:
                try:
                    _transition(
                        ledger_path, state, "restore-failed",
                        restore_required=True,
                        last_error=f"{type(restore_error).__name__}: {restore_error}",
                    )
                except BaseException:
                    pass
                raise InstallError(
                    f"E6 failed ({primary}); E5 restoration also failed "
                    f"({restore_error}); recovery remains required"
                ) from restore_error
            raise InstallError(
                f"E6 acceptance failed and exact E5 was restored: {primary}"
            ) from primary
        raise
    finally:
        E5._set_controls_false(device)


def recover_same_handle(*, device: Any, recovery_directory: pathlib.Path,
                        runtime: Any, state: dict[str, Any] | None = None) -> dict[str, Any]:
    _owned_directory(recovery_directory, "recovery directory")
    ledger_path = recovery_directory / LEDGER_NAME
    state = _load_ledger(ledger_path) if state is None else state
    _discard_uncommitted_ledger_replacement(ledger_path, state)
    if state.get("device_identity_sha256") != EXPECTED_DEVICE_SHA256:
        raise InstallError("recovery ledger is bound to another device")
    _validate_recovery_inventory(state)
    if state.get("restore_required") is not True:
        raise InstallError("ledger does not require restoration")
    return _restore_e5_same_handle(device, runtime, ledger_path, state)


def _arm_signals() -> dict[int, Any]:
    previous: dict[int, Any] = {}

    def interrupted(signum: int, _frame: Any) -> None:
        raise InstallError(f"E6 transaction interrupted by signal {signum}")

    for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
        previous[signum] = signal.getsignal(signum)
        signal.signal(signum, interrupted)
    return previous


def install_from_trust_anchor(
    *, port: str, artifact: pathlib.Path, authorization: pathlib.Path,
    authorization_sha256: str, recovery_directory: pathlib.Path,
    capture_seconds: float = 45.0,
) -> dict[str, Any]:
    """Run the install only for the separate frozen issuance wrapper."""

    previous = _arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        contract, _auth = _contract_from_authorization(
            authorization, authorization_sha256
        )
        _read_sealed_artifact(artifact, contract)
        _owned_directory(recovery_directory, "recovery directory")
        runtime = E5._production_runtime()
        device = E5.open_serial_once(port)
        result = install_same_handle(
            device=device, artifact_path=artifact, contract=contract,
            recovery_directory=recovery_directory, runtime=runtime,
            capture_seconds=capture_seconds,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                E5._set_controls_false(device)
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
        raise InstallError("E6 transaction ended without a result")
    return result


def main() -> int:
    """Expose crash recovery only; installation requires the issuance wrapper."""

    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("recover",))
    parser.add_argument("--port", required=True)
    parser.add_argument("--recovery-directory", type=pathlib.Path, required=True)
    args = parser.parse_args()

    previous = _arm_signals()
    device = None
    error: BaseException | None = None
    result: dict[str, Any] | None = None
    try:
        _owned_directory(args.recovery_directory, "recovery directory")
        recovery_state = _load_ledger(
            args.recovery_directory / LEDGER_NAME
        )
        _discard_uncommitted_ledger_replacement(
            args.recovery_directory / LEDGER_NAME, recovery_state
        )
        _validate_recovery_inventory(recovery_state)
        # Helper/inventory validation is complete before loading the pinned
        # esptool runtime or opening the programming UART.
        runtime = E5._production_runtime()
        device = E5.open_serial_once(args.port)
        result = recover_same_handle(
            device=device, recovery_directory=args.recovery_directory,
            runtime=runtime, state=recovery_state,
        )
    except BaseException as caught:
        error = caught
    finally:
        for signum in previous:
            signal.signal(signum, signal.SIG_IGN)
        if device is not None:
            try:
                E5._set_controls_false(device)
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
        raise InstallError("E6 transaction ended without a result")
    print(json.dumps(result, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
