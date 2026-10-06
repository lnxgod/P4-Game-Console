#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from __future__ import annotations
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
def load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
content = load(ROOT / 'scripts/p4-content.py', 'p4_content_tool')

class ContentInstallTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='p4-content-test-')
        self.root = Path(self.temporary.name)
        self.sd = self.root / 'sd'; self.sd.mkdir()
        self.cart = self.root / 'fixture.p4cart'
        self.cart.write_bytes(b'P4CART1\0removed format marker')
    def tearDown(self):
        self.temporary.cleanup()
    def test_retired_cart_rejected_before_any_directory_or_staging_write(self):
        for name in (None, 'OTHER.P4CART', 'renamed.txt'):
            with self.subTest(name=name), self.assertRaisesRegex(content.ContentError, 'retired'):
                content.install_cart(self.cart, self.sd, name=name, replace=True)
        self.assertEqual(list(self.sd.iterdir()), [])
    def test_retired_cart_preserves_existing_user_content_even_with_replace(self):
        destination = self.sd / 'P4/GAMES/fixture.p4cart'
        destination.parent.mkdir(parents=True)
        destination.write_bytes(b'user original cartridge')
        with self.assertRaisesRegex(content.ContentError, 'retired'):
            content.install_cart(self.cart, self.sd, replace=True)
        self.assertEqual(destination.read_bytes(), b'user original cartridge')
        self.assertFalse((self.sd / 'P4/INBOX').exists())
    def test_legacy_cli_rejects_without_mutation(self):
        output = io.StringIO()
        with contextlib.redirect_stderr(output):
            result = content.main(['cart', str(self.cart), '--sd-root', str(self.sd), '--replace'])
        self.assertEqual(result, 2)
        self.assertIn('retired', output.getvalue())
        self.assertEqual(list(self.sd.iterdir()), [])
    def test_rejects_wrong_quake_data(self):
        wrong = self.root / 'PAK0.PAK'; wrong.write_bytes(b'not quake')
        with self.assertRaisesRegex(content.ContentError, 'expected'):
            content.install_quake(wrong, self.sd)
        self.assertEqual(list(self.sd.iterdir()), [])
    def install_fixture(self, replace=False):
        source = self.root / 'native-resource.bin'; source.write_bytes(b'original native resource')
        return content._install(source, self.sd, Path('GAMES/FIXTURE.P4R'), 'FIXTURE.P4R',
                                lambda path: content._sha256_file(path)[1], replace)
    def test_atomic_content_copy_still_validates_and_requires_explicit_replace(self):
        destination, digest = self.install_fixture()
        self.assertEqual(hashlib.sha256(destination.read_bytes()).hexdigest(), digest)
        self.assertEqual(list((self.sd / 'P4/INBOX').iterdir()), [])
        with self.assertRaisesRegex(content.ContentError, '--replace'):
            self.install_fixture()
        self.assertEqual(self.install_fixture(replace=True)[0], destination)
    def test_atomic_copy_rejects_symlinked_directory(self):
        outside = self.root / 'outside'; outside.mkdir()
        (self.sd / 'P4').symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(content.ContentError, 'real directory'):
            self.install_fixture()
        self.assertEqual(list(outside.iterdir()), [])
    def test_atomic_replace_refuses_non_file_destination(self):
        (self.sd / 'GAMES/FIXTURE.P4R').mkdir(parents=True)
        with self.assertRaisesRegex(content.ContentError, 'regular file'):
            self.install_fixture(replace=True)

if __name__ == '__main__': unittest.main()
