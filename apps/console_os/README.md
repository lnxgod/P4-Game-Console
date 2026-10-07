# P4 Console OS

[Monorepo overview](../../README.md) · [Games](../../games/README.md) · [Tab5 guide](../../docs/boards/M5STACK_TAB5.md)

Console OS is the **shared platform** in the P4 Game Console monorepo. It is
currently branded *Game Changers AI OS* on the device. It provides the launcher,
system pages, hardware services and game lifecycle. The **Game Changers AI
multiplayer arena** is one integrated Doom-based game mode; its rules, maps,
content and acceptance belong in the [arena README](../../docs/games/arena/README.md).

## Primary device and current source

**M5Stack Tab5** is the actively maintained board. The current source release
is **0.57** (curated library and Blast Circuit home feature); the [board record](../../docs/boards/M5STACK_TAB5.md) identifies
exact installed images on A/ST7121 and B/ST7123. Installation/readback and
healthy launcher startup passed for those recorded images. Physical gameplay,
multiplayer, controls/audio and sustained game cadence require their own
acceptance records.

The Tab5 shell renders its 1280×720 interface with cached scrolling, cartridge
artwork, category views and protected file actions. Native games negotiate a
768×480 RGB565 surface or use the 320×200 fallback; the display backend owns
presentation to the panel. A higher-resolution shell is not a promise that
every game renders at that resolution.

## What the OS owns

| Service | Purpose |
| --- | --- |
| Launcher and catalog | Discover validated games, show their own icons/categories, and launch/return reliably |
| System pages | Settings, battery/storage/motion status, sound levels, file/game management and diagnostics |
| Input | Touch and normalized game controls; shared controller services where the board supports them |
| Audio | Bounded tones and PCM; the Tab5 native-game worker mixes/outputs on P4 core 1 while game callbacks run on core 0 |
| Storage and saves | SD-backed content, validated USB-C installs, protected data and stable game save identities |
| Multiplayer | P4MP Host/Join, matching packages, rosters, start barriers, transport routing and teardown |
| Legacy engines | Reviewed Doom/Chex/arena integration with separate game data and lifecycle ownership |

The C6 is the radio processor; it is separate from the P4's two application
cores. Games consume stable APIs instead of creating peripheral drivers or
private audio tasks. Bluetooth controller support is not currently enabled
on Tab5; normalized mappings alone do not establish named-controller support.
See [controllers](../../docs/CONTROLLERS.md) for exact limits.

## Games are separate projects

Native games and utilities live under [`games/`](../../games/README.md), each
with a manifest, source and README. Compatible `.P4G` cartridges and their
optional `.P4R` sidecars update independently of firmware. Protected payloads
such as Red Dragon require their exact paired OS lineage.

[Doom](../../docs/games/doom/README.md), [Chex Quest](../../docs/games/chex-quest/README.md)
and the [arena](../../docs/games/arena/README.md) use the engine integrated into
firmware, with verified files on SD. [Pure Hell](../../game-data/pure-hell/README.md)
is arena content. Experimental [Wacky Wheels](../../scripts/wacky/README.md)
and retained [Quake](../../ports/quake/README.md) have their own port workflows
and are not default native-library titles.

## Build and install for Tab5

Current game storage requires **microSD**; internal-flash game storage is not
implemented. With the pinned environment prepared, run from the repository root:

```sh
make setup                 # Only if the pinned tools are missing
make verify
make prepare-game-data     # Verified local Doom shareware
make console-os-tab5-idf
```

`make console-os-idf` is the Tab5 alias. Outputs are in `build-tab5/` below this
app, including firmware, the verified update package and `sd-card/` contents.
Builds do not flash hardware. Use [ESP32 - Set Up](../../.agents/skills/esp32-setup/SKILL.md)
and the [Tab5 guarded workflow](../../docs/boards/M5STACK_TAB5.md) for a real
install: preserve any new unit's complete factory image, bind its identity and
use the reviewed artifact. Never reuse a legacy board's offsets or authorization.

For a game-only update on a compatible OS, leave the game at the launcher,
keep SD inserted and use the actual USB-C port:

```sh
python3 scripts/p4-transfer.py push-bundle   apps/console_os/build-tab5/sd-card --port /dev/cu.usbmodem...
```

Use [ESP32 - Add Game](../../.agents/skills/esp32-add-game/SKILL.md) for single
packages and resources. Keep data, saves, SDK locks and recovery artifacts
intact. See [content handling](../../docs/CONTENT_LIBRARY.md).

## Development and validation

Shared services belong in [`components/`](../../components/README.md); app
integration belongs here. Run the focused tests for the changed boundary,
then the Tab5 build when firmware changed. Documentation-only edits require
reference checks, not a device flash. Host tests, firmware compilation,
verified install, physical controls/audio and measured frame cadence are
separate forms of evidence. See the [SDK](../../docs/GAME_SDK.md),
[performance contract](../../docs/GAME_PERFORMANCE.md) and
[multiplayer guide](../../docs/MULTIPLAYER.md).

## Older boards and history

Elecrow, Olimex and Waveshare are legacy targets with explicitly named builds
and independent safety/acceptance histories. The earlier README's versioned
scrolling, touch, audio and storage notes are retained in [HISTORY.md](HISTORY.md).
They do not define the Tab5 release or its installation route. Use the root
[legacy board table](../../README.md#legacy-board-ports) for the matching guide.
