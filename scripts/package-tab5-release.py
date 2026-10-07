#!/usr/bin/env python3
"""Export a full-verified, build-only Tab5 runtime release without local WADs."""
from __future__ import annotations

import argparse
import gzip
import importlib.util
import io
import json
import os
import pathlib
import subprocess
import tarfile
import tempfile

import p4_prebuilt as portable

ROOT = pathlib.Path(__file__).resolve().parents[1]


def verify_source(root: pathlib.Path, commit: str) -> None:
    portable.require(isinstance(commit, str) and portable.COMMIT_RE.fullmatch(commit),
                     "--commit must be the complete 40-character source commit")
    actual = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    portable.require(actual == commit, "export commit differs from source HEAD")
    changed = subprocess.run(["git", "-C", str(root), "diff", "--quiet", "HEAD", "--"], check=False)
    portable.require(changed.returncode == 0, "tracked source differs from export commit")
    # Untracked build outputs are harmless; untracked inputs must never receive
    # the identity of a committed source snapshot.
    paths = portable.source_paths(root)
    tracked = set(subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"], text=True).split("\0"))
    portable.require(paths <= tracked, "reviewed source includes uncommitted inputs")
    for name in paths:
        portable.regular_file(root, name)
    for game in portable.standard_games(root):
        portable.require(game["source"] in paths, "untracked standard game manifest")


def full_verify(root: pathlib.Path, build: pathlib.Path) -> dict:
    spec = importlib.util.spec_from_file_location("tab5_export_verifier", root / "scripts/verify-console-os-tab5.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)
    verifier.ROOT = root
    verifier.APP = root / "apps/console_os"
    return verifier.verify(build, firmware_only=False)


def canonical_json(value: dict) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n").encode("ascii")


def export_release(root: pathlib.Path, build: pathlib.Path, output: pathlib.Path, commit: str) -> dict:
    """Run all build gates before creating any release output."""
    root, build, output = pathlib.Path(root), pathlib.Path(build), pathlib.Path(output)
    verify_source(root, commit)
    before = portable.source_bindings(root)
    verified = full_verify(root, build)
    metadata = portable.load_json(portable.regular_file(root, "apps/console_os/app-metadata.json").read_bytes())
    portable.require(metadata.get("game_data_embedded") is False, "runtime release must not embed game data")
    games = portable.standard_games(root)
    files = {}
    payloads = {}
    for name, (role, limit) in portable.expected_files(games).items():
        source = (name.removeprefix("firmware/") if name.startswith("firmware/") else
                  "sd-card/" + name.removeprefix("content/"))
        path = portable.regular_file(build, source)
        portable.require(0 < path.stat().st_size <= limit, f"runtime file exceeds export limit: {source}")
        data = path.read_bytes()
        payloads[name] = data
        files[name] = {"bytes": len(data), "sha256": portable.sha256(data), "role": role}
    project = portable.load_json(portable.regular_file(build, "project_description.json").read_bytes())
    app_elf = portable.safe_path(project["app_elf"]).as_posix()
    evidence = {name: portable.file_sha256(portable.regular_file(build, name))
                for name in ("sdkconfig", "project_description.json", "compile_commands.json")}
    evidence["app_elf"] = portable.file_sha256(portable.regular_file(build, app_elf))
    manifest = {
        "format": portable.FORMAT, "schema": 1, "board": "m5stack-tab5",
        "target": "esp32p4-tab5", "version": verified["version"], "source_commit": commit,
        "source_files": before,
        "lock_sha256": {name: before[name] for name in portable.LOCK_PATHS},
        "features": {name: verified[name] for name in portable.FEATURES},
        "files": files, "games": games, "export_verification": verified,
        "build_evidence": evidence, "firmware_only": False,
        "content_scope": "standard-native-only", "wad_included": False,
        "hardware_verified": False, "flash_authorized": False,
    }
    portable.validate_manifest(manifest)
    portable.validate_binaries(payloads, manifest)
    portable.require(portable.source_bindings(root) == before, "source changed during full verification")
    verify_source(root, commit)
    raw = canonical_json(manifest)
    portable.require(len(raw) <= portable.MAX_MANIFEST_BYTES, "export manifest exceeds portable bound")
    # Only these collected byte strings enter the tar. Neither source metadata
    # nor the verified SD WAD/README/update directory is recursively copied.
    payloads["manifest.json"] = raw
    archive_name = f"tab5-{commit}.tar.gz"
    output.mkdir(parents=True, exist_ok=True)
    destinations = [output / archive_name, output / "SHA256SUMS", output / "release-manifest.json"]
    portable.require(not output.is_symlink() and all(not path.exists() and not path.is_symlink()
                                                   for path in destinations), "release output already exists or is linked")
    with tempfile.TemporaryDirectory(prefix=".tab5-export-", dir=output) as temporary:
        staged = pathlib.Path(temporary) / archive_name
        with staged.open("wb") as raw_archive:
            with gzip.GzipFile(fileobj=raw_archive, mode="wb", filename="", mtime=0) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.USTAR_FORMAT) as archive:
                    for name, data in sorted(payloads.items()):
                        member = tarfile.TarInfo(name)
                        member.size = len(data)
                        member.mode = 0o644
                        member.mtime = member.uid = member.gid = 0
                        member.uname = member.gname = ""
                        archive.addfile(member, io.BytesIO(data))
        digest = portable.file_sha256(staged)
        # Exercise the same SDK-free reader before making the release visible.
        portable.extract_release(staged, pathlib.Path(temporary) / "verified", expected_sha256=digest,
                                 expected_source_commit=commit, source_root=root)
        staged_checksum = pathlib.Path(temporary) / "SHA256SUMS"
        staged_manifest = pathlib.Path(temporary) / "release-manifest.json"
        staged_checksum.write_text(f"{digest}  {archive_name}\n", encoding="ascii")
        staged_manifest.write_bytes(raw)
        published = []
        try:
            for source, destination in zip((staged, staged_checksum, staged_manifest), destinations):
                # The staging directory shares the output filesystem. Hardlink
                # publication is atomic and refuses an unexpectedly existing file.
                os.link(source, destination)
                published.append(destination)
        except OSError:
            for destination in published:
                destination.unlink()
            raise
    return {"archive": str(destinations[0]), "archive_sha256": digest,
            "manifest_sha256": portable.sha256(raw), "source_commit": commit,
            "runtime_files": len(files), "native_cartridges": len(games),
            "hardware_verified": False, "flash_authorized": False}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, default=ROOT / "apps/console_os/build-tab5")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    print(json.dumps(export_release(ROOT, args.build.resolve(), args.output.absolute(), args.commit), sort_keys=True))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (portable.ReleaseError, OSError, KeyError, TypeError, subprocess.CalledProcessError,
            UnicodeError, ValueError) as error:
        raise SystemExit(f"Tab5 release export failed: {error}") from error
