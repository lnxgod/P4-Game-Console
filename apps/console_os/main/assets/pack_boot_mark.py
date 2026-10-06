#!/usr/bin/env python3
"""Pack the reviewed transparent mark for the RGB565 + alpha8 boot renderer."""
from pathlib import Path
import hashlib, json
from PIL import Image
ROOT = Path(__file__).resolve().parent
source = ROOT / 'gamechangers_mark_v042.png'
image = Image.open(source).convert('RGBA').resize((384,384), Image.Resampling.LANCZOS)
packed = bytearray()
for r,g,b,a in image.getdata():
    v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    packed.extend((v & 255, v >> 8, a))
out = ROOT / 'gamechangers_mark_v042.rgb565a8'
out.write_bytes(packed)
print('Packed',len(packed),'bytes; SHA256',hashlib.sha256(packed).hexdigest())
