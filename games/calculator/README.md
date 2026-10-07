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

All visuals are original code-rendered RGB565 primitives. No raster or
third-party assets are used. License: MIT.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/calculator/tools/pack_launcher.py` (offline Pillow only).
