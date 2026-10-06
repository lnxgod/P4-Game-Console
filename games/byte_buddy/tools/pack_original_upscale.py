#!/usr/bin/env python3
"""Preserve the original 672 cells; pack their masters at 128px for native video.

The unchanged 64px bank remains first for exact fallback rendering. A bounded
128px, 5bpp/32-color bank follows it, keeping the complete P4R below 8 MiB.
No generated replacement art, runtime PNG loader, or gameplay identity changes.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
from PIL import Image

GAME = Path(__file__).resolve().parents[1]
ROOT = GAME.parents[1]
SIZE = 128
COLORS = 32
MAGIC = b"BBDHD5\0\0"


def original_converter():
    spec = importlib.util.spec_from_file_location(
        "byte_buddy_original_atlas", GAME / "tools/png_to_dragon_atlas.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def encode(frame):
    opaque = [tuple(frame[i:i+3]) for i in range(0, len(frame), 4)
              if frame[i+3] >= 128]
    if not opaque:
        return bytes(64), bytes(SIZE*SIZE*5//8)
    image = Image.new("RGB", (len(opaque), 1))
    image.putdata(opaque)
    indexed = image.quantize(colors=COLORS-1, method=Image.Quantize.MEDIANCUT,
                             dither=Image.Dither.NONE)
    raw_palette = indexed.getpalette() + [0] * (COLORS*3)
    palette = [0]
    for i in range(COLORS-1):
        r, g, b = raw_palette[i*3:i*3+3]
        palette.append(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    indices = iter(indexed.tobytes())
    packed = bytearray()
    bits = count = 0
    for i in range(0, len(frame), 4):
        value = next(indices)+1 if frame[i+3] >= 128 else 0
        bits |= value << count
        count += 5
        if count >= 8:
            packed.append(bits & 255)
            bits >>= 8
            count -= 8
    assert count == 0 and len(packed) == SIZE*SIZE*5//8
    return struct.pack("<32H", *palette), packed


def main():
    converter = original_converter()
    sources = [GAME / "assets" / name for name in converter.EXPECTED_SHEETS]
    converter.validate_sources(sources)
    converter.FRAME_WIDTH = converter.FRAME_HEIGHT = SIZE
    converter.RUNTIME_SHEET_WIDTH = converter.RUNTIME_SHEET_HEIGHT = SIZE*4
    converter.LARGE_BACKGROUND_COMPONENT_PIXELS = 256
    palettes, pixels = bytearray(), bytearray()
    for sheet, source in enumerate(sources):
        raw = converter.decode_sheet(source)
        for cell in range(16):
            rgba = converter.frame_rgba(raw, cell)
            converter.clear_baked_checkerboard(
                rgba, sheet in converter.ENCLOSED_BACKGROUND_SHEET_INDEXES)
            palette, packed = encode(rgba)
            palettes.extend(palette)
            pixels.extend(packed)
        print(f"packed {sheet+1}/{len(sources)} {source.name}", flush=True)
    frames = len(sources)*16
    total = 64 + len(palettes) + len(pixels)
    header = struct.pack("<8s11I12x", MAGIC, 1, len(sources), SIZE, SIZE,
                         16, COLORS, frames, 64, 64+len(palettes), total, 0)
    legacy = (GAME / "assets/generated/byte_buddy_dragon_art.bin").read_bytes()
    assert hashlib.sha256(legacy).hexdigest() == "ac76b0e89ec0c7c0f18703d2baf853764eded82f1edc067dc3c877a0f14c2931"
    result = legacy + header + palettes + pixels
    assert len(result)+128 <= 8*1024*1024
    output = GAME / "assets/generated/byte_buddy_dragon_art_hd.bin"
    output.write_bytes(result)
    # The launcher uses the same original game cells, not a replacement cover.
    cover = Image.new("RGBA", (256, 144), (8, 0, 123, 255))
    for sheet, cell, xy, target in [(29, 1, (2, 20), (126, 126)),
                                   (29, 2, (184, 4), (64, 64)),
                                   (13, 0, (104, 0), (144, 144))]:
        rgba = converter.frame_rgba(converter.decode_sheet(sources[sheet]), cell)
        converter.clear_baked_checkerboard(rgba, sheet in converter.ENCLOSED_BACKGROUND_SHEET_INDEXES)
        sprite = Image.frombytes("RGBA", (SIZE, SIZE), bytes(rgba))
        cover.alpha_composite(sprite.resize(target, Image.Resampling.NEAREST), xy)
    cover_path = GAME / "assets/original-upscale/launcher-source.png"
    cover.convert("RGB").save(cover_path)
    spec = importlib.util.spec_from_file_location("original_icon_packer", ROOT / "scripts/pack-game-icon.py")
    packer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packer)
    icon_path = GAME / "assets/original-upscale/launcher.p4i"
    icon_path.write_bytes(packer.pack(cover_path))
    record = {
        "schema": 1, "date": "2026-10-05", "mode": "Original art only; deterministic runtime upscale",
        "sources": [{"path": "assets/"+p.name, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                    for p in sources],
        "conversion": "Original 4x4 cell mapping and nearest-neighbor sampling at 512x512 per sheet; same alpha/checkerboard cleanup, area threshold scaled 4x; 31 opaque median-cut colors plus transparent index 0; RGB565 palette and little-endian 5-bit pixels. No dithering.",
        "pillow": Image.__version__, "frames": frames, "native_frame": [SIZE, SIZE],
        "legacy_frame": [64, 64], "legacy_bytes": len(legacy), "native_bytes": total,
        "resource_payload_bytes": len(result), "resource_payload_sha256": hashlib.sha256(result).hexdigest(),
        "legacy_sha256": hashlib.sha256(legacy).hexdigest(),
        "source_pngs_modified": False, "new_images_generated": False,
        "launcher": {"mode": "Original environment sheet 29 cells 1/2 and baby-idle sheet 13 cell 0, nearest-neighbor composition; shared P4ICON1 conversion",
                     "source_sha256": hashlib.sha256(cover_path.read_bytes()).hexdigest(),
                     "packed_sha256": hashlib.sha256(icon_path.read_bytes()).hexdigest()},
    }
    (GAME / "assets/original-upscale/provenance.json").write_text(json.dumps(record, indent=2)+"\n")
    print(json.dumps({k:v for k,v in record.items() if k != "sources"}, indent=2))


if __name__ == "__main__":
    main()
