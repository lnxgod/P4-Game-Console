"""Render original Pure Hades typography using the BSD-licensed Freedoom font."""
from pathlib import Path
import os,struct,zlib
from wadlib import read_wad
R=Path(__file__).resolve().parents[1]
W=dict(read_wad(Path(os.environ['PURE_HADES_IWAD']).read_bytes()));PAL=W['PLAYPAL'][:768]
A=R/'assets';A.mkdir(exist_ok=True)
def decode(b):
 w,h,_,_=struct.unpack_from('<4h',b);im=[[None]*w for _ in range(h)]
 for x in range(w):
  p=struct.unpack_from('<I',b,8+4*x)[0]
  while b[p]!=255:
   top,l=b[p:p+2]
   for y,c in enumerate(b[p+3:p+3+l],top):
    if y<h:im[y][x]=c
   p+=l+4
 return im
font={chr(c):decode(W[f'STCFN{c:03}']) for c in range(33,96) if f'STCFN{c:03}' in W}
def nearest(rgb):return min(range(256),key=lambda i:sum((PAL[i*3+j]-rgb[j])**2 for j in range(3)))
RED=nearest((218,51,23));GOLD=nearest((240,185,89));WHITE=nearest((222,213,191));DARK=nearest((19,12,12));GRAY=nearest((142,126,107))
def width(text,scale=1):return sum((4 if c==' ' else len(font.get(c,font['?'])[0])+1)*scale for c in text.upper())
def draw(im,text,x,y,scale=1,color=WHITE):
 for c in text.upper():
  if c==' ':x+=4*scale;continue
  glyph=font.get(c,font['?'])
  for gy,row in enumerate(glyph):
   for gx,p in enumerate(row):
    if p is None or max(PAL[p*3:p*3+3])<100:continue
    for yy in range(scale):
     for xx in range(scale):
      a=x+gx*scale+xx;b=y+gy*scale+yy
      if 0<=b<len(im) and 0<=a<len(im[0]):im[b][a]=color
  x+=(len(glyph[0])+1)*scale

def patch(im,left=0,top=0):
 h=len(im);w=len(im[0]);out=bytearray(struct.pack('<4h',w,h,left,top)+bytes(4*w))
 for x in range(w):
  struct.pack_into('<I',out,8+4*x,len(out));y=0
  while y<h:
   while y<h and im[y][x] is None:y+=1
   if y==h:break
   start=y;ps=[]
   while y<h and im[y][x] is not None and len(ps)<254:ps.append(im[y][x]);y+=1
   out.extend(bytes([start,len(ps),0]+ps+[0]))
  out.append(255)
 return bytes(out)
def png(im,path):
 w=len(im[0]);h=len(im);raw=b''.join(b'\0'+b''.join(PAL[(p or 0)*3:(p or 0)*3+3] for p in row) for row in im)
 def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
name='PURE HADES';w=width(name,2);im=[[None]*(w+2) for _ in range(22)];draw(im,name,2,2,2,nearest((61,10,2)));draw(im,name,0,0,2,GOLD)
(A/'M_DOOM.lmp').write_bytes(patch(im,left=(w+2-132)//2));png(im,R/'menu-preview.png')
im=[[DARK]*320 for _ in range(200)]
for y in (16,76,174):
 for x in range(16,304):im[y][x]=RED
for x in (16,303):
 for y in range(16,175):im[y][x]=RED
for text,y,scale,color in [(name,35,3,GOLD),('FIVE MAP DEATHMATCH',64,1,WHITE),('01 SHOTGUNS',87,1,WHITE),('02 ROCKETS',103,1,WHITE),('03 PLASMA',119,1,WHITE),('04 PURE CHAOS',135,1,WHITE),('05 DOUBLE-BARREL FINALE',151,1,WHITE),('FREEDOOM PHASE 2  /  V0.6',184,1,GRAY)]:draw(im,text,(320-width(text,scale))//2,y,scale,color)
(A/'TITLEPIC.lmp').write_bytes(patch(im));png(im,R/'title-preview.png')
for i,title in enumerate(['PURE HADES - SHOTGUNS','PURE HADES - ROCKETS','PURE HADES - PLASMA','PURE HADES - PURE CHAOS','PURE HADES - FINALE']):
 im=[[None]*width(title) for _ in range(10)];draw(im,title,0,0,1,GOLD);(A/f'CWILV{i:02}.lmp').write_bytes(patch(im))
print('Rendered Pure Hades title, menu logo and five intermission titles')
