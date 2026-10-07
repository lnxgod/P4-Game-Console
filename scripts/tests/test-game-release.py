#!/usr/bin/env python3
"""Release selection stays fail-closed; opt-in does not change game identity."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

registry = module('registry', ROOT / 'scripts/generate-game-registry.py')
package = module('package', ROOT / 'scripts/build-game-package.py')
fixtures = module('fixtures', ROOT / 'scripts/tests/test-game-registry.py')

class ReleaseTests(unittest.TestCase):
    def test_current_selection_and_retained_tide_maze(self):
        standard = {m['component'] for m in registry.discover(ROOT / 'games')}
        dev = {m['component'] for m in registry.discover(ROOT / 'games', dev_only=True)}
        self.assertEqual(dev, {'byte_buddy', 'lord', 'skyline_leap'})
        self.assertFalse(standard & dev)
        self.assertTrue({'blast_circuit', 'tide_maze'} <= standard)

    def test_enabled_wip_and_disabled_normal_folder_need_opt_in(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for i, (name, folder, enabled) in enumerate((
                    ('release', 'GAMES/ARCADE', True),
                    ('wip', 'GAMES/WIP', True),
                    ('draft', 'GAMES/ARCADE', False))):
                data = fixtures.manifest(name, 100 + i)
                data.update(folder=folder, enabled=enabled)
                fixtures.write_manifest(root, name, data)
            self.assertEqual([m['component'] for m in registry.discover(root)], ['release'])
            dev = registry.discover(root, dev_only=True)
            self.assertEqual({m['component'] for m in dev}, {'wip', 'draft'})
            self.assertIn(' DEV)', registry.cmake_text(dev, dev_only=True))
            for name in ('wip', 'draft'):
                path = root / name / 'game.json'
                with self.assertRaisesRegex(package.PackageError, '--allow-dev'):
                    package.load_manifest(path)
                self.assertEqual(package.load_manifest(path, allow_dev=True)['id'],
                                 json.loads(path.read_text())['id'])

    def test_hidden_id_stays_reserved_against_other_games(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, enabled in (('release', True), ('draft', False)):
                data = fixtures.manifest(name, 100)
                data['enabled'] = enabled
                fixtures.write_manifest(root, name, data)
            with self.assertRaisesRegex(registry.ManifestError, 'duplicate launcher_id'):
                registry.discover(root)

if __name__ == '__main__':
    unittest.main()
