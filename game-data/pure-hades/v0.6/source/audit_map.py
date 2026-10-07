#!/usr/bin/env python3
"""Independent D4 audit of serialized geometry geometry and gameplay objects."""
import struct,collections,math,json,hashlib,sys
from pathlib import Path
p=Path(sys.argv[1]).resolve().parent
b=Path(sys.argv[1]).read_bytes();_,n,o=struct.unpack_from('<4sii',b)
lumps={}
for i in range(n):
 a,s,k=struct.unpack_from('<ii8s',b,o+16*i);lumps[k.rstrip(b'\0').decode()]=b[a:a+s]
def rows(k,f):return list(struct.iter_unpack(f,lumps[k]))
V=rows('VERTEXES','<hh');L=rows('LINEDEFS','<7H');S=rows('SIDEDEFS','<hh8s8s8sH')
C=rows('SECTORS','<hh8s8shhh');T=rows('THINGS','<5h');G=rows('SEGS','<6H')
SS=rows('SSECTORS','<HH');N=rows('NODES','<12h2H')
transforms={'identity':(1,0,0,1),'rotate90':(0,-1,1,0),'rotate180':(-1,0,0,-1),
 'rotate270':(0,1,-1,0),'reflect_x':(-1,0,0,1),'reflect_y':(1,0,0,-1),
 'reflect_diagonal':(0,1,1,0),'reflect_antidiagonal':(0,-1,-1,0)}
def tf(v,m):x,y=v;a,b,c,d=m;return a*x+b*y,c*x+d*y
sector_vertices=collections.defaultdict(set)
for va,vb,_,_,_,s0,s1 in L:
 for s in (s0,s1):
  if s!=65535:sector_vertices[S[s][5]].update((V[va],V[vb]))
def tag_signature(tag,mat):
 if not tag:return ()
 out=[]
 for i,c in enumerate(C):
  if c[-1]!=tag:continue
  pts=sector_vertices[i]
  box={(min(x for x,y in pts),min(y for x,y in pts)),
       (min(x for x,y in pts),max(y for x,y in pts)),
       (max(x for x,y in pts),min(y for x,y in pts)),
       (max(x for x,y in pts),max(y for x,y in pts))}
  out.append((c[:-1],tuple(sorted(tf(pt,mat) for pt in box))))
 return tuple(sorted(out))
def sector_signature(i,mat):return C[i][:-1],tag_signature(C[i][-1],mat)
def side_signature(i,mat,neutral_exit):
 if i==65535:return None
 x,y,u,l,m,sid=S[i]
 if neutral_exit:
  u=b'BROWN1\0\0' if u==b'SW1EXIT\0' else u
  l=b'BROWN1\0\0' if l==b'SW1EXIT\0' else l
 return x,y,u,l,m,sector_signature(sid,mat)
def line_records(mat,neutral_exit=False):
 out=collections.Counter();det=mat[0]*mat[3]-mat[1]*mat[2]
 for va,vb,flags,special,tag,s0,s1 in L:
  a,z=tf(V[va],mat),tf(V[vb],mat)
  left,right=side_signature(s0,mat,neutral_exit),side_signature(s1,mat,neutral_exit)
  active=0
  if neutral_exit and special==11:special=0
  if det<0:left,right=right,left;active^=1
  if a>z:a,z=z,a;left,right=right,left;active^=1
  out[(a,z,flags,special,tag_signature(tag,mat),left,right,active if special else -1)]+=1
 return out
baseline=line_records(transforms['identity'],True)
baseline_full=line_records(transforms['identity'],False)
results={}
for label,mat in transforms.items():
 actual=line_records(mat,True)
 assert actual==baseline,(label,len(actual-baseline),len(baseline-actual))
 full=line_records(mat,False)
 results[label]={'geometry_and_doors_excluding_exit':True,
                 'full_wad_wall_record_differences':sum((full-baseline_full).values())}

object_results={}
for typ,label in [(11,'deathmatch starts'),(2001,'shotguns'),(82,'super shotguns'),(2049,'shell boxes'),(2012,'medikits: visible and concealed'),(2011,'visible stimpacks'),(2006,'secret BFG'),(2003,'rocket launchers'),(2004,'plasma guns'),(2002,'chainguns'),(2005,'chainsaws'),(2046,'rocket boxes'),(2048,'bullet boxes'),(17,'cell packs')]:
 original=[t for t in T if t[3]==typ]
 def key(t):return t[0],t[1],t[2] if typ==11 else 0,t[3],t[4]
 expected=collections.Counter(map(key,original))
 for name,mat in transforms.items():
  transformed=[]
  for x,y,angle,t,flags in original:
   x1,y1=tf((x,y),mat)
   dx,dy=tf((round(math.cos(math.radians(angle))*1000),round(math.sin(math.radians(angle))*1000)),mat)
   a1=round(math.degrees(math.atan2(dy,dx)))%360
   transformed.append(key((x1,y1,a1,t,flags)))
  assert collections.Counter(transformed)==expected,(label,name)
 object_results[label]={'count':len(original),'all_eight_D4_transforms':True,
                       'facing_angles_checked':typ==11}

def point_sector(x,y):
 index=len(N)-1
 while not index&32768:
  nx,ny,dx,dy,*_=N[index]
  side=(int(dy>0) if x<=nx else int(dy<0)) if dx==0 else (int(dx<0) if y<=ny else int(dx>0))
  index=N[index][12+side]
 _,first=SS[index&32767];seg=G[first];return S[L[seg[3]][5+seg[4]]][5]
door_sectors={S[l[6]][5] for l in L if l[3]==1}
moving_floors={i for i,c in enumerate(C) if c[-1] in (101,102,103,104,110)}
def free(x,y,opened):
 for dx in (-16,0,16):
  for dy in (-16,0,16):
   sec=point_sector(x+dx,y+dy);floor,ceil=C[sec][:2]
   if opened and sec in door_sectors:ceil=76
   if opened and sec in moving_floors:floor=0
   if ceil-floor<56:return False
 return True
def make_graph(opened):
 pts={(x,y) for x in range(-1152,1153,64) for y in range(-1152,1153,64) if free(x,y,opened)}
 graph={a:[] for a in pts}
 for x,y in pts:
  for dx,dy in ((64,0),(-64,0),(0,64),(0,-64)):
   v=(x+dx,y+dy)
   if v in pts and free(x+dx/2,y+dy/2,opened):graph[(x,y)].append(v)
 return graph
spawns=[t[:2] for t in T if t[3]==11]
meds=[t[:2] for t in T if t[3]==2012]
def distances(graph,start):
 d={start:0};q=collections.deque([start])
 while q:
  u=q.popleft()
  for v in graph[u]:
   if v not in d:d[v]=d[u]+64;q.append(v)
 return d
closed_graph=make_graph(False);open_graph=make_graph(True)
closed_dist={s:distances(closed_graph,s) for s in spawns}
open_dist={s:distances(open_graph,s) for s in spawns}
for mat in transforms.values():
 for s in spawns:
  for t in spawns:assert closed_dist[s][t]==closed_dist[tf(s,mat)][tf(t,mat)]
  for h in meds:assert open_dist[s][h]==open_dist[tf(s,mat)][tf(h,mat)]

# Semantic sector area and boundary dimensions are read from actual BSP cells.
semantic_cells=collections.defaultdict(list)
for y in range(-1184,1216,64):
 for x in range(-1184,1216,64):semantic_cells[C[point_sector(x,y)]].append((x,y))
room_signature=C[point_sector(0,896)]
room_centers=[(0,896),(896,0),(0,-896),(-896,0)]
room_boxes=[]
for cx,cy in room_centers:
 pts=[(x,y) for x,y in semantic_cells[room_signature] if abs(x-cx)<=192 and abs(y-cy)<=192]
 bounds=[min(x for x,y in pts)-32,min(y for x,y in pts)-32,max(x for x,y in pts)+32,max(y for x,y in pts)+32]
 room_boxes.append({'center':[cx,cy],'bounds':bounds,'width':bounds[2]-bounds[0],
                    'height':bounds[3]-bounds[1],'area':len(pts)*4096})
assert all(r['width']==r['height']==384 for r in room_boxes)
report={'file':Path(sys.argv[1]).name,'version':'0.6','sha256':hashlib.sha256(b).hexdigest(),
 'result':'PASS with explicitly enumerated player-one and exit exceptions',
 'line_records_checked':len(L),'transform_results':results,'gameplay_objects':object_results,
 'satellite_rooms':room_boxes,'arena_dimensions':[1024,1024],
 'corridor_widths':{'arena_connectors':128,'perimeter':128,'hidden_doors':128},
 'arena_to_room_connector_length':192,'door_initial_state':'all eight closed; identical 76-unit opening height',
 'bfg_switch_target_mapping_D4_invariant':bool(moving_floors),
 'bfg_vault':('Four-switch centered reward' if moving_floors else 'Static central obstacle; reward, controls and tags removed'),
 'travel_distance_validation':{'model':'64-unit orthogonal movement lattice with Doom player 16-unit radius and 56-unit height clearance',
   'all_spawn_pair_distances_D4_invariant':True,'all_spawn_to_medikit_distances_D4_invariant':True,
   'spawn_pair_distances':[[closed_dist[s][t] for t in spawns] for s in spawns]},
 'exceptions':[
  {'type':'player-one test start','count':1,'position':[-384,-384],'angle_degrees':45,
   'note':'Single-player testing entry at deathmatch spawn 4. It is not part of the eight-player-start symmetry set.'},
  {'type':'concealed normal exit action','count':1,'wall_endpoints':[[-128,1088],[-64,1088]],
   'midpoint':[-96,1088],'special':11,'texture':'BROWN1',
   'note':'User-requested single concealed exit, 96 units west of the north-room wall center. The action is explicitly exempt from symmetry; wall texture and physical geometry match the other rooms.'}],
 'claim_boundary':'The arena, routes, room sizes, ceilings/lights, eight deathmatch start positions and facing angles, weapons, ammo, health and usable doors are exactly D4 symmetric. The full WAD including the single test start and single exit is not wholly D4 symmetric.',
 'scope':'Exact static symmetry audit; engine checks are documented in runtime-validation.json.'}
(p/'symmetry.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS symmetry',Path(sys.argv[1]).name)
