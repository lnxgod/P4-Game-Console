#!/usr/bin/env python3
"""Content acquisition must preserve local files and only admit pinned bytes."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'doom/arena-content.py'
spec = importlib.util.spec_from_file_location('arena_content', SCRIPT)
arena = importlib.util.module_from_spec(spec)
spec.loader.exec_module(arena)


class FetchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.addCleanup(patch.stopall)
        patch.object(arena, 'ROOT', self.root).start()
        patch.object(arena, 'check_input_location').start()
        self.files = []
        self.archives = {}
        for symbol, filename in [('BASE', 'FREEDOOM2.WAD'), ('DWANGO', 'DWANGO5.WAD'),
                                  ('DWANGO_NOTICE', 'DWANGO5.TXT'), ('DWANGO_DIZ', 'FILE_ID.DIZ')]:
            content = symbol.encode()
            self.files.append(dict(symbol=symbol, filename=filename, local_path='inputs/'+filename,
                                   size_bytes=len(content), sha256=hashlib.sha256(content).hexdigest()))
        def archive(entries):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, 'w') as z:
                for f in entries:
                    name = 'freedoom-0.13.0/freedoom2.wad' if f['symbol']=='BASE' else f['filename']
                    z.writestr(name, f['symbol'].encode())
                z.writestr('../../escape.txt', b'never extract this')
            return stream.getvalue()
        base, dwango = archive(self.files[:1]), archive(self.files[1:])
        self.archives = {'https://fixture/base':base, 'https://fixture/dwango':dwango}
        self.data = {'files':self.files, 'dwango5_source':{'url':'https://fixture/dwango',
                     'archive_size_bytes':len(dwango), 'archive_sha256':hashlib.sha256(dwango).hexdigest()}}
        metadata = {'game_data':[{'id':'freedoom-0.13.0-phase-2','source_archive':{
                    'url':'https://fixture/base','size_bytes':len(base),'sha256':hashlib.sha256(base).hexdigest()}}]}
        (self.root/'third_party').mkdir()
        (self.root/'third_party/game-data.json').write_text(json.dumps(metadata))
        self.network = patch.object(arena.urllib.request, 'urlopen',
            side_effect=lambda request, timeout:io.BytesIO(self.archives[request.full_url])).start()

    def compact_bundle(self):
        metadata_path = self.root/'third_party/game-data.json'
        metadata = json.loads(metadata_path.read_text())
        metadata['game_data'][0].update(self.files[0])
        metadata_path.write_text(json.dumps(metadata))
        payload = b'compact derivative'
        entry = dict(self.files[0], filename='ARENA2.WAD', local_path='inputs/ARENA2.WAD',
                     size_bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
        self.data = dict(self.data, base_game_data_id='p4-arena-compact-v1',
                         files=[entry, *self.files[1:]])
        return entry, payload

    def test_derivative_fetch_preserves_original_and_derives_after_verified_inputs(self):
        entry, payload = self.compact_bundle()
        def derive(data):
            self.assertEqual((self.root/self.files[0]['local_path']).read_bytes(), b'BASE')
            self.assertTrue(all((self.root/f['local_path']).is_file() for f in self.files))
            (self.root/entry['local_path']).write_bytes(payload)
        with patch.object(arena, 'derive_compact_base', side_effect=derive) as generated:
            arena.fetch(self.data)
            generated.assert_called_once_with(self.data)
        self.assertEqual((self.root/entry['local_path']).read_bytes(), payload)
        self.assertEqual((self.root/self.files[0]['local_path']).read_bytes(), b'BASE')
        self.assertEqual(self.network.call_count, 2)

    def test_conflicting_original_stops_derivative_before_download_or_generation(self):
        self.compact_bundle()
        path = self.root/self.files[0]['local_path'];path.parent.mkdir();path.write_bytes(b'wrong')
        with patch.object(arena, 'derive_compact_base') as generated:
            with self.assertRaisesRegex(ValueError, 'Conflicting existing content'):
                arena.fetch(self.data)
            generated.assert_not_called()
        self.network.assert_not_called()
        self.assertEqual(path.read_bytes(), b'wrong')

    def test_conflicting_derivative_stops_before_original_download(self):
        entry, _ = self.compact_bundle()
        path = self.root/entry['local_path'];path.parent.mkdir();path.write_bytes(b'wrong')
        with patch.object(arena, 'derive_compact_base') as generated:
            with self.assertRaisesRegex(ValueError, 'Conflicting existing content'):
                arena.fetch(self.data)
            generated.assert_not_called()
        self.network.assert_not_called()

    def test_only_verified_named_members_and_idempotent_fetch(self):
        arena.fetch(self.data)
        self.assertEqual(self.network.call_count, 2)
        self.assertEqual(sorted(p.name for p in (self.root/'inputs').iterdir()),
                         sorted(f['filename'] for f in self.files))
        arena.fetch(self.data)
        self.assertEqual(self.network.call_count, 2)
        self.assertFalse((self.root.parent/'escape.txt').exists())

    def test_conflict_is_preserved_before_network(self):
        target=self.root/self.files[0]['local_path'];target.parent.mkdir();target.write_bytes(b'old data')
        with self.assertRaisesRegex(ValueError, 'Conflicting existing content'):
            arena.fetch(self.data)
        self.assertEqual(target.read_bytes(), b'old data')
        self.network.assert_not_called()

    def test_bad_archive_never_installed(self):
        self.archives['https://fixture/base']=b'bad'
        with self.assertRaisesRegex(ValueError, 'Archive identity mismatch'):
            arena.fetch(self.data)
        self.assertFalse((self.root/'inputs').exists())

    def test_bad_member_never_installed(self):
        self.files[0]['sha256']='00'*32
        with self.assertRaisesRegex(ValueError, 'Archive member hash mismatch'):
            arena.fetch(self.data)
        self.assertFalse((self.root/'inputs').exists())

    def test_symlink_refused_before_network(self):
        target=self.root/self.files[0]['local_path'];target.parent.mkdir();target.symlink_to(self.root/'missing')
        with self.assertRaisesRegex(ValueError, 'Conflicting existing content'):
            arena.fetch(self.data)
        self.network.assert_not_called()


if __name__=='__main__':
    unittest.main()
