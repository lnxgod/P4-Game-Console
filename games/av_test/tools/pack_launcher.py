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
accent="#e8a4ea"
d.rounded_rectangle((143,120,185,177),radius=6,fill=accent)
d.polygon([(184,120),(236,80),(236,215),(184,177)],fill=accent)
d.arc((212,99,307,198),-62,62,fill=accent,width=12)
d.arc((206,67,350,229),-60,60,fill=accent,width=12)
image.save(GAME/"assets/launcher-source.png")
spec=importlib.util.spec_from_file_location("pack_game_icon",ROOT/"scripts/pack-game-icon.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
(GAME/"assets/launcher.p4i").write_bytes(module.pack(GAME/"assets/launcher-source.png"))
