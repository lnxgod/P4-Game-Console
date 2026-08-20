#!/usr/bin/env python3

"""Guardedly replace only the authorized native cartridge suite on Waveshare SD."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
AUTH = ROOT / (
    "hardware/evidence/"
    "waveshare-console-os-0.4.34-usb-role-fix-20260819-"
    "exact-unit-authorization.json"
)
AUTH_SHA256 = "7014c177ae9767f3cd99b5ef33cbf5cd95f1af87b95f68847996f75a1a56f0c9"
RECOVERY = ROOT / (
    "hardware/local-state/"
    "waveshare-console-os-0.4.34-usb-role-fix-20260819"
)
SOURCE_GAMES = ROOT / (
    "apps/console_os/build-waveshare-usb-role-fix/sd-card/GAMES"
)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"refusing game-suite install: {message}")


def c_string(field: bytes, label: str) -> str:
    require(b"\0" in field, f"{label} is not terminated")
    value, padding = field.split(b"\0", 1)
    require(bool(value) and not any(padding), f"{label} padding is invalid")
    try:
        return value.decode("ascii")
    except UnicodeDecodeError as error:
        raise SystemExit(f"{label} is not ASCII") from error


def load_authorization() -> dict[str, object]:
    require(AUTH.is_file(), "authorization is missing")
    require(sha256(AUTH) == AUTH_SHA256, "authorization changed")
    authorization = json.loads(AUTH.read_text(encoding="utf-8"))
    require(authorization.get("active") is True, "authorization is inactive")
    return authorization


def resolve_authorized_file(entry: dict[str, object], label: str) -> pathlib.Path:
    relative = entry.get("path")
    expected_bytes = entry.get("bytes")
    expected_hash = entry.get("sha256")
    require(isinstance(relative, str), f"{label} path is invalid")
    path = (ROOT / relative).resolve()
    try:
        path.relative_to(ROOT.resolve())
    except ValueError as error:
        raise SystemExit(f"{label} path escapes the repository") from error
    require(path.is_file(), f"{label} is missing")
    require(isinstance(expected_bytes, int) and path.stat().st_size == expected_bytes,
            f"{label} byte count changed")
    require(isinstance(expected_hash, str) and sha256(path) == expected_hash,
            f"{label} digest changed")
    return path


def validate_game(path: pathlib.Path) -> None:
    package = path.read_bytes()
    require(256 < len(package) <= 512 * 1024, "P4G size is invalid")
    require(package[:8] == b"P4GAME1\0", "P4G magic differs")
    fields = struct.unpack_from("<9IHH", package, 8)
    header, total, offset, payload, fmt, api, launcher, required, optional, _, flags = fields
    require(
        header == offset == 256
        and total == len(package)
        and payload == len(package) - offset
        and fmt == api == 1
        and launcher >= 100
        and required & 1
        and not required & optional
        and flags == 1,
        "P4G layout differs",
    )
    require(package[48:80] == hashlib.sha256(package[offset:]).digest(),
            "P4G payload digest differs")
    require("." in c_string(package[80:128], "P4G game id"),
            "P4G game id is invalid")
    require(bool(c_string(package[208:224], "P4G version")),
            "P4G version is invalid")


def validate_resource(path: pathlib.Path) -> None:
    package = path.read_bytes()
    require(128 < len(package) <= 8 * 1024 * 1024, "P4R size is invalid")
    require(package[:8] == b"P4RES01\0", "P4R magic differs")
    header, total, offset, payload, version, flags = struct.unpack_from(
        "<6I", package, 8
    )
    require(
        header == offset == 128
        and total == len(package)
        and payload == len(package) - offset
        and version == 1
        and flags == 0,
        "P4R layout differs",
    )
    require(package[32:64] == hashlib.sha256(package[offset:]).digest(),
            "P4R payload digest differs")
    require(c_string(package[64:112], "P4R game id") ==
            "org.p4console.byte-buddy",
            "P4R game id differs")
    require(not any(package[112:128]), "P4R reserved bytes differ")


def mounted_waveshare_card(path: pathlib.Path) -> pathlib.Path:
    target = path.expanduser().resolve(strict=True)
    forbidden = {pathlib.Path("/"), pathlib.Path.home().resolve(), ROOT.resolve()}
    require(target not in forbidden and ROOT.resolve() not in target.parents,
            f"unsafe target: {target}")
    require(target.is_dir() and os.path.ismount(target),
            "target must be an existing mounted filesystem")
    require(sys.platform == "darwin", "target verification requires macOS")
    result = subprocess.run(
        ["diskutil", "info", "-plist", str(target)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    require(result.returncode == 0, "diskutil could not inspect the target")
    try:
        info = plistlib.loads(result.stdout)
    except plistlib.InvalidFileException as error:
        raise SystemExit("diskutil returned invalid target metadata") from error
    expected = {
        "FilesystemType": "msdos",
        "Content": "DOS_FAT_32",
        "VolumeName": "P4GAMES",
        "BusProtocol": "USB",
        "Internal": False,
        "RemovableMediaOrExternalDevice": True,
        "WritableVolume": True,
        "MountPoint": str(target),
    }
    mismatches = [
        f"{key}={info.get(key)!r} expected={value!r}"
        for key, value in expected.items()
        if info.get(key) != value
    ]
    require(not mismatches, "; ".join(mismatches))
    return target


def preserve_existing(source: pathlib.Path, destination: pathlib.Path) -> dict[str, object]:
    if not source.is_file():
        return {"path": str(source), "result": "absent"}
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(not destination.exists(), f"backup already exists: {destination}")
    with source.open("rb") as input_file, destination.open("xb") as output:
        shutil.copyfileobj(input_file, output, length=1024 * 1024)
        output.flush()
        os.fsync(output.fileno())
    os.chmod(destination, 0o400)
    return {
        "path": str(source),
        "backup": str(destination),
        "bytes": destination.stat().st_size,
        "sha256": sha256(destination),
        "result": "preserved",
    }


def copy_atomic(source: pathlib.Path, destination: pathlib.Path) -> dict[str, object]:
    destination.parent.mkdir(parents=True, exist_ok=True)
    expected_hash = sha256(source)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".p4-suite-", dir=destination.parent
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output, length=1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        require(temporary.stat().st_size == source.stat().st_size,
                f"temporary {destination.name} byte count differs")
        require(sha256(temporary) == expected_hash,
                f"temporary {destination.name} digest differs")
        os.replace(temporary, destination)
        directory_fd = os.open(destination.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary.exists():
            temporary.unlink()
    require(destination.stat().st_size == source.stat().st_size,
            f"installed {destination.name} byte count differs")
    require(sha256(destination) == expected_hash,
            f"installed {destination.name} digest differs")
    return {
        "path": str(destination),
        "bytes": destination.stat().st_size,
        "sha256": expected_hash,
        "result": "installed",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", type=pathlib.Path)
    parser.add_argument("--check-source-only", action="store_true")
    arguments = parser.parse_args()
    require(arguments.check_source_only != (arguments.target is not None),
            "choose exactly one of --check-source-only or --target")

    authorization = load_authorization()
    candidate = authorization.get("candidate")
    require(isinstance(candidate, dict), "candidate record is invalid")
    game_entries = candidate.get("game_packages")
    resource_entry = candidate.get("byte_buddy_resource")
    require(isinstance(game_entries, list) and len(game_entries) == 13 and
            all(isinstance(entry, dict) for entry in game_entries),
            "authorized game package set is invalid")
    require(isinstance(resource_entry, dict),
            "authorized Byte Buddy resource is missing")
    games_to_install: list[pathlib.Path] = []
    for entry in game_entries:
        filename = entry.get("file")
        expected_bytes = entry.get("bytes")
        expected_hash = entry.get("sha256")
        require(isinstance(filename, str) and
                pathlib.Path(filename).name == filename and
                filename.endswith(".P4G"),
                "authorized game filename is invalid")
        source = SOURCE_GAMES / filename
        require(source.is_file(), f"source game is missing: {filename}")
        require(isinstance(expected_bytes, int) and
                source.stat().st_size == expected_bytes,
                f"source game byte count changed: {filename}")
        require(isinstance(expected_hash, str) and
                sha256(source) == expected_hash,
                f"source game digest changed: {filename}")
        validate_game(source)
        games_to_install.append(source)
    resource = resolve_authorized_file(resource_entry, "P4R")
    validate_resource(resource)
    sources = games_to_install + [resource]
    if arguments.check_source_only:
        print(json.dumps({
            "result": "game-suite-source-verified",
            "game_count": len(games_to_install),
            "game_sha256": {game.name: sha256(game)
                            for game in games_to_install},
            "resource_sha256": sha256(resource),
        }, sort_keys=True))
        return

    require(RECOVERY.is_dir(),
            "0.4.34 application install recovery record is missing")
    target = mounted_waveshare_card(arguments.target)
    games = target / "GAMES"
    require(games.is_dir(), "mounted card has no GAMES directory")
    backup = RECOVERY / "sd-preimage"
    require(not backup.exists(), "SD preimage directory already exists")
    backup.mkdir(mode=0o700)
    preserved = [
        preserve_existing(games / source.name, backup / source.name)
        for source in sources
    ]
    installed = [
        copy_atomic(source, games / source.name) for source in sources
    ]
    receipt = {
        "schema": 1,
        "authorization_sha256": AUTH_SHA256,
        "result": "game-suite-installed",
        "game_count": len(games_to_install),
        "preserved": preserved,
        "installed": installed,
    }
    receipt_path = RECOVERY / "sd-install.json"
    with receipt_path.open("x", encoding="utf-8") as output:
        json.dump(receipt, output, indent=2, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.chmod(receipt_path, 0o400)
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
