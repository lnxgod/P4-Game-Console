#!/usr/bin/env python3
"""Convert finished launcher artwork to the bounded cartridge icon format.

Requires Pillow only on the artwork author's machine; package builds and the
OS decode the fixed palette/pixel format without an image library.
"""
import argparse
import struct
from pathlib import Path
from PIL import Image, ImageOps

def pack(source: Path) -> bytes:
    with Image.open(source) as image:
        image = ImageOps.fit(image.convert('RGB'), (128,72), method=Image.Resampling.LANCZOS)
        indexed = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
        palette = indexed.getpalette()
        palette += [0] * (768-len(palette))
        rgb565 = [((palette[i]>>3)<<11)|((palette[i+1]>>2)<<5)|(palette[i+2]>>3) for i in range(0,768,3)]
        return b'P4ICON1\0'+struct.pack('<HHI',128,72,1)+struct.pack('<256H',*rgb565)+indexed.tobytes()
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();data=pack(args.source);assert len(data)==9744
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(data)
