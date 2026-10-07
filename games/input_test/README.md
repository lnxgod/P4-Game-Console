# Input Monitor

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included system utility. **Folder:** `SYSTEM/TESTS`.

**Package:** `INPUT.P4G`. **Players:** One operator; no linked game mode.

**Local preview:** `make play-game GAME=input_test` from the repository root.

Input Monitor is a removable `INPUT.P4G` diagnostic under `SYSTEM/TESTS`. It
shows the normalized held, pressed, and released button masks, current touch
contacts, and an event counter. Start clears the counter and Back returns to
Program Manager.

Version 1.0.3 requires `video-highres` in both `game.json` and its C descriptor.
On maintained Tab5, buttons, touch markers and text are rasterized directly into
the native 768×480 RGB565 surface. Touch coordinates remain canonical 320×200
input units and markers map those coordinates into native pixels. The 320×200
rendering branch is preserved for explicitly requested legacy diagnostics/source
maintenance; the released descriptor rejects a low-resolution launch.

The app sees only P4 Game API input snapshots. It owns no GT911, USB, GPIO or
board handle. Maintained validation targets Tab5; PC keyboard/mouse input uses
the same normalized API through the native host.

The diagnostic visuals are original code-rendered RGB565 primitives under MIT.
Native text uses the shared pinned Arimo font, licensed separately under
[SIL OFL 1.1](../../third_party/arimo/OFL.txt).

On 2026-10-07, the native 768×480 SDL smoke and shared ASan/UBSan utility tests
passed, including surface/stride guards, low-resolution service rejection and
native touch-marker alignment. The native host capture was visually checked for
readability. Run `make input-test-host` for SDL smoke and `make calculator-host`
for the shared utility regressions. These host results do not establish Tab5
FPS acceptance: the 60 FPS target and actual-device 30 FPS release floor remain
pending measurement for this version.

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
