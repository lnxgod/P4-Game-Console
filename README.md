# Game Changers AI OS for M5Stack Tab5

A native game console and game-making platform for **M5Stack Tab5**, built on
ESP32-P4. Play storage-installed games, create your own native C cartridges,
and use the repository's AI-assisted workflows to develop, test, and install them.

**Tab5 is the only actively maintained board target.** Elecrow, Olimex, and
Waveshare ports are legacy. Their source, recovery tools, and historical evidence
remain available for existing devices; new development and setup target Tab5.

Start with the [Tab5 guide](docs/boards/M5STACK_TAB5.md) for hardware status or
[`installos`](.agents/skills/installos/SKILL.md) for guided first-time setup.

## What runs on Tab5

- A touch-driven Console OS with a game launcher, file and game management,
  settings, saves, and system tools.
- Native C `.P4G` games through a stable API for drawing, normalized controls,
  audio, saves, and return-to-launcher behavior. Compatible game updates normally
  require no OS reflash.
- Native 768×480 RGB565 game rendering with a tested 320×200 fallback. Custom
  2D engines, software 3D, and raycasting are supported within the same API and
  resource contracts.
- Doom with verified game data, plus an optional Chex Quest installation.
  The [Game Changers AI Doom mode](docs/GAME_CHANGERS_AI_DOOM.md) adds arena
  selection and voting with the original [Pure Hell pack](game-data/pure-hell/README.md).
- OS-owned controller and multiplayer services shared by games. See
  [controllers](docs/CONTROLLERS.md) and [multiplayer](docs/MULTIPLAYER.md) for
  implemented transports and exact acceptance limits. Tab5 Bluetooth controller
  support is not currently enabled.

The [game library](docs/GAME_LIBRARY.md) tracks titles, categories, and release
quality. New game scaffolds are drafts until play-tested. Device performance
and named-controller compatibility require their own measurements; a successful
build does not establish hardware acceptance.

## Set up a Tab5

Current game storage **requires a microSD card**. The internal-flash/no-SD game
storage route is not implemented. Keep the card in the Tab5 and transfer games
through its USB-C connection. See the
[storage status](.agents/skills/installos/references/tab5-storage.md).

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

Continue with [`installos`](.agents/skills/installos/SKILL.md) and the
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
original Pure Hell pack has its own notices; that does not permit committing
commercial Doom data. See the [content library](docs/CONTENT_LIBRARY.md).

## Make and test games

Open this repository at its root in Codex and describe the game you want.
Project skills ship in `.agents/skills/`; no personal skill installation is
required. See the [skill index](.agents/skills/README.md).

| Workflow | Skill |
| --- | --- |
| Install Console OS and set up a Tab5 | [`installos`](.agents/skills/installos/SKILL.md) |
| Design or change native gameplay | [`develop-p4-console-games`](.agents/skills/develop-p4-console-games/SKILL.md) |
| Create sprites, textures, and launcher artwork | [`create-p4-game-art`](.agents/skills/create-p4-game-art/SKILL.md) |
| Play-test the real game sources on a Mac | [`test-p4-games-locally`](.agents/skills/test-p4-games-locally/SKILL.md) |
| Package and install `.P4G` games and resources | [`develop-p4-games`](.agents/skills/develop-p4-games/SKILL.md) |
| Add linked-console play through OS sessions | [`develop-p4-multiplayer-games`](.agents/skills/develop-p4-multiplayer-games/SKILL.md) |
| Work on Tab5 firmware and shared services | [`develop-esp32-p4-platform`](.agents/skills/develop-esp32-p4-platform/SKILL.md) |
| Work on controller transports and normalized input | [`add-usb-gamepad-support`](.agents/skills/add-usb-gamepad-support/SKILL.md) |

For example:

```text
Make an original native game with a software 3D renderer.
Build and locally play-test a platformer with controller support.
Package my native game and install it on my Tab5 through USB-C.
```

The tools also work without Codex. Preview a scaffold with:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
```

Use the [native Game SDK](docs/GAME_SDK.md), optional
[game starters](docs/GAME_STARTERS.md), and [presentation contract](docs/GAME_ART.md).
Shared drawing helpers are optional. Games consume OS-owned display, input,
audio, storage, and multiplayer services; they do not own raw peripheral drivers.
Current packaging supports C. C++ ports need an explicitly tested C-ABI/toolchain
adapter. Lua game creation and `.P4CART` delivery are retired.

Follow the [performance contract](docs/GAME_PERFORMANCE.md): target 60 FPS,
retain fractional motion, and measure the 30 FPS release floor on the device.
Host play and sanitizer tests complement hardware testing. The
[launch quality contract](docs/LAUNCH_QUALITY.md) preserves game/save identities
and keeps incomplete prototypes out of default bundles.

## Development and validation

Run the tests that cover the changed boundary before building a firmware
candidate. Common focused checks include:

```sh
make game-registry-check
make p4-game-package-host
make p4-game-platform-host
make h1-usb-drive-control-host
make gamepad-host
```

The [reliability review](docs/RELIABILITY_REVIEW_2026_10_06.md) records upload
failure fixes, BLE-controller hardening for the legacy Waveshare port, sanitizer
results, and pending device checks. The [cleanup audit](docs/REPOSITORY_CLEANUP_2026_10_06.md)
and [test audit](docs/OBSOLETE_TEST_CLEANUP_2026_10_06.md) document retained
recovery dependencies and removed obsolete workflows. Historical acceptance
records describe their exact artifacts; they do not qualify later builds.

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
apps/           ESP-IDF applications and acceptance firmware
components/     Shared platform services and board adapters
games/          Native game sources and manifests
docs/           SDK, Tab5 setup, design contracts, and historical guides
hardware/       Board profiles, guarded-install records, and backup metadata
scripts/        Build, package, transfer, verification, and recovery tools
third_party/    Pinned dependencies and licensing information
.agents/skills/ Repository-local development and installation workflows
```
