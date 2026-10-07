#!/usr/bin/env python3
"""Pure Hades base geometry, preserving the Pure Hell Phase 2 v0.4 reconstruction.

Rebuild with Python 3 (standard library only): python3 build.py
No IWAD, texture pixels, engine, or third-party WAD is embedded.
The rectilinear grid supplies convex subsectors and a balanced, classic BSP.
"""
from pathlib import Path
import os
import collections
import hashlib
import json
import math
import struct
import zlib

OUT = Path(os.environ.get("PURE_HADES_BUILD_DIR", str(Path(__file__).resolve().parent / "base-build")))
OUT.mkdir(parents=True, exist_ok=True)
STEP, LO, HI = 64, -1216, 1216
AXIS = sorted(set(range(LO, HI+1, STEP)) | {-32,32})
N = len(AXIS)-1
SECTORS = []

def sector(name, floor=0, ceiling=80, flat='FLOOR0_1', light=176, special=0, tag=0):
    SECTORS.append(dict(name=name, floor=floor, ceiling=ceiling, flat=flat,
                        ceiling_flat='CEIL3_5', light=light, special=special, tag=tag))
    return len(SECTORS)-1

VOID = sector('solid space', 128, 128, light=0)
ARENA = sector('central arena', ceiling=96, flat='FLOOR0_1', light=208)
ROOM = sector('satellite rooms', ceiling=96, flat='FLOOR0_1', light=192)
LINK = sector('arena connectors', flat='FLOOR0_1', light=176)
RING = sector('red perimeter halls', flat='FLOOR5_3', light=176)
cells = [[VOID for _ in range(N)] for _ in range(N)]

def paint(x0, y0, x1, y1, sid):
    assert all(v in AXIS for v in (x0,y0,x1,y1))
    for j in range(AXIS.index(y0), AXIS.index(y1)):
        for i in range(AXIS.index(x0), AXIS.index(x1)):
            cells[j][i] = sid

paint(-960,832,960,960,RING)
paint(-960,-960,960,-832,RING)
paint(-960,-960,-832,960,RING)
paint(832,-960,960,960,RING)
paint(-64,-896,64,896,LINK)
paint(-896,-64,896,64,LINK)
paint(-512,-512,512,512,ARENA)
for box in [(-192,704,192,1088),(-192,-1088,192,-704),
            (704,-192,1088,192),(-1088,-192,-704,192)]:
    paint(*box,ROOM)

doors, health = [], []
for direction in ('north','south','east','west'):
    for t in (-448,448):
        d = sector(f'{direction} hidden door {t}', ceiling=0,
                   flat='FLOOR5_3', light=176)
        h = sector(f'{direction} health alcove {t}', flat='FLOOR0_1',
                   light=192, special=9)
        doors.append(d)
        if direction == 'north':
            paint(t-64,960,t+64,1024,d); paint(t-64,1024,t+64,1152,h)
            health.append((t,1088))
        elif direction == 'south':
            paint(t-64,-1024,t+64,-960,d); paint(t-64,-1152,t+64,-1024,h)
            health.append((t,-1088))
        elif direction == 'east':
            paint(960,t-64,1024,t+64,d); paint(1024,t-64,1152,t+64,h)
            health.append((1088,t))
        else:
            paint(-1024,t-64,-960,t+64,d); paint(-1152,t-64,-1024,t+64,h)
            health.append((-1088,t))

# Compact, symmetric, vanilla AND gate. Each remote S1 switch lowers one
# support. The center's SR lower-to-highest-neighbour action cannot descend
# until ALL four surrounding supports are at floor zero. Repeated use of one
# switch never substitutes for the others. No scripts or voodoo dolls.
vault = sector('concealed BFG vault', floor=96, ceiling=96,
               flat='FLOOR5_3', light=208, special=9, tag=110)
paint(-32,-32,32,32,vault)
supports=[]
support_boxes=[(-32,32,32,64),(32,-32,64,32),(-32,-64,32,-32),(-64,-32,-32,32)]
for k,(direction,box) in enumerate(zip(('north','east','south','west'),support_boxes)):
    sid=sector(f'{direction} BFG interlock',floor=96,ceiling=96,
               flat='FLOOR0_1',light=208,tag=101+k)
    paint(*box,sid);supports.append(sid)
moving_floors={vault,*supports}

spawns = [(-384,384,315),(384,384,225),(384,-384,135),(-384,-384,45),
          (0,896,270),(896,0,180),(0,-896,90),(-896,0,0)]
things = [(-384,-384,45,1,7)]
for x,y,a in spawns:
    things += [(x,y,a,11,7),(x,y,0,2001,7)]
    # Shell boxes remain close to, but distinct from, the spawn positions.
    dx = -32 if x > 0 else 32 if x < 0 else 0
    dy = -32 if y > 0 else 32 if y < 0 else 0
    things.append((x+dx,y+dy,0,2049,7))
things += [(x,y,0,2012,7) for x,y in health]
visible_health=[(-320,320),(320,320),(320,-320),(-320,-320),
                (0,800),(800,0),(0,-800),(-800,0)]
things += [(x,y,0,2011,7) for x,y in visible_health]
# Phase 2 additions preserve all existing pickups and exact D4 symmetry.
# SSGs sit just inward of each spawn, while the extra shell boxes replenish
# exposed arena corners and connecting passages. New visible medikits reward
# both arena traversal and the four remote switch routes.
super_shotguns=[(-288,288),(288,288),(288,-288),(-288,-288),
                (0,832),(832,0),(0,-832),(-832,0)]
extra_shells=[(-448,448),(448,448),(448,-448),(-448,-448),
              (0,640),(640,0),(0,-640),(-640,0)]
extra_medikits=[(0,384),(384,0),(0,-384),(-384,0),
                (0,1024),(1024,0),(0,-1024),(-1024,0)]
things += [(x,y,0,82,7) for x,y in super_shotguns]
things += [(x,y,0,2049,7) for x,y in extra_shells]
things += [(x,y,0,2012,7) for x,y in extra_medikits]
things.append((0,0,0,2006,7))

vertices = [(x,y) for y in AXIS for x in AXIS]
vid = {v:i for i,v in enumerate(vertices)}
lines, sides, segs, subsectors = [], [], [], []
edge_map = {}

def sidedef(sid, wall, mid='-'):
    sides.append((0,0,wall,wall,mid,sid))
    return len(sides)-1

def wall_for(sid):
    if sid == RING or sid in doors: return 'REDWALL1'
    if sid == ROOM: return 'BROWN1'
    return 'STARTAN3'

# Each tile is convex. Its boundary follows clockwise order (interior right).
for j in range(N):
    for i in range(N):
        x,y=AXIS[i],AXIS[j];x1,y1=AXIS[i+1],AXIS[j+1]
        corners=[(x,y1),(x1,y1),(x1,y),(x,y)]
        for a,b in zip(corners,corners[1:]+corners[:1]):
            key=tuple(sorted((a,b)))
            if key not in edge_map:
                edge_map[key]={'a':a,'b':b,'front':cells[j][i],
                               'front_cell':(i,j),'back':None}
            else:
                edge_map[key]['back']=cells[j][i]
                edge_map[key]['back_cell']=(i,j)

cell_segs=collections.defaultdict(list)
for e in edge_map.values():
    a,b,f,bk=e['a'],e['b'],e['front'],e['back']
    # A moving door must not have an internal same-sector linedef: the vanilla
    # lowest-neighbour-ceiling query would count its own closed ceiling.
    # BSP partition boundaries need no rendered seg along this open interior.
    if f==bk and f in set(doors)|moving_floors: continue
    special=0;tag=0
    # Manual door specials operate on the BACK sector; orient both usable faces.
    if bk is not None and ((f in doors) != (bk in doors)):
        other=bk if f in doors else f
        if other != VOID:
            special=1
            if f in doors:
                a,b,f,bk=b,a,bk,f
                e['front_cell'],e['back_cell']=e['back_cell'],e['front_cell']
    flags=4 if bk is not None else 1
    if f==bk: flags |=128  # hide construction splits in automap
    if special==1: flags |=32
    front_tex,back_tex=wall_for(f),wall_for(bk) if bk is not None else '-'
    if special==1: front_tex=back_tex='REDWALL1'
    if (f in doors and bk==VOID) or (bk in doors and f==VOID):
        front_tex=back_tex='DOORTRAK'; flags |=16
    # Four symmetrically placed room switches, each linked to the support
    # on the same compass side of the central vault.
    mx,my=(a[0]+b[0])/2,(a[1]+b[1])/2
    switch_index=None
    if a[1]==b[1]==1088 and -32<mx<32:switch_index=0
    if a[0]==b[0]==1088 and -32<my<32:switch_index=1
    if a[1]==b[1]==-1088 and -32<mx<32:switch_index=2
    if a[0]==b[0]==-1088 and -32<my<32:switch_index=3
    if switch_index is not None:
        if f!=ROOM:
            a,b,f,bk=b,a,bk,f
            e['front_cell'],e['back_cell']=e['back_cell'],e['front_cell']
        special=23;tag=101+switch_index;front_tex='SW1BRCOM'
        back_tex=wall_for(bk)
    if (f==vault and bk in supports) or (bk==vault and f in supports):
        if f==vault:
            a,b,f,bk=b,a,bk,f
            e['front_cell'],e['back_cell']=e['back_cell'],e['front_cell']
        special=45;tag=110;front_tex=back_tex='STARTAN3'
    # One hidden exit, explicitly exempt from symmetry by the user's choice.
    # It is west of the visible BFG switch and uses ordinary room-wall art.
    if set((a,b))=={(-128,1088),(-64,1088)}:
        if f!=ROOM:
            a,b,f,bk=b,a,bk,f
            e['front_cell'],e['back_cell']=e['back_cell'],e['front_cell']
        special=11;front_tex='BROWN1'
    s0=sidedef(f,front_tex,front_tex if bk is None else '-')
    s1=sidedef(bk,back_tex) if bk is not None else 65535
    li=len(lines)
    lines.append((vid[a],vid[b],flags,special,tag,s0,s1))
    cell_segs[e['front_cell']].append((vid[a],vid[b],li,0))
    if bk is not None:
        cell_segs[e['back_cell']].append((vid[b],vid[a],li,1))

for j in range(N):
    for i in range(N):
        ss=cell_segs[(i,j)]
        assert len(ss) in (2,3,4)
        first=len(segs)
        for va,vb,li,side in ss:
            ax,ay=vertices[va];bx,by=vertices[vb]
            angle=round(math.atan2(by-ay,bx-ax)*65536/(2*math.pi))&65535
            segs.append((va,vb,angle,li,side,0))
        subsectors.append((len(ss),first))

nodes=[]
def bbox(i0,j0,i1,j1):
    return (AXIS[j1],AXIS[j0],AXIS[i0],AXIS[i1])

def tree(i0,j0,i1,j1):
    if i1-i0==j1-j0==1:return 0x8000|(j0*N+i0)
    if i1-i0>=j1-j0:
        m=(i0+i1)//2
        ranges=[(m,j0,i1,j1),(i0,j0,m,j1)] # east=0, west=1
        split=(AXIS[m],AXIS[j0],0,AXIS[j1]-AXIS[j0])
    else:
        m=(j0+j1)//2
        ranges=[(i0,j0,i1,m),(i0,m,i1,j1)] # south=0, north=1
        split=(AXIS[i0],AXIS[m],AXIS[i1]-AXIS[i0],0)
    kids=[tree(*r) for r in ranges]
    index=len(nodes)
    nodes.append((*split,*bbox(*ranges[0]),*bbox(*ranges[1]),*kids))
    return index

assert tree(0,0,N,N)==len(nodes)-1

origin=-1280;bw=bh=20
blocks=[set() for _ in range(bw*bh)]
for li,line in enumerate(lines):
    a,b=vertices[line[0]],vertices[line[1]]
    x0=max(0,(min(a[0],b[0])-1-origin)//128)
    x1=min(bw-1,(max(a[0],b[0])+1-origin)//128)
    y0=max(0,(min(a[1],b[1])-1-origin)//128)
    y1=min(bh-1,(max(a[1],b[1])+1-origin)//128)
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):blocks[y*bw+x].add(li)
words=[origin&65535,origin&65535,bw,bh]+[0]*(bw*bh)
lists={}
for k,block in enumerate(blocks):
    entries=(0,*sorted(block),65535)
    if entries not in lists:
        lists[entries]=len(words);words.extend(entries)
    words[4+k]=lists[entries]
assert len(words)<32768

name8=lambda s:s.encode('ascii').ljust(8,b'\0')
pack_many=lambda fmt,rows:b''.join(struct.pack(fmt,*r) for r in rows)
sectorbytes=b''.join(struct.pack('<hh8s8shhh',s['floor'],s['ceiling'],name8(s['flat']),
    name8(s['ceiling_flat']),s['light'],s['special'],s['tag']) for s in SECTORS)
sidebytes=b''.join(struct.pack('<hh8s8s8sH',x,y,name8(t),name8(b),name8(m),s)
                  for x,y,t,b,m,s in sides)
lumps=[('MAP01',b''),('THINGS',pack_many('<hhhhh',things)),
       ('LINEDEFS',pack_many('<7H',lines)),('SIDEDEFS',sidebytes),
       ('VERTEXES',pack_many('<hh',vertices)),('SEGS',pack_many('<6H',segs)),
       ('SSECTORS',pack_many('<HH',subsectors)),('NODES',pack_many('<12h2H',nodes)),
       ('SECTORS',sectorbytes),('REJECT',bytes((len(SECTORS)**2+7)//8)),
       ('BLOCKMAP',pack_many('<H',[(w,) for w in words]))]
data=bytearray(b'\0'*12); directory=[]
for name,blob in lumps:
    directory.append((len(data),len(blob),name8(name)));data.extend(blob)
offset=len(data)
data.extend(b''.join(struct.pack('<ii8s',*d) for d in directory))
struct.pack_into('<4sii',data,0,b'PWAD',len(lumps),offset)
(OUT/'base-map.wad').write_bytes(data)
(OUT/'base-geometry.json').write_text(json.dumps(dict(sectors=SECTORS,cells=cells,grid_coordinates=AXIS,spawns=spawns,health=health,doors=doors,visible_health=visible_health,extra_medikits=extra_medikits,super_shotguns=super_shotguns,extra_shells=extra_shells,vault=vault,supports=supports),indent=2)+'\n')

