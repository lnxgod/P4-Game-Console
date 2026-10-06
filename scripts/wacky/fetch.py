#!/usr/bin/env python3
"""Fetch pinned recreation source and original shareware; never run DOS EXEs."""
from pathlib import Path
import hashlib,io,subprocess,urllib.request,zipfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'.tools/wacky-wheels-upstream'
REV='7dd510096c58ee84c36970a11c4f57b4a9c2a4cf'
URL='https://git.retrodamage.com/jmarshall/wacky-wheels.git'
DATA=ROOT/'.tools/wacky-shareware'
ZIP_URL='https://www.classicdosgames.com/files/games/beavis-soft/1wacky.zip'
ZIP_SHA='3c3c35bb696194b420c5156ac9641819213f01812801f5c1683817c8bb70f4a3'
DAT_SHA='ae36b4204f1b44fbdb26294a45495e46616feffcb18e67d48de9324d76bed85f'
if not SOURCE.exists():
    SOURCE.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['git','-c','core.hooksPath=/dev/null','clone','--no-checkout',URL,str(SOURCE)],check=True)
    subprocess.run(['git','-C',str(SOURCE),'sparse-checkout','set','--no-cone','/src/','/README.md'],check=True)
    subprocess.run(['git','-C',str(SOURCE),'checkout','--detach',REV],check=True)
if subprocess.check_output(['git','-C',str(SOURCE),'rev-parse','HEAD'],text=True).strip()!=REV:
    raise SystemExit('Existing source has a different revision; it was left untouched')
DATA.mkdir(parents=True,exist_ok=True)
archive=DATA/'1wacky.zip'
if not archive.exists():
    with urllib.request.urlopen(ZIP_URL,timeout=30) as response:
        blob=response.read(1655317)
    if len(blob)!=1655316 or hashlib.sha256(blob).hexdigest()!=ZIP_SHA:
        raise SystemExit('Shareware download hash/size mismatch')
    archive.write_bytes(blob)
blob=archive.read_bytes()
if hashlib.sha256(blob).hexdigest()!=ZIP_SHA:raise SystemExit('Shareware archive hash mismatch')
outer=zipfile.ZipFile(io.BytesIO(blob))
inner=zipfile.ZipFile(io.BytesIO(outer.read('WWSW11.SHR')))
raw=inner.read('WACKY.DAT')
if hashlib.sha256(raw).hexdigest()!=DAT_SHA:raise SystemExit('Shareware data hash mismatch')
for name in ['WACKY.DAT','VENDOR.DOC','ORDER.FRM']:
    (DATA/name).write_bytes(inner.read(name))
(DATA/'FILE_ID.DIZ').write_bytes(outer.read('FILE_ID.DIZ'))
subprocess.run(['git','-C',str(ROOT),'check-ignore','-q',str(SOURCE/'src/main.c')],check=True)
subprocess.run(['git','-C',str(ROOT),'check-ignore','-q',str(DATA/'WACKY.DAT')],check=True)
print('Pinned recreation and shareware ready under ignored .tools/')
