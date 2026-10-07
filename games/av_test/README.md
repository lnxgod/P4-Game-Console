# Sound & Motion

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included system utility. **Folder:** `SYSTEM/TESTS`.

**Package:** `AVTEST.P4G`. **Players:** One operator; no linked game mode.

**Local preview:** `make play-game GAME=av_test` from the repository root.

Sound & Motion is a removable `AVTEST.P4G` diagnostic under `SYSTEM/TESTS`. Left and
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

## Current maintained Tab5 native rendering

Version 1.0.3 requires `video-highres` in both its native manifest and C
descriptor. The maintained Tab5 renderer draws geometry and glyphs directly
into a 768×480 RGB565 surface. Canonical 320×200 coordinates are input/layout
units only; no completed low-resolution frame is upscaled. Any retained
320×200 rendering or board descriptions above apply only to explicitly
requested legacy diagnostics/source maintenance.

Native headers and the pattern/tone instructions are separated from the standard touch controls; the frame counter is visible in the central footer.

The utility's primitive visuals remain original MIT code. Native text uses the
shared pinned Arimo font under [SIL OFL 1.1](../../third_party/arimo/OFL.txt);
earlier descriptions of third-party-free visuals do not describe that native
font.

The global target is 60 FPS with an actual-device 30 FPS release floor.
Physical Tab5 acceptance remains pending for this version: preserve exact
package, OS and unit evidence, verify the runtime 768×480 surface, measure
busy-view cadence, and confirm readable title/control labels and appropriate
opening, pause/results views. Host tests or readable captures do not establish
that device acceptance.
