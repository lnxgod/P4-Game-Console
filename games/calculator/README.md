# Calculator

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included system utility. **Folder:** `SYSTEM/TOOLS`.

**Package:** `CALC.P4G`. **Players:** One operator; no linked game mode.

**Local preview:** `make play-game GAME=calculator` from the repository root.

Calculator is an original, code-rendered integer desk calculator packaged as
`CALC.P4G`. It appears under `SYSTEM/TOOLS` and runs entirely through P4 Game
API v1, so it can be added, replaced, or removed from microSD without changing
Console OS.

Use the D-pad to select a key and A to press it. B erases one digit, Start
equals, Back returns to Program Manager, and the keypad can be tapped directly.
Results are bounded to -999,999,999 through 999,999,999; overflow and division
by zero fail closed with `ERROR`.

Version 1.0.3 requires `video-highres` in both `game.json` and its C descriptor.
On maintained Tab5, keys, the result display and text are rasterized directly into
the native 768×480 RGB565 surface. Touch coordinates remain canonical 320×200
input units. The native keypad and its hit-testing fit above the D-pad touch
region, so the C key can be tapped without also pressing Up. The original
320×200 layout/rendering branch remains available for explicitly requested legacy
diagnostics/source maintenance; the released descriptor rejects a low-resolution
launch.

The calculator visuals are original code-rendered RGB565 primitives under MIT.
Native text uses the shared pinned Arimo font, licensed separately under
[SIL OFL 1.1](../../third_party/arimo/OFL.txt).

On 2026-10-07, the ASan/UBSan arithmetic/lifecycle and shared utility tests passed,
as did the native 768×480 SDL smoke. The utility tests cover surface/stride guards,
low-resolution service rejection and all 16 mapped keypad taps against rendered
key borders. The native host capture was visually checked for readability.
Reproduce the host checks with `make calculator-host`. Host tests and captures
do not establish Tab5 FPS acceptance: the 60 FPS target and actual-device 30 FPS
release floor remain pending measurement for this version.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/calculator/tools/pack_launcher.py` (offline Pillow only).

## Current maintained Tab5 native rendering

Version 1.0.3 requires `video-highres` in both its native manifest and C
descriptor. The maintained Tab5 renderer draws geometry and glyphs directly
into a 768×480 RGB565 surface. Canonical 320×200 coordinates are input/layout
units only; no completed low-resolution frame is upscaled. Any retained
320×200 rendering or board descriptions above apply only to explicitly
requested legacy diagnostics/source maintenance.

Native labels clear the standard controls. The native keypad and its matching touch hit-testing end above the D-pad region, so tapping C does not also press Up.

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
