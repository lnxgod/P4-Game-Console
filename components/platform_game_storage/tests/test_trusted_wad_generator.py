#!/usr/bin/env python3
"""Metadata generation rejects wrong authoritative inputs before publishing."""
from pathlib import Path
import copy, hashlib, importlib.util, json, os, shutil, tempfile, unittest
ROOT=Path(__file__).resolve().parents[3]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
GEN=module('trusted_generator',ROOT/'scripts/doom/arena-trusted-digests.py')
ARENA=module('arena_content',ROOT/'scripts/doom/arena-content.py')
class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name);self.out=self.root/'out'
        scripts=self.root/'scripts/doom';scripts.mkdir(parents=True)
        for name in ('arena-content.py','arena-trusted-digests.py'):shutil.copyfile(ROOT/'scripts/doom'/name,scripts/name)
        self.generator=module('isolated_trusted_generator',scripts/'arena-trusted-digests.py')
        original=json.loads((ROOT/'third_party/game-data.json').read_text())
        self.meta=copy.deepcopy(original);data=self.meta['game_changers_ai_bundle']
        for i,f in enumerate(data['files']):
            payload=bytes((j*13+i)%256 for j in range(4096+i*7+3 if i<3 else 3+i))
            p=self.root/f['local_path'];p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(payload)
            f['size_bytes']=len(payload);f['sha256']=hashlib.sha256(payload).hexdigest();f['repository_asset']=True
        self.meta['policy']['repository_asset_exceptions']=[f['local_path'] for f in data['files']]
        data['content_sha256']=hashlib.sha256(b'P4-GCA-ARENAS-1\0'+b''.join(bytes.fromhex(f['sha256']) for f in data['files'][:3])).hexdigest()
        self.write_metadata()
    def write_metadata(self):
        p=self.root/'third_party/game-data.json';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(self.meta))
        p=self.root/'components/platform_game_storage/include/platform/doom_arena_content.h';p.parent.mkdir(parents=True,exist_ok=True)
        p.write_text(ARENA.header_text(self.meta['game_changers_ai_bundle']))
    def generate(self):return self.generator.generate(self.root,self.out)
    def test_good_deterministic_exact_tails(self):
        receipt=self.generate();before={p.name:p.read_bytes() for p in self.out.iterdir()}
        self.assertEqual(receipt['table_bytes'],192)
        for f,record in zip(self.meta['game_changers_ai_bundle']['files'][:3],receipt['tables']):
            b=(self.root/f['local_path']).read_bytes();leaves=b''.join(hashlib.sha256(b[i:i+4096]).digest() for i in range(0,len(b),4096))
            self.assertEqual(record['table_sha256'],hashlib.sha256(leaves).hexdigest());self.assertEqual(record['tail_bytes'],len(b)%4096)
        self.generate();self.assertEqual(before,{p.name:p.read_bytes() for p in self.out.iterdir()})
        self.assertNotIn((self.root/self.meta['game_changers_ai_bundle']['files'][0]['local_path']).read_bytes(),before['arena_trusted_digests.c'])
    def test_bad_inputs_preserve_no_success(self):
        entries=self.meta['game_changers_ai_bundle']['files']
        for kind in ('corrupt','truncate','extend','symlink','directory','fifo'):
            with self.subTest(kind=kind):
                p=self.root/entries[0]['local_path'];original=p.read_bytes();p.unlink()
                if kind=='symlink':p.symlink_to(self.root/entries[1]['local_path'])
                elif kind=='directory':p.mkdir()
                elif kind=='fifo':os.mkfifo(p)
                else:p.write_bytes(original[:-1] if kind=='truncate' else original+b'x' if kind=='extend' else b'x'+original[1:])
                with self.assertRaises((ValueError,OSError)):self.generate()
                self.assertFalse(self.out.exists())
                if p.is_dir() and not p.is_symlink():p.rmdir()
                else:p.unlink()
                p.write_bytes(original)
    def test_stale_output_never_turns_failure_into_success(self):
        self.generate();before={p.name:p.read_bytes() for p in self.out.iterdir()}
        p=self.root/self.meta['game_changers_ai_bundle']['files'][2]['local_path'];p.write_bytes(b'bad')
        with self.assertRaises(ValueError):self.generate()
        self.assertEqual(before,{p.name:p.read_bytes() for p in self.out.iterdir()})
    def test_contract_rejects_identity_order_header_and_path(self):
        baseline=copy.deepcopy(self.meta)
        for kind in ('identity','order','header','path','count','size','sha','selection'):
            with self.subTest(kind=kind):
                self.meta=copy.deepcopy(baseline);d=self.meta['game_changers_ai_bundle']
                if kind=='identity':d['content_sha256']='00'*32
                elif kind=='order':d['files'][0]['symbol']='OTHER'
                elif kind=='path':d['files'][0]['local_path']='../outside.wad'
                elif kind=='count':d['files'].pop()
                elif kind=='size':d['files'][0]['size_bytes']=True
                elif kind=='sha':d['files'][0]['sha256']='ab'
                elif kind=='selection':d['selections'][0]['id']=30
                (self.root/'third_party/game-data.json').write_text(json.dumps(self.meta))
                if kind=='header':(self.root/'components/platform_game_storage/include/platform/doom_arena_content.h').write_text('stale')
                with self.assertRaises(ValueError):self.generate()
                self.assertFalse(self.out.exists())
                self.meta=copy.deepcopy(baseline);self.write_metadata()
    def test_contract_and_tool_drift_rejected_before_publication(self):
        paths=(self.root/'third_party/game-data.json',
               self.root/'components/platform_game_storage/include/platform/doom_arena_content.h',
               self.root/'scripts/doom/arena-content.py',self.root/'scripts/doom/arena-trusted-digests.py')
        actual=self.generator.leaves
        for path in paths:
            with self.subTest(path=path.name):
                original=path.read_bytes();changed=False
                def drift(root,metadata,entry):
                    nonlocal changed
                    result=actual(root,metadata,entry)
                    if not changed:path.write_bytes(original+b'\n');changed=True
                    return result
                self.generator.leaves=drift
                try:
                    with self.assertRaisesRegex(ValueError,'changed during generation'):self.generate()
                    self.assertFalse(self.out.exists())
                finally:path.write_bytes(original);self.generator.leaves=actual
if __name__=='__main__':unittest.main()
