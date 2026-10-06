#!/usr/bin/env python3
"""Wrap the validated local prototype with the existing development formats."""
from pathlib import Path
import hashlib,importlib.util,json,subprocess,sys,re,struct
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod);return mod
package=module('wacky_package',ROOT/'scripts/build-game-package.py')
resource=module('wacky_resource',ROOT/'scripts/build-game-resource.py')
transfer=module('wacky_transfer',ROOT/'scripts/p4-transfer.py')
build=ROOT/'build-host/wacky-riscv'
elf=build/'wacky-stripped.elf'
subprocess.run([str(ROOT/'build-host/wacky-p4/wacky_validate_elf'),str(elf)],check=True)
# This manifest describes only a local development package. It is deliberately
# absent from games/ and the shipped catalog; the standard header marks it dev.
manifest={
 'schema':1,'format':'p4-native-elf-v1','api_version':1,'version':'0.1.1',
 'package_file':'WACKYTRY.P4G','resource_file':'WACKYTRY.P4R',
 'component':'wacky_wheels_probe','entry_symbol':'p4_wacky_probe_game',
 'launcher_id':9001,'id':'org.p4console.wacky-probe','title':'Wacky Wheels',
 'subtitle':'Racing, duck shoot and MIDI','folder':'GAMES/ARCADE',
 'accent_rgb565':'0xffe0','required_capabilities':['video','controls','storage'],
 'optional_capabilities':['multiplayer-session','audio-stream','save'],'license':'Unspecified','enabled':False,
 'assets':'local-original-v1.1-shareware; no data committed',
 '_required_mask':19,'_optional_mask':584,'_multiplayer_profile':None}
manifest['multiplayer']={'schema':1,'style':'realtime','min_players':2,'max_players':4,'message_bytes':64,'protocol':2,'tick_rate_hz':12}
manifest['_multiplayer_profile']=package.normalize_multiplayer_profile(manifest, {'multiplayer-session'})
# Fail on a newly claimed identity; the experiment must not replace a game.
for p in (ROOT/'games').glob('*/game.json'):
    d=json.loads(p.read_text())
    if d.get('launcher_id')==9001 or d.get('id')==manifest['id']:
        raise SystemExit(f'Probe identity conflicts with {p}; choose a new identity first')
raw=(ROOT/'.tools/wacky-shareware/WACKY.DAT').read_bytes()
if hashlib.sha256(raw).hexdigest()!='ae36b4204f1b44fbdb26294a45495e46616feffcb18e67d48de9324d76bed85f':
    raise SystemExit('Shareware data hash mismatch')
# Preserve the original title art as the launcher's bounded indexed icon.
# Decode just this pinned 8-bit, one-plane PCX; keep every generated asset local.
for i in range(struct.unpack_from('<H',raw)[0]):
    name,size,offset=struct.unpack_from('<14sII',raw,2+i*22)
    if name.split(b'\0')[0]==b'WINTRO.PCX':
        pcx=raw[offset+2:offset+2+size];break
else: raise SystemExit('Original title art is absent')
if pcx[:4]!=bytes([10,5,1,8]) or pcx[65]!=1 or pcx[-769]!=12:
    raise SystemExit('Unexpected title-art encoding')
width=struct.unpack_from('<H',pcx,8)[0]+1;height=struct.unpack_from('<H',pcx,10)[0]+1
stride=struct.unpack_from('<H',pcx,66)[0]
if (width,height,stride)!=(320,200,320):raise SystemExit('Unexpected title-art dimensions')
pixels=bytearray();pos=128
while len(pixels)<stride*height and pos<len(pcx)-769:
    v=pcx[pos];pos+=1
    if v>=192:
        count=v&63;v=pcx[pos];pos+=1
    else:count=1
    if len(pixels)+count>stride*height:raise SystemExit('Title-art RLE overflow')
    pixels.extend(bytes([v])*count)
if len(pixels)!=64000:raise SystemExit('Truncated title art')
palette=pcx[-768:]
colors=[((palette[i]>>3)<<11)|((palette[i+1]>>2)<<5)|(palette[i+2]>>3) for i in range(0,768,3)]
icon=b'P4ICON1\0'+struct.pack('<HHI256H',128,72,1,*colors)+bytes(pixels[(y*200//72)*320+(x*320//128)] for y in range(72) for x in range(128))
assert len(icon)==9744
icon_path=build/'launcher.p4i';icon_path.write_bytes(icon)
cache=(ROOT/'apps/console_os/build-tab5/CMakeCache.txt').read_text()
objcopy=re.search(r'^CMAKE_C_COMPILER_AR:FILEPATH=(.*)-gcc-ar$',cache,re.M)[1]+'-objcopy'
icon_elf=build/'wacky-icon.elf'
subprocess.run([objcopy,'--add-section',f'.p4icon={icon_path}','--set-section-flags','.p4icon=readonly',str(elf),str(icon_elf)],check=True)
subprocess.run([str(ROOT/'build-host/wacky-p4/wacky_validate_elf'),str(icon_elf)],check=True)
payload=icon_elf.read_bytes();p4g=package.build_header(manifest,payload)+payload
p4r=resource.build_resource(manifest['id'],raw)
transfer.validate_p4g_host(p4g)
# p4-transfer independently validates the P4R when uploading; the resource
# builder already bounds its payload and binds its game ID and SHA-256.
output=build/'bundle/GAMES';output.mkdir(parents=True,exist_ok=True)
report={}
for name,data in [('WACKYTRY.P4G',p4g),('WACKYTRY.P4R',p4r)]:
    (output/name).write_bytes(data)
    report[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
(build/'probe-manifest.json').write_text(json.dumps({k:v for k,v in manifest.items() if not k.startswith('_')},indent=2)+'\n')
for notice in ['VENDOR.DOC','ORDER.FRM','FILE_ID.DIZ']:
    (output.parent/notice).write_bytes((ROOT/'.tools/wacky-shareware'/notice).read_bytes())
(build/'packages.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))

subprocess.run([str(ROOT/'build-host/wacky-p4/wacky_validate_elf'),'--package',str(output/'WACKYTRY.P4G')],check=True)
