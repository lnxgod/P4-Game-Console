#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

from __future__ import annotations

import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "scripts" / "p4-content.py"
PACKER_PATH = ROOT / "game-platform" / "scripts" / "p4cart.py"


def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


content = load(TOOL_PATH, "p4_content_tool")
packer = load(PACKER_PATH, "p4cart_packer")


class ContentInstallTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="p4-content-test-")
        self.root = Path(self.temporary.name)
        self.sd = self.root / "sd"
        self.sd.mkdir()
        self.cart = self.root / "bounce.p4cart"
        packer.pack_source(ROOT / "game-platform" / "templates" / "bounce-lab", self.cart)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_cart_stages_validates_and_activates(self) -> None:
        destination, digest = content.install_cart(self.cart, self.sd)
        self.assertEqual(
            destination,
            self.sd.resolve() / "P4" / "GAMES" / "bounce.p4cart",
        )
        self.assertTrue(destination.is_file())
        self.assertEqual(content.validate_p4cart(destination), digest)
        self.assertEqual(list((self.sd / "P4" / "INBOX").iterdir()), [])

    def test_existing_destination_requires_replace(self) -> None:
        content.install_cart(self.cart, self.sd)
        with self.assertRaisesRegex(content.ContentError, "--replace"):
            content.install_cart(self.cart, self.sd)
        destination, _digest = content.install_cart(self.cart, self.sd, replace=True)
        self.assertTrue(destination.is_file())

    def test_rejects_bad_cart_before_activation(self) -> None:
        bad = self.root / "bad.p4cart"
        shutil.copyfile(self.cart, bad)
        bytes_value = bytearray(bad.read_bytes())
        bytes_value[-1] ^= 1
        bad.write_bytes(bytes_value)
        with self.assertRaisesRegex(content.ContentError, "validation failed"):
            content.install_cart(bad, self.sd)
        self.assertFalse((self.sd / "P4" / "GAMES" / "bad.p4cart").exists())

    def test_rejects_wrong_quake_data(self) -> None:
        wrong = self.root / "PAK0.PAK"
        wrong.write_bytes(b"not quake")
        with self.assertRaisesRegex(content.ContentError, "expected"):
            content.install_quake(wrong, self.sd)
        self.assertFalse((self.sd / "GAMES" / "QUAKE" / "ID1" / "PAK0.PAK").exists())

    def test_rejects_symlinked_sd_directory(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        (self.sd / "P4").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(content.ContentError, "real directory"):
            content.install_cart(self.cart, self.sd)
        self.assertEqual(list(outside.iterdir()), [])

    def test_replace_refuses_non_file_destination(self) -> None:
        destination = self.sd / "P4" / "GAMES" / "bounce.p4cart"
        destination.mkdir(parents=True)
        with self.assertRaisesRegex(content.ContentError, "regular file"):
            content.install_cart(self.cart, self.sd, replace=True)


if __name__ == "__main__":
    unittest.main()
