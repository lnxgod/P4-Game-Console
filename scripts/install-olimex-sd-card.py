#!/usr/bin/env python3

"""Atomically copy the reviewed Console OS bundle to a mounted microSD card."""

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

from p4_multiplayer_manifest import expected_multiplayer_extension
from p4cart_seed_registry import load_seed_carts


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_BUNDLE = ROOT / "apps/console_os/build-olimex-esp32-p4-pc/sd-card"
DOOM_BYTES = 4_196_020
DOOM_SHA256 = "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def mounted_card(path: pathlib.Path) -> pathlib.Path:
    resolved = path.expanduser().resolve(strict=True)
    forbidden = {pathlib.Path("/"), pathlib.Path.home().resolve(), ROOT.resolve()}
    if resolved in forbidden or ROOT.resolve() in resolved.parents:
        raise SystemExit(f"refusing unsafe target: {resolved}")
    if not resolved.is_dir() or not os.path.ismount(resolved):
        raise SystemExit(f"target must be an existing mounted filesystem: {resolved}")
    return resolved


def require_waveshare_h2_fat32(target: pathlib.Path) -> None:
    if sys.platform != "darwin":
        raise SystemExit(
            "Waveshare H2 target verification currently requires macOS diskutil"
        )
    result = subprocess.run(
        ["diskutil", "info", "-plist", str(target)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"cannot inspect Waveshare H2 target: {detail}")
    try:
        info = plistlib.loads(result.stdout)
    except plistlib.InvalidFileException as error:
        raise SystemExit("diskutil returned invalid target metadata") from error
    required = {
        "FilesystemType": "msdos",
        "Content": "DOS_FAT_32",
        "VolumeName": "P4GAMES",
        "BusProtocol": "USB",
        "Internal": False,
        "RemovableMediaOrExternalDevice": True,
        "WritableVolume": True,
    }
    mismatches = [
        f"{key}={info.get(key)!r} (expected {expected!r})"
        for key, expected in required.items()
        if info.get(key) != expected
    ]
    if info.get("MountPoint") != str(target):
        mismatches.append(
            f"MountPoint={info.get('MountPoint')!r} (expected {str(target)!r})"
        )
    if mismatches:
        raise SystemExit(
            "refusing incompatible Waveshare H2 card; format it as MBR/FAT32 "
            "named P4GAMES: " + "; ".join(mismatches)
        )


def c_string(field: bytes, label: str) -> str:
    if b"\0" not in field:
        raise SystemExit(f"{label} is not terminated")
    value, padding = field.split(b"\0", 1)
    if not value or any(padding):
        raise SystemExit(f"{label} padding is invalid")
    try:
        return value.decode("ascii")
    except UnicodeDecodeError as error:
        raise SystemExit(f"{label} is not ASCII") from error


def validate_game(path: pathlib.Path, manifest: dict[str, object]) -> None:
    package = path.read_bytes()
    if not 256 < len(package) <= 512 * 1024 or package[:8] != b"P4GAME1\0":
        raise SystemExit(f"invalid game package: {path.name}")
    fields = struct.unpack_from("<9IHH", package, 8)
    header, total, offset, payload, fmt, api, launcher, required, optional, _, flags = fields
    profile_flag, profile_bytes = expected_multiplayer_extension(
        manifest,
        set(manifest["required_capabilities"]) |
        set(manifest["optional_capabilities"]),
    )
    if not (
        header == offset == 256
        and total == len(package)
        and payload == len(package) - offset
        and fmt == api == 1
        and launcher == manifest["launcher_id"]
        and required & 1
        and not required & optional
        and flags == (1 | profile_flag)
        and package[240:256] == profile_bytes
        and package[48:80] == hashlib.sha256(package[offset:]).digest()
        and c_string(package[80:128], f"{path.name} game id") == manifest["id"]
    ):
        raise SystemExit(f"game package validation failed: {path.name}")


def validate_game_resource(path: pathlib.Path, manifest: dict[str, object]) -> None:
    package = path.read_bytes()
    if not 128 < len(package) <= 8 * 1024 * 1024 or package[:8] != b"P4RES01\0":
        raise SystemExit(f"invalid game resource: {path.name}")
    header, total, offset, payload, version, flags = struct.unpack_from(
        "<6I", package, 8
    )
    if not (
        header == offset == 128
        and total == len(package)
        and payload == len(package) - offset
        and version == 1
        and flags == 0
        and package[32:64] == hashlib.sha256(package[offset:]).digest()
        and c_string(package[64:112], f"{path.name} game id") == manifest["id"]
        and not any(package[112:128])
    ):
        raise SystemExit(f"game resource validation failed: {path.name}")


def validate_update(path: pathlib.Path) -> None:
    package = path.read_bytes()
    if len(package) <= 256 or package[:8] != b"P4OSUP1\0":
        raise SystemExit("invalid P4UPDATE.P4U")
    header, total, offset, payload, version, flags = struct.unpack_from(
        "<6I", package, 8
    )
    if not (
        header == offset == 256
        and total == len(package)
        and payload == len(package) - offset
        and version == 1
        and flags == 0
        and package[32:64] == hashlib.sha256(package[offset:]).digest()
        and c_string(package[160:176], "update target") == "esp32p4"
        and not any(package[176:256])
    ):
        raise SystemExit("P4UPDATE.P4U validation failed")


def validate_p4cart(path: pathlib.Path) -> None:
    result = subprocess.run(
        [sys.executable,
         str(ROOT / "game-platform/scripts/p4cart.py"),
         "inspect", str(path)],
        cwd=ROOT, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip()
        raise SystemExit(f"P4 Cart validation failed: {detail}")


def enabled_manifests() -> dict[str, dict[str, object]]:
    return {
        manifest["package_file"]: manifest
        for manifest in (
            json.loads(path.read_text(encoding="utf-8"))
            for path in sorted((ROOT / "games").glob("*/game.json"))
        )
        if manifest.get("enabled") is True
    }


def bundle_files(manifests: dict[str, dict[str, object]]) -> tuple[pathlib.Path, ...]:
    resources = tuple(
        pathlib.Path("GAMES") / str(manifest["resource_file"])
        for manifest in manifests.values()
        if isinstance(manifest.get("resource_file"), str)
    )
    script_carts = tuple(
        pathlib.Path(*seed.relative_path.parts)
        for seed in load_seed_carts()
    )
    return tuple(pathlib.Path("GAMES") / name for name in manifests) + resources + (
        pathlib.Path("DOOM1.WAD"),
        pathlib.Path("README.TXT"),
        *script_carts,
        pathlib.Path("UPDATE/P4UPDATE.P4U"),
    )


def validate_bundle(bundle: pathlib.Path) -> tuple[pathlib.Path, ...]:
    manifests = enabled_manifests()
    files = bundle_files(manifests)
    missing = [str(relative) for relative in files if not (bundle / relative).is_file()]
    if missing:
        raise SystemExit("bundle is incomplete: " + ", ".join(missing))
    doom = bundle / "DOOM1.WAD"
    if doom.stat().st_size != DOOM_BYTES or sha256(doom) != DOOM_SHA256:
        raise SystemExit("DOOM1.WAD identity does not match the pinned shareware input")
    expected_readme = ROOT / "apps/console_os/game-storage" / (
        "README-OLIMEX.TXT"
        if "olimex" in bundle.parent.name else "README.TXT"
    )
    if (bundle / "README.TXT").read_bytes() != expected_readme.read_bytes():
        raise SystemExit("README.TXT does not match the Olimex storage contract")
    for name in manifests:
        validate_game(bundle / "GAMES" / name, manifests[name])
        resource_name = manifests[name].get("resource_file")
        if isinstance(resource_name, str):
            validate_game_resource(
                bundle / "GAMES" / resource_name, manifests[name]
            )
    for seed in load_seed_carts():
        validate_p4cart(bundle.joinpath(*seed.relative_path.parts))
    validate_update(bundle / "UPDATE/P4UPDATE.P4U")
    return files


def copy_atomic(source: pathlib.Path, destination: pathlib.Path) -> dict[str, object]:
    destination.parent.mkdir(parents=True, exist_ok=True)
    source_hash = sha256(source)
    if destination.is_file() and destination.stat().st_size == source.stat().st_size:
        if sha256(destination) == source_hash:
            return {"path": str(destination), "bytes": source.stat().st_size,
                    "sha256": source_hash, "result": "unchanged"}
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".p4-copy-", dir=destination.parent
    )
    temporary = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output, length=1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        if temporary.stat().st_size != source.stat().st_size or sha256(temporary) != source_hash:
            raise SystemExit(f"copy verification failed before commit: {destination.name}")
        os.replace(temporary, destination)
        directory_fd = os.open(destination.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary.exists():
            temporary.unlink()
    return {"path": str(destination), "bytes": source.stat().st_size,
            "sha256": source_hash, "result": "installed"}


def main() -> None:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--target", type=pathlib.Path)
    action.add_argument("--check-bundle-only", action="store_true")
    parser.add_argument("--bundle", type=pathlib.Path, default=DEFAULT_BUNDLE)
    parser.add_argument("--require-waveshare-h2-fat32", action="store_true")
    arguments = parser.parse_args()
    if arguments.check_bundle_only and arguments.require_waveshare_h2_fat32:
        parser.error(
            "--require-waveshare-h2-fat32 requires a mounted --target"
        )
    bundle = arguments.bundle.expanduser().resolve(strict=True)
    if not bundle.is_dir():
        raise SystemExit(f"bundle is not a directory: {bundle}")
    files = validate_bundle(bundle)
    native_games = [str(path) for path in files if path.suffix == ".P4G"]
    script_games = [str(path) for path in files if path.suffix == ".P4CART"]
    if arguments.check_bundle_only:
        print(json.dumps({
            "result": "p4-sd-card-bundle-verified",
            "bundle": str(bundle),
            "native_game_count": len(native_games),
            "native_games": native_games,
            "script_game_count": len(script_games),
            "script_games": script_games,
        }, sort_keys=True))
        return

    assert arguments.target is not None
    target = mounted_card(arguments.target)
    if arguments.require_waveshare_h2_fat32:
        require_waveshare_h2_fat32(target)
    required = sum((bundle / relative).stat().st_size for relative in files)
    if shutil.disk_usage(target).free < required + 1024 * 1024:
        raise SystemExit("microSD card does not have enough free space")
    installed = [copy_atomic(bundle / relative, target / relative) for relative in files]
    print(json.dumps({"result": "p4-sd-card-ready", "target": str(target),
                      "native_game_count": len(native_games),
                      "native_games": native_games,
                      "script_game_count": len(script_games),
                      "script_games": script_games,
                      "files": installed}, sort_keys=True))


if __name__ == "__main__":
    main()
