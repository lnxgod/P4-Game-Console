#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the pinned Doom source plus the explicit, reversible P4 patch."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def relative_path(value: object) -> Path:
    if not isinstance(value, str):
        raise ValueError("source path must be a string")
    path = Path(value)
    if not value or path.is_absolute() or ".." in path.parts or str(path) != value:
        raise ValueError(f"unsafe source path: {value!r}")
    return path


def blob_digest(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()


def verify_upstream(tree: Path, expected: dict[Path, str]) -> None:
    paths = set()
    for path in tree.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"symlink in Doom source tree: {path.relative_to(tree)}")
        if path.is_file():
            paths.add(path.relative_to(tree))
    if paths != set(expected):
        raise ValueError("Doom source tree contains missing or unmanifested files")
    for rel, digest in expected.items():
        if blob_digest((tree / rel).read_bytes()) != digest:
            raise ValueError(f"modified vendored doomgeneric file: {rel}")


def check(root: Path, tree: Path, *, apply: bool = False) -> int:
    lock = json.loads((root / "third_party/source-lock.json").read_text())["sources"]["doomgeneric"]
    manifest = (root / relative_path(lock["vendor_manifest"])).read_bytes()
    if hashlib.sha256(manifest).hexdigest() != lock["vendor_manifest_sha256"]:
        raise ValueError("upstream manifest hash differs from source lock")
    expected = {}
    for line in manifest.decode().splitlines():
        mode, kind, digest, name = line.split(maxsplit=3)
        rel = relative_path(name)
        if mode != "100644" or kind != "blob" or not re.fullmatch(r"[0-9a-f]{40}", digest) or rel in expected:
            raise ValueError(f"invalid upstream manifest entry: {line}")
        expected[rel] = digest
    if not expected:
        raise ValueError("empty upstream manifest")
    local = json.loads((root / "third_party/doomgeneric-p4.json").read_text())
    if local.get("schema") != 1 or local.get("upstream_commit") != lock["commit"]:
        raise ValueError("P4 patch is not bound to the pinned upstream commit")
    patch = (root / relative_path(local["patch"])).read_bytes()
    if hashlib.sha256(patch).hexdigest() != local["patch_sha256"]:
        raise ValueError("P4 patch SHA-256 mismatch")
    # The patch may modify only regular files already in the upstream manifest.
    # Reject binary/new/deleted/renamed files and path escapes before git apply.
    if any(marker in patch for marker in (b"GIT binary patch", b"/dev/null", b"rename from", b"new file mode", b"deleted file mode")):
        raise ValueError("P4 patch must only edit existing text files")
    for line in patch.decode().splitlines():
        if line.startswith(("--- ", "+++ ")):
            name = line[4:]
            prefix = "a/" if line.startswith("--- ") else "b/"
            if not name.startswith(prefix) or relative_path(name[2:]) not in expected:
                raise ValueError(f"unexpected P4 patch path: {name}")
    if apply:
        verify_upstream(tree, expected)
        work = tree
        context = None
    else:
        # Reject links before copytree follows them. Never mutate the live tree
        # while checking whether the reviewed patch reverses to exact upstream.
        if tree.is_symlink() or any(path.is_symlink() for path in tree.rglob("*")):
            raise ValueError("symlink in Doom source tree")
        context = tempfile.TemporaryDirectory(prefix="p4-doom-source-")
        work = Path(context.name) / "tree"
        shutil.copytree(tree, work)
    try:
        command = ["git", "apply", "--whitespace=nowarn"]
        if not apply:
            command.append("--reverse")
        result = subprocess.run(command, cwd=work, input=patch, capture_output=True)
        if result.returncode:
            raise ValueError("P4 patch does not apply cleanly: " + result.stderr.decode().strip())
        if not apply:
            verify_upstream(work, expected)
    finally:
        if context is not None:
            context.cleanup()
    return len(expected)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--apply-to", type=Path, help="apply the pinned patch to a pristine vendor staging tree")
    args = parser.parse_args()
    root = args.root.resolve()
    lock = json.loads((root / "third_party/source-lock.json").read_text())["sources"]["doomgeneric"]
    tree = args.apply_to if args.apply_to is not None else root / relative_path(lock["vendor_path"])
    try:
        count = check(root, tree, apply=args.apply_to is not None)
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit(str(error)) from error
    print(f"P4_DOOM SOURCE {'PATCHED' if args.apply_to is not None else 'PASS'} files={count} upstream={lock['commit']}")


if __name__ == "__main__":
    main()
