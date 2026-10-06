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
accent="#67e7ee"
d.rounded_rectangle((159,94,353,223),radius=40,fill=accent)
d.rounded_rectangle((173,109,339,209),radius=29,fill=ink)
d.rounded_rectangle((190,143,247,158),radius=4,fill=accent)
d.rounded_rectangle((211,123,226,180),radius=4,fill=accent)
d.ellipse((292,129,310,147),fill=accent)
d.ellipse((270,162,288,180),fill=accent)
d.line((252,94,252,73,287,73),fill=accent,width=10)
image.save(GAME/"assets/launcher-source.png")
spec=importlib.util.spec_from_file_location("pack_game_icon",ROOT/"scripts/pack-game-icon.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
(GAME/"assets/launcher.p4i").write_bytes(module.pack(GAME/"assets/launcher-source.png"))
