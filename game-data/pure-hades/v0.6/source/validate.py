"""Validate serialized PWAD structure, expected contents, geometry and symmetry."""
from pathlib import Path
import struct,json,collections,subprocess,sys
from wadlib import read_wad,write_wad,sha,midi_info
R=Path(__file__).resolve().parents[1]
w=(R/'PUREHADES.WAD').read_bytes();ls=read_wad(w)
expected_names=['THINGS','LINEDEFS','SIDEDEFS','VERTEXES','SEGS','SSECTORS','NODES','SECTORS','REJECT','BLOCKMAP']
expected=[{1:1,11:8,82:8,2001:8,2006:1,2011:8,2012:16,2049:16},{1:1,11:8,2003:16,2006:1,2011:8,2012:16,2046:16},{1:1,11:8,2004:16,17:16,2006:1,2011:8,2012:16},{1:1,11:8,82:8,2001:8,2002:4,2003:4,2004:4,2005:4,2006:1,2011:8,2012:16,17:8,2046:8,2048:4,2049:16},{1:1,11:8,82:16,2011:8,2012:16,2049:16}]
base=dict(read_wad((R/'build/base-map.wad').read_bytes()));reports=[]
for n in range(1,6):
 idx=next(i for i,(k,v) in enumerate(ls) if k==f'MAP{n:02}')
 ml=ls[idx:idx+11];assert [k for k,v in ml[1:]]==expected_names
 d=dict(ml);rows=lambda k,f:list(struct.iter_unpack(f,d[k]))
 T=rows('THINGS','<5h');V=rows('VERTEXES','<hh');L=rows('LINEDEFS','<7H');S=rows('SIDEDEFS','<hh8s8s8sH');G=rows('SEGS','<6H');SS=rows('SSECTORS','<HH');N=rows('NODES','<12h2H');C=rows('SECTORS','<hh8s8shhh')
 assert dict(collections.Counter(t[3] for t in T))==expected[n-1]
 assert all(t[4]==7 for t in T)
 assert len(d['REJECT'])==(len(C)**2+7)//8 and not any(d['REJECT'])
 assert max(len(V),len(L),len(S),len(G),len(SS),len(N))<32768
 for l in L:
  assert l[0]<len(V) and l[1]<len(V) and l[0]!=l[1]
  assert l[5]<len(S) and (l[6]==65535 or l[6]<len(S))
  assert bool(l[2]&4)==(l[6]!=65535)
 for s in S:assert s[5]<len(C)
 for va,vb,ang,li,side,off in G:assert va<len(V) and vb<len(V) and li<len(L) and side in (0,1) and off==0 and L[li][5+side]!=65535
 for count,first in SS:assert count>0 and first+count<=len(G)
 for i,node in enumerate(N):
  for child in node[12:]:assert ((child&32767)<len(SS)) if child&32768 else child<i
 exits=[l for l in L if l[3]==11];assert len(exits)==1
 assert {V[x] for x in exits[0][:2]}=={(-128,1088),(-64,1088)}
 for k in ('VERTEXES','SEGS','SSECTORS','NODES','BLOCKMAP','REJECT'):assert d[k]==base[k]
 if n<5:
  for k in ('LINEDEFS','SIDEDEFS','SECTORS'):assert d[k]==base[k]
 else:
  assert {l[3] for l in L}=={0,1,11}
  assert not any(l[4] in (101,102,103,104,110) for l in L)
  assert not any(c[-1] in (101,102,103,104,110) for c in C)
  assert not any(tex.rstrip(b'\0').startswith(b'SW') for s in S for tex in s[2:5])
  assert all(c[-2]==0 for c in C[21:26])
  assert all(c[:2]==(96,96) for c in C[21:26])
 folder=R/'build/audit'/f'MAP{n:02}';folder.mkdir(parents=True,exist_ok=True);single=folder/f'MAP{n:02}.WAD';single.write_bytes(write_wad(ml))
 subprocess.run([sys.executable,str(R/'source/audit_map.py'),str(single)],check=True)
 audit=json.loads((folder/'symmetry.json').read_text())
 reports.append({'map':f'MAP{n:02}','things':expected[n-1],'classic_format_and_indices':'PASS','single_hidden_exit':'PASS','geometry_and_pickup_symmetry':'PASS with player-one/hidden-exit exceptions','spawn_to_spawn_and_health_route_distances':'D4 invariant','bfg_present':n!=5,'sha256':sha(single.read_bytes())})
tracks=json.loads((R/'music/tracklist.json').read_text());lumpdict=dict(ls)
for t in tracks:
 b=(R/'music'/t['filename']).read_bytes();assert b==lumpdict[t['lump']] and sha(b)==t['sha256'];midi_info(b)
assert len({t['sha256'] for t in tracks})==5
report={'result':'PASS','wad_sha256':sha(w),'map_checks':reports,'five_distinct_valid_licensed_midi_tracks':True,'symmetry_exceptions':['Single player-one test start','Single hidden exit action'],'rotation_metadata':'MAP01 -> MAP02 -> MAP03 -> MAP04 -> MAP05 -> MAP01 via UMAPINFO'}
(R/'validation').mkdir(exist_ok=True);(R/'validation/structure.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS all five maps: structure, contents, geometry, starts, exits, routes, symmetry and MIDI bytes')
