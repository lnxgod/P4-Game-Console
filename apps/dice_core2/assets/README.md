# Core2 dice assets

`dice-atlas.rgb565` contains 96 looping tumble frames, six stopped faces,
and six held faces, each 64×64 RGB565 in big-endian byte order. Zero-valued pixels are transparent
so moving dice can overlap without rectangular backgrounds. The original
rounded-cube geometry, lighting, recessed pips and antialiasing are generated
by `scripts/render-dice-core2-atlas.py` (MIT). This moves floating-point
rendering off the ESP32. The PNG is a development contact sheet.

`kenney/` preserves six original recordings and their CC0 license from
[Kenney Casino Audio 1.1](https://kenney.nl/assets/casino-audio). The official
archive was downloaded from
`https://kenney.nl/media/pages/assets/casino-audio/2472606a04-1721639069/kenney_casino-audio.zip`.

`prepare-dice-core2-audio.py` converts these recordings to six 235 ms shake
segments and three complete landing clips. It uses mono 22,050 Hz PCM, a
120 Hz high-pass filter, peak normalization, and short edge fades. Shake contacts are positioned
approximately 35 ms after the motor/audio event for ERM spin-up. A secondary
landing pulse is emitted only when a later strong contact is detected in
that recording. Audio
metadata records source hashes, cut positions, lengths, and the derived hash.
There are no synthesized clanks in firmware 0.3.0.

Regenerate with Python providing NumPy/Pillow and ffmpeg on PATH:

```
python scripts/render-dice-core2-atlas.py
python scripts/prepare-dice-core2-audio.py
```

The small derived assets are checked in so an ordinary pinned IDF firmware
build does not need Python imaging libraries, ffmpeg, or network access.
