#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Acquire exact pinned local Doom data; never install, embed or redistribute it."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "third_party/game-data.json"
DOOM = "doom-shareware-1.9"
CHEX_WAD = "chex-quest-1.0"
CHEX_PATCH = "chex-quest-dehacked-2008-08-14"
CHUNK = 128 * 1024


class PreparationError(RuntimeError):
    """Local data cannot be prepared without weakening its pinned identity."""


def identity(path: Path) -> tuple[int, str] | None:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return None
    if not stat.S_ISREG(info.st_mode):
        raise PreparationError(f"destination must be a regular file, not a link: {path}")
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        for data in iter(lambda: stream.read(CHUNK), b""):
            size += len(data)
            digest.update(data)
    return size, digest.hexdigest()


def expected(spec: dict) -> tuple[int, str]:
    size, digest = spec.get("size_bytes"), spec.get("sha256")
    if (not isinstance(size, int) or isinstance(size, bool) or size <= 0 or
            not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None):
        raise PreparationError("manifest contains an invalid byte count or SHA-256")
    return size, digest


def selected_entries(manifest: dict, chex: bool, sd: bool) -> list[dict]:
    if chex and not sd:
        raise PreparationError("Chex requires explicit --chex selection and --sd")
    if manifest.get("schema") != 1 or any(
        manifest.get("policy", {}).get(key) is not False
        for key in ("bytes_in_git", "bytes_in_firmware", "redistribution_by_project")
    ):
        raise PreparationError("manifest must preserve the local-only game-data policy")
    chosen = [DOOM, CHEX_WAD, CHEX_PATCH] if chex else [DOOM]
    records = manifest.get("game_data", [])
    result = []
    for name in chosen:
        matches = [r for r in records if isinstance(r, dict) and r.get("id") == name]
        if len(matches) != 1:
            raise PreparationError(f"manifest needs one exact {name} entry")
        spec = matches[0]
        expected(spec)
        local = PurePosixPath(spec.get("local_path", ""))
        if (local.is_absolute() or ".." in local.parts or len(local.parts) < 2 or
                local.parts[0] != "local-data" or local.name != spec.get("filename")):
            raise PreparationError(f"unsafe manifest local_path for {name}")
        result.append(spec)
    return result


def acquisition(spec: dict) -> tuple[str, str, dict | None]:
    if spec["id"] == DOOM:
        url = spec.get("local_copy_obtained_from", "")
        kind, archive = "gzip", None
    elif spec["id"] == CHEX_WAD:
        archive = spec.get("source_archive", {})
        expected(archive)
        url, kind = archive.get("url", ""), "zip"
    elif spec["id"] == CHEX_PATCH:
        # The pinned Debian /src/ URL is HTML. /data/ serves that exact version's
        # file bytes, not a newer patch or a third-party mirror.
        page = urllib.parse.urlsplit(spec.get("source", ""))
        prefix = "/src/prboom-plus/"
        if (page.scheme != "https" or page.netloc != "sources.debian.org" or
                not page.path.startswith(prefix) or page.query or page.fragment):
            raise PreparationError("Chex patch source must be its pinned Debian source page")
        suffix = page.path[len(prefix):]
        if (len(suffix.split("/")) != 4 or not suffix.endswith("/data/lumps/chexdeh.lmp") or
                ".." in urllib.parse.unquote(suffix).split("/")):
            raise PreparationError("unexpected pinned Chex patch path")
        url = "https://sources.debian.org/data/main/p/prboom-plus/" + suffix
        kind, archive = "raw", None
    else:
        raise PreparationError("unsupported game data")
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.fragment:
        raise PreparationError("acquisition needs the manifest's HTTPS URL")
    return url, kind, archive


class HttpsRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlsplit(newurl).scheme != "https":
            raise PreparationError("refusing a non-HTTPS download redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def open_download(url: str, timeout: float):
    request = urllib.request.Request(url, headers={"User-Agent": "P4ConsoleGameData/1.0"})
    return urllib.request.build_opener(HttpsRedirect()).open(request, timeout=timeout)


def copy_bounded(source, destination: Path, limit: int) -> None:
    total = 0
    with destination.open("xb") as output:
        while data := source.read(min(CHUNK, limit + 1 - total)):
            total += len(data)
            if total > limit:
                raise PreparationError("download or decompressed data exceeds its pinned bound")
            output.write(data)
        output.flush()
        os.fsync(output.fileno())


def acquire(spec: dict, stage: Path, timeout: float, opener) -> tuple[Path, dict]:
    url, kind, archive = acquisition(spec)
    size, digest = expected(spec)
    downloaded = stage / "download.tmp"
    transport_limit = expected(archive)[0] if archive else size + (65536 if kind == "gzip" else 0)
    with opener(url, timeout) as response:
        resolved = response.geturl() if hasattr(response, "geturl") else url
        copy_bounded(response, downloaded, transport_limit)
    payload = stage / "payload.tmp"
    if kind == "gzip":
        with gzip.open(downloaded, "rb") as source:
            copy_bounded(source, payload, size)
    elif kind == "zip":
        if identity(downloaded) != expected(archive):
            raise PreparationError("Chex archive size/SHA-256 differs from the pinned manifest")
        with zipfile.ZipFile(downloaded) as zipped:
            members = [m for m in zipped.infolist()
                       if PurePosixPath(m.filename).name.casefold() == spec["filename"].casefold()]
            if (len(members) != 1 or members[0].is_dir() or members[0].file_size != size or
                    members[0].flag_bits & 1):
                raise PreparationError("archive needs one unencrypted exact-size Chex WAD")
            # Stream only the intended member; never extract filenames from the archive.
            with zipped.open(members[0]) as source:
                copy_bounded(source, payload, size)
    else:
        downloaded.rename(payload)
    if identity(payload) != (size, digest):
        raise PreparationError(f"{spec['filename']} size/SHA-256 differs from the pinned manifest")
    return payload, {"url": url, "resolved_url": resolved, "encoding": kind}


def real_directory(path: Path) -> None:
    missing = []
    current = path
    while not current.exists() and not current.is_symlink():
        missing.append(current)
        current = current.parent
    for directory in (current, *current.parents):
        if directory.is_symlink() or not directory.is_dir():
            raise PreparationError(f"output directory must not be a link: {directory}")
    for directory in reversed(missing):
        directory.mkdir(mode=0o700)


def require_ignored(path: Path) -> None:
    try:
        relative = path.relative_to(ROOT)
    except ValueError as error:
        raise PreparationError("--output-root must be inside this checkout at an ignored location") from error
    checked = subprocess.run(["git", "check-ignore", "-q", "--", str(relative)], cwd=ROOT,
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if checked.returncode != 0:
        raise PreparationError(f"destination is not Git-ignored: {relative}")


def prepare(manifest: dict, output_root: Path, *, chex=False, sd=False,
            timeout=30.0, opener=open_download, destination_check=require_ignored) -> dict:
    specs = selected_entries(manifest, chex, sd)  # Policy before network or files.
    output_root = Path(os.path.abspath(output_root))
    paths = [output_root / spec["local_path"] for spec in specs]
    for path in paths:
        destination_check(path)
    rows = []
    # Verify every selected input before activating any download. Thus a patch
    # or network failure cannot replace one half of an existing Chex pair.
    with ExitStack() as stack:
        pending = []
        for spec, path in zip(specs, paths):
            real_directory(path.parent)
            original = identity(path)
            row = {"id": spec["id"], "path": str(path), "bytes": spec["size_bytes"],
                   "sha256": spec["sha256"], "notice": spec.get("notice", ""),
                   "status": "reused"}
            rows.append(row)
            if original == expected(spec):
                continue
            stage = Path(stack.enter_context(tempfile.TemporaryDirectory(
                prefix=".prepare-game-data-", suffix=".tmp", dir=path.parent)))
            payload, provenance = acquire(spec, stage, timeout, opener)
            pending.append((spec, path, original, payload, row))
            row.update(status="downloaded", acquisition=provenance)
        for spec, path, original, payload, row in pending:
            current = identity(path)
            if current == expected(spec):
                row["status"] = "reused-concurrent"
                continue
            if current != original:
                raise PreparationError(f"destination changed during preparation: {path}")
            os.replace(payload, path)
            fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
            if identity(path) != expected(spec):
                raise PreparationError(f"activated file failed readback: {path}")
    return {"schema": 1, "result": "local-data-ready", "installed": False, "files": rows}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chex", action="store_true", help="explicitly select optional Chex WAD + patch")
    parser.add_argument("--sd", action="store_true", help="confirm SD storage for optional Chex")
    parser.add_argument("--output-root", type=Path, default=ROOT,
                        help="checkout directory prefix for manifest local paths; every destination must be ignored")
    parser.add_argument("--timeout", type=float, default=30.0, help="per-network-operation timeout, 1–120 seconds")
    args = parser.parse_args(argv)
    try:
        if not 1 <= args.timeout <= 120:
            raise PreparationError("timeout must be between 1 and 120 seconds")
        result = prepare(json.loads(MANIFEST.read_text()), args.output_root,
                         chex=args.chex, sd=args.sd, timeout=args.timeout)
        result["manifest"] = "third_party/game-data.json"
        result["manifest_sha256"] = hashlib.sha256(MANIFEST.read_bytes()).hexdigest()
        for row in result["files"]:
            row["path"] = str(Path(row["path"]).relative_to(ROOT))
        print(json.dumps(result, indent=2))
    except (PreparationError, OSError, ValueError, EOFError, zipfile.BadZipFile) as error:
        print(f"prepare-game-data: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
