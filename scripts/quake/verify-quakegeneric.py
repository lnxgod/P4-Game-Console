#!/usr/bin/env python3
"""Verify pinned quakegeneric source and keep Quake game data local."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
MANIFEST_LINE = re.compile(
    r"^(?P<mode>[0-7]{6}) (?P<kind>blob) (?P<sha>[0-9a-f]{40})\t(?P<path>[^\r\n]+)$"
)


class VerificationError(RuntimeError):
    pass


def fail(message: str) -> None:
    raise VerificationError(message)


def load_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        fail(f"cannot load {path}: {error}")
    if type(value) is not dict:
        fail(f"{path} must contain a JSON object")
    return value


def relative_path(root: Path, value: object, field: str) -> Path:
    if type(value) is not str or not value.startswith("third_party/"):
        fail(f"{field} must remain under third_party")
    candidate = Path(value)
    if candidate.is_absolute() or ".." in candidate.parts:
        fail(f"{field} contains an unsafe path")
    return root / candidate


def git_blob_id(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def git_lines(root: Path, *arguments: str) -> list[str]:
    process = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=False, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if process.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {process.stderr.strip()}")
    return process.stdout.splitlines()


def ignored(root: Path, path: Path) -> bool:
    process = subprocess.run(
        ["git", "-C", str(root), "check-ignore", "-q", "--", str(path)],
        check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    return process.returncode == 0


def verify(root: Path, require_data: bool) -> tuple[str, str, int, bool]:
    lock = load_json(root / "third_party" / "source-lock.json")
    try:
        source = lock["sources"]["quakegeneric_esp32p4"]
    except (KeyError, TypeError):
        fail("source-lock.json has no quakegeneric_esp32p4 object")
    if type(source) is not dict:
        fail("quakegeneric source lock must be an object")
    for field in ("commit", "tree"):
        if type(source.get(field)) is not str or not HEX40.fullmatch(source[field]):
            fail(f"invalid quakegeneric {field}")
    expected_manifest_hash = source.get("vendor_manifest_sha256")
    if type(expected_manifest_hash) is not str or not HEX64.fullmatch(expected_manifest_hash):
        fail("invalid quakegeneric vendor_manifest_sha256")

    vendor = relative_path(root, source.get("vendor_path"), "vendor_path")
    manifest_path = relative_path(root, source.get("vendor_manifest"), "vendor_manifest")
    if not vendor.is_dir() or not manifest_path.is_file():
        fail("vendored quakegeneric source or manifest is missing")
    manifest_bytes = manifest_path.read_bytes()
    if hashlib.sha256(manifest_bytes).hexdigest() != expected_manifest_hash:
        fail("quakegeneric vendor manifest hash does not match source-lock.json")
    if not (vendor / "LICENSE").is_file() or not (vendor / "README.md").is_file():
        fail("upstream Quake license or README is missing")

    expected_paths: list[str] = []
    seen_casefolded: set[str] = set()
    for number, line in enumerate(manifest_bytes.decode("utf-8").splitlines(), 1):
        match = MANIFEST_LINE.fullmatch(line)
        if match is None or match["mode"] != "100644":
            fail(f"invalid vendor manifest line {number}")
        relative = match["path"]
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or not relative or "\\" in relative:
            fail(f"unsafe vendor path on line {number}: {relative}")
        folded = relative.casefold()
        if folded in seen_casefolded:
            fail(f"duplicate/case-colliding vendor path: {relative}")
        seen_casefolded.add(folded)
        file_path = vendor / path
        if not file_path.is_file() or file_path.is_symlink():
            fail(f"missing or symlinked vendored Quake file: {relative}")
        if git_blob_id(file_path.read_bytes()) != match["sha"]:
            fail(f"modified vendored Quake file: {relative}")
        expected_paths.append(relative)
    if expected_paths != sorted(expected_paths):
        fail("quakegeneric vendor manifest is not sorted")
    actual_paths = sorted(
        path.relative_to(vendor).as_posix()
        for path in vendor.rglob("*") if path.is_file()
    )
    if actual_paths != expected_paths:
        fail("quakegeneric vendor tree contains missing or unmanifested files")

    tracked_paks = [path for path in git_lines(root, "ls-files") if path.lower().endswith(".pak")]
    if tracked_paks:
        fail("Quake PAK files must never be tracked: " + ", ".join(tracked_paks))

    metadata = load_json(root / "third_party" / "game-data.json")
    matches = [
        item for item in metadata.get("game_data", [])
        if type(item) is dict and item.get("id") == "quake-shareware-1.06-pak0"
    ]
    if len(matches) != 1:
        fail("game-data.json must contain one Quake shareware PAK record")
    record = matches[0]
    expected = {
        "filename": "pak0.pak",
        "local_path": "local-data/quake/id1/pak0.pak",
        "sd_path": "/sdcard/GAMES/QUAKE/ID1/PAK0.PAK",
        "size_bytes": 18689235,
        "md5": "5906e5998fc3d896ddaf5e6a62e03abb",
        "sha256": "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af",
    }
    for field, wanted in expected.items():
        if record.get(field) != wanted:
            fail(f"Quake game-data {field} does not match the pinned shareware identity")
    local_data = root / expected["local_path"]
    data_present = local_data.is_file()
    if require_data and not data_present:
        fail(f"required local Quake shareware data is missing: {local_data}")
    if data_present:
        if not ignored(root, local_data):
            fail("local Quake shareware PAK is not ignored by Git")
        data = local_data.read_bytes()
        if len(data) != expected["size_bytes"]:
            fail("local Quake shareware PAK size mismatch")
        if hashlib.md5(data).hexdigest() != expected["md5"]:
            fail("local Quake shareware PAK MD5 mismatch")
        if hashlib.sha256(data).hexdigest() != expected["sha256"]:
            fail("local Quake shareware PAK SHA-256 mismatch")

    for path in root.rglob("*"):
        if not path.is_file() or not path.name.lower().endswith(".pak"):
            continue
        if ".git" in path.parts:
            continue
        if not ignored(root, path):
            fail(f"local Quake PAK is not ignored: {path}")

    return source["commit"], source["tree"], len(expected_paths), data_present


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--require-data", action="store_true")
    arguments = parser.parse_args(argv)
    try:
        commit, tree, count, data_present = verify(arguments.root.resolve(), arguments.require_data)
    except VerificationError as error:
        print(f"P4_QUAKE PROVENANCE FAIL {error}", file=sys.stderr)
        return 1
    print(
        f"P4_QUAKE PROVENANCE PASS commit={commit} tree={tree} "
        f"files={count} data={'verified' if data_present else 'absent'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
