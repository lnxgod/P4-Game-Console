#!/usr/bin/env python3
"""Actual host runners select and bind the authoritative three WAD inputs."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
class HostInputs(unittest.TestCase):
    def modules(self):
        for name in ('late-join','rejoin'):
            spec=importlib.util.spec_from_file_location('arena_'+name.replace('-','_'),ROOT/'scripts/doom'/('test-arena-'+name+'.py'))
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            yield name,module
    def test_selected_contract_drives_validation_and_actual_process_command(self):
        for name,module in self.modules():
            with self.subTest(runner=name),tempfile.TemporaryDirectory() as temporary:
                root=Path(temporary);(root/'third_party').mkdir()
                files=[]
                for symbol in ('BASE','PWAD','DWANGO'):
                    relative='inputs/'+symbol+'.wad';path=root/relative;path.parent.mkdir(exist_ok=True)
                    content=('new-'+symbol).encode();path.write_bytes(content)
                    files.append(dict(symbol=symbol,local_path=relative,size_bytes=len(content),sha256=hashlib.sha256(content).hexdigest(),repository_asset=True))
                (root/'third_party/game-data.json').write_text(json.dumps({'game_changers_ai_bundle':{'files':files}}))
                with patch.object(module,'OVERRIDE',root),patch.object(module,'OUT',root):
                    module.validate_wads()
                    expected=[root/f['local_path'] for f in files]
                    self.assertEqual([p for _,p in module.selected_wads()],expected)
                    def execute(command,**kwargs):
                        self.assertEqual(command[1:6],['-iwad',expected[0],'-file',expected[1],expected[2]])
                        kwargs['stdout'].write('PROCESS PASS\n')
                    with patch.object(module,'run',side_effect=execute) as run:
                        module.process('host');run.assert_called_once()
                    expected[0].write_bytes(b'changed')
                    with self.assertRaisesRegex(RuntimeError,'pinned WAD mismatch'):
                        module.validate_wads()
                    expected[0].unlink();expected[0].symlink_to(expected[1])
                    with self.assertRaisesRegex(RuntimeError,'pinned WAD mismatch'):
                        module.validate_wads()

if __name__=='__main__':unittest.main()
