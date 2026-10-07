#!/usr/bin/env python3
"""Fetch and verify the Tab5 build for this exact source revision, without ESP-IDF."""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import re
import stat
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request

from p4_prebuilt import extract_release, load_json, validate_release

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY = "lnxgod/P4-Game-Console"
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_CHECKSUM_BYTES = 64 * 1024


def source_commit(root: pathlib.Path) -> str:
    """A source archive's stamp is the fallback when Git metadata is absent."""
    if (root / ".git").exists():
        result = subprocess.run(["git", "--no-replace-objects", "rev-parse", "HEAD"], cwd=root,
                                capture_output=True, text=True, check=True)
        commit = result.stdout.strip()
    else:
        try:
            stamp_path = root / ".p4-source.json"
            if not stat.S_ISREG(stamp_path.lstat().st_mode):
                raise ValueError("Source-archive stamp must be a small regular file")
            with stamp_path.open("rb") as stream:
                raw = stream.read(1025)
            if len(raw) > 1024:
                raise ValueError("Source-archive stamp must be a small regular file")
            stamp = load_json(raw)
        except (OSError, ValueError) as error:
            raise ValueError("No Git revision or verified source-archive stamp; use the lite clone or a CI source archive") from error
        if not isinstance(stamp, dict) or set(stamp) != {"schema", "source_commit"} or type(stamp["schema"]) is not int or stamp["schema"] != 1:
            raise ValueError("Invalid source-archive stamp")
        commit = stamp["source_commit"]
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected a complete 40-character source revision")
    return commit


def checksum_for(raw: bytes, filename: str) -> str:
    if len(raw) > MAX_CHECKSUM_BYTES:
        raise ValueError("Release checksum inventory is too large")
    matches = []
    for line in raw.decode("ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64}) [ *]([^\r\n]+)", line)
        if match is None:
            raise ValueError("Invalid release checksum inventory")
        if match[2] == filename:
            matches.append(match[1])
    if len(matches) != 1:
        raise ValueError("Release must list the exact firmware archive once")
    return matches[0]


def download(url: str, output: pathlib.Path, limit: int) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "P4-Game-Console-prebuilt/1"})
    with urllib.request.urlopen(request, timeout=30) as response:
        final = urllib.parse.urlsplit(response.geturl())
        host = final.hostname or ""
        if final.scheme != "https" or not (host == "github.com" or host.endswith(".githubusercontent.com")):
            raise ValueError("Release redirected outside GitHub HTTPS asset hosting")
        length = response.headers.get("Content-Length")
        if length is not None and int(length) > limit:
            raise ValueError("Release download exceeds its size limit")
        count = 0
        with output.open("xb") as stream:
            while chunk := response.read(1024 * 1024):
                count += len(chunk)
                if count > limit:
                    raise ValueError("Release download exceeds its size limit")
                stream.write(chunk)


def fetch(root: pathlib.Path, output: pathlib.Path | None = None,
          archive: pathlib.Path | None = None, archive_sha256: str | None = None) -> pathlib.Path:
    commit = source_commit(root)
    destination = output or root / "build-host/prebuilt/tab5" / commit
    if (archive is None) != (archive_sha256 is None):
        raise ValueError("Offline archives require both --archive and --sha256")
    if archive_sha256 is not None and not re.fullmatch(r"[0-9a-f]{64}", archive_sha256):
        raise ValueError("Archive SHA-256 must be 64 lowercase hex characters")
    if destination.exists():
        if destination.is_symlink() or not destination.is_dir():
            raise ValueError("Prebuilt destination must be a real directory")
        validate_release(destination, expected_source_commit=commit, source_root=root)
        print(f"Verified cached Tab5 package: {destination}")
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".tab5-download-", dir=destination.parent) as temporary:
        staging = pathlib.Path(temporary)
        if archive is None:
            filename = f"tab5-{commit}.tar.gz"
            base = f"https://github.com/{REPOSITORY}/releases/download/tab5-{commit}"
            checksum = staging / "SHA256SUMS"
            try:
                download(f"{base}/SHA256SUMS", checksum, MAX_CHECKSUM_BYTES)
                archive_sha256 = checksum_for(checksum.read_bytes(), filename)
                archive = staging / filename
                print(f"Downloading verified Tab5 package for {commit}…", flush=True)
                download(f"{base}/{filename}", archive, MAX_ARCHIVE_BYTES)
            except urllib.error.HTTPError as error:
                if error.code == 404:
                    raise ValueError(f"No prebuilt Tab5 release for {commit}. Wait for its main-branch CI build, download its CI artifact, or use the documented source build; no firmware was selected.") from error
                raise
        if archive.stat().st_size > MAX_ARCHIVE_BYTES:
            raise ValueError("Firmware archive exceeds its size limit")
        unpacked = staging / "verified"
        extract_release(archive, unpacked, expected_sha256=archive_sha256,
                        expected_source_commit=commit, source_root=root)
        # Do not replace an existing compile output or a previously downloaded package.
        if destination.exists():
            raise ValueError("Prebuilt destination appeared during verification; retry to validate it")
        unpacked.rename(destination)
    print(f"Verified Tab5 package: {destination}")
    print(f"Manifest SHA-256: {hashlib.sha256((destination / 'manifest.json').read_bytes()).hexdigest()}")
    return destination


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, help="default: ignored build-host/prebuilt/tab5/<revision>")
    parser.add_argument("--archive", type=pathlib.Path, help="use an already downloaded CI archive")
    parser.add_argument("--sha256", help="externally pinned SHA-256 for --archive")
    args = parser.parse_args()
    fetch(ROOT, args.output, args.archive, args.sha256)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from None
