#!/usr/bin/env python3
"""Arena preparation uses an archive's own policy without discovering parent Git."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPTS = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / "doom" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CONTENT = load("arena-content")
COMPACT = load("arena-compact")
DIGESTS = load("arena-trusted-digests")
PAYLOAD = b"bounded synthetic Arena input"


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="p4-arena-source-")
        self.addCleanup(self.temp.cleanup)
        self.parent = Path(self.temp.name).resolve()
        subprocess.run(["git", "init", "--quiet", "--template=", str(self.parent)], check=True)
        (self.parent / ".gitignore").write_text("*\n")
        self.root = self.parent / "source archive"
        self.root.mkdir()
        (self.root / ".gitignore").write_text("/local-data/\n")
        (self.root / ".p4-source.json").write_text(json.dumps({
            "schema": 1, "source_commit": "a" * 40}))
        self.path = self.root / "local-data/input.wad"
        self.path.parent.mkdir()
        self.path.write_bytes(PAYLOAD)

    def check(self, operation, path=None):
        path = self.path if path is None else path
        with patch.object(CONTENT, "ROOT", self.root), patch.object(COMPACT, "ROOT", self.root):
            if operation == "content":
                CONTENT.check_input_location({"local_path": str(path)})
            elif operation == "compact":
                COMPACT.install_generated(path, PAYLOAD)
            else:
                return DIGESTS.leaves(self.root, {}, {"local_path": str(path),
                    "size_bytes": len(PAYLOAD), "sha256": hashlib.sha256(PAYLOAD).hexdigest()})

    def assert_rejected(self, message=None, path=None):
        for operation in ("content", "compact", "digests"):
            with self.subTest(operation=operation):
                if message:
                    with self.assertRaisesRegex((RuntimeError, ValueError), message):
                        self.check(operation, path)
                else:
                    with self.assertRaises((RuntimeError, ValueError, subprocess.CalledProcessError)):
                        self.check(operation, path)

    def test_stamped_archive_accepts_only_its_own_ignored_inputs(self):
        for operation in ("content", "compact", "digests"):
            with self.subTest(operation=operation):
                result = self.check(operation)
                if operation == "digests":
                    self.assertEqual(result, hashlib.sha256(PAYLOAD).digest())
        output = self.root / "local-data/generated.wad"
        self.check("compact", output)
        self.assertEqual(output.read_bytes(), PAYLOAD)
        self.assertFalse((self.root / ".git").exists())

    def test_missing_or_invalid_stamp_never_inherits_parent_checkout(self):
        stamp = self.root / ".p4-source.json"
        stamp.unlink()
        self.assert_rejected("valid source archive stamp")
        stamp.write_text('{"schema": true, "source_commit": "' + "a" * 40 + '"}')
        self.assert_rejected("invalid source archive stamp")

    def test_parent_ignore_rules_cannot_admit_unignored_archive_input(self):
        (self.root / ".gitignore").write_text("")
        self.assert_rejected("not Git-ignored")
        self.assertEqual(self.path.read_bytes(), PAYLOAD)

    def test_archive_paths_cannot_escape_root(self):
        outside = self.parent / "outside.wad"
        outside.write_bytes(PAYLOAD)
        self.assert_rejected(path=outside)
        self.assertEqual(outside.read_bytes(), PAYLOAD)

    def test_checkout_uses_index_aware_ignore_checks_without_stamp(self):
        subprocess.run(["git", "init", "--quiet", "--template=", str(self.root)], check=True)
        (self.root / ".p4-source.json").unlink()
        for operation in ("content", "compact", "digests"):
            self.check(operation)
        subprocess.run(["git", "-C", str(self.root), "add", "-f", "local-data/input.wad"], check=True)
        self.assert_rejected()


if __name__ == "__main__":
    unittest.main()
