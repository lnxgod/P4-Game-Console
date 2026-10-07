#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify bounded acquisition and atomic activation without network or hardware."""
from __future__ import annotations

from copy import deepcopy
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_game_data", ROOT / "scripts/prepare-game-data.py")
data = importlib.util.module_from_spec(spec)
spec.loader.exec_module(data)


def pin(payload):
    return {"size_bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()}


def archive(payload, *, duplicate=False):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as zipped:
        zipped.writestr("original/CHEX.WAD", payload)
        zipped.writestr("../unused.txt", b"never extract this")
        if duplicate:
            zipped.writestr("duplicate/chex.wad", payload)
    return output.getvalue()


class DataPreparationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="p4-data-tests-")
        self.root = Path(self.directory.name).resolve()
        self.addCleanup(self.directory.cleanup)
        self.doom = b"IWAD mocked Doom data\0" * 19
        self.chex = b"IWAD mocked Chex data\0" * 23
        self.deh = b"Patch File for DeHackEd\n" * 11
        self.zip = archive(self.chex)
        self.manifest = {"schema": 1, "policy": {
            "bytes_in_git": False, "bytes_in_firmware": False,
            "redistribution_by_project": False}, "game_data": [
            {"id": data.DOOM, "filename": "doom1.wad", "local_path": "local-data/doom/doom1.wad",
             "local_copy_obtained_from": "https://pinned.example/doom.wad.gz", **pin(self.doom)},
            {"id": data.CHEX_WAD, "filename": "chex.wad", "local_path": "local-data/doom/chex.wad",
             "source_archive": {"url": "https://pinned.example/chex.zip", **pin(self.zip)}, **pin(self.chex)},
            {"id": data.CHEX_PATCH, "filename": "chex.deh", "local_path": "local-data/doom/chex.deh",
             "source": "https://sources.debian.org/src/prboom-plus/2%3A1/data/lumps/chexdeh.lmp",
             **pin(self.deh)}]}
        self.responses = {"https://pinned.example/doom.wad.gz": gzip.compress(self.doom),
                          "https://pinned.example/chex.zip": self.zip,
                          "https://sources.debian.org/data/main/p/prboom-plus/2%3A1/data/lumps/chexdeh.lmp": self.deh}
        self.calls = []

    def open(self, url, timeout):
        self.calls.append(url)
        value = self.responses[url]
        if isinstance(value, Exception):
            raise value
        return io.BytesIO(value)

    def run_prepare(self, **kwargs):
        return data.prepare(self.manifest, self.root, opener=self.open,
                            destination_check=lambda path: None, **kwargs)

    def path(self, name):
        return self.root / "local-data/doom" / name

    def existing(self, name, payload):
        self.path(name).parent.mkdir(parents=True, exist_ok=True)
        self.path(name).write_bytes(payload)

    def assert_no_staging(self):
        self.assertEqual(list(self.root.rglob(".prepare-game-data-*")), [])

    def git(self, *args):
        executable = shutil.which("git")
        if executable is None:
            self.skipTest("Git is required for ignore-rule fixtures")
        env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        return subprocess.run([executable, *args], cwd=self.root, env=env,
                              capture_output=True, text=True, check=True)

    def source_archive(self, *, stamped=True, ignored=True):
        self.git("init", "--quiet", "--template=")
        # A parent checkout must neither authorize an unstamped archive nor
        # supply ignore rules when the archive's own rule excludes nothing.
        (self.root / ".gitignore").write_text("*\n")
        self.root = self.root / "source archive"
        self.root.mkdir()
        (self.root / ".gitignore").write_text("*.[Ww][Aa][Dd]\n" if ignored else "")
        if stamped:
            (self.root / ".p4-source.json").write_text(json.dumps({
                "schema": 1, "source_commit": "a" * 40}))

    def prepare_with_ignore_rules(self):
        with patch.object(data, "ROOT", self.root):
            return data.prepare(self.manifest, self.root, opener=self.open)

    def test_stamped_source_archive_uses_its_own_ignore_rules_without_root_metadata(self):
        self.source_archive()
        result = self.prepare_with_ignore_rules()
        self.assertEqual(result["files"][0]["status"], "downloaded")
        self.assertEqual(self.path("doom1.wad").read_bytes(), self.doom)
        self.assertFalse((self.root / ".git").exists())
        self.assert_no_staging()

    def test_unstamped_archive_does_not_inherit_parent_git_checkout(self):
        self.source_archive(stamped=False)
        with self.assertRaisesRegex(data.PreparationError, "valid source archive stamp"):
            self.prepare_with_ignore_rules()
        self.assertEqual(self.calls, [])
        self.assertFalse((self.root / "local-data").exists())
        self.assertFalse((self.root / ".git").exists())

    def test_stamped_archive_rejects_unignored_destination_despite_parent_rules(self):
        self.source_archive(ignored=False)
        with self.assertRaisesRegex(data.PreparationError, "not Git-ignored"):
            self.prepare_with_ignore_rules()
        self.assertEqual(self.calls, [])
        self.assertFalse((self.root / "local-data").exists())
        self.assertFalse((self.root / ".git").exists())

    def test_source_archive_stamp_is_strict_and_bounded(self):
        self.source_archive()
        stamp = self.root / ".p4-source.json"
        cases = ["[]", "{}", '{"schema": true, "source_commit": "' + "a" * 40 + '"}',
                 json.dumps({"schema": 2, "source_commit": "a" * 40}),
                 json.dumps({"schema": 1, "source_commit": "a" * 39}),
                 json.dumps({"schema": 1, "source_commit": "a" * 40, "extra": 1}),
                 '{"schema": 1, "schema": 1, "source_commit": "' + "a" * 40 + '"}',
                 " " * 1025, "not JSON"]
        for raw in cases:
            with self.subTest(stamp=raw[:80]):
                stamp.write_text(raw)
                with self.assertRaises(data.PreparationError):
                    self.prepare_with_ignore_rules()
                self.assertEqual(self.calls, [])
                self.assertFalse((self.root / "local-data").exists())
                self.assertFalse((self.root / ".git").exists())
        target = self.root / "stamp-target.json"
        target.write_text(json.dumps({"schema": 1, "source_commit": "a" * 40}))
        stamp.unlink()
        stamp.symlink_to(target)
        with self.assertRaisesRegex(data.PreparationError, "regular file"):
            self.prepare_with_ignore_rules()

    def test_normal_git_checkout_still_rejects_tracked_destination(self):
        self.git("init", "--quiet", "--template=")
        (self.root / ".gitignore").write_text("*.wad\n")
        self.existing("doom1.wad", self.doom)
        self.git("add", "-f", "local-data/doom/doom1.wad")
        with self.assertRaisesRegex(data.PreparationError, "not Git-ignored"):
            self.prepare_with_ignore_rules()
        self.assertEqual(self.calls, [])
        self.assertEqual(self.path("doom1.wad").read_bytes(), self.doom)

    def test_fresh_default_is_verified_and_reused_without_network(self):
        result = self.run_prepare()
        self.assertEqual(result["result"], "local-data-ready")
        self.assertFalse(result["installed"])
        self.assertEqual(len(result["files"]), 1)
        self.assertEqual(result["files"][0]["status"], "downloaded")
        self.assertEqual(self.path("doom1.wad").read_bytes(), self.doom)
        before = self.path("doom1.wad").stat()
        self.calls.clear()
        self.responses.clear()
        self.assertEqual(self.run_prepare()["files"][0]["status"], "reused")
        self.assertEqual(self.calls, [])
        self.assertEqual(self.path("doom1.wad").stat().st_ino, before.st_ino)
        self.assertEqual(self.path("doom1.wad").stat().st_mtime_ns, before.st_mtime_ns)
        self.assert_no_staging()

    def test_bad_hash_preserves_existing_file(self):
        self.existing("doom1.wad", b"preserved old data")
        self.responses[next(iter(self.responses))] = gzip.compress(b"X" * len(self.doom))
        with self.assertRaisesRegex(data.PreparationError, "SHA-256"):
            self.run_prepare()
        self.assertEqual(self.path("doom1.wad").read_bytes(), b"preserved old data")
        self.assert_no_staging()

    def test_truncated_gzip_preserves_existing_file(self):
        self.existing("doom1.wad", b"preserved old data")
        self.responses[next(iter(self.responses))] = gzip.compress(self.doom)[:-6]
        with self.assertRaises(EOFError):
            self.run_prepare()
        self.assertEqual(self.path("doom1.wad").read_bytes(), b"preserved old data")
        self.assert_no_staging()

    def test_decompression_bound_prevents_oversized_activation(self):
        self.responses[next(iter(self.responses))] = gzip.compress(b"X" * (len(self.doom) + 1))
        with self.assertRaisesRegex(data.PreparationError, "bound"):
            self.run_prepare()
        self.assertFalse(self.path("doom1.wad").exists())
        self.assert_no_staging()

    def test_transport_bound_prevents_oversized_download(self):
        self.responses[next(iter(self.responses))] = b"X" * (len(self.doom) + 65537)
        with self.assertRaisesRegex(data.PreparationError, "bound"):
            self.run_prepare()
        self.assertFalse(self.path("doom1.wad").exists())
        self.assert_no_staging()

    def test_network_failure_preserves_existing_file(self):
        self.existing("doom1.wad", b"preserved old data")
        self.responses[next(iter(self.responses))] = urllib.error.URLError("offline")
        with self.assertRaises(urllib.error.URLError):
            self.run_prepare()
        self.assertEqual(self.path("doom1.wad").read_bytes(), b"preserved old data")
        self.assert_no_staging()

    def test_chex_requires_explicit_selection_and_sd(self):
        with self.assertRaisesRegex(data.PreparationError, "--sd"):
            self.run_prepare(chex=True)
        self.assertEqual(list(self.root.iterdir()), [])
        self.assertEqual(self.calls, [])
        self.run_prepare(sd=True)
        self.assertEqual(self.calls, ["https://pinned.example/doom.wad.gz"])
        self.assertFalse(self.path("chex.wad").exists())

    def test_chex_exact_archive_and_raw_patch_without_extraction(self):
        result = self.run_prepare(chex=True, sd=True)
        self.assertEqual(len(result["files"]), 3)
        self.assertEqual(self.path("chex.wad").read_bytes(), self.chex)
        self.assertEqual(self.path("chex.deh").read_bytes(), self.deh)
        self.assertEqual(self.calls[-1],
                         "https://sources.debian.org/data/main/p/prboom-plus/2%3A1/data/lumps/chexdeh.lmp")
        self.assertEqual(sorted(p.name for p in self.root.rglob("*") if p.is_file()),
                         ["chex.deh", "chex.wad", "doom1.wad"])
        self.assert_no_staging()

    def test_chex_patch_failure_preserves_all_existing_inputs(self):
        self.existing("doom1.wad", self.doom)
        self.existing("chex.wad", b"old WAD")
        self.existing("chex.deh", b"old patch")
        self.responses[next(reversed(self.responses))] = b"<html>source page</html>"
        with self.assertRaisesRegex(data.PreparationError, "SHA-256"):
            self.run_prepare(chex=True, sd=True)
        self.assertEqual(self.path("doom1.wad").read_bytes(), self.doom)
        self.assertEqual(self.path("chex.wad").read_bytes(), b"old WAD")
        self.assertEqual(self.path("chex.deh").read_bytes(), b"old patch")
        self.assertNotIn("https://pinned.example/doom.wad.gz", self.calls)
        self.assert_no_staging()

    def test_chex_archive_hash_is_checked_before_member_access(self):
        self.existing("doom1.wad", self.doom)
        self.responses["https://pinned.example/chex.zip"] = b"!" * len(self.zip)
        with self.assertRaisesRegex(data.PreparationError, "Chex archive size/SHA-256"):
            self.run_prepare(chex=True, sd=True)
        self.assertFalse(self.path("chex.wad").exists())
        self.assert_no_staging()

    def test_duplicate_wad_members_are_rejected(self):
        zipped = archive(self.chex, duplicate=True)
        self.manifest["game_data"][1]["source_archive"].update(pin(zipped))
        self.responses["https://pinned.example/chex.zip"] = zipped
        with self.assertRaisesRegex(data.PreparationError, "one unencrypted"):
            self.run_prepare(chex=True, sd=True)
        self.assertFalse(self.path("doom1.wad").exists())
        self.assert_no_staging()

    def test_symlink_destination_and_parent_are_rejected(self):
        target = self.root / "outside"
        target.write_bytes(b"preserve")
        self.path("doom1.wad").parent.mkdir(parents=True)
        self.path("doom1.wad").symlink_to(target)
        with self.assertRaisesRegex(data.PreparationError, "not a link"):
            self.run_prepare()
        self.assertEqual(target.read_bytes(), b"preserve")
        self.path("doom1.wad").unlink()
        self.path("doom1.wad").parent.rmdir()
        self.path("doom1.wad").parent.symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(data.PreparationError, "not be a link"):
            self.run_prepare()
        self.assertEqual(self.calls, [])

    def test_destination_ignore_failure_precedes_network_or_creation(self):
        def reject(path):
            raise data.PreparationError("not ignored")
        with self.assertRaisesRegex(data.PreparationError, "not ignored"):
            data.prepare(self.manifest, self.root, opener=self.open, destination_check=reject)
        self.assertEqual(self.calls, [])
        self.assertEqual(list(self.root.iterdir()), [])

    def test_concurrent_valid_file_is_preserved(self):
        original_acquire = data.acquire
        def other_writer(*args):
            payload, provenance = original_acquire(*args)
            self.existing("doom1.wad", self.doom)
            self.inode = self.path("doom1.wad").stat().st_ino
            return payload, provenance
        with patch.object(data, "acquire", side_effect=other_writer):
            result = self.run_prepare()
        self.assertEqual(result["files"][0]["status"], "reused-concurrent")
        self.assertEqual(self.path("doom1.wad").stat().st_ino, self.inode)
        self.assert_no_staging()

    def test_local_only_policy_and_paths_are_required(self):
        original = deepcopy(self.manifest)
        self.manifest["policy"]["bytes_in_git"] = True
        with self.assertRaisesRegex(data.PreparationError, "local-only"):
            self.run_prepare()
        self.manifest = original
        self.manifest["game_data"][0]["local_path"] = "local-data/../../doom1.wad"
        with self.assertRaisesRegex(data.PreparationError, "unsafe"):
            self.run_prepare()
        self.assertEqual(self.calls, [])

    def test_https_redirect_downgrade_is_rejected(self):
        with self.assertRaisesRegex(data.PreparationError, "non-HTTPS"):
            data.HttpsRedirect().redirect_request(None, None, 302, "", {}, "http://pinned.example/file")

    def test_pinned_manifest_contract_and_patch_raw_version(self):
        manifest = json.loads(data.MANIFEST.read_text())
        selected = data.selected_entries(manifest, True, True)
        self.assertEqual(data.expected(selected[0]), (4196020,
            "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"))
        self.assertEqual(data.expected(selected[1]), (12361532,
            "d8eb5277918883f490fb1a4be3c9a8588df2dbaee6dc4beb8df4929148bbffb1"))
        self.assertEqual(data.expected(selected[2]), (20367,
            "8c0345089fb227fa7f71c25a6c6e31ff5bd4bea0580f286cd74e05918d72dd40"))
        url, kind, _ = data.acquisition(selected[2])
        self.assertEqual(url, "https://sources.debian.org/data/main/p/prboom-plus/"
                         "2%3A2.5.1.5~svn4462%2Bdfsg1-1/data/lumps/chexdeh.lmp")
        self.assertEqual(kind, "raw")


if __name__ == "__main__":
    unittest.main(verbosity=2)
