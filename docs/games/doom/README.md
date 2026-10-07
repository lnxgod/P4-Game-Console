# Doom

[Monorepo](../../../README.md) · [Console OS](../../../apps/console_os/README.md) · [Arena mode](../arena/README.md)

Doom is an OS-integrated game in this monorepo. The pinned doomgeneric engine,
platform input/video/audio adapters and OS launch path are separate from
native `.P4G` cartridges. Default setup uses the verified **Doom v1.9 shareware**
IWAD. The commercial game data is not included.

## Play on Tab5

Install compatible Console OS and verified `DOOM1.WAD` on microSD, then select
Doom in the launcher. Use the visible touch controls for movement, fire, Use
and menus. Supported physical controllers feed the same normalized input
adapters; see [controller mappings and board limits](../../../docs/CONTROLLERS.md).
Music and sound effects use the engine audio integration. Exact device audio
and cadence acceptance belongs to the selected image's record.

Ordinary Doom multiplayer uses the OS **Multiplayer → Host/Join** flow and
its two-player engine adapter. Both consoles must match the game data and
settings. The separate [Game Changers AI arena](../arena/README.md) supplies
the four-slot Wi-Fi mode; do not apply its player count to ordinary Doom.

## Data, build and installation

From the repository root:

```sh
make prepare-game-data
make doom-smoke WAD=local-data/doom/doom1.wad
make console-os-tab5-idf
python3 scripts/p4-usb-content.py doom --port /dev/cu.usbmodem...
```

The first command obtains missing verified shareware in ignored local storage.
The smoke target is headless engine validation, not an interactive game test.
The firmware build does not install it; follow the [Tab5 guide](../../boards/M5STACK_TAB5.md)
for guarded OS installation before sending content to a compatible running OS.
Leave the console at the launcher during content transfer.

Exact data identities and acquisition sources live in
[`third_party/game-data.json`](../../../third_party/game-data.json). Keep the
shareware notices and WAD outside Git. Engine source is GPL-covered; see
[third-party policy](../../../third_party/README.md). The original Pure Hades
publication exception does not authorize committing Doom's IWAD.

## Source and evidence

- [Engine host adapter](../../../apps/doom/) and [Doom build tools](../../../scripts/doom/).
- [Console OS integration](../../../apps/console_os/) and shared
  [multiplayer adapter](../../../components/doom_multiplayer/).
- [Acceptance design and historical engine bring-up](../../DOOM.md).
- [Current Tab5 artifact and device records](../../boards/M5STACK_TAB5.md).

Do not flash the old standalone acceptance applications as the Tab5 console.
A passed build, headless smoke or content hash is not proof of physical
controls, speaker quality or sustained multiplayer frame rate.
