#!/usr/bin/env python3
"""Lanczos-fit original ImageGen cover into a bounded RGB565 title image."""
from pathlib import Path
from PIL import Image, ImageOps
root=Path(__file__).resolve().parents[1]
im=ImageOps.fit(Image.open(root/'assets/launcher-source.png').convert('RGB'),(288,162),method=Image.Resampling.LANCZOS)
values=[((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b in im.getdata()]
(root/'src/generated/cover.inc').write_text('// Original ImageGen art, 288x162 RGB565, Lanczos fit.\nstatic const uint16_t cover[46656]={\n'+''.join(','.join(f'0x{x:04x}' for x in values[i:i+16])+',\n' for i in range(0,len(values),16))+'};\n')
