#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Provenance regressions: local P4 patches must not bless unrelated changes."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "doom/verify-source-tree.py"
spec = importlib.util.spec_from_file_location("doom_source", SCRIPT)
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="p4-doom-source-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.third = self.root / "third_party"
        self.tree = self.third / "doomgeneric"
        self.tree.mkdir(parents=True)
        (self.tree / "engine.c").write_text("patched\n")
        (self.tree / "LICENSE").write_text("notice\n")
        manifest = (f"100644 blob {source.blob_digest(b'original'+bytes([10]))}\tengine.c\n"
                    f"100644 blob {source.blob_digest(b'notice'+bytes([10]))}\tLICENSE\n").encode()
        (self.third / "upstream.git-tree").write_bytes(manifest)
        lock = {"sources": {"doomgeneric": {
            "commit": "a" * 40, "vendor_path": "third_party/doomgeneric",
            "vendor_manifest": "third_party/upstream.git-tree",
            "vendor_manifest_sha256": hashlib.sha256(manifest).hexdigest(),
        }}}
        (self.third / "source-lock.json").write_text(json.dumps(lock))
        patch = b"diff --git a/engine.c b/engine.c\n--- a/engine.c\n+++ b/engine.c\n@@ -1 +1 @@\n-original\n+patched\n"
        (self.third / "doomgeneric-p4.patch").write_bytes(patch)
        self.local = {"schema": 1, "upstream_commit": "a" * 40,
                      "patch": "third_party/doomgeneric-p4.patch",
                      "patch_sha256": hashlib.sha256(patch).hexdigest()}
        self.write_local()

    def write_local(self):
        (self.third / "doomgeneric-p4.json").write_text(json.dumps(self.local))

    def test_exact_patch_reverses_to_upstream_without_mutating_source(self):
        self.assertEqual(source.check(self.root, self.tree), 2)
        self.assertEqual((self.tree / "engine.c").read_text(), "patched\n")

    def test_pristine_import_applies_the_same_patch(self):
        (self.tree / "engine.c").write_text("original\n")
        self.assertEqual(source.check(self.root, self.tree, apply=True), 2)
        self.assertEqual(source.check(self.root, self.tree), 2)

    def test_extra_engine_edit_rejected(self):
        (self.tree / "engine.c").write_text("patched\nunreviewed\n")
        with self.assertRaises(ValueError):
            source.check(self.root, self.tree)

    def test_untouched_upstream_edit_rejected(self):
        (self.tree / "LICENSE").write_text("changed notice\n")
        with self.assertRaisesRegex(ValueError, "modified vendored"):
            source.check(self.root, self.tree)

    def test_missing_or_extra_file_rejected(self):
        (self.tree / "extra.c").write_text("extra\n")
        with self.assertRaisesRegex(ValueError, "unmanifested"):
            source.check(self.root, self.tree)
        (self.tree / "extra.c").unlink()
        (self.tree / "LICENSE").unlink()
        with self.assertRaisesRegex(ValueError, "missing"):
            source.check(self.root, self.tree)

    def test_patch_tampering_rejected(self):
        with (self.third / "doomgeneric-p4.patch").open("ab") as patch:
            patch.write(b"tampered")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            source.check(self.root, self.tree)

    def test_upstream_manifest_tampering_rejected(self):
        (self.third / "upstream.git-tree").write_text("")
        with self.assertRaisesRegex(ValueError, "manifest hash"):
            source.check(self.root, self.tree)

    def test_wrong_upstream_binding_rejected(self):
        self.local["upstream_commit"] = "b" * 40
        self.write_local()
        with self.assertRaisesRegex(ValueError, "pinned upstream"):
            source.check(self.root, self.tree)

    def test_symlink_rejected(self):
        (self.tree / "LICENSE").unlink()
        (self.tree / "LICENSE").symlink_to(self.third / "source-lock.json")
        with self.assertRaisesRegex(ValueError, "symlink"):
            source.check(self.root, self.tree)

    def test_patch_path_escape_rejected(self):
        self.local["patch"] = "../outside.patch"
        self.write_local()
        with self.assertRaisesRegex(ValueError, "unsafe source path"):
            source.check(self.root, self.tree)


if __name__ == "__main__":
    unittest.main()
