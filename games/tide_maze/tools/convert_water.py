#!/usr/bin/env python3
"""Compile the original ImageGen material into bounded RGB565 light palettes."""
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
im=Image.open(root/'assets/water-source.png').convert('RGB').resize((128,128),Image.Resampling.LANCZOS)
values=[]
for brightness in (0.78,0.93,1.07):
 for pixel in im.getdata():
  r,g,b=(min(255,round(c*brightness)) for c in pixel)
  values.append((r>>3)<<11|(g>>2)<<5|(b>>3))
(root/'src/generated/water.inc').write_text('// Original water material, 128x128 RGB565 x3 light palettes; Lanczos.\nstatic const uint16_t water_material[49152]={\n'+''.join(','.join(f'0x{x:04x}' for x in values[i:i+16])+',\n' for i in range(0,len(values),16))+'};\n')
