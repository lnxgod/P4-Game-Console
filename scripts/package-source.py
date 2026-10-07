#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package an exact committed source tree, with optional original art separated."""

import argparse
from dataclasses import dataclass
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STAMP = ".p4-source.json"


@dataclass(frozen=True)
class Entry:
    path: str
    mode: str
    oid: str
    size: int


def is_authoring_media(path: str) -> bool:
    """Keep this policy aligned with clone-lite.sh and .gitattributes."""
    parts = PurePosixPath(path).parts
    suffix = PurePosixPath(path).suffix.lower()
    return (len(parts) >= 4 and parts[0] == "games" and parts[2] == "assets"
            and suffix == ".png") or (parts[0] == "design" and suffix in {".png", ".gif"})


def is_art_context(path: str) -> bool:
    """Include authoring tools, originals, notices and provenance in the art pack."""
    parts = PurePosixPath(path).parts
    name = parts[-1].lower()
    return (is_authoring_media(path) or parts[0] == "design" or "assets" in parts
            or (parts[0] in {"games", "components"} and "tools" in parts)
            or (parts[0] == "games" and name.startswith("readme"))
            or path.startswith("third_party/arimo/") or path == "docs/GAME_ART.md"
            or any(token in name for token in ("license", "copying", "notice")))


def git(repo: Path, *args: str) -> bytes:
    return subprocess.check_output(["git", "--no-replace-objects", "-C", str(repo), *args])


def committed_tree(repo: Path, revision: str) -> tuple[str, int, list[Entry]]:
    commit = git(repo, "rev-parse", "--verify", "--end-of-options",
                 f"{revision}^{{commit}}").decode("ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("source commit must resolve to a 40-character SHA-1")
    timestamp = int(git(repo, "show", "-s", "--format=%ct", commit))
    entries = []
    for record in git(repo, "ls-tree", "-rz", "-l", commit).split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, kind, oid, size = metadata.decode("ascii").split()
        path = raw_path.decode("utf-8")
        if kind != "blob" or mode not in {"100644", "100755", "120000"}:
            raise ValueError(f"unsupported source entry: {path} ({mode} {kind})")
        if path == STAMP:
            raise ValueError(f"{STAMP} is reserved for the generated source-commit stamp")
        entries.append(Entry(path, mode, oid, int(size)))
    return commit, timestamp, entries


class Blobs:
    """Read committed blobs directly; export-ignore and dirty files cannot interfere."""

    def __init__(self, repo: Path):
        self.process = subprocess.Popen(["git", "--no-replace-objects", "-C", str(repo),
                                         "cat-file", "--batch"],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def __enter__(self):
        return self

    def read(self, entry: Entry) -> bytes:
        self.process.stdin.write(entry.oid.encode("ascii") + b"\n")
        self.process.stdin.flush()
        header = self.process.stdout.readline().decode("ascii").split()
        if header != [entry.oid, "blob", str(entry.size)]:
            raise ValueError(f"cannot read committed blob: {entry.path}")
        data = self.process.stdout.read(entry.size)
        if len(data) != entry.size or self.process.stdout.read(1) != b"\n":
            raise ValueError(f"incomplete committed blob: {entry.path}")
        return data

    def __exit__(self, *args):
        self.process.stdin.close()
        self.process.stdout.close()
        result = self.process.wait()
        if result and args[0] is None:
            raise ValueError("git cat-file failed while reading committed source")


def validate_runtime_inputs(repo: Path, entries: list[Entry]) -> None:
    """Fail if a future manifest makes an omitted original a runtime dependency."""
    paths = {entry.path for entry in entries}
    manifests = [entry for entry in entries if len(PurePosixPath(entry.path).parts) == 3
                 and entry.path.startswith("games/") and entry.path.endswith("/game.json")]
    with Blobs(repo) as blobs:
        for entry in manifests:
            manifest = json.loads(blobs.read(entry))
            game = PurePosixPath(entry.path).parent
            sources = manifest.get("sources", [f"{manifest.get('component', game.name)}.c"])
            required = [str(game / "src" / source) for source in sources]
            required += [str(game / manifest[key]) for key in ("launcher_icon", "resource_payload")
                         if key in manifest]
            for path in required:
                if path not in paths or is_authoring_media(path):
                    raise ValueError(f"required runtime input missing from lite source: {path}")


def write_archive(repo: Path, path: Path, entries: list[Entry], commit: str,
                  timestamp: int) -> dict:
    prefix = f"p4console-{commit}/"
    with path.open("wb") as output, gzip.GzipFile(filename="", mode="wb", fileobj=output,
                                                 mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
            with Blobs(repo) as blobs:
                for entry in entries:
                    data = blobs.read(entry)
                    member = tarfile.TarInfo(prefix + entry.path)
                    member.mtime = timestamp
                    member.mode = 0o755 if entry.mode == "100755" else 0o644
                    if entry.mode == "120000":
                        member.type = tarfile.SYMTYPE
                        member.mode = 0o777
                        member.linkname = data.decode("utf-8")
                        archive.addfile(member)
                    else:
                        member.size = len(data)
                        archive.addfile(member, io.BytesIO(data))
            stamp = (json.dumps({"schema": 1, "source_commit": commit}, sort_keys=True) + "\n").encode()
            member = tarfile.TarInfo(prefix + STAMP)
            member.mode = 0o644
            member.mtime = timestamp
            member.size = len(stamp)
            archive.addfile(member, io.BytesIO(stamp))
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return {"file": path.name, "bytes": path.stat().st_size, "sha256": digest.hexdigest(),
            "tracked_files": len(entries), "tracked_bytes": sum(entry.size for entry in entries)}


def create_packages(repo: Path, revision: str, output: Path) -> dict:
    commit, timestamp, entries = committed_tree(repo, revision)
    validate_runtime_inputs(repo, entries)
    lite = [entry for entry in entries if not is_authoring_media(entry.path)]
    art = [entry for entry in entries if is_art_context(entry.path)]
    omitted = [entry for entry in entries if is_authoring_media(entry.path)]
    output.mkdir(parents=True, exist_ok=True)
    report = {"schema": 1, "source_commit": commit, "archive_prefix": f"p4console-{commit}/",
              "tracked_files": len(entries), "tracked_bytes": sum(entry.size for entry in entries),
              "lite_tracked_bytes": sum(entry.size for entry in lite),
              "omitted_media_files": len(omitted),
              "omitted_media_bytes": sum(entry.size for entry in omitted),
              "omitted_media_paths": [entry.path for entry in omitted], "archives": {}}
    with tempfile.TemporaryDirectory(prefix=".p4-source-", dir=output) as temporary:
        staging = Path(temporary)
        for kind, selected in (("source_lite", lite), ("art_source", art)):
            name = f"p4console-{commit}-{kind.replace('_', '-')}.tar.gz"
            report["archives"][kind] = write_archive(repo, staging / name, selected, commit, timestamp)
        manifest_name = f"p4console-{commit}-source-manifest.json"
        (staging / manifest_name).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        for path in staging.iterdir():
            os.replace(path, output / path.name)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", default="HEAD", help="commit to package (default: HEAD)")
    parser.add_argument("--output", required=True, type=Path, help="artifact output directory")
    parser.add_argument("--repo", default=ROOT, type=Path, help="source Git checkout")
    args = parser.parse_args()
    try:
        report = create_packages(args.repo, args.commit, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"source packaging failed: {error}\n")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
