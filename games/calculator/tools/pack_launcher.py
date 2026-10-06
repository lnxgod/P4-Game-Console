#!/usr/bin/env python3
"""Reproduce original semantic utility artwork and its packed cartridge icon.
Offline Pillow only; no runtime image dependency or text baked into the icon.
"""
from pathlib import Path
from PIL import Image,ImageDraw
import importlib.util
ROOT=Path(__file__).resolve().parents[3]
GAME=Path(__file__).resolve().parents[1]
ink="#071822"
image=Image.new("RGB",(512,288),ink)
d=ImageDraw.Draw(image)
d.rounded_rectangle((20,18,492,270),radius=28,fill="#102d3c",outline="#254957",width=3)
accent="#ffd269"
d.rounded_rectangle((184,61,328,236),radius=21,fill=accent)
d.rounded_rectangle((195,72,317,225),radius=13,fill=ink)
d.rounded_rectangle((206,85,306,120),radius=6,fill=accent)
for row in range(3):
 for col in range(3):
  x=208+col*34;y=137+row*27
  d.rounded_rectangle((x,y,x+24,y+17),radius=4,fill=accent)
image.save(GAME/"assets/launcher-source.png")
spec=importlib.util.spec_from_file_location("pack_game_icon",ROOT/"scripts/pack-game-icon.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
(GAME/"assets/launcher.p4i").write_bytes(module.pack(GAME/"assets/launcher-source.png"))
