#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
from __future__ import annotations
import json
import pathlib
import subprocess
import tempfile
ROOT=pathlib.Path(__file__).resolve().parents[2]
REGISTRY=ROOT/'scripts/p4cart_seed_registry.py'
def run(*arguments):
    return subprocess.run(['python3',str(REGISTRY),*arguments],cwd=ROOT,check=False,text=True,
                          stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def main():
    actual=run('--check');assert actual.returncode==0,actual.stderr
    assert json.loads(actual.stdout)['seed_carts']==[]
    with tempfile.TemporaryDirectory() as temporary:
        root=pathlib.Path(temporary);metadata=root/'metadata.json';generated=root/'seeds.cmake'
        generated.write_text('existing user file')
        for value in ({},{'legacy_p4cart':{'seed_carts':[]}}):
            metadata.write_text(json.dumps(value))
            checked=run('--metadata',str(metadata),'--check');assert checked.returncode==0,checked.stderr
            for args in (('--output-cmake',str(generated)),('--templates-root',str(root))):
                rejected=run('--metadata',str(metadata),'--check',*args)
                assert rejected.returncode!=0 and 'generator removed' in rejected.stderr
                assert generated.read_text()=='existing user file'
        for legacy in ({'seed_carts':['P4/GAMES/OLD.P4CART']},{'seed_carts':['../OLD.P4CART']},
                       {'seed_carts':[],'seed_cart':'P4/GAMES/OLD.P4CART'}, {'seed_carts':'bad'},False):
            metadata.write_text(json.dumps({'legacy_p4cart':legacy}))
            assert run('--metadata',str(metadata),'--check').returncode!=0
        assert generated.read_text()=='existing user file'
    print('Native-only seed guard tests passed; removed generator options cannot write output')
if __name__=='__main__':main()
