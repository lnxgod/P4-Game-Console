#!/usr/bin/env python3
"""Reproduce this cartridge icon with the repository's bounded offline packer."""
from pathlib import Path
import importlib.util
ROOT=Path(__file__).resolve().parents[3]
GAME=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("pack_game_icon",ROOT/"scripts/pack-game-icon.py")
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
(GAME/"assets/launcher.p4i").write_bytes(module.pack(GAME/"assets/launcher-source.png"))
