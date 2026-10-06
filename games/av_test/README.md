# AV Test

AV Test is a removable `AVTEST.P4G` diagnostic under `SYSTEM/TESTS`. Left and
Right cycle color bars, a geometry grid, checkerboard pixels, and a color
gradient. A plays a four-step tone sequence, B pauses or resumes the 30 Hz
motion marker, Start resets its counters, and Back returns to Program Manager.

The app uses only the 320x200 RGB565 surface, normalized controls, bounded
timing, and the optional host-owned tone service. It does not access display,
codec, I2S, touch, or SD hardware directly.

All visuals and tones are original and code-generated. No raster or
third-party assets are used. License: MIT.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/av_test/tools/pack_launcher.py` (offline Pillow only).
