"""Small WAD reader/writer and MIDI inspector; Python standard library only."""
import struct,hashlib

def sha(b):return hashlib.sha256(b).hexdigest()
def read_wad(b):
 magic,n,o=struct.unpack_from('<4sII',b)
 assert magic in (b'IWAD',b'PWAD') and o+n*16==len(b)
 out=[]
 for i in range(n):
  p,s,k=struct.unpack_from('<II8s',b,o+i*16)
  assert p>=12 and p+s<=o
  out.append((k.rstrip(b'\0').decode(),b[p:p+s]))
 return out

def write_wad(lumps):
 out=bytearray(12);entries=[]
 for k,b in lumps:
  assert len(k)<=8
  entries.append((len(out),len(b),k.encode().ljust(8,b'\0')));out.extend(b)
 o=len(out)
 for entry in entries:out.extend(struct.pack('<II8s',*entry))
 struct.pack_into('<4sII',out,0,b'PWAD',len(entries),o)
 return bytes(out)

def midi_info(b):
 assert b[:4]==b'MThd'
 h,fmt,nt,ppq=struct.unpack_from('>IHHH',b,4);assert fmt in (0,1) and not ppq&0x8000
 tempos=[(0,500000)];programs=set();notes=0;ends=[];texts=[];pos=8+h
 def vlq(t,p):
  v=0
  for _ in range(4):
   c=t[p];p+=1;v=(v<<7)|(c&127)
   if not c&128:return v,p
  raise ValueError('Invalid MIDI VLQ')
 for _ in range(nt):
  assert b[pos:pos+4]==b'MTrk'
  n=struct.unpack_from('>I',b,pos+4)[0];t=b[pos+8:pos+8+n];assert len(t)==n;pos+=8+n
  p=0;tick=0;status=0;ended=False
  while p<len(t):
   dt,p=vlq(t,p);tick+=dt
   if t[p]&128:status=t[p];p+=1
   assert status>=128
   if status==255:
    typ=t[p];p+=1;l,p=vlq(t,p);v=t[p:p+l];assert len(v)==l;p+=l
    if typ==81:assert l==3;tempos.append((tick,int.from_bytes(v,'big')))
    if typ in (1,2,3):texts.append(v.decode('latin1','replace'))
    if typ==47:ended=True
   elif status in (240,247):
    l,p=vlq(t,p);p+=l
   else:
    op=status&240;l=1 if op in (192,208) else 2;v=t[p:p+l];assert len(v)==l and all(c<128 for c in v);p+=l
    if op==192:programs.add(v[0])
    if op==144 and v[1]:notes+=1
  assert ended
  ends.append(tick)
 assert pos==len(b)
 last=max(ends);prev=0;tempo=500000;seconds=0
 for tick,t in sorted(tempos,key=lambda v:v[0]):
  if tick>last:break
  seconds+=(tick-prev)*tempo/1e6/ppq;prev=tick;tempo=t
 seconds+=(last-prev)*tempo/1e6/ppq
 return {'format':fmt,'tracks':nt,'ppq':ppq,'seconds':round(seconds,3),'note_on_events':notes,'gm_programs_zero_based':sorted(programs),'embedded_text':texts}
