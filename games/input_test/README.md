# Input Monitor

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included system utility. **Folder:** `SYSTEM/TESTS`.

**Package:** `INPUT.P4G`. **Players:** One operator; no linked game mode.

**Local preview:** `make play-game GAME=input_test` from the repository root.

Input Monitor is a removable `INPUT.P4G` diagnostic under `SYSTEM/TESTS`. It
shows the normalized held, pressed, and released button masks, current touch
contacts, and an event counter. Start clears the counter and Back returns to
Program Manager.

The app sees only P4 Game API input snapshots. It owns no GT911, USB, GPIO, or
board handle, so the same package can test the Waveshare touch path and PC
keyboard/mouse mapping through Console OS.

All visuals are original code-rendered RGB565 primitives. No raster or
third-party assets are used. License: MIT.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/input_test/tools/pack_launcher.py` (offline Pillow only).

## Current maintained Tab5 native rendering

Version 1.0.3 requires `video-highres` in both its native manifest and C
descriptor. The maintained Tab5 renderer draws geometry and glyphs directly
into a 768×480 RGB565 surface. Canonical 320×200 coordinates are input/layout
units only; no completed low-resolution frame is upscaled. Any retained
320×200 rendering or board descriptions above apply only to explicitly
requested legacy diagnostics/source maintenance.

Native headers and reset instructions clear the standard controls, and canonical touch contacts are displayed at their corresponding native pixel positions.

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
