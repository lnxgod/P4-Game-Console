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
