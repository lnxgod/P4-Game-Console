#!/usr/bin/env python3

"""Durable fail-closed reservation for the native gamepad diagnostic.

The committed authorization is intentionally inactive.  These helpers become
usable only after a reviewed physical fixture record and an exact two-gate
active firmware artifact replace the inactive preparation record.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import secrets
import stat
import tempfile


AUTH_ID = "gamepad-diag-d1-one-shot-authorization-2026-08-13"
ACTIVE_CLASSIFICATION = "user-requested-gamepad-diagnostic-one-shot-active"
EXPECTED_OFFSET = "0x10000"
# Filled from the final reproducible build before this gate is accepted.
# Provisional inactive build identity.  The exact active token-bound artifact
# must replace these values and receive three clean builds before activation.
EXPECTED_BYTES = 301328
EXPECTED_SHA256 = "ad197d70eca589cb104bb654e5d35178dbb2cefd8fa60cdd184cd8806b1350fb"
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)
METADATA_REL = "apps/gamepad_diag/app-metadata.json"
AUTH_REL = "hardware/evidence/gamepad-diag-d1-one-shot-authorization.json"
BUILD_EVIDENCE_REL = "test-runs/2026-08-13-gamepad-d1-build.json"
BOARD_PROFILE_REL = "hardware/board-profile.json"
STATE_REL = "hardware/local-state/gamepad-diag-d1-one-shot.json"
RESTORE_TOOL_PATH = pathlib.Path(__file__).resolve().with_name("gamepad-diag-restore.py")
SECTOR_BYTES = 4096
INSTALL_WRITE_BLOCK_BYTES = 0x4000
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_BYTES = 0xC00
FIXTURE_CLASSIFICATION = (
    "reviewed-powered-current-limited-backfeed-safe-direct-j16-host-fixture"
)
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
UTC_RE = re.compile(r"^20[0-9]{2}-[01][0-9]-[0-3][0-9]T[0-2][0-9]:[0-5][0-9]:[0-5][0-9]Z$")


class StateError(RuntimeError):
    pass


def canonical_utc(value: object) -> bool:
    if not isinstance(value, str) or UTC_RE.fullmatch(value) is None:
        return False
    try:
        parsed = dt.datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ")
    except ValueError:
        return False
    return parsed.strftime("%Y-%m-%dT%H:%M:%SZ") == value


def load_restore_tool():
    spec = importlib.util.spec_from_file_location("gamepad_diag_restore_state", RESTORE_TOOL_PATH)
    if spec is None or spec.loader is None:
        raise StateError("cannot load the exact gamepad restoration helper")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def exact_project_root(project_root: pathlib.Path) -> pathlib.Path:
    expected = pathlib.Path(__file__).resolve().parents[1]
    try:
        supplied = project_root.resolve(strict=True)
    except OSError as error:
        raise StateError(f"project root is unavailable: {error}") from error
    if supplied != expected:
        raise StateError("project root must be the repository containing this state tool")
    return supplied


def exact_new_state_path(path: pathlib.Path) -> pathlib.Path:
    """Require the one canonical durable ledger path before O_EXCL creation."""

    root = pathlib.Path(__file__).resolve().parents[1]
    expected = root / STATE_REL
    try:
        parent = path.parent.resolve(strict=True)
    except OSError as error:
        raise StateError(f"state parent is unavailable: {error}") from error
    if path.name != expected.name or parent != expected.parent.resolve(strict=True):
        raise StateError("one-shot state must use the canonical local-state ledger path")
    return expected


def mutation_span(byte_count: int) -> int:
    if not isinstance(byte_count, int) or isinstance(byte_count, bool) or byte_count <= 0:
        raise StateError("artifact byte count must be a positive integer")
    return (
        (byte_count + INSTALL_WRITE_BLOCK_BYTES - 1) // INSTALL_WRITE_BLOCK_BYTES
    ) * INSTALL_WRITE_BLOCK_BYTES


def validate_recovery_directory(path: pathlib.Path) -> pathlib.Path:
    try:
        info = path.lstat()
    except OSError as error:
        raise StateError(f"recovery directory is unavailable: {error}") from error
    expected_parent = (pathlib.Path(__file__).resolve().parents[1] / "hardware/local-state").resolve()
    resolved = path.resolve(strict=True)
    if not (
        stat.S_ISDIR(info.st_mode)
        and not stat.S_ISLNK(info.st_mode)
        and info.st_uid == os.getuid()
        and stat.S_IMODE(info.st_mode) == 0o700
        and resolved.parent == expected_parent
        and resolved.name.startswith("gamepad-diag-recovery.")
    ):
        raise StateError("recovery directory must be an owned 0700 dedicated local-state child")
    return resolved


def private_file_binding(
    path: pathlib.Path, expected_bytes: int, label: str
) -> dict[str, object]:
    try:
        info = path.lstat()
    except OSError as error:
        raise StateError(f"cannot stat {label}: {error}") from error
    if not (
        stat.S_ISREG(info.st_mode)
        and not stat.S_ISLNK(info.st_mode)
        and info.st_uid == os.getuid()
        and stat.S_IMODE(info.st_mode) == 0o600
        and info.st_size == expected_bytes
    ):
        raise StateError(f"{label} is not an exact private regular file")
    return {
        "path": str(path.resolve(strict=True)),
        "device": info.st_dev,
        "inode": info.st_ino,
        "mode": stat.S_IMODE(info.st_mode),
        "bytes": info.st_size,
        "sha256": sha256_file(path),
    }


def binding_matches(path: pathlib.Path, binding: object) -> bool:
    if not isinstance(binding, dict):
        return False
    try:
        current = private_file_binding(path, int(binding.get("bytes", -1)), "bound file")
    except (StateError, TypeError, ValueError):
        return False
    core = {
        key: binding.get(key)
        for key in ("path", "device", "inode", "mode", "bytes", "sha256")
    }
    return current == core


def load_object(path: pathlib.Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise StateError(f"cannot read {label}: {error}") from error
    if not isinstance(value, dict):
        raise StateError(f"{label} must contain a JSON object")
    return value


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise StateError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def fsync_private_file(path: pathlib.Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    try:
        info = os.fstat(descriptor)
        if not (
            stat.S_ISREG(info.st_mode)
            and info.st_uid == os.getuid()
            and stat.S_IMODE(info.st_mode) == 0o600
        ):
            raise StateError("private recovery file changed before fsync")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
    directory = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def new_owner_token(path: pathlib.Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    token = secrets.token_hex(32) + "\n"
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise StateError("owner-token file already exists") from error
    with os.fdopen(descriptor, "w", encoding="ascii") as output:
        output.write(token)
        output.flush()
        os.fsync(output.fileno())
    directory = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def read_owner_token(path: pathlib.Path) -> str:
    try:
        info = path.lstat()
    except OSError as error:
        raise StateError(f"cannot stat owner-token file: {error}") from error
    if not (
        stat.S_ISREG(info.st_mode)
        and not stat.S_ISLNK(info.st_mode)
        and info.st_uid == os.getuid()
        and stat.S_IMODE(info.st_mode) == 0o600
        and info.st_size == 65
    ):
        raise StateError("owner-token file permissions/ownership are not exact 0600")
    try:
        descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    except OSError as error:
        raise StateError(f"cannot open owner-token file: {error}") from error
    payload = bytearray(65)
    try:
        opened = os.fstat(descriptor)
        if (
            opened.st_dev != info.st_dev
            or opened.st_ino != info.st_ino
            or opened.st_size != 65
        ):
            raise StateError("owner-token file changed while opening")
        view = memoryview(payload)
        count = 0
        while count < len(payload):
            step = os.readv(descriptor, [view[count:]])
            if step <= 0:
                raise StateError("owner-token file was truncated")
            count += step
        if os.read(descriptor, 1):
            raise StateError("owner-token file grew while reading")
        if payload[64] != 0x0A or any(
            not (0x30 <= value <= 0x39 or 0x61 <= value <= 0x66)
            for value in payload[:64]
        ):
            raise StateError("owner token must be 256 bits of lowercase hexadecimal")
        value = bytes(memoryview(payload)[:64]).decode("ascii")
    finally:
        zeroize(payload)
        os.close(descriptor)
    if re.fullmatch(r"[0-9a-f]{64}", value) is None:
        raise StateError("owner token must be 256 bits of lowercase hexadecimal")
    return value


def zeroize(value: bytearray) -> None:
    for index in range(len(value)):
        value[index] = 0


def destroy_bound_arm_secret(state: dict) -> bool:
    """Unlink a bound pre-install ARM secret without reading its contents."""

    binding = state.get("arm_secret")
    if not isinstance(binding, dict):
        return False
    recovery_text = state.get("recovery_directory")
    if not isinstance(recovery_text, str) or not recovery_text:
        raise StateError("bound ARM secret has no recovery directory")
    recovery = validate_recovery_directory(pathlib.Path(recovery_text))
    path = pathlib.Path(str(binding.get("path", "")))
    if not (
        path.name == "arm-token"
        and path.parent.resolve(strict=True) == recovery
        and binding.get("path") == str(path)
        and binding.get("token_sha256") == state.get("arm_token_sha256")
    ):
        raise StateError("refusing to unlink a changed or unbound ARM secret")
    try:
        path.lstat()
    except FileNotFoundError:
        # Receipt consumption and recovery deliberately unlink+fsync this exact
        # child before reset.  Absence is therefore an idempotent destroyed
        # state, but only after the recovery directory and lexical child have
        # been validated above.
        directory = os.open(recovery, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        return True
    if not binding_matches(path, binding):
        raise StateError("refusing to unlink a changed or substituted ARM secret")
    path.unlink()
    directory = os.open(recovery, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
    return True


def read_arm_secret(path: pathlib.Path) -> bytearray:
    try:
        info = path.lstat()
    except OSError as error:
        raise StateError(f"cannot stat private ARM secret: {error}") from error
    if not (
        stat.S_ISREG(info.st_mode)
        and not stat.S_ISLNK(info.st_mode)
        and info.st_uid == os.getuid()
        and stat.S_IMODE(info.st_mode) == 0o600
        and info.st_size == 65
    ):
        raise StateError("private ARM secret must be an owned exact 0600 file")
    descriptor = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
    payload = bytearray(65)
    try:
        opened = os.fstat(descriptor)
        if opened.st_dev != info.st_dev or opened.st_ino != info.st_ino:
            raise StateError("private ARM secret changed while opening")
        view = memoryview(payload)
        read = 0
        while read < len(payload):
            count = os.readv(descriptor, [view[read:]])
            if count <= 0:
                raise StateError("private ARM secret was truncated")
            read += count
        if os.read(descriptor, 1):
            raise StateError("private ARM secret grew while reading")
        if payload[64] != 0x0A or any(
            not (0x30 <= value <= 0x39 or 0x61 <= value <= 0x66)
            for value in payload[:64]
        ):
            raise StateError("private ARM secret must be 64 lowercase hex bytes plus LF")
        return payload
    except BaseException:
        zeroize(payload)
        raise
    finally:
        os.close(descriptor)


def check_arm_secret(
    path: pathlib.Path, auth_path: pathlib.Path, project_root: pathlib.Path
) -> str:
    auth, *_rest = validate_active_authorization(auth_path, project_root)
    payload = read_arm_secret(path)
    try:
        digest = hashlib.sha256(memoryview(payload)[:64]).hexdigest()
        if digest != auth["host_arm"]["token_sha256"]:
            raise StateError("private ARM secret differs from the active compiled digest")
        return digest
    finally:
        zeroize(payload)


def stage_arm_secret(
    source: pathlib.Path,
    destination: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
) -> None:
    digest = check_arm_secret(source, auth_path, project_root)
    recovery = validate_recovery_directory(destination.parent)
    if destination.resolve(strict=False).parent != recovery:
        raise StateError("staged ARM secret must be a direct recovery-bundle child")
    payload = read_arm_secret(source)
    descriptor = -1
    try:
        if hashlib.sha256(memoryview(payload)[:64]).hexdigest() != digest:
            raise StateError("private ARM secret changed between validation and staging")
        descriptor = os.open(
            destination,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0),
            0o600,
        )
        written = 0
        view = memoryview(payload)
        while written < len(payload):
            count = os.write(descriptor, view[written:])
            if count <= 0:
                raise StateError("staging private ARM secret made no progress")
            written += count
        os.fsync(descriptor)
        os.close(descriptor)
        descriptor = -1
        directory = os.open(recovery, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        source.unlink()
        source_directory = os.open(source.parent, os.O_RDONLY)
        try:
            os.fsync(source_directory)
        finally:
            os.close(source_directory)
    except BaseException:
        if descriptor >= 0:
            os.close(descriptor)
        try:
            destination.unlink()
        except FileNotFoundError:
            pass
        raise
    finally:
        zeroize(payload)


def qualification_valid(value: object) -> bool:
    if not isinstance(value, dict):
        return False
    required_true = (
        "j16_vbus_physically_open",
        "controller_side_regulated_5v",
        "current_limit_measured",
        "backfeed_blocked",
        "common_ground",
        "data_pair_direct",
        "source_role_compliant",
        "overcurrent_fault_visible",
        "exact_board_usb_path_reviewed",
    )
    current_limit = value.get("current_limit_ma")
    return (
        set(value) == {*required_true, "current_limit_ma"}
        and all(value.get(field) is True for field in required_true)
        and isinstance(current_limit, int)
        and not isinstance(current_limit, bool)
        and 100 <= current_limit <= 500
    )


def physical_evidence_complete(fixture: dict) -> bool:
    fixture_identity = fixture.get("fixture")
    measurements = fixture.get("measurements")
    provenance = fixture.get("provenance")
    review = fixture.get("review")
    if not (
        isinstance(fixture_identity, dict)
        and set(fixture_identity) == {"manufacturer", "model", "revision"}
        and isinstance(fixture_identity.get("manufacturer"), str)
        and fixture_identity.get("manufacturer").strip()
        and isinstance(fixture_identity.get("model"), str)
        and fixture_identity.get("model").strip()
        and isinstance(fixture_identity.get("revision"), str)
        and fixture_identity.get("revision").strip()
        and isinstance(measurements, dict)
        and set(measurements)
        == {
            "controller_vbus_voltage_mv",
            "current_limit_trip_ma",
            "j16_vbus_to_controller_vbus_ohms",
            "backfeed_to_j16_vbus_mv",
            "backfeed_to_j1_programmer_vbus_mv",
            "fault_observed",
        }
        and isinstance(provenance, dict)
        and set(provenance)
        == {
            "exact_board_path_review",
            "schematic_sha256",
            "fixture_schematic_sha256",
            "wiring_photo_sha256",
        }
        and isinstance(review, dict)
        and set(review) == {"reviewer", "reviewed_at_utc", "approved"}
    ):
        return False
    required_measurements = {
        "controller_vbus_voltage_mv": (4750, 5250, "mV"),
        "current_limit_trip_ma": (100, 500, "mA"),
        "j16_vbus_to_controller_vbus_ohms": (1_000_000, None, "ohm"),
        "backfeed_to_j16_vbus_mv": (0, 100, "mV"),
        "backfeed_to_j1_programmer_vbus_mv": (0, 100, "mV"),
    }
    for field, (minimum, maximum, unit) in required_measurements.items():
        measurement = measurements.get(field)
        if not (
            isinstance(measurement, dict)
            and set(measurement) == {"value", "unit", "instrument", "observed_at_utc"}
            and isinstance(measurement.get("value"), (int, float))
            and not isinstance(measurement.get("value"), bool)
            and isinstance(measurement.get("instrument"), str)
            and measurement.get("instrument").strip()
            and canonical_utc(measurement.get("observed_at_utc"))
            and measurement.get("unit") == unit
        ):
            return False
        value = measurement["value"]
        if value < minimum or (maximum is not None and value > maximum):
            return False
    fault = measurements.get("fault_observed")
    if not (
        fixture.get("qualification", {}).get("current_limit_ma")
        == measurements.get("current_limit_trip_ma", {}).get("value")
        and isinstance(fault, dict)
        and set(fault) == {"asserted", "controller_vbus_removed", "observed_at_utc"}
        and fault.get("asserted") is True
        and fault.get("controller_vbus_removed") is True
        and canonical_utc(fault.get("observed_at_utc"))
        and isinstance(provenance.get("exact_board_path_review"), str)
        and provenance.get("exact_board_path_review").strip()
        and SHA256_RE.fullmatch(str(provenance.get("schematic_sha256")))
        and SHA256_RE.fullmatch(str(provenance.get("fixture_schematic_sha256")))
        and SHA256_RE.fullmatch(str(provenance.get("wiring_photo_sha256")))
        and isinstance(review.get("reviewer"), str)
        and review.get("reviewer").strip()
        and canonical_utc(review.get("reviewed_at_utc"))
        and review.get("approved") is True
    ):
        return False
    return True


def resolve_fixture(project_root: pathlib.Path, relative: object) -> pathlib.Path:
    if not isinstance(relative, str) or not relative:
        raise StateError("authorization has no fixture evidence path")
    relative_path = pathlib.PurePosixPath(relative)
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise StateError("fixture evidence path must be repository-relative")
    root = project_root.resolve()
    path = (root / pathlib.Path(*relative_path.parts)).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise StateError("fixture evidence is missing or leaves the repository")
    return path


def validate_active_metadata(
    project_root: pathlib.Path, fixture_path: pathlib.Path, fixture_sha256: str
) -> tuple[pathlib.Path, str]:
    root = project_root.resolve()
    metadata_path = (root / METADATA_REL).resolve()
    if not metadata_path.is_relative_to(root) or not metadata_path.is_file():
        raise StateError("active gamepad metadata is missing")
    metadata = load_object(metadata_path, "gamepad metadata")
    fixture_relative = fixture_path.relative_to(root).as_posix()
    if not (
        metadata.get("schema") == 1
        and metadata.get("app") == "gamepad_diag"
        and metadata.get("build_evidence") == BUILD_EVIDENCE_REL
        and metadata.get("one_shot_authorization_evidence") == AUTH_REL
        and metadata.get("one_shot_authorization_active") is True
        and metadata.get("usb_runtime_authorized") is True
        and metadata.get("flash_authorized") is False
        and metadata.get("flash_app_authorized") is True
        and metadata.get("flash_project_authorized") is False
        and metadata.get("fixture_authorized") is True
        and metadata.get("fixture_evidence") == fixture_relative
        and metadata.get("fixture_evidence_sha256") == fixture_sha256
        and metadata.get("runtime_hardware_interfaces")
        == ["esp32p4_usb_otg_hs_peripheral_0"]
    ):
        raise StateError("metadata is not the exact active app-only USB authorization")
    return metadata_path, sha256_file(metadata_path)


def validate_active_board_profile(
    project_root: pathlib.Path,
    fixture_path: pathlib.Path,
    fixture_sha256: str,
    fixture_id: str,
) -> tuple[pathlib.Path, str]:
    root = exact_project_root(project_root)
    profile_path = (root / BOARD_PROFILE_REL).resolve(strict=True)
    profile = load_object(profile_path, "board profile")
    usb_host = profile.get("peripheral_authorizations", {}).get("usb_host")
    fixture_relative = fixture_path.relative_to(root).as_posix()
    if not (
        profile.get("schema") == 1
        and profile.get("pin_map_authorized") is False
        and profile.get("device_identity", {}).get("sha256")
        == EXPECTED_DEVICE_SHA256
        and isinstance(usb_host, dict)
        and set(usb_host)
        == {
            "authorized",
            "scope",
            "peripheral",
            "j16_vbus_mode",
            "fixture_evidence_id",
            "fixture_evidence",
            "fixture_evidence_sha256",
        }
        and usb_host.get("authorized") is True
        and usb_host.get("scope")
        == "esp32p4_hs_peripheral_0_direct_j16_data_only"
        and usb_host.get("peripheral") == "esp32p4_usb_otg_hs_peripheral_0"
        and usb_host.get("j16_vbus_mode") == "physically-open-fixture-owned"
        and usb_host.get("fixture_evidence_id") == fixture_id
        and usb_host.get("fixture_evidence") == fixture_relative
        and usb_host.get("fixture_evidence_sha256") == fixture_sha256
    ):
        raise StateError("board profile has no exact scoped USB Host authorization")
    return profile_path, sha256_file(profile_path)


def expected_compiled_fixture(
    fixture_id: str, fixture_sha256: str, qualification: object
) -> dict[str, object]:
    """Translate the reviewed fixture into the only accepted Kconfig payload."""

    if (
        re.fullmatch(r"[A-Za-z0-9._-]{1,96}", fixture_id or "") is None
        or SHA256_RE.fullmatch(fixture_sha256 or "") is None
        or not qualification_valid(qualification)
    ):
        raise StateError("reviewed fixture cannot form an exact compiled binding")
    assert isinstance(qualification, dict)
    return {
        "fixture_authorized": True,
        "fixture_evidence_id": fixture_id,
        "fixture_evidence_sha256": fixture_sha256,
        "current_limit_ma": qualification["current_limit_ma"],
        "external_vbus": qualification["controller_side_regulated_5v"],
        "current_limited": qualification["current_limit_measured"],
        "backfeed_blocked": qualification["backfeed_blocked"],
        "common_ground": qualification["common_ground"],
        "data_pair_direct": qualification["data_pair_direct"],
        "source_role_compliant": qualification["source_role_compliant"],
        "fault_visible": qualification["overcurrent_fault_visible"],
        "board_path_reviewed": qualification["exact_board_usb_path_reviewed"],
    }


def validate_active_resolved_fixture_config(
    project_root: pathlib.Path,
    record: object,
    expected: dict[str, object],
) -> None:
    """Bind the generated active sdkconfig bytes and every fixture Kconfig."""

    root = exact_project_root(project_root)
    if not isinstance(record, dict) or set(record) != {
        "sdkconfig_path", "sdkconfig_sha256", "compiled_fixture",
    }:
        raise StateError("active build evidence has no exact resolved fixture config")
    if record.get("compiled_fixture") != expected:
        raise StateError("active build compiled fixture differs from reviewed evidence")
    if record.get("sdkconfig_path") != "apps/gamepad_diag/sdkconfig":
        raise StateError("active sdkconfig path changed")
    sdkconfig = (root / str(record["sdkconfig_path"])).resolve(strict=True)
    if not sdkconfig.is_relative_to(root) or not sdkconfig.is_file():
        raise StateError("active sdkconfig is missing or leaves the repository")
    if (
        SHA256_RE.fullmatch(str(record.get("sdkconfig_sha256"))) is None
        or sha256_file(sdkconfig) != record["sdkconfig_sha256"]
    ):
        raise StateError("active sdkconfig/compiled fixture binding changed")
    try:
        lines = sdkconfig.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise StateError(f"cannot read active sdkconfig: {error}") from error
    values: dict[str, str] = {}
    for line in lines:
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            if key in values:
                raise StateError("active sdkconfig repeats a configuration key")
            values[key] = value
    exact_values = {
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_AUTHORIZED": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_ID": json.dumps(
            expected["fixture_evidence_id"]
        ),
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_EVIDENCE_SHA256": json.dumps(
            expected["fixture_evidence_sha256"]
        ),
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMIT_MA": str(
            expected["current_limit_ma"]
        ),
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_EXTERNAL_VBUS": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_CURRENT_LIMITED": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_BACKFEED_BLOCKED": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_COMMON_GROUND": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_DATA_PAIR_DIRECT": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_SOURCE_ROLE_COMPLIANT": "y",
        "CONFIG_PLATFORM_USB_HOST_FIXTURE_FAULT_VISIBLE": "y",
        "CONFIG_PLATFORM_USB_HOST_BOARD_PATH_REVIEWED": "y",
    }
    if any(values.get(key) != value for key, value in exact_values.items()):
        raise StateError("resolved active sdkconfig differs from reviewed fixture")


def validate_active_build_evidence(
    project_root: pathlib.Path,
    arm_token_sha256: str,
    fixture_id: str,
    fixture_sha256: str,
    qualification: object,
) -> tuple[pathlib.Path, str]:
    """Require an exact, hardware-inert, three-build active-artifact record."""

    root = exact_project_root(project_root)
    path = (root / BUILD_EVIDENCE_REL).resolve(strict=True)
    evidence = load_object(path, "active gamepad build evidence")
    build = evidence.get("build", {})
    artifacts = evidence.get("artifacts", {})
    reproducibility = evidence.get("reproducibility", {})
    source_audit = evidence.get("source_and_link_audit", {})
    execution = evidence.get("execution", {})
    host_arm = evidence.get("host_arm", {})
    dependencies = evidence.get("dependencies", {})
    toolchain = evidence.get("toolchain", {})
    identities = reproducibility.get("identities")
    inventory = evidence.get("source_inventory")
    gate_inventory = evidence.get("gate_inventory")
    compiled_fixture = expected_compiled_fixture(
        fixture_id, fixture_sha256, qualification
    )
    if not all(
        isinstance(value, dict)
        for value in (
            build,
            artifacts,
            reproducibility,
            source_audit,
            execution,
            host_arm,
            dependencies,
            toolchain,
            inventory,
            gate_inventory,
        )
    ):
        raise StateError("active build evidence has malformed contract objects")
    required_gate_paths = (
        "scripts/gamepad-diag-one-shot-state.py",
        "scripts/gamepad-diag-capture.py",
        "scripts/gamepad-diag-restore.py",
        "scripts/gamepad-diag-install.py",
        "scripts/tests/test-gamepad-diag-install.py",
        "scripts/verify-gamepad-diag.py",
        "scripts/flash.sh",
    )
    required_source_paths = (
        "apps/gamepad_diag/CMakeLists.txt",
        "apps/gamepad_diag/main/CMakeLists.txt",
        "apps/gamepad_diag/main/gamepad_diag_main.c",
        "apps/gamepad_diag/main/gamepad_diag_arm_model.c",
        "apps/gamepad_diag/main/gamepad_diag_arm_model.h",
        "apps/gamepad_diag/main/idf_component.yml",
        "apps/gamepad_diag/sdkconfig.defaults",
        "apps/gamepad_diag/dependencies.lock",
        "components/gamepad_core/include/gamepad/hid_gamepad.h",
        "components/gamepad_core/src/hid_gamepad.c",
        "components/platform_usb_host/Kconfig",
        "components/platform_usb_host/cmake/verify_native_gamepad_link.cmake",
        "components/platform_usb_host/src/platform_usb_host.c",
        "components/platform_usb_host/src/policy.c",
        "components/platform_gamepad_usb/CMakeLists.txt",
        "components/platform_gamepad_usb/src/platform_gamepad_usb.c",
        "components/platform_gamepad_usb/src/usb_host_open_guard.c",
        "components/platform_gamepad_usb/src/usb_host_open_wrapper.c",
        "toolchain.lock.json",
    )
    if not (
        evidence.get("schema") == 1
        and evidence.get("one_shot_authorization_evidence") == AUTH_REL
        and toolchain.get("esp_idf_version") == "5.5.3"
        and toolchain.get("esp_idf_commit")
        == "2c211b236707889e8400c4dc5644dd5c4ee071e0"
        and dependencies.get("lock_file") == "apps/gamepad_diag/dependencies.lock"
        and dependencies.get("lock_file_sha256")
        == inventory.get("apps/gamepad_diag/dependencies.lock")
        and dependencies.get("usb_version") == "1.5.0"
        and dependencies.get("usb_host_hid_version") == "1.2.0"
        and dependencies.get("managed_component_sources_modified") is False
        and build.get("clean_builds_compared") == 3
        and build.get("independent_build_directories_compared") == 3
        and build.get("identical_binary") is True
        and build.get("identical_elf") is True
        and build.get("target") == "esp32p4"
        and build.get("application_offset") == EXPECTED_OFFSET
        and build.get("usb_fixture_authorization_enabled") is True
        and build.get("global_pin_map_authorized") is False
        and artifacts.get("app_binary_bytes") == EXPECTED_BYTES
        and artifacts.get("app_binary_sha256") == EXPECTED_SHA256
        and isinstance(artifacts.get("elf_bytes"), int)
        and not isinstance(artifacts.get("elf_bytes"), bool)
        and artifacts.get("elf_bytes") > 0
        and SHA256_RE.fullmatch(str(artifacts.get("elf_sha256"))) is not None
        and source_audit.get("post_link_audit") == "pass"
        and source_audit.get("app_usb_gate_value") == 1
        and source_audit.get("platform_usb_gate_value") == 1
        and source_audit.get("required_native_usb_host_hid_symbols_present") is True
        and source_audit.get("usb_host_install_and_daemon_retained") is True
        and source_audit.get("guarded_hid_device_open_wrapper_retained") is True
        and source_audit.get("host_then_hid_then_root_enable_call_order") is True
        and source_audit.get("compiled_fixture_post_link_verified") is True
        and source_audit.get("compiled_fixture_binding") == compiled_fixture
        and source_audit.get("compiled_arm_token_post_link_verified") is True
        and source_audit.get("compiled_arm_token_sha256") == arm_token_sha256
        and isinstance(source_audit.get("descriptor_hash_binary_occurrences"), int)
        and not isinstance(source_audit.get("descriptor_hash_binary_occurrences"), bool)
        and source_audit.get("descriptor_hash_binary_occurrences") >= 2
        and execution.get("firmware_flashed") is False
        and execution.get("firmware_executed") is False
        and execution.get("hardware_accessed") is False
        and host_arm
        == {
            "token_sha256": arm_token_sha256,
            "private_token_committed_or_logged": False,
            "compiled_digest_only": True,
        }
        and isinstance(identities, list)
        and len(identities) == 3
        and len({item.get("label") for item in identities if isinstance(item, dict)}) == 3
        and all(
            isinstance(item, dict)
            and item.get("app_binary_bytes") == EXPECTED_BYTES
            and item.get("app_binary_sha256") == EXPECTED_SHA256
            and item.get("elf_bytes") == artifacts.get("elf_bytes")
            and item.get("elf_sha256") == artifacts.get("elf_sha256")
            for item in identities
        )
    ):
        raise StateError("active build evidence is not an exact three-build record")
    for relative, expected_hash in {**inventory, **gate_inventory}.items():
        if not isinstance(relative, str) or SHA256_RE.fullmatch(str(expected_hash)) is None:
            raise StateError("active build source inventory is malformed")
        candidate = (root / relative).resolve(strict=True)
        if not candidate.is_relative_to(root) or not candidate.is_file():
            raise StateError("active build source inventory leaves the repository")
        if sha256_file(candidate) != expected_hash:
            raise StateError(f"active build source changed: {relative}")
    if any(relative not in gate_inventory for relative in required_gate_paths):
        raise StateError("active build evidence omits a gate/restore source")
    if any(relative not in inventory for relative in required_source_paths):
        raise StateError("active build evidence omits a runtime/dependency source")
    validate_active_resolved_fixture_config(
        root, evidence.get("resolved_active_config"), compiled_fixture
    )
    return path, sha256_file(path)


def validate_active_authorization(
    path: pathlib.Path, project_root: pathlib.Path
) -> tuple[dict, dict, pathlib.Path, str, pathlib.Path, str, pathlib.Path, str]:
    root = exact_project_root(project_root)
    if path.resolve() != (root / AUTH_REL).resolve():
        raise StateError("authorization path must be the repository authorization")
    auth = load_object(path, "authorization")
    exact = auth.get("exact_artifact")
    binding = auth.get("device_binding")
    gates = auth.get("compiled_gate_state")
    scope = auth.get("scope")
    fixture_binding = auth.get("fixture_evidence")
    if not (
        auth.get("schema") == 1
        and auth.get("id") == AUTH_ID
        and auth.get("classification") == ACTIVE_CLASSIFICATION
        and auth.get("active") is True
        and auth.get("issuable") is True
        and auth.get("consumed") is False
        and exact
        == {
            "offset": EXPECTED_OFFSET,
            "bytes": EXPECTED_BYTES,
            "sha256": EXPECTED_SHA256,
            "build_evidence": "test-runs/2026-08-13-gamepad-d1-build.json",
        }
        and isinstance(binding, dict)
        and set(binding)
        == {"identity_kind", "identity_sha256", "flash_bytes", "chip", "chip_revision"}
        and binding.get("identity_kind") == "sha256-of-normalized-base-identity"
        and binding.get("identity_sha256") == EXPECTED_DEVICE_SHA256
        and binding.get("flash_bytes") == 16777216
        and binding.get("chip") == "ESP32-P4"
        and binding.get("chip_revision") == "v1.3"
        and gates == {
            "app_runtime_authorization_gate": 1,
            "platform_build_authorization_gate": 1,
        }
        and isinstance(scope, dict)
        and set(scope)
        == {
            "app",
            "app_partition_only",
            "full_project_flash",
            "bootloader_write",
            "partition_table_write",
            "data_partition_write",
            "erase_flash",
            "usb_runtime",
            "usb_fixture_authorized",
            "controller_acceptance_run",
            "display",
            "audio",
            "storage",
            "touch",
        }
        and scope.get("app") == "gamepad_diag"
        and scope.get("app_partition_only") is True
        and scope.get("full_project_flash") is False
        and scope.get("bootloader_write") is False
        and scope.get("partition_table_write") is False
        and scope.get("data_partition_write") is False
        and scope.get("erase_flash") is False
        and scope.get("usb_runtime") is True
        and scope.get("usb_fixture_authorized") is True
        and scope.get("controller_acceptance_run") is True
        and scope.get("display") is False
        and scope.get("audio") is False
        and scope.get("storage") is False
        and scope.get("touch") is False
        and isinstance(fixture_binding, dict)
        and set(fixture_binding) == {"id", "path", "sha256", "qualification"}
        and isinstance(auth.get("host_arm"), dict)
        and set(auth["host_arm"]) == {"token_sha256"}
    ):
        raise StateError("authorization is not an exact active gamepad one-shot")

    fixture_path = resolve_fixture(root, fixture_binding.get("path"))
    fixture_sha256 = fixture_binding.get("sha256")
    if SHA256_RE.fullmatch(str(fixture_sha256)) is None:
        raise StateError("fixture evidence SHA-256 is invalid")
    if sha256_file(fixture_path) != fixture_sha256:
        raise StateError("fixture evidence SHA-256 mismatch")
    fixture = load_object(fixture_path, "fixture evidence")
    if not (
        set(fixture)
        == {
            "schema",
            "id",
            "classification",
            "result",
            "device_identity_sha256",
            "fixture",
            "qualification",
            "measurements",
            "provenance",
            "review",
        }
        and fixture.get("schema") == 1
        and fixture.get("id") == fixture_binding.get("id")
        and fixture.get("classification") == FIXTURE_CLASSIFICATION
        and fixture.get("result") == "pass"
        and fixture.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and qualification_valid(fixture.get("qualification"))
        and physical_evidence_complete(fixture)
    ):
        raise StateError("fixture evidence does not satisfy the physical contract")
    if fixture_binding.get("qualification") != fixture.get("qualification"):
        raise StateError("authorization fixture qualification differs from evidence")
    metadata_path, metadata_sha256 = validate_active_metadata(
        root, fixture_path, fixture_sha256
    )
    profile_path, profile_sha256 = validate_active_board_profile(
        root, fixture_path, fixture_sha256, fixture["id"]
    )
    arm_digest = auth.get("host_arm", {}).get("token_sha256")
    if SHA256_RE.fullmatch(str(arm_digest)) is None or arm_digest == "0" * 64:
        raise StateError("authorization has no valid public host-arm token digest")
    validate_active_build_evidence(
        root,
        arm_digest,
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    return (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    )


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def write_replacement(path: pathlib.Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", dir=str(path.parent), text=True
    )
    temporary_path = pathlib.Path(temporary)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(value, output, indent=2, sort_keys=True)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
        directory = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        try:
            temporary_path.unlink()
        except FileNotFoundError:
            pass


def check_available(
    state_path: pathlib.Path, auth_path: pathlib.Path, project_root: pathlib.Path
) -> None:
    exact_new_state_path(state_path)
    validate_active_authorization(auth_path, project_root)
    if state_path.exists():
        state = load_object(state_path, "one-shot state")
        raise StateError(
            "gamepad one-shot is unavailable: durable state already exists "
            f"with status={state.get('status', 'invalid')}"
        )


def reservation_matches(
    state: dict,
    auth_sha256: str,
    fixture_sha256: str,
    metadata_path: pathlib.Path,
    metadata_sha256: str,
    profile_path: pathlib.Path,
    profile_sha256: str,
    build_evidence_path: pathlib.Path,
    build_evidence_sha256: str,
    owner_token: str,
) -> bool:
    return (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("status") == "reserved"
        and state.get("terminal") is False
        and state.get("launch_hard_reset_count") == 0
        and state.get("hard_reset_attempt_count") == 0
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and state.get("authorization_sha256") == auth_sha256
        and state.get("fixture_evidence_sha256") == fixture_sha256
        and state.get("metadata_path") == str(metadata_path.resolve())
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path.resolve())
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("build_evidence_path") == str(build_evidence_path.resolve())
        and state.get("build_evidence_sha256") == build_evidence_sha256
        and state.get("exact_artifact")
        == {
            "offset": EXPECTED_OFFSET,
            "bytes": EXPECTED_BYTES,
            "sha256": EXPECTED_SHA256,
            "sector_bytes": SECTOR_BYTES,
            "install_write_block_bytes": INSTALL_WRITE_BLOCK_BYTES,
            "mutation_span_bytes": mutation_span(EXPECTED_BYTES),
        }
    )


def bind_receipt(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
    receipt_path: pathlib.Path,
    receipt_sha256: str,
) -> None:
    if SHA256_RE.fullmatch(receipt_sha256) is None:
        raise StateError("receipt SHA-256 is invalid")
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_path, build_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "installed-verified"
        and state.get("terminal") is False
        and state.get("restore_required") is True
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and state.get("authorization_sha256") == sha256_file(auth_path)
        and state.get("fixture_evidence_path") == str(fixture_path.resolve())
        and state.get("fixture_evidence_sha256") == fixture_sha256
        and state.get("metadata_path") == str(metadata_path.resolve())
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path.resolve())
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("build_evidence_path") == str(build_path.resolve())
        and state.get("build_evidence_sha256") == build_sha256
        and state.get("arm_token_sha256") == auth["host_arm"]["token_sha256"]
        and binding_matches(
            pathlib.Path(state.get("restore_preimage", {}).get("path", "")),
            {key: value for key, value in state.get("restore_preimage", {}).items()
             if key in {"path", "device", "inode", "mode", "bytes", "sha256"}},
        )
        and binding_matches(
            pathlib.Path(state.get("arm_secret", {}).get("path", "")),
            state.get("arm_secret"),
        )
    ):
        raise StateError("receipt has no exact installed image and bound recovery state")
    state["status"] = "pending-capture"
    state["receipt_path"] = str(receipt_path.resolve())
    state["receipt_sha256"] = receipt_sha256
    write_replacement(state_path, state)


def bind_preimage(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
    recovery_dir: pathlib.Path,
    partition_table_path: pathlib.Path,
    preimage_path: pathlib.Path,
    arm_secret_path: pathlib.Path,
) -> None:
    state = check_reservation(state_path, auth_path, project_root, owner_token)
    recovery = validate_recovery_directory(recovery_dir)
    if any(path.resolve().parent != recovery for path in (
        partition_table_path, preimage_path, arm_secret_path
    )):
        raise StateError("recovery files must be direct children of the bound directory")
    span = mutation_span(EXPECTED_BYTES)
    for path in (partition_table_path, preimage_path, arm_secret_path):
        fsync_private_file(path)
    restore = load_restore_tool()
    try:
        parsed_table = restore.parse_partition_table_file(
            partition_table_path,
            mutation_offset=int(EXPECTED_OFFSET, 0),
            mutation_bytes=span,
        )
        restore_runtime = restore.pinned_restore_runtime_binding()
    except Exception as error:
        raise StateError(f"live partition/restore runtime validation failed: {error}") from error
    table_binding = private_file_binding(
        partition_table_path, PARTITION_TABLE_BYTES, "live partition table"
    )
    if table_binding["sha256"] != parsed_table.get("sha256"):
        raise StateError("parsed live partition bytes differ from their file binding")
    preimage_binding = private_file_binding(preimage_path, span, "restore preimage")
    secret_binding = private_file_binding(arm_secret_path, 65, "arm secret")
    secret = read_arm_secret(arm_secret_path)
    try:
        if hashlib.sha256(memoryview(secret)[:64]).hexdigest() != state.get(
            "arm_token_sha256"
        ):
            raise StateError("private arm secret differs from compiled public digest")
    finally:
        zeroize(secret)
    state["status"] = "preimage-bound"
    state["recovery_directory"] = str(recovery)
    state["live_partition_table"] = {
        **table_binding,
        **parsed_table,
        "restore_runtime": restore_runtime,
        "restore_tool": {
            "path": str(RESTORE_TOOL_PATH.resolve(strict=True)),
            "sha256": sha256_file(RESTORE_TOOL_PATH),
        },
    }
    state["restore_preimage"] = {
        **preimage_binding,
        "offset": int(EXPECTED_OFFSET, 0),
        "sector_bytes": SECTOR_BYTES,
        "install_write_block_bytes": INSTALL_WRITE_BLOCK_BYTES,
        "mutation_span_bytes": span,
    }
    state["arm_secret"] = {
        **secret_binding,
        "token_sha256": state["arm_token_sha256"],
    }
    write_replacement(state_path, state)


def mark_install_write_attempt(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
) -> None:
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_path, build_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    state = load_object(state_path, "one-shot state")
    restore = load_restore_tool()
    table = state.get("live_partition_table", {})
    try:
        restore.validate_partition_binding(
            table,
            expected_offset=int(EXPECTED_OFFSET, 0),
            expected_span=mutation_span(EXPECTED_BYTES),
        )
    except Exception as error:
        raise StateError(f"live partition binding changed before install: {error}") from error
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("status") == "preimage-bound"
        and state.get("terminal") is False
        and state.get("restore_required") is False
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and state.get("project_root") == str(exact_project_root(project_root))
        and state.get("authorization_sha256") == sha256_file(auth_path)
        and state.get("fixture_evidence_path") == str(fixture_path.resolve())
        and state.get("fixture_evidence_sha256") == fixture_sha256
        and state.get("metadata_path") == str(metadata_path.resolve())
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path.resolve())
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("build_evidence_path") == str(build_path.resolve())
        and state.get("build_evidence_sha256") == build_sha256
        and state.get("arm_token_sha256") == auth["host_arm"]["token_sha256"]
        and state.get("exact_artifact")
        == {
            "offset": EXPECTED_OFFSET,
            "bytes": EXPECTED_BYTES,
            "sha256": EXPECTED_SHA256,
            "sector_bytes": SECTOR_BYTES,
            "install_write_block_bytes": INSTALL_WRITE_BLOCK_BYTES,
            "mutation_span_bytes": mutation_span(EXPECTED_BYTES),
        }
        and binding_matches(
            pathlib.Path(state.get("restore_preimage", {}).get("path", "")),
            {key: value for key, value in state.get("restore_preimage", {}).items()
             if key in {"path", "device", "inode", "mode", "bytes", "sha256"}},
        )
        and state.get("restore_preimage", {}).get("offset")
        == int(EXPECTED_OFFSET, 0)
        and state.get("restore_preimage", {}).get("sector_bytes") == SECTOR_BYTES
        and state.get("restore_preimage", {}).get("install_write_block_bytes")
        == INSTALL_WRITE_BLOCK_BYTES
        and state.get("restore_preimage", {}).get("mutation_span_bytes")
        == mutation_span(EXPECTED_BYTES)
        and binding_matches(
            pathlib.Path(state.get("arm_secret", {}).get("path", "")),
            state.get("arm_secret"),
        )
        and state.get("arm_secret", {}).get("token_sha256")
        == state.get("arm_token_sha256")
        and table.get("restore_tool")
        == {
            "path": str(RESTORE_TOOL_PATH.resolve(strict=True)),
            "sha256": sha256_file(RESTORE_TOOL_PATH),
        }
    ):
        raise StateError("install attempt has no exact owned preimage-bound state")
    state["status"] = "install-write-attempted"
    state["restore_required"] = True
    state["install_write_outcome"] = "unknown"
    state["install_write_attempted_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_installed_verified(
    state_path: pathlib.Path,
    owner_token: str,
    readback_sha256: str,
    install_span_readback_sha256: str,
) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "install-write-attempted"
        and state.get("restore_required") is True
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and readback_sha256 == EXPECTED_SHA256
        and SHA256_RE.fullmatch(str(install_span_readback_sha256)) is not None
    ):
        raise StateError("installed image readback does not match the attempt")
    state["status"] = "installed-verified"
    state["install_write_outcome"] = "verified"
    state["install_readback_sha256"] = readback_sha256
    state["install_span_readback_sha256"] = install_span_readback_sha256
    state["install_span_readback_bytes"] = mutation_span(EXPECTED_BYTES)
    write_replacement(state_path, state)


def mark_restore_entry(state_path: pathlib.Path, owner_token: str) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") in {
            "installed-verified", "pending-capture", "hard-reset-attempted",
            "hard-reset-succeeded", "arm-frame-attempted", "arm-accepted",
            "restore-entry", "restore-write-attempted", "restore-required",
        }
        and state.get("restore_required") is True
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("restore entry has no owned installed diagnostic")
    state["status"] = "restore-entry"
    state["restore_write_outcome"] = "unknown"
    state["restore_download_reset_attempt_count"] = (
        int(state.get("restore_download_reset_attempt_count", 0)) + 1
    )
    state["restore_download_reset_outcome"] = "unknown"
    state["restore_entry_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_restore_write_attempt(state_path: pathlib.Path, owner_token: str) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "restore-entry"
        and state.get("restore_required") is True
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("restore write has no durable restore entry")
    state["status"] = "restore-write-attempted"
    state["restore_download_reset_outcome"] = "succeeded"
    state["restore_write_attempt_count"] = (
        int(state.get("restore_write_attempt_count", 0)) + 1
    )
    state["restore_write_outcome"] = "unknown"
    state["restore_write_attempted_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_restore_verified(
    state_path: pathlib.Path, owner_token: str, readback_sha256: str
) -> None:
    state = load_object(state_path, "one-shot state")
    preimage = state.get("restore_preimage", {})
    if not (
        state.get("status") == "restore-write-attempted"
        and state.get("restore_required") is True
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and readback_sha256 == preimage.get("sha256")
    ):
        raise StateError("restore readback differs from the sealed preimage")
    state["status"] = "restore-verified"
    state["restore_required"] = False
    state["restore_write_outcome"] = "verified"
    state["restore_download_reset_outcome"] = "succeeded"
    state["restore_readback_sha256"] = readback_sha256
    state["restored_at_utc"] = utc_now()
    write_replacement(state_path, state)


def reserve(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    device_sha256: str,
    offset: str,
    byte_count: int,
    sha256: str,
    owner_token: str,
) -> None:
    exact_new_state_path(state_path)
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_evidence_path, build_evidence_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    if (
        device_sha256 != EXPECTED_DEVICE_SHA256
        or offset != EXPECTED_OFFSET
        or byte_count != EXPECTED_BYTES
        or sha256 != EXPECTED_SHA256
    ):
        raise StateError("reservation identity does not match the exact authorization")
    state_path.parent.mkdir(parents=True, exist_ok=True)
    if re.fullmatch(r"[0-9a-f]{64}", owner_token) is None:
        raise StateError("owner token must be 256 bits of lowercase hexadecimal")
    value = {
        "schema": 1,
        "authorization_id": AUTH_ID,
        "state_path": str((pathlib.Path(__file__).resolve().parents[1] / STATE_REL).resolve()),
        "status": "reserved",
        "terminal": False,
        "restore_required": False,
        "launch_hard_reset_count": 0,
        "hard_reset_attempt_count": 0,
        "arm_frame_tx_attempt_count": 0,
        "restore_download_reset_attempt_count": 0,
        "restore_write_attempt_count": 0,
        "owner_token_sha256": hashlib.sha256(
            owner_token.encode("ascii")
        ).hexdigest(),
        "reserved_at_utc": utc_now(),
        "device_identity_sha256": device_sha256,
        "authorization_sha256": sha256_file(auth_path),
        "project_root": str(exact_project_root(project_root)),
        "fixture_evidence_path": str(fixture_path.resolve()),
        "fixture_evidence_sha256": fixture_sha256,
        "fixture_id": fixture["id"],
        "fixture_current_limit_ma": fixture["qualification"]["current_limit_ma"],
        "metadata_path": str(metadata_path.resolve()),
        "metadata_sha256": metadata_sha256,
        "board_profile_path": str(profile_path.resolve()),
        "board_profile_sha256": profile_sha256,
        "build_evidence_path": str(build_evidence_path.resolve()),
        "build_evidence_sha256": build_evidence_sha256,
        "arm_token_sha256": auth["host_arm"]["token_sha256"],
        "exact_artifact": {
            "offset": offset,
            "bytes": byte_count,
            "sha256": sha256,
            "sector_bytes": SECTOR_BYTES,
            "install_write_block_bytes": INSTALL_WRITE_BLOCK_BYTES,
            "mutation_span_bytes": mutation_span(byte_count),
        },
    }
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    try:
        descriptor = os.open(state_path, flags, 0o600)
    except FileExistsError as error:
        raise StateError("gamepad one-shot was already reserved or consumed") from error
    with os.fdopen(descriptor, "w", encoding="utf-8") as output:
        json.dump(value, output, indent=2, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    directory = os.open(state_path.parent, os.O_RDONLY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def check_reservation(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
) -> dict:
    (
        auth,
        fixture,
        _fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_evidence_path, build_evidence_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    state = load_object(state_path, "one-shot state")
    if not reservation_matches(
        state,
        sha256_file(auth_path),
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
        build_evidence_path,
        build_evidence_sha256,
        owner_token,
    ):
        raise StateError("one-shot state is not the exact live reservation")
    return state


def mark_hard_reset_attempt(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
    receipt_path: pathlib.Path,
) -> None:
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_path, build_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("status") == "pending-capture"
        and state.get("terminal") is False
        and state.get("restore_required") is True
        and state.get("launch_hard_reset_count") == 0
        and state.get("hard_reset_attempt_count") == 0
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and state.get("receipt_path") == str(receipt_path.resolve())
        and state.get("receipt_sha256") == sha256_file(receipt_path)
        and state.get("authorization_sha256") == sha256_file(auth_path)
        and state.get("fixture_evidence_path") == str(fixture_path.resolve())
        and state.get("fixture_evidence_sha256") == fixture_sha256
        and state.get("metadata_path") == str(metadata_path.resolve())
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path.resolve())
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("build_evidence_path") == str(build_path.resolve())
        and state.get("build_evidence_sha256") == build_sha256
    ):
        raise StateError("one-shot state has no exact owned pending receipt")
    state["status"] = "hard-reset-attempted"
    state["hard_reset_attempt_count"] = 1
    state["hard_reset_outcome"] = "unknown"
    state["hard_reset_attempted_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_hard_reset_succeeded(state_path: pathlib.Path, owner_token: str) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "hard-reset-attempted"
        and state.get("terminal") is False
        and state.get("restore_required") is True
        and state.get("hard_reset_attempt_count") == 1
        and state.get("launch_hard_reset_count") == 0
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("hard-reset success has no exact durable attempt")
    state["status"] = "hard-reset-succeeded"
    state["launch_hard_reset_count"] = 1
    state["hard_reset_outcome"] = "hard-reset-succeeded"
    state["hard_reset_succeeded_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_arm_frame_attempt(state_path: pathlib.Path, owner_token: str) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "hard-reset-succeeded"
        and state.get("terminal") is False
        and state.get("restore_required") is True
        and state.get("hard_reset_attempt_count") == 1
        and state.get("arm_frame_tx_attempt_count", 0) == 0
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("ARM frame attempt has no owned hard-reset attempt")
    state["status"] = "arm-frame-attempted"
    state["arm_frame_tx_attempt_count"] = 1
    state["arm_frame_tx_outcome"] = "unknown"
    state["arm_frame_tx_attempted_at_utc"] = utc_now()
    write_replacement(state_path, state)


def mark_arm_accepted(
    state_path: pathlib.Path, owner_token: str, frame_bytes: int, frame_sha256: str
) -> None:
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("status") == "arm-frame-attempted"
        and state.get("arm_frame_tx_attempt_count") == 1
        and frame_bytes == 133
        and SHA256_RE.fullmatch(frame_sha256) is not None
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("ARM acceptance does not match the durable attempt")
    state["status"] = "arm-accepted"
    state["arm_frame_tx_outcome"] = "accepted"
    state["arm_accepted_at_utc"] = utc_now()
    state["arm_frame"] = {
        "bytes": frame_bytes,
        "sha256": frame_sha256,
        "count": 1,
        "secret_logged": False,
    }
    write_replacement(state_path, state)


def complete(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    project_root: pathlib.Path,
    owner_token: str,
    detail: str,
    raw_path: pathlib.Path,
    raw_sha256: str,
    summary_path: pathlib.Path,
    summary_sha256: str,
) -> None:
    (
        auth,
        fixture,
        fixture_path,
        fixture_sha256,
        metadata_path,
        metadata_sha256,
        profile_path,
        profile_sha256,
    ) = validate_active_authorization(auth_path, project_root)
    build_evidence_path, build_evidence_sha256 = validate_active_build_evidence(
        project_root,
        auth["host_arm"]["token_sha256"],
        fixture["id"],
        fixture_sha256,
        fixture["qualification"],
    )
    state = load_object(state_path, "one-shot state")
    restore = load_restore_tool()
    try:
        restore.validate_partition_binding(
            state.get("live_partition_table", {}),
            expected_offset=int(EXPECTED_OFFSET, 0),
            expected_span=mutation_span(EXPECTED_BYTES),
        )
    except Exception as error:
        raise StateError(f"restore binding changed before completion: {error}") from error
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("status") == "restore-verified"
        and state.get("terminal") is False
        and state.get("restore_required") is False
        and state.get("launch_hard_reset_count") == 1
        and state.get("hard_reset_attempt_count") == 1
        and state.get("arm_frame_tx_attempt_count") == 1
        and state.get("arm_frame_tx_outcome") == "accepted"
        and state.get("restore_write_outcome") == "verified"
        and state.get("restore_download_reset_attempt_count") == 1
        and state.get("restore_write_attempt_count") == 1
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
        and state.get("authorization_sha256") == sha256_file(auth_path)
        and state.get("fixture_evidence_path") == str(fixture_path.resolve())
        and state.get("fixture_evidence_sha256") == fixture_sha256
        and state.get("metadata_path") == str(metadata_path.resolve())
        and state.get("metadata_sha256") == metadata_sha256
        and state.get("board_profile_path") == str(profile_path.resolve())
        and state.get("board_profile_sha256") == profile_sha256
        and state.get("build_evidence_path") == str(build_evidence_path.resolve())
        and state.get("build_evidence_sha256") == build_evidence_sha256
        and SHA256_RE.fullmatch(raw_sha256) is not None
        and SHA256_RE.fullmatch(summary_sha256) is not None
        and not raw_path.exists()
        and not summary_path.exists()
    ):
        raise StateError("one-shot state has no owned hard-reset attempt")
    state["status"] = "completed"
    state["terminal"] = True
    state["restoration_verified_before_completion"] = True
    state["runtime_evidence"] = {
        "raw_path": str(raw_path.resolve()),
        "raw_sha256": raw_sha256,
        "summary_path": str(summary_path.resolve()),
        "summary_sha256": summary_sha256,
        "publication_status": "bound-before-exclusive-publication",
    }
    state["terminal_at_utc"] = utc_now()
    state["detail"] = detail
    write_replacement(state_path, state)


def fail_if_reserved(
    state_path: pathlib.Path, owner_token: str, detail: str
) -> None:
    if not state_path.exists():
        return
    state = load_object(state_path, "one-shot state")
    if state.get("terminal") is True:
        return
    # Cleanup must remain able to transition a reservation even if the active
    # authorization or fixture file was concurrently changed or removed.
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("terminal") is False
        and state.get("owner_token_sha256")
        == hashlib.sha256(owner_token.encode("ascii")).hexdigest()
    ):
        raise StateError("nonterminal state is not this owner's reservation")
    if state.get("restore_required") is True:
        state["status"] = "restore-required"
        state["terminal"] = False
        state["recovery_only"] = True
        state["failure_at_utc"] = utc_now()
    else:
        state["arm_secret_destroyed_before_terminal"] = destroy_bound_arm_secret(state)
        state["status"] = "failed-safe-no-restore-required"
        state["terminal"] = True
        state["terminal_at_utc"] = utc_now()
    state["detail"] = detail
    write_replacement(state_path, state)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    token_parser = subparsers.add_parser("new-owner-token")
    token_parser.add_argument("--path", type=pathlib.Path, required=True)

    secret_check = subparsers.add_parser("check-arm-secret")
    secret_check.add_argument("--path", type=pathlib.Path, required=True)
    secret_check.add_argument("--authorization", type=pathlib.Path, required=True)
    secret_check.add_argument("--project-root", type=pathlib.Path, required=True)

    secret_stage = subparsers.add_parser("stage-arm-secret")
    secret_stage.add_argument("--source", type=pathlib.Path, required=True)
    secret_stage.add_argument("--destination", type=pathlib.Path, required=True)
    secret_stage.add_argument("--authorization", type=pathlib.Path, required=True)
    secret_stage.add_argument("--project-root", type=pathlib.Path, required=True)

    for command in ("check-available", "check-reservation"):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--state", type=pathlib.Path, required=True)
        subparser.add_argument("--authorization", type=pathlib.Path, required=True)
        subparser.add_argument("--project-root", type=pathlib.Path, required=True)
        if command == "check-reservation":
            subparser.add_argument("--owner-token-file", type=pathlib.Path, required=True)

    reserve_parser = subparsers.add_parser("reserve")
    reserve_parser.add_argument("--state", type=pathlib.Path, required=True)
    reserve_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    reserve_parser.add_argument("--project-root", type=pathlib.Path, required=True)
    reserve_parser.add_argument("--device-identity-sha256", required=True)
    reserve_parser.add_argument("--offset", required=True)
    reserve_parser.add_argument("--bytes", type=int, required=True)
    reserve_parser.add_argument("--sha256", required=True)
    reserve_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)

    bind_parser = subparsers.add_parser("bind-receipt")
    bind_parser.add_argument("--state", type=pathlib.Path, required=True)
    bind_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    bind_parser.add_argument("--project-root", type=pathlib.Path, required=True)
    bind_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    bind_parser.add_argument("--receipt", type=pathlib.Path, required=True)
    bind_parser.add_argument("--receipt-sha256", required=True)

    preimage_parser = subparsers.add_parser("bind-preimage")
    preimage_parser.add_argument("--state", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--project-root", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--recovery-dir", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--partition-table", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--preimage", type=pathlib.Path, required=True)
    preimage_parser.add_argument("--arm-secret", type=pathlib.Path, required=True)

    for command in (
        "mark-install-write-attempt", "mark-installed-verified",
        "mark-hard-reset-succeeded", "mark-arm-frame-attempt", "mark-arm-accepted",
        "mark-restore-entry", "mark-restore-write-attempt",
        "mark-restore-verified",
    ):
        subparser = subparsers.add_parser(command)
        subparser.add_argument("--state", type=pathlib.Path, required=True)
        subparser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
        if command == "mark-install-write-attempt":
            subparser.add_argument("--authorization", type=pathlib.Path, required=True)
            subparser.add_argument("--project-root", type=pathlib.Path, required=True)
        if command in {"mark-installed-verified", "mark-restore-verified"}:
            subparser.add_argument("--readback-sha256", required=True)
        if command == "mark-installed-verified":
            subparser.add_argument("--install-span-readback-sha256", required=True)
        if command == "mark-arm-accepted":
            subparser.add_argument("--frame-bytes", type=int, required=True)
            subparser.add_argument("--frame-sha256", required=True)

    complete_parser = subparsers.add_parser("complete")
    complete_parser.add_argument("--state", type=pathlib.Path, required=True)
    complete_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    complete_parser.add_argument("--project-root", type=pathlib.Path, required=True)
    complete_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    complete_parser.add_argument("--detail", required=True)
    complete_parser.add_argument("--raw", type=pathlib.Path, required=True)
    complete_parser.add_argument("--raw-sha256", required=True)
    complete_parser.add_argument("--summary", type=pathlib.Path, required=True)
    complete_parser.add_argument("--summary-sha256", required=True)

    attempt_parser = subparsers.add_parser("mark-hard-reset-attempt")
    attempt_parser.add_argument("--state", type=pathlib.Path, required=True)
    attempt_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    attempt_parser.add_argument("--project-root", type=pathlib.Path, required=True)
    attempt_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    attempt_parser.add_argument("--receipt", type=pathlib.Path, required=True)

    fail_parser = subparsers.add_parser("fail")
    fail_parser.add_argument("--state", type=pathlib.Path, required=True)
    fail_parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    fail_parser.add_argument("--detail", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        if args.command == "new-owner-token":
            new_owner_token(args.path)
        elif args.command == "check-arm-secret":
            print(check_arm_secret(args.path, args.authorization, args.project_root))
        elif args.command == "stage-arm-secret":
            stage_arm_secret(
                args.source, args.destination, args.authorization, args.project_root
            )
        elif args.command == "check-available":
            check_available(args.state, args.authorization, args.project_root)
        elif args.command == "check-reservation":
            check_reservation(
                args.state,
                args.authorization,
                args.project_root,
                read_owner_token(args.owner_token_file),
            )
        elif args.command == "reserve":
            reserve(
                args.state,
                args.authorization,
                args.project_root,
                args.device_identity_sha256,
                args.offset,
                args.bytes,
                args.sha256,
                read_owner_token(args.owner_token_file),
            )
        elif args.command == "bind-receipt":
            bind_receipt(
                args.state,
                args.authorization,
                args.project_root,
                read_owner_token(args.owner_token_file),
                args.receipt,
                args.receipt_sha256,
            )
        elif args.command == "bind-preimage":
            bind_preimage(
                args.state, args.authorization, args.project_root,
                read_owner_token(args.owner_token_file), args.recovery_dir,
                args.partition_table, args.preimage, args.arm_secret,
            )
        elif args.command == "mark-install-write-attempt":
            mark_install_write_attempt(
                args.state,
                args.authorization,
                args.project_root,
                read_owner_token(args.owner_token_file),
            )
        elif args.command == "mark-installed-verified":
            mark_installed_verified(
                args.state, read_owner_token(args.owner_token_file),
                args.readback_sha256, args.install_span_readback_sha256,
            )
        elif args.command == "mark-arm-frame-attempt":
            mark_arm_frame_attempt(
                args.state, read_owner_token(args.owner_token_file)
            )
        elif args.command == "mark-hard-reset-succeeded":
            mark_hard_reset_succeeded(
                args.state, read_owner_token(args.owner_token_file)
            )
        elif args.command == "mark-arm-accepted":
            mark_arm_accepted(
                args.state, read_owner_token(args.owner_token_file),
                args.frame_bytes, args.frame_sha256,
            )
        elif args.command == "mark-restore-entry":
            mark_restore_entry(
                args.state, read_owner_token(args.owner_token_file)
            )
        elif args.command == "mark-restore-write-attempt":
            mark_restore_write_attempt(
                args.state, read_owner_token(args.owner_token_file)
            )
        elif args.command == "mark-restore-verified":
            mark_restore_verified(
                args.state, read_owner_token(args.owner_token_file),
                args.readback_sha256,
            )
        elif args.command == "mark-hard-reset-attempt":
            mark_hard_reset_attempt(
                args.state,
                args.authorization,
                args.project_root,
                read_owner_token(args.owner_token_file),
                args.receipt,
            )
        elif args.command == "complete":
            complete(
                args.state,
                args.authorization,
                args.project_root,
                read_owner_token(args.owner_token_file),
                args.detail,
                args.raw,
                args.raw_sha256,
                args.summary,
                args.summary_sha256,
            )
        else:
            fail_if_reserved(
                args.state, read_owner_token(args.owner_token_file), args.detail
            )
    except StateError as error:
        raise SystemExit(f"gamepad one-shot state error: {error}") from error


if __name__ == "__main__":
    main()
