# Chex Quest

[Monorepo](../../../README.md) · [Console OS](../../../apps/console_os/README.md) · [Doom engine](../doom/README.md)

Chex Quest is an **optional game** using the integrated Doom engine. It has
its own launcher/content identity and five supported levels, E1M1–E1M5.
It is not part of the default setup and is not a `.P4G` cartridge.

## Play and multiplayer

Choose Chex Quest from the optional games after installing its verified data.
The Doom touch controls cover movement, fire, Use and menus. Physical input
uses the shared [controller service](../../CONTROLLERS.md) when supported by
the selected board. Multiplayer uses the OS Host/Join flow and the existing
two-player adapter. Every console must select Chex with matching data; a Doom
room cannot admit a Chex player. The arena's four-player mode is a separate game.

## Data and installation

Explicitly select Chex during [ESP32 - Set Up](../../../.agents/skills/esp32-setup/SKILL.md),
or install it later with microSD present. The exact original `CHEX.WAD` and
matching `CHEX.DEH` patch must pass the identities pinned in
[`third_party/game-data.json`](../../../third_party/game-data.json). Local inputs
are `local-data/doom/chex.wad` and `local-data/doom/chex.deh`; both stay ignored.

With those files prepared, a compatible Console OS running and the console
at the launcher, run from the repository root using its actual USB-C port:

```sh
python3 scripts/p4-usb-content.py chex --port /dev/cu.usbmodem...
```

Both files are required; an optional launcher tile alone does not mean the
data has been installed. Adding verified data to a compatible engine needs
no firmware rebuild. Engine changes follow `make console-os-tab5-idf` and the
[Tab5 guarded workflow](../../boards/M5STACK_TAB5.md).

The original release's WAD is a specifically accepted PWAD; that is not a
promise to load arbitrary PWADs or newer Chex games. Preserve original notices
and do not commit the WAD, patch or WAD-bearing firmware. See the
[Doom content/validation contract](../../DOOM.md) and
[content library](../../CONTENT_LIBRARY.md). Physical gameplay and multiplayer
acceptance must name the actual OS, content and devices.
