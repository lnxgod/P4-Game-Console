#!/usr/bin/env python3

"""Durable fail-closed reservation for the E3 app-only one-shot install."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import tempfile


AUTH_ID = "doom-embedded-gamepad-e3-one-shot-authorization-2026-08-13"
ACTIVE_CLASSIFICATION = "user-requested-doom-e3-app-only-one-shot-active"
EXPECTED_OFFSET = "0x10000"
EXPECTED_BYTES = 4927824
EXPECTED_SHA256 = "32814a97b72b3b227e2ace02393222178ceebf02d18a4f0b35bead1f4584ea24"
EXPECTED_DEVICE_SHA256 = (
    "4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0"
)


class StateError(RuntimeError):
    pass


def load_object(path: pathlib.Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text())
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise StateError(f"cannot read {label}: {error}") from error
    if not isinstance(value, dict):
        raise StateError(f"{label} must contain a JSON object")
    return value


def validate_active_authorization(path: pathlib.Path) -> dict:
    auth = load_object(path, "authorization")
    exact = auth.get("exact_artifact")
    binding = auth.get("device_binding")
    if not (
        auth.get("schema") == 1
        and auth.get("id") == AUTH_ID
        and auth.get("classification") == ACTIVE_CLASSIFICATION
        and auth.get("active") is True
        and auth.get("consumed") is False
        and isinstance(exact, dict)
        and exact.get("offset") == EXPECTED_OFFSET
        and exact.get("bytes") == EXPECTED_BYTES
        and exact.get("sha256") == EXPECTED_SHA256
        and isinstance(binding, dict)
        and binding.get("identity_sha256") == EXPECTED_DEVICE_SHA256
    ):
        raise StateError("authorization is not the exact active E3 one-shot")
    return auth


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


def check_available(state_path: pathlib.Path, auth_path: pathlib.Path) -> None:
    validate_active_authorization(auth_path)
    if state_path.exists():
        state = load_object(state_path, "one-shot state")
        raise StateError(
            "E3 one-shot is unavailable: durable state already exists "
            f"with status={state.get('status', 'invalid')}"
        )


def reserve(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    device_sha256: str,
    offset: str,
    byte_count: int,
    sha256: str,
) -> None:
    validate_active_authorization(auth_path)
    if (
        device_sha256 != EXPECTED_DEVICE_SHA256
        or offset != EXPECTED_OFFSET
        or byte_count != EXPECTED_BYTES
        or sha256 != EXPECTED_SHA256
    ):
        raise StateError("reservation identity does not match the exact authorization")
    state_path.parent.mkdir(parents=True, exist_ok=True)
    value = {
        "schema": 1,
        "authorization_id": AUTH_ID,
        "status": "reserved",
        "terminal": False,
        "reserved_at_utc": utc_now(),
        "device_identity_sha256": device_sha256,
        "exact_artifact": {
            "offset": offset,
            "bytes": byte_count,
            "sha256": sha256,
        },
    }
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    try:
        descriptor = os.open(state_path, flags, 0o600)
    except FileExistsError as error:
        raise StateError("E3 one-shot was already reserved or consumed") from error
    try:
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
    except BaseException:
        # Keep any partially created state fail-closed. A new explicit
        # authorization is required rather than guessing whether it ran.
        raise


def terminal_update(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    status: str,
    detail: str,
) -> None:
    validate_active_authorization(auth_path)
    state = load_object(state_path, "one-shot state")
    if not (
        state.get("schema") == 1
        and state.get("authorization_id") == AUTH_ID
        and state.get("status") == "reserved"
        and state.get("terminal") is False
        and state.get("device_identity_sha256") == EXPECTED_DEVICE_SHA256
        and state.get("exact_artifact")
        == {
            "offset": EXPECTED_OFFSET,
            "bytes": EXPECTED_BYTES,
            "sha256": EXPECTED_SHA256,
        }
    ):
        raise StateError("one-shot state is not the exact live reservation")
    state["status"] = status
    state["terminal"] = True
    state["terminal_at_utc"] = utc_now()
    state["detail"] = detail
    write_replacement(state_path, state)


def fail_if_reserved(
    state_path: pathlib.Path,
    auth_path: pathlib.Path,
    detail: str,
) -> None:
    validate_active_authorization(auth_path)
    if not state_path.exists():
        return
    state = load_object(state_path, "one-shot state")
    if state.get("terminal") is True:
        return
    terminal_update(state_path, auth_path, "failed", detail)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    check = subparsers.add_parser("check-available")
    check.add_argument("--state", type=pathlib.Path, required=True)
    check.add_argument("--authorization", type=pathlib.Path, required=True)

    reserve_parser = subparsers.add_parser("reserve")
    reserve_parser.add_argument("--state", type=pathlib.Path, required=True)
    reserve_parser.add_argument("--authorization", type=pathlib.Path, required=True)
    reserve_parser.add_argument("--device-identity-sha256", required=True)
    reserve_parser.add_argument("--offset", required=True)
    reserve_parser.add_argument("--bytes", type=int, required=True)
    reserve_parser.add_argument("--sha256", required=True)

    for command in ("complete", "fail"):
        terminal = subparsers.add_parser(command)
        terminal.add_argument("--state", type=pathlib.Path, required=True)
        terminal.add_argument("--authorization", type=pathlib.Path, required=True)
        terminal.add_argument("--detail", required=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    try:
        if args.command == "check-available":
            check_available(args.state, args.authorization)
        elif args.command == "reserve":
            reserve(
                args.state,
                args.authorization,
                args.device_identity_sha256,
                args.offset,
                args.bytes,
                args.sha256,
            )
        elif args.command == "complete":
            terminal_update(args.state, args.authorization, "completed", args.detail)
        else:
            fail_if_reserved(args.state, args.authorization, args.detail)
    except StateError as error:
        raise SystemExit(f"E3 one-shot state error: {error}") from error


if __name__ == "__main__":
    main()
