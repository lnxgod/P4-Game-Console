#!/usr/bin/env python3
"""Build Pure Hades v0.6 deterministically with Python 3. No network access."""
from pathlib import Path
import collections,json,os,runpy,struct,sys
from wadlib import read_wad,write_wad,sha,midi_info
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build';BUILD.mkdir(exist_ok=True)
os.environ['PURE_HADES_BUILD_DIR']=str(BUILD)
runpy.run_path(str(ROOT/'source/base_map.py'))
base=read_wad((BUILD/'base-map.wad').read_bytes())
assert sha((BUILD/'base-map.wad').read_bytes())=='28a6cd08ff6ab54b51a15ef9994b60dcc2d25b38e0212a8f137859b33bec7cf1'
things=list(struct.iter_unpack('<5h',dict(base)['THINGS']))
pack=lambda fmt,rs:b''.join(struct.pack(fmt,*r) for r in rs)

def variant(number,replace=None,extras=(),finale=False):
 ts=[(x,y,a,(replace or {}).get(t,t),f) for x,y,a,t,f in things if not(finale and t==2006)]
 ts+=list(extras)
 lumps=[(f'MAP{number:02}',b'')]+[(k,pack('<5h',ts) if k=='THINGS' else v) for k,v in base[1:]]
 if finale:
  d=dict(lumps);lines=[list(x) for x in struct.iter_unpack('<7H',d['LINEDEFS'])];sides=[list(x) for x in struct.iter_unpack('<hh8s8s8sH',d['SIDEDEFS'])];sectors=[list(x) for x in struct.iter_unpack('<hh8s8shhh',d['SECTORS'])]
  for line in lines:
   if line[3] in (23,45):
    if line[3]==23:
     for si in line[5:7]:
      if si!=65535:
       for j in (2,3,4):
        if sides[si][j].rstrip(b'\0').startswith(b'SW'):sides[si][j]=b'BROWN1\0\0'
    line[3]=line[4]=0
  for s in sectors:
   if s[-1] in (101,102,103,104,110):s[-1]=0;s[-2]=0
  edits={'LINEDEFS':pack('<7H',lines),'SIDEDEFS':pack('<hh8s8s8sH',sides),'SECTORS':pack('<hh8s8shhh',sectors)}
  lumps=[(k,edits.get(k,v)) for k,v in lumps]
 return lumps

def orbit(x,y):return sorted({(x,y),(-y,x),(-x,-y),(y,-x),(-x,y),(x,-y),(y,x),(-y,-x)})
extras=[]
for typ,x,y in [(2003,224,224),(2004,0,288),(2002,0,960),(2005,416,416),(2048,0,992),(2046,224,320),(17,128,320)]:
 extras.extend((a,b,0,typ,7) for a,b in orbit(x,y))
maps=[base,variant(2,{2001:2003,82:2003,2049:2046}),variant(3,{2001:2004,82:2004,2049:17}),variant(4,extras=extras),variant(5,{2001:82},finale=True)]
names=['Shotguns','Rockets','Plasma','Pure Chaos','Double-Barrel Finale']
lumps=[item for m in maps for item in m]
tracks=json.loads((ROOT/'music/tracklist.json').read_text())
for tr in tracks:
 b=(ROOT/'music'/tr['filename']).read_bytes();assert sha(b)==tr['sha256'];midi_info(b);lumps.append((tr['lump'],b))
lumps += [('D_DM2TTL',(ROOT/'music'/tracks[0]['filename']).read_bytes()),('D_DM2INT',(ROOT/'music'/tracks[3]['filename']).read_bytes())]
strings='Patch File for DeHackEd v3.0\n# Pure Hades v0.6 map names.\nDoom version = 19\nPatch format = 6\n\n[STRINGS]\n'+''.join(f'HUSTR_{i} = level {i}: Pure Hades - {name}\n' for i,name in enumerate(names,1))
lumps.append(('DEHACKED',strings.encode()))
# Engines supporting UMAPINFO use a five-map rotation. Vanilla engines retain
# their normal sequential progression and can select any included map directly.
umap='// Pure Hades: five-map deathmatch rotation.\n'+''.join(f'map MAP{i:02} {{\n levelname = "Pure Hades - {name}"\n next = "MAP{(i%5)+1:02}"\n nextsecret = "MAP{(i%5)+1:02}"\n music = "{tracks[i-1]["lump"]}"\n}}\n' for i,name in enumerate(names,1))
lumps.append(('UMAPINFO',umap.encode()))
for path in sorted((ROOT/'assets').glob('*.lmp')):lumps.append((path.stem,path.read_bytes()))
wad=write_wad(lumps);(ROOT/'PUREHADES.WAD').write_bytes(wad)
manifest={'title':'Pure Hades','version':'0.6','edition':'Shareable licensed community MIDI edition','file':'PUREHADES.WAD','sha256':sha(wad),'bytes':len(wad),'format':'Classic Doom II-format PWAD','requirements':{'base_iwad':'Official Freedoom Phase 2 0.13.0 freedoom2.wad','iwad_sha256':'a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b','maps':'MAP01-MAP05','recommended_rotation_engine':'UMAPINFO-compatible Doom II engine','vanilla_note':'Chocolate Doom plays all five maps; its MAP05 exit continues into the base IWAD MAP06.'},'launch_examples':['chocolate-doom -iwad freedoom2.wad -file PUREHADES.WAD -warp 1','chocolate-doom -iwad freedoom2.wad -file PUREHADES.WAD -warp 5'],'maps':[],'music':tracks,'music_edits':'MIDI bytes unchanged; assigned to new map music lumps. MAP01/04 tracks also used for title/intermission.','licenses':{'map_geometry_and_level_design':'CC-BY-SA-4.0','python_source':'MIT','music':'CC-BY-SA-4.0','Freedoom_font-derived_title_graphics':'BSD-3-Clause'},'provenance':'Remix of a new reconstruction from Will\u2019s rough 2012 sketch and memory. The historical Pure Hell WAD was not recovered. Previous Pure Hell releases remain separate.'}
for i,m in enumerate(maps,1):
 d=dict(m);ts=list(struct.iter_unpack('<5h',d['THINGS']))
 manifest['maps'].append({'marker':m[0][0],'name':names[i-1],'things':dict(sorted(collections.Counter(t[3] for t in ts).items())),'lump_sha256':{k:sha(v) for k,v in m[1:]},'bfg_four_switch_gate':i!=5,'hidden_exit':{'x':-96,'y':1088,'face':'north','special':11},'next_map_with_umapinfo':f'MAP{i%5+1:02}','next_map_vanilla':f'MAP{i+1:02}'})
(ROOT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'file':'PUREHADES.WAD','bytes':len(wad),'sha256':sha(wad),'maps':[(m['marker'],m['name'],m['things']) for m in manifest['maps']]},indent=2))
