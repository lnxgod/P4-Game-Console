# Native games and utilities

[Back to P4 Game Console](../README.md)

Each folder below owns a native C game's `game.json`, source, art, tests and
README. Console OS loads its `.P4G` cartridge; a declared `.P4R` supplies extra
resources. The manifest is the source of truth for identity, version, category,
capabilities and inclusion. An enabled manifest outside `GAMES/WIP` makes a package eligible for
the bundle, not automatically release-qualified.

## Games

| Game | Category | What you do | Players / mode | Availability |
| --- | --- | --- | --- | --- |
| [Air Hockey](p4_air_hockey/README.md) | Sports | Move your paddle by touch; play the CPU or a friend. | Solo vs CPU; 2 linked | Included |
| [Blast Circuit](blast_circuit/README.md) | Arcade | Bomb battles with three arenas, bots and a level editor. | Solo vs bots; 2–4 linked | Included candidate |
| [Checkers](checkers/README.md) | Tabletop | Compulsory captures, chained jumps and kings. | 2 on one device or 2 linked | Included |
| [Color Clash](color_clash/README.md) | Cards | Match colors and action cards to empty your hand. | Solo vs CPUs; 2–4 linked | Included |
| [Frog Hop](frog_hop/README.md) | Arcade | Cross traffic and rivers to fill five lily-pad homes. | Solo | Included |
| [Maze Chase](maze_chase/README.md) | Arcade | Collect pellets, power up and evade the spirits. | Solo | Included |
| [Rummy 500](p4_rummy/README.md) | Cards | Draw, build melds, lay off cards and reach 500. | Solo vs CPUs; 2–4 linked | Included |
| [Solitaire](solitaire/README.md) | Cards | Klondike with direct-touch cards and drag-and-drop. | Solo | Included |
| [Space Invaders](space_invaders/README.md) | Arcade | Defend destructible shields against alien waves. | Solo | Included |
| [Texas Hold'em](texas_holdem/README.md) | Cards | Poker with betting, all-ins and side pots. | 2–4 local human/CPU seats; 2–4 linked | Included |
| [Tide Maze](tide_maze/README.md) | Arcade | Tilt a flooded marble maze and collect pearls. | Solo; 2 linked co-op | Included candidate; device lag acceptance open |
| [Yahtzee](p4_yahtzee/README.md) | Tabletop | Roll, hold dice and fill the scorecard. | 2–4 pass-and-play or linked | Included |

Held-back games are available only through an explicit developer install:

| Game | Category | What you do | Players / mode | Availability |
| --- | --- | --- | --- | --- |
| [Byte Buddy](byte_buddy/README.md) | WIP | Raise a dragon, explore Signal City and play activities. | Solo | Developer install only; requires resource sidecar |
| [Red Dragon](lord/README.md) | WIP | A text-and-ANSI fantasy adventure with saved progression. | Solo; 2-player linked profile / realm features | Developer install only; protected OS pairing |
| [Skyline Leap](skyline_leap/README.md) | WIP | A rooftop platformer retained for further development. | Solo | Developer install only; unfinished |

## Utilities

- [Calculator](calculator/README.md): Integer calculator with a touch keypad.
- [Input Monitor](input_test/README.md): Inspect normalized buttons and touch input.
- [Sound & Motion](av_test/README.md): Check screen patterns, animation and tones.

Doom, Chex Quest and the Game Changers AI arena use the OS-integrated engine.
Wacky Wheels and Quake use separate port workflows. Find their own READMEs in
the [root inventory](../README.md#engine-games-and-ports). Pure Hades is a map
pack, not a native cartridge.

## Build and play

Run commands from the repository root with the pinned development tools ready.
Replace `frog_hop` with the owning directory name, not the launcher title:

```sh
make play-game GAME=frog_hop
cmake -S tools/p4-game-host -B build-host/play-frog_hop -G Ninja -DP4_GAME=frog_hop
cmake --build build-host/play-frog_hop
ctest --test-dir build-host/play-frog_hop --output-on-failure
```

Use the game's README for focused rules/protocol tests and resource setup.
`make play-game` also previews disabled drafts; direct CMake configurations
need `-DP4_ALLOW_DRAFT_GAME=ON` for them. Some games require resource sidecars.

## Install on Tab5

Build/package with [ESP32 - Add Game](../.agents/skills/esp32-add-game/SKILL.md)
and the [SDK](../docs/GAME_SDK.md). The enabled bundle comes from
`make console-os-tab5-idf`; firmware construction itself does not flash a unit.
For an existing compatible OS, leave the console at the launcher and use its
actual native USB-C port:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G --port /dev/cu.usbmodem...
```

If the manifest declares resources, validate and upload the matching `.P4R`
before the cartridge. Preserve saves and package IDs. Protected titles such
as Red Dragon need their exact paired OS lineage; follow the SDK's
[protected-payload contract](../docs/GAME_SDK.md#protected-game-payloads).
A compatible game-only update normally needs no OS reflash. First-time OS
installation belongs to [ESP32 - Set Up](../.agents/skills/esp32-setup/SKILL.md).

## Developer installs

Byte Buddy, Red Dragon and Skyline Leap are hidden from standard installs.
Their sources, game IDs and save namespaces remain intact. Local previews still
work with `make play-game GAME=<slug>`. To explicitly install Byte Buddy on a
Tab5 that is at the launcher:

```sh
make install-dev GAME=byte_buddy PORT=/dev/cu.usbmodem...
```

Use `GAME=skyline_leap` for that draft. `make dev-games` builds all development
cartridges into `apps/console_os/build-tab5/dev-games/GAMES`, separate from the
normal `sd-card/GAMES` bundle. The installer copies only the selected game and
its required resources. Standard `push-bundle` rejects WIP cartridges unless
`--include-dev` is explicitly supplied. Ordinary `push` remains an individual
file operation for deliberate custom workflows.

Red Dragon additionally requires `PROTECTED_PAYLOAD_SHA256=<64 hex digits>`
from the **installed OS's frozen lineage evidence**. The installer rejects a
mismatched payload before opening USB. An OS rebuild alone is not evidence
that the device has that matching OS; follow the protected-payload contract.
Holding back a game never deletes its saves or retires its IDs.

## Test status and multiplayer

Per-title READMEs and their linked records separate host tests, interactive
play, installation and physical acceptance. A linked player count is a game
protocol capability, not proof that those many tablets have been tested.
Tab5 Local Wi-Fi permits up to four where the game profile supports it;
Bluetooth and USB serial permit two. Games use OS Host/Join and the shared
start barrier; same-device play is a separate mode.

Use [ESP32 - Start Here](../.agents/skills/esp32-start/SKILL.md) to select the
workflows automatically. Follow [library policy](../docs/GAME_LIBRARY.md),
[presentation](../docs/GAME_ART.md) and [performance](../docs/GAME_PERFORMANCE.md).
Keep [retired identities](retired.json) reserved. A new scaffold stays disabled
until it has a complete, play-tested loop and honest device status.
