#!/usr/bin/env python3
"""Fetch and verify the Tab5 build for this exact source revision, without ESP-IDF."""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import re
import selectors
import stat
import subprocess
import tempfile
import time

from p4_prebuilt import extract_release, load_json, validate_release

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY = "openai/P4-Game-Console"
MAX_ARCHIVE_BYTES = 128 * 1024 * 1024
MAX_CHECKSUM_BYTES = 64 * 1024
MAX_METADATA_BYTES = 1024 * 1024


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


def download(endpoint: str, output: pathlib.Path, limit: int,
             accept: str = "application/vnd.github+json", timeout: float = 300) -> None:
    """Let gh handle private-release credentials; bound every byte it returns."""
    if not endpoint.startswith(f"repos/{REPOSITORY}/releases/"):
        raise ValueError("Prebuilt requests must target this internal repository")
    environment = os.environ.copy()
    environment.pop("GH_DEBUG", None)
    environment["GH_PROMPT_DISABLED"] = "1"
    process = None
    created = False
    try:
        with output.open("xb") as stream:
            created = True
            try:
                process = subprocess.Popen(
                    ["gh", "api", "--hostname", "github.com", "--method", "GET",
                     "--header", f"Accept: {accept}", "--allow-escape-sequences", endpoint],
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL, env=environment,
                )
            except FileNotFoundError as error:
                raise ValueError("Online prebuilt downloads require GitHub CLI (gh); offline archives do not") from error
            deadline = time.monotonic() + timeout
            count = 0
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not selector.select(remaining):
                        raise ValueError("GitHub prebuilt download timed out")
                    chunk = os.read(process.stdout.fileno(), min(64 * 1024, limit - count + 1))
                    if not chunk:
                        break
                    count += len(chunk)
                    if count > limit:
                        raise ValueError("Release download exceeds its size limit")
                    stream.write(chunk)
            try:
                result = process.wait(timeout=max(0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired as error:
                raise ValueError("GitHub prebuilt download timed out") from error
            if result:
                raise ValueError(
                    f"Unable to access the exact prebuilt release in {REPOSITORY}. "
                    "Sign in with gh auth login --hostname github.com and verify repository access. "
                    "If the release is not available, wait for its main-branch CI build, use its "
                    "downloaded CI artifact, or build from source; no firmware was selected."
                )
    except BaseException:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait()
        if created:
            output.unlink(missing_ok=True)
        raise
    finally:
        if process is not None:
            process.stdout.close()


def release_assets(commit: str, staging: pathlib.Path) -> dict:
    tag = f"tab5-{commit}"
    metadata = staging / "release.json"
    download(f"repos/{REPOSITORY}/releases/tags/{tag}", metadata,
             MAX_METADATA_BYTES, timeout=30)
    release = load_json(metadata.read_bytes())
    if (not isinstance(release, dict) or release.get("tag_name") != tag
            or release.get("draft") is not False or not isinstance(release.get("assets"), list)):
        raise ValueError("GitHub release metadata does not identify the exact published source revision")
    assets = {}
    for filename, limit in (("SHA256SUMS", MAX_CHECKSUM_BYTES),
                            (f"tab5-{commit}.tar.gz", MAX_ARCHIVE_BYTES)):
        matches = [asset for asset in release["assets"]
                   if isinstance(asset, dict) and asset.get("name") == filename]
        if len(matches) != 1:
            raise ValueError("Release must contain each exact prebuilt asset once")
        asset = matches[0]
        if (type(asset.get("id")) is not int or asset["id"] <= 0
                or type(asset.get("size")) is not int or not 0 < asset["size"] <= limit
                or asset.get("state") != "uploaded"):
            raise ValueError("Release asset metadata is invalid or exceeds its size limit")
        assets[filename] = asset
    return assets


def download_asset(asset: dict, output: pathlib.Path, limit: int) -> None:
    # Ignore metadata URLs: the authenticated API host/repository are fixed.
    download(f"repos/{REPOSITORY}/releases/assets/{asset['id']}", output,
             limit, accept="application/octet-stream")
    if output.stat().st_size != asset["size"]:
        raise ValueError("Downloaded release asset does not match its recorded byte count")


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
            assets = release_assets(commit, staging)
            checksum = staging / "SHA256SUMS"
            download_asset(assets["SHA256SUMS"], checksum, MAX_CHECKSUM_BYTES)
            archive_sha256 = checksum_for(checksum.read_bytes(), filename)
            archive = staging / filename
            print(f"Downloading verified Tab5 package for {commit} from {REPOSITORY}…", flush=True)
            download_asset(assets[filename], archive, MAX_ARCHIVE_BYTES)
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
