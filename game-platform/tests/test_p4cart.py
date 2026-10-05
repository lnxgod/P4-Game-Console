#!/usr/bin/env python3

import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest


GAME_PLATFORM = Path(__file__).resolve().parents[1]
SCRIPT = GAME_PLATFORM / "scripts" / "p4cart.py"
FIXTURE = GAME_PLATFORM / "tests" / "fixtures" / "minimal-cart"
SPEC = importlib.util.spec_from_file_location("p4cart", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
p4cart = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p4cart)


class P4CartTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        shutil.copytree(FIXTURE, self.source)

    def tearDown(self):
        self.temporary.cleanup()

    def manifest(self):
        return json.loads((self.source / "p4.json").read_text(encoding="utf-8"))

    def write_manifest(self, manifest):
        (self.source / "p4.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8",
        )

    def test_pack_inspect_unpack_is_deterministic(self):
        first = self.root / "first.p4cart"
        first_hash = p4cart.pack_source(self.source, first)
        manifest, payloads, inspected_hash = p4cart.inspect_cart(first)
        self.assertEqual(first_hash, inspected_hash)
        self.assertEqual(manifest["runtime"]["logical_width"], 768)
        self.assertEqual([path for path, _, _ in payloads], [
            "LICENSES/NOTICE.txt", "README.md", "main.lua",
        ])

        unpacked = self.root / "unpacked"
        p4cart.unpack_cart(first, unpacked)
        second = self.root / "second.p4cart"
        second_hash = p4cart.pack_source(unpacked, second)
        self.assertEqual(first_hash, second_hash)
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_console_canvas_is_locked_to_768_by_480(self):
        manifest = self.manifest()
        manifest["runtime"]["logical_width"] = 320
        manifest["runtime"]["logical_height"] = 200
        self.write_manifest(manifest)
        with self.assertRaisesRegex(p4cart.CartError, "logical_width"):
            p4cart.validate_source(self.source)

    def test_non_remixable_license_is_rejected(self):
        manifest = self.manifest()
        manifest["license"]["assets"] = "CC-BY-NC-ND-4.0"
        self.write_manifest(manifest)
        with self.assertRaisesRegex(p4cart.CartError, "remixable SPDX"):
            p4cart.validate_source(self.source)

    def test_corruption_and_lua_bytecode_are_rejected(self):
        cart = self.root / "game.p4cart"
        p4cart.pack_source(self.source, cart)
        damaged = bytearray(cart.read_bytes())
        damaged[-1] ^= 0x80
        cart.write_bytes(damaged)
        with self.assertRaisesRegex(p4cart.CartError, "SHA-256"):
            p4cart.inspect_cart(cart)

        (self.source / "main.lua").write_bytes(b"\x1bLua\x54\x00")
        with self.assertRaisesRegex(p4cart.CartError, "bytecode"):
            p4cart.validate_source(self.source)


if __name__ == "__main__":
    unittest.main()
