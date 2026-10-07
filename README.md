# P4 Game Console

**A monorepo for an ESP32-P4 game console: Console OS, games, shared services,
development tools and AI skills in one repository.** The primary device is the
**M5Stack Tab5**. Open this repository to build the console, play or create a
game, work on a port, or improve the shared platform.

**Console OS** is the launcher and system platform, currently branded
*Game Changers AI OS* on the device. **Doom Arena by Game Changers** is
one Doom-based game mode that runs inside it. **Pure Hades** is a map pack used
by that mode. Each has its own documentation below.

## What is in this monorepo?

| Part | What it contains | Start here |
| --- | --- | --- |
| Console OS | Tab5 launcher, settings, game/file management, lifecycle and engine integration | [OS README](apps/console_os/README.md) |
| Native games | C source, manifests, art, rules, saves and tests; delivered as `.P4G` cartridges | [Game workspace](games/README.md), inventory below |
| Engine games and ports | Doom, optional Chex Quest, the arena mode, experimental Wacky Wheels and retained Quake work | [Ports and engine games](#engine-games-and-ports) |
| Shared services | Game API, rendering, input, audio/MIDI, storage, P4MP multiplayer and board adapters | [Components guide](components/README.md), [SDK](docs/GAME_SDK.md) |
| Tools and diagnostics | SDL3 local runners, build/package/upload tools, hardware diagnostics and the Core2 dice accessory | [Apps guide](apps/README.md), [tools guide](tools/README.md), [dice accessory](apps/dice_core2/README.md) |
| AI workflows | Automatically selected skills for making games, art, testing and console setup | [ESP32 - Start Here](.agents/skills/esp32-start/SKILL.md), [all skills](.agents/skills/README.md) |
| Hardware and evidence | Board profiles, guarded installation records and exact-artifact test results | [Tab5 guide](docs/boards/M5STACK_TAB5.md), [architecture](docs/ARCHITECTURE.md) |

Tab5 is the only actively maintained board target. Other board ports are
retained for existing devices; see [legacy boards](#legacy-board-ports).
Current Tab5 game storage requires microSD. Games normally update through
USB-C while the card stays inserted; compatible cartridge updates need no
OS reflash.

[Blast Circuit](games/blast_circuit/README.md) is the featured native game: bomb
battles, three arenas, bots and up to four linked players. Tide Maze was removed
from the product library at the owner’s request; its disabled development source
and failed device acceptance records remain available.

## Native game inventory

There are **15 native game source directories**: 11 standard-install games
and 4 hidden development games. Three additional native
packages are system utilities. These counts describe the checked-in manifests,
not a claim that every title is release-qualified or installed on your unit.
Each title links to its own controls, rules, assets and test notes.

| Game | Category | What you do | Players / mode | Availability |
| --- | --- | --- | --- | --- |
| [Air Hockey](games/p4_air_hockey/README.md) | Sports | Move your paddle by touch; play the CPU or a friend. | Solo vs CPU; 2 linked | Included |
| [Blast Circuit](games/blast_circuit/README.md) | Arcade | Bomb battles with three arenas, bots and a level editor. | Solo vs bots; 2–4 linked | Included candidate |
| [Checkers](games/checkers/README.md) | Tabletop | Compulsory captures, chained jumps and kings. | 2 on one device or 2 linked | Included |
| [Color Clash](games/color_clash/README.md) | Cards | Match colors and action cards to empty your hand. | Solo vs CPUs; 2–4 linked | Included |
| [Frog Hop](games/frog_hop/README.md) | Arcade | Cross traffic and rivers to fill five lily-pad homes. | Solo | Included |
| [Maze Chase](games/maze_chase/README.md) | Arcade | Collect pellets, power up and evade the spirits. | Solo | Included |
| [Rummy 500](games/p4_rummy/README.md) | Cards | Draw, build melds, lay off cards and reach 500. | Solo vs CPUs; 2–4 linked | Included |
| [Solitaire](games/solitaire/README.md) | Cards | Klondike with direct-touch cards and drag-and-drop. | Solo | Included |
| [Space Invaders](games/space_invaders/README.md) | Arcade | Defend destructible shields against alien waves. | Solo | Included |
| [Texas Hold'em](games/texas_holdem/README.md) | Cards | Poker with betting, all-ins and side pots. | 2–4 local human/CPU seats; 2–4 linked | Included |
| [Yahtzee](games/p4_yahtzee/README.md) | Tabletop | Roll, hold dice and fill the scorecard. | 2–4 pass-and-play or linked | Included |

Held-back games are available only through an explicit developer install:

| Game | Category | What you do | Players / mode | Availability |
| --- | --- | --- | --- | --- |
| [Byte Buddy](games/byte_buddy/README.md) | WIP | Raise a dragon, explore Signal City and play activities. | Solo | Developer install only; requires resource sidecar |
| [Red Dragon](games/lord/README.md) | WIP | A text-and-ANSI fantasy adventure with saved progression. | Solo; 2-player linked profile / realm features | Developer install only; protected OS pairing |
| [Skyline Leap](games/skyline_leap/README.md) | WIP | A rooftop platformer retained for further development. | Solo | Developer install only; unfinished |
| [Tide Maze](games/tide_maze/README.md) | Arcade | Tilt a flooded marble maze and collect pearls. | Solo; 2 linked co-op | Removed from product library; development only; lag acceptance failed |

Linked player counts describe the implemented game profiles. Tab5 Local Wi-Fi
supports up to four consoles where the game does; Bluetooth and USB serial
retain a two-console limit. Same-device play is listed separately. Read each
game's evidence and the [multiplayer guide](docs/MULTIPLAYER.md) before claiming
physical compatibility: host tests and installed files do not prove a smooth
four-tablet match. Wacky and the arena still need physical multiplayer acceptance.

| System utility | Purpose | Location |
| --- | --- | --- |
| [Calculator](games/calculator/README.md) | Integer calculator with a touch keypad. | System / Tools |
| [Input Monitor](games/input_test/README.md) | Inspect normalized buttons and touch input. | System / Tests |
| [Sound & Motion](games/av_test/README.md) | Check screen patterns, animation and tones. | System / Tests |

The [library policy](docs/GAME_LIBRARY.md) covers categories, unfinished games
and release quality. [Retired identities](games/retired.json) stay reserved;
removed titles are not counted as playable games.

## Engine games and ports

These use distinct engine/data workflows; they are not all ordinary cartridge
folders or default-installed games.

| Game / project | What is included | Status and data |
| --- | --- | --- |
| [Doom](docs/games/doom/README.md) | OS-integrated engine, controls, MIDI/effects and two-player adapter | Verified v1.9 shareware is the default setup data; WAD stays local |
| [Chex Quest](docs/games/chex-quest/README.md) | Optional title using the integrated Doom engine | Explicit SD opt-in; verified WAD and patch required |
| [Doom Arena by Game Changers](docs/games/arena/README.md) | Doom-based 2–4 player Wi-Fi mode with arena voting and visit scores | Optional exact content bundle; hardware multiplayer/cadence acceptance pending |
| [Pure Hades](game-data/pure-hades/README.md) | Original Shotguns and Rockets arenas with MIDI and notices | Map pack for the arena mode; committed with the owner's authorization |
| [Wacky Wheels](scripts/wacky/README.md) | Five race courses, championship, Duck Shoot, MIDI/effects and new 2–4 racer sprint | Experimental local port; absent from default catalog, source/data redistribution and device acceptance unresolved |
| [Quake](ports/quake/README.md) | Pinned engine, SDL3 host adapter and retained OS adapter | Dormant: not linked or exposed by current OS; local shareware PAK required |

Downloaded WADs/PAKs and Wacky upstream data stay in ignored local storage.
The original Pure Hades pack has its own notices and a narrow publication
exception. See [content handling](docs/CONTENT_LIBRARY.md) and
[third-party licensing](third_party/README.md); this repository's visibility
does not change third-party rights.

## Set up a Tab5

Current game storage **requires a microSD card**. The internal-flash/no-SD game
storage route is not implemented. Keep the card in the Tab5 and transfer games
through its USB-C connection. See the
[storage status](.agents/skills/esp32-setup/references/tab5-storage.md).

From the repository root:

```sh
make setup                  # Install the pinned tools if they are absent
make verify
make prepare-game-data      # Fetch and verify missing Doom shareware locally
make console-os-tab5-idf
```

`make console-os-idf` is an alias for the Tab5 build. The repository pins
ESP-IDF and component versions in [`toolchain.lock.json`](toolchain.lock.json)
and the committed dependency lockfiles. If the SDK is already installed,
`P4_IDF_PATH` can point to that matching installation. Use its Python environment
for builds and serial transfer.

Build outputs live in `apps/console_os/build-tab5/`, including the application,
verified update package, and `sd-card/` content bundle. The current build targets
ESP32-P4 revision 1.x; revision 3.x hardware needs a separately reviewed target.

Continue with [ESP32 - Set Up](.agents/skills/esp32-setup/SKILL.md) and the
[Tab5 installation guide](docs/boards/M5STACK_TAB5.md) to preserve a new unit's
factory image and install through the exact-unit guarded workflow. Firmware
builds do not flash a device. Default setup includes the native game bundle and
Doom shareware; Chex Quest is optional and requires an explicit choice.

For a Tab5 already running compatible Console OS, select its actual USB-C port:

```sh
python3 scripts/p4-transfer.py push-bundle \
  apps/console_os/build-tab5/sd-card --port /dev/cu.usbmodem...

python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.usbmodem...

python3 scripts/p4-usb-content.py doom --port /dev/cu.usbmodem...
```

The transfer tools validate content before activation. Keep downloaded game data,
firmware binaries, and factory backups in ignored local storage. The committed
Pure Hades pack has its own notices; that does not permit committing
commercial Doom data. See the [content library](docs/CONTENT_LIBRARY.md).

## Make a game or change the console

Open the repository root in your coding agent and say what you want:

```text
Make a racing game for my Tab5.
Draw its cover and add music.
Let four friends race on their own consoles.
Play-test it, then put it on my Tab5.
```

[AGENTS.md](AGENTS.md) routes these requests through **ESP32 - Start Here**.
It knows all the repository skills and selects only those needed. The common
names are **ESP32 - Make Game**, **Game Art**, **Test Game**, **Multiplayer**,
**Add Game**, **Set Up**, **Fix Console** and **Controllers**. You do not need
to remember a skill command. The [skill index](.agents/skills/README.md) lists
all names, optional commands and legacy-board workflows.

Without an agent, use the [native SDK](docs/GAME_SDK.md) and
[game workspace guide](games/README.md). For example:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
make play-game GAME=frog_hop
make game-registry-check
```

New games use native C `.P4G` cartridges. Custom 2D engines, software 3D and
raycasting use the same stable API; drawing helpers are optional. C++ ports
need a tested C-ABI adapter. Lua game authoring is retired. Keep hardware
ownership in the OS and reusable services in `components/`.

Follow the [presentation](docs/GAME_ART.md), [performance](docs/GAME_PERFORMANCE.md)
and [release-quality](docs/LAUNCH_QUALITY.md) contracts. Target 60 FPS and
measure the 30 FPS release floor on the actual device. Keep each game's README
and this inventory current when adding or changing a title.

## Validation and project status

Run focused checks for the changed component or game; use the real SDL3 game
sources for native play-testing. Firmware builds, exact install/readback,
interactive play, physical audio and sustained device performance are separate
claims. The game READMEs and exact records under `test-runs/` and
`hardware/` identify what passed and what remains pending.

Start with [Tab5 hardware status](docs/boards/M5STACK_TAB5.md),
[OS documentation](apps/console_os/README.md), [controllers](docs/CONTROLLERS.md)
and [multiplayer](docs/MULTIPLAYER.md). Historical records remain attached to
their original artifacts; they do not certify every later build.

## Legacy board ports

All non-Tab5 board targets are legacy. Use their explicit targets and guides
only when maintaining an existing device. Shared code and exact-device recovery
contracts remain preserved; Tab5 installation instructions and flash
authorizations do not apply to these boards.

| Legacy board | Retained build target | Reference |
| --- | --- | --- |
| Elecrow CrowPanel Advanced 10.1-inch | `make console-os-elecrow-idf` | [Hardware and historical evidence](docs/HARDWARE.md) |
| Olimex ESP32-P4-PC Rev.B | `make console-os-olimex-idf` | [Board guide](docs/boards/OLIMEX_ESP32_P4_PC.md) |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | `make console-os-waveshare-idf` | [Port guide](docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md) |

These retained targets have different verification and hardware histories;
legacy status is not a claim that each builds or passes its current verifier.
Board-specific skills remain listed in the [legacy skill index](.agents/skills/README.md#legacy-board-maintenance).

## Repository layout

```text
apps/           Console OS, diagnostics and accessory firmware
components/     Shared APIs, engine adapters, services and board support
games/          Native game/utility sources, manifests, art and per-title READMEs
ports/          Engine port adapters, including the Quake host runner
game-data/      Original Pure Hades pack and its notices
docs/           SDK, game/arena guides, Tab5 setup and design contracts
hardware/       Board profiles, installation evidence and backup metadata
scripts/        Setup, build, package, upload, verification and recovery tools
tools/          Native game/console SDL3 runners and the Red Dragon realm hub
third_party/    Pinned source, licenses and external-data identities
.agents/skills/ Friendly, automatically selected agent workflows
```
