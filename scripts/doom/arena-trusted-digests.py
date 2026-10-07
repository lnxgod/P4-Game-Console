#!/usr/bin/env python3
"""Generate firmware-owned SHA-256 metadata, never WAD payloads.

Every generation verifies each exact authoritative whole WAD in the same pass
that produces its 4 KiB leaves. Runtime verified-on-read preparation is not a
claim that every block on the current card has already been consumed/verified.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import types
import json
import os
import re
import stat
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
BLOCK=4096
FIXED_INPUTS=("local-data/doom/arena-compact-v1/ARENA2.WAD", "game-data/pure-hades/v0.6/PUREHADES.WAD", "local-data/doom/arena-inbox/dwango5/DWANGO5.WAD")
SYMBOLS=("BASE","PWAD","DWANGO")
HEADER="components/platform_game_storage/include/platform/doom_arena_content.h"

def require(condition,message):
    if not condition:raise ValueError(message)

def sha(data):return hashlib.sha256(data).hexdigest()

def contract(root,inputs):
    metadata=json.loads(inputs['manifest'][1]);data=metadata['game_changers_ai_bundle'];files=data['files']
    require(isinstance(files,list) and len(files)==17,'Expected exact 17-file Arena contract')
    require(tuple(f['symbol'] for f in files[:3])==SYMBOLS,'Wrong WAD symbols/order')
    require(tuple(f['local_path'] for f in files[:3])==FIXED_INPUTS,'Wrong fixed WAD input paths')
    require([s['id'] for s in data['selections']]==list(range(1,30)),'Wrong Arena selections')
    require(len({f['usb_kind'] for f in files})==len(files),'Duplicate content kind')
    for f in files:
        require(type(f['usb_kind']) is int and 4<f['usb_kind']<256,'Invalid content kind')
        require(type(f['size_bytes']) is int and 0<f['size_bytes']<=64*1024*1024,'Invalid source size')
        require(isinstance(f['sha256'],str) and re.fullmatch('[0-9a-f]{64}',f['sha256']) is not None,'Invalid whole SHA')
        require(isinstance(f['local_path'],str) and re.fullmatch('[A-Za-z0-9_./-]+',f['local_path']) is not None,'Invalid local path')
        require(not Path(f['local_path']).is_absolute() and '..' not in Path(f['local_path']).parts,'Escaping local path')
        require(isinstance(f['filename'],str) and re.fullmatch('[A-Za-z0-9_.-]+',f['filename']) is not None,'Invalid device filename')
        require(isinstance(f['directory'],str) and re.fullmatch('(?:/[A-Za-z0-9_.-]+)*',f['directory']) is not None,'Invalid device directory')
    identity=sha(b'P4-GCA-ARENAS-1\0'+b''.join(bytes.fromhex(f['sha256']) for f in files[:3]))
    require(identity==data['content_sha256'],'Arena content identity mismatch')
    helper=types.ModuleType('p4_arena_content');helper.__file__=str(inputs['content_generator'][0])
    exec(compile(inputs['content_generator'][1],helper.__file__,'exec'),helper.__dict__)
    original=next(g for g in metadata['game_data'] if g['id']=='freedoom-0.13.0-phase-2')
    require(inputs['content_header'][1].decode()==helper.header_text(data,original=original),'Compiled content header does not match manifest')
    return metadata,data

def leaves(root,metadata,entry):
    path=root/entry['local_path']
    require(not path.is_symlink() and path.resolve().is_relative_to(root.resolve()),'Symlink or escaping WAD input')
    if entry.get('repository_asset'):
        require(entry['local_path'] in metadata['policy']['repository_asset_exceptions'],'Unlisted repository asset')
    else:
        if (root/'.git').exists() or (root/'.git').is_symlink():
            subprocess.run(['git','check-ignore','-q',str(path)],cwd=root,check=True)
        else:
            spec=importlib.util.spec_from_file_location('arena_data_policy',Path(__file__).resolve().parents[1]/'prepare-game-data.py')
            policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)
            policy.require_ignored(path,root=root)
    flags=os.O_RDONLY|os.O_NONBLOCK|os.O_NOFOLLOW
    fd=os.open(path,flags)
    try:
        before=os.fstat(fd)
        require(stat.S_ISREG(before.st_mode) and before.st_size==entry['size_bytes'],'Wrong regular WAD input length')
        whole=hashlib.sha256();table=bytearray();consumed=0
        while consumed<entry['size_bytes']:
            need=min(BLOCK,entry['size_bytes']-consumed);block=bytearray()
            while len(block)<need:
                chunk=os.read(fd,need-len(block))
                require(bool(chunk),'Short WAD input')
                block.extend(chunk)
            whole.update(block);table.extend(hashlib.sha256(block).digest());consumed+=need
        require(os.read(fd,1)==b'','Extended WAD input')
        after=os.fstat(fd)
        require((before.st_dev,before.st_ino,before.st_size,before.st_mtime_ns,before.st_ctime_ns)==(after.st_dev,after.st_ino,after.st_size,after.st_mtime_ns,after.st_ctime_ns),'WAD changed during verification')
        require(consumed==entry['size_bytes'] and whole.hexdigest()==entry['sha256'],'Whole WAD identity mismatch')
        return bytes(table)
    finally:os.close(fd)

def initializer(data):return '{'+','.join(f'0x{b:02x}' for b in data)+'}'

def generate(root,output):
    root=Path(root).resolve();output=Path(output)
    # Capture the exact contract and helper bytes used below, rather than
    # hashing a possibly different revision after the WAD scan.
    paths={'generator':Path(__file__),
           'content_generator':Path(__file__).with_name('arena-content.py'),
           'manifest':root/'third_party/game-data.json','content_header':root/HEADER}
    inputs={name:(path,path.read_bytes()) for name,path in paths.items()}
    metadata,data=contract(root,inputs)
    tables=[leaves(root,metadata,f) for f in data['files'][:3]]
    records=[];lines=['// SPDX-License-Identifier: MIT','/* Generated metadata only; exact whole input identities verified by arena-trusted-digests.py. */','#include "trusted_wad_table.h"','']
    for i,(f,table) in enumerate(zip(data['files'][:3],tables)):
        lines.append(f'static const uint8_t s_arena_digests_{i}[{len(table)}] = {{')
        lines.extend('    '+','.join(f'0x{b:02x}' for b in table[n:n+16])+',' for n in range(0,len(table),16));lines.append('};')
        records.append({'file_index':i,'symbol':f['symbol'],'path':'/game-data'+f['directory']+'/'+f['filename'],'local_path':f['local_path'],'size_bytes':f['size_bytes'],'whole_sha256':f['sha256'],'block_bytes':BLOCK,'block_count':len(table)//32,'tail_bytes':f['size_bytes']%BLOCK or BLOCK,'table_bytes':len(table),'table_sha256':sha(table)})
    lines.append('const p4_trusted_wad_table_t p4_arena_trusted_wad_tables[3] = {')
    for i,r in enumerate(records):
        lines.extend(['    {',f'        .schema=1, .file_index={i}, .block_bytes={BLOCK},',f'        .symbol={json.dumps(r["symbol"])}, .path={json.dumps(r["path"])},',f'        .size={r["size_bytes"]}, .block_count={r["block_count"]}, .digest_bytes={r["table_bytes"]},',f'        .content_sha256={initializer(bytes.fromhex(data["content_sha256"]))},',f'        .whole_sha256={initializer(bytes.fromhex(r["whole_sha256"]))},',f'        .table_sha256={initializer(bytes.fromhex(r["table_sha256"]))},',f'        .digests=s_arena_digests_{i},','    },'])
    lines.extend(['};',''])
    source='\n'.join(lines).encode()
    receipt={'schema':1,**{name+'_sha256':sha(payload) for name,(_,payload) in inputs.items()},'content_sha256':data['content_sha256'],'block_bytes':BLOCK,'table_bytes':sum(map(len,tables)),'tables':records,'generated_source_sha256':sha(source),'payload_embedded':False,'verification':'exact-whole-input-and-unpadded-leaves-in-one-pass'}
    # Publish only after every authoritative input passes. A build must require
    # this command's successful exit; a stale prior output is never a fallback.
    for name,(path,payload) in inputs.items():
        require(path.read_bytes()==payload,f'{name} changed during generation')
    output.mkdir(parents=True,exist_ok=True)
    for name,payload in [('arena_trusted_digests.c',source),('arena_trusted_digests.json',(json.dumps(receipt,indent=2,sort_keys=True)+'\n').encode())]:
        with tempfile.NamedTemporaryFile(dir=output,delete=False) as f:temporary=Path(f.name);f.write(payload)
        try:os.replace(temporary,output/name)
        finally:temporary.unlink(missing_ok=True)
    return receipt

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--output-dir',type=Path,required=True);args=p.parse_args()
    receipt=generate(args.root,args.output_dir)
    print(f'P4_ARENA_TRUSTED_DIGESTS PASS files=3 bytes={receipt["table_bytes"]} content={receipt["content_sha256"]}')
if __name__=='__main__':main()
