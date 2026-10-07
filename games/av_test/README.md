# Sound & Motion

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included system utility. **Folder:** `SYSTEM/TESTS`.

**Package:** `AVTEST.P4G`. **Players:** One operator; no linked game mode.

**Local preview:** `make play-game GAME=av_test` from the repository root.

Sound & Motion is a removable `AVTEST.P4G` diagnostic under `SYSTEM/TESTS`. Left and
Right cycle color bars, a geometry grid, checkerboard pixels, and a color
gradient. A plays a four-step tone sequence, B pauses or resumes the 30 Hz
motion marker, Start resets its counters, and Back returns to Program Manager.

Version 1.0.3 requires `video-highres` in both `game.json` and its C descriptor.
On maintained Tab5, the app draws its patterns, motion marker and text directly
into the native 768×480 RGB565 surface. Touch coordinates remain canonical
320×200 input units. The 320×200 rendering branch is retained only for explicitly
requested legacy diagnostics/source maintenance; the released descriptor
rejects a low-resolution launch.

The app uses normalized controls, bounded timing and the optional host-owned
tone service. It does not access display, codec, I2S, touch or SD hardware
directly. The 30 Hz motion-marker update is separate from display frame cadence.

The diagnostic patterns, primitives and tones are original and code-generated
under MIT. Native text uses the shared pinned Arimo font, licensed separately
under [SIL OFL 1.1](../../third_party/arimo/OFL.txt).

On 2026-10-07, the native 768×480 SDL smoke and shared ASan/UBSan utility tests
passed, including all four patterns, surface/stride guards and rejection of
low-resolution services. The native host capture was visually checked for
readability. Run `make av-test-host` for SDL smoke and `make calculator-host`
for the shared utility regressions. Host tests and captures do not establish
Tab5 FPS acceptance: the 60 FPS target and actual-device 30 FPS release floor
remain pending measurement for this version.

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
