# Game Changers AI OS

The initial launch of Game Changers AI OS is the foundation for our future
STEM box: a native ESP32-P4 game console with repository-local AI workflows for
creating games, testing them and setting up the device. Start with
[`installos`](.agents/skills/installos/SKILL.md) for first-time installation;
it prepares the default games and verified Doom shareware data. See the
[launch quality contract](docs/LAUNCH_QUALITY.md) for release and remix gates.
The [initial launch notes](docs/INITIAL_LAUNCH.md) describe current game,
storage and source-publication status.

M5Stack Tab5 is the permanent primary hardware for this game console. Build it
with `make console-os-tab5-idf` or `make console-os-idf`. Use the connected USB-C
cable to install games while the SD card stays in the device. The
[Tab5 guide](docs/boards/M5STACK_TAB5.md) records exact-board installation and
hardware acceptance. Other ESP32-P4 boards retain explicit build targets.

The [game library](docs/GAME_LIBRARY.md) lists the curated games, categories,
ranking and release quality bar. New projects stay out of the installed library
until they are ready.

For a fresh setup, run `make setup` when the pinned tools are absent, then
`make verify` and `make prepare-game-data`. The last command downloads missing
Doom shareware into ignored local storage and verifies its pinned size and
SHA-256 before use. It never installs or commits WAD data. Continue with
[`installos`](.agents/skills/installos/SKILL.md) for the exact device's guarded
installation and first-boot checks. Current Tab5 game storage requires microSD;
Chex Quest is an explicit SD-only option, outside the default bundle.

Console OS has versioned game APIs for drawing, normalized input, bounded
audio, saves, multiplayer-facing player slots, and return-to-launcher
lifecycle. Native C games are bounded `.P4G` RISC-V cartridges loaded from
persistent storage. Native C is the supported game-creation path, including
custom 2D engines, software 3D and raycasting through the stable APIs. `.P4G`
is not UF2, and a compatible game update normally needs no OS reflash. The old
Lua game-creation stack, games, tools and skill are removed. See [the native P4 Game SDK](docs/GAME_SDK.md) and
[the performance contract](docs/GAME_PERFORMANCE.md).

On the Elecrow target, the Program Manager shell exposes that FAT volume to a
laptop through J16 USB device mode. On the Olimex ESP32-P4-PC target, games and
updates live on removable microSD instead: power the board off, move the card to
the laptop, and use the generated `sd-card/` bundle. Game Manager discovers and
removes cartridges and installs verified `UPDATE/P4UPDATE.P4U` images into the
inactive OTA slot; File Manager handles other root files. Each backend has one
filesystem owner at a time.

## Make games with the Console API

New games use native cartridges. Separately integrated legacy engines retain
their reviewed OS boundary:

| Tier | Best for | Runtime and delivery |
|---|---|---|
| Native cartridge | Original games, remixes and custom 2D/3D engines | C against P4 Game API v1, storage-installed `.P4G`, native 768x480 RGB565 with a tested 320x200 fallback |
| OS-integrated engine | Separately reviewed legacy ports such as Doom | Engine integration in Console OS plus legally supplied data; requires an OS build and is not a tradeable kid cartridge |

Shared drawing helpers are optional; a custom renderer may write the supplied
surface within the SDK's lifecycle and resource bounds. Current packaging
supports C sources. C++ ports need an explicitly tested C-ABI/toolchain adapter,
not an assumed STL/runtime environment. Every engine must prove P4 cadence.

Repository-local Codex skills live under `.agents/skills/`; the
[complete skill index](.agents/skills/README.md) describes all 14. Open this
repository at its root and describe the game you want in your own words.
The skills route ordinary requests automatically; naming a skill is optional.
[Game starters and OS services](docs/GAME_STARTERS.md) offers examples without
requiring a template or limiting the idea:

- Use `$installos` for Console OS installation and first setup. It asks about
  microSD, includes basic games and verified Doom shareware, and offers Chex
  Quest only with SD. See its [Tab5 storage status](.agents/skills/installos/references/tab5-storage.md)
  for the internal-storage implementation requirements; the current Tab5
  firmware still uses SD.
- Use `$develop-p4-console-games` to design or change a native C game, its
  manifest, drawing, controls, sound, and launcher category. Include normalized
  controller mappings by default; the OS owns the USB/BLE hardware.
- Use `$create-p4-game-art` for native-resolution sprites, textures, exact UI
  and launcher artwork, with source provenance and deterministic asset packing.
- Add `$develop-p4-multiplayer-games` automatically for linked-console play.
  It guides the game's synchronization through existing OS Host/Join sessions,
  while preserving the original game idea.
- Add `$test-p4-games-locally` to play the native game's real sources in the
  SDL3 runner before making firmware.
- Use `$develop-p4-games` for `.P4G` packaging, resource sidecars, catalog
  integration, SD/USB copy, install, update, or removal.
- Add `$add-usb-gamepad-support` when USB or Bluetooth controller transports,
  HID descriptors, mappings, pairing, hot-plug, or game input adapters change.
- Use `$develop-waveshare-p4-4-3` for a Waveshare firmware build, flash,
  storage, display, touch, audio, USB-role, or exact-hardware test. Use
  `$develop-esp32-p4-platform` for shared platform or toolchain work.

For example:

```text
Use $develop-p4-console-games to make an original native game with a software 3D renderer.
Use $develop-p4-console-games and $test-p4-games-locally to build and play a native platformer.
Use $develop-p4-games to package that native game and stage it for the Waveshare SD card.
```

The same contracts are usable without Codex. Start a native game with
`python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run`, or
start a multiplayer-aware native game with
`python3 scripts/new-game.py "Dice Link" --multiplayer turn-based`.
Games must stay behind the documented APIs;
they never own display, touch, audio, USB, SD, UART, or raw GPIO drivers.

## Board profiles

- `waveshare-esp32-p4-wifi6-touch-lcd-4.3` is the active 4.3 in console target.
  Its 480x800 ST7701 scanout is rotated into an 800x480 landscape console with
  a native 768x480 OS viewport. Games use portable 320x200 or negotiate the
  optional 768x480 Game API high-resolution surface. Games
  and resources live under `GAMES/` on microSD. Build with
  `make console-os-waveshare-idf`; use the on-device USB Drive app plus
  `make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES` for a validated
  card update. H2 remains controller-first and must not be assumed to provide
  safe VBUS; follow the powered, backfeed-safe fixture rules in the board
  skill and port guide. Its main-screen Controllers app manages encrypted BLE
  HID pads, persistent A/B/X/Y/Start/Back mappings, and a controller-only BLE
  mode toggle through the same normalized input API used by wired pads. One
  paired pad can remain connected while one BLE Doom multiplayer peer joins.
  Opening Multiplayer performs no WAD scan. Doom/Chex terminal launch keeps
  BLE serviced while one exact pass over only the selected title also captures
  an immutable PSRAM snapshot; engine random reads then perform zero SD I/O.
  Native multiplayer cartridges never inspect WAD data. Transient pad loss is
  neutralized before a bounded three-attempt saved-peer reconnect.
  Multiplayer opens with an explicit Host/Join choice: Host selects a game and
  its settings, while Join presents a cross-game room list and binds the exact
  installed game automatically from the selected room beacon.
  Modern Bluetooth-capable Xbox controllers are the priority target. See
  [Console controllers](docs/CONTROLLERS.md) for exact scope and limits.
- `elecrow-crowpanel-advanced-10` is the explicitly selected 10 in variant;
  M5Stack Tab5 remains the default. Its display/framebuffer and selected runtime
  paths have hardware evidence; the complete new Console OS feature set still
  needs one integrated hardware pass. It keeps the existing 1024x600
  window-manager UI, touch, speaker path, J16 game-storage MSC, and guarded
  exact-device flash workflow.
- `olimex-esp32-p4-pc` is an additive Rev.B development target. It uses the
  LT8912B HDMI bridge at 1280x720, onboard microSD for persistent content, and
  the powered four-port USB-A hub for a generic HID gamepad, boot keyboard, and
  boot mouse concurrently. It has no touch; the official ES8311/I2S path drives
  its 3.5mm audio jack, and Doom uses the same storage-backed exclusive handoff
  as Console OS on the other boards. It is build-tested but
  remains write-locked until the connected unit has a complete factory backup
  and identity-bound manifest.

See [the Olimex board guide](docs/boards/OLIMEX_ESP32_P4_PC.md) for connectors,
card installation, input mappings, and the hardware acceptance checklist. See
[the deterministic board-port workflow](docs/BOARD_PORTING.md) before adding a
third target; its parity contract prevents a new BSP from dropping shared apps.

## Current target

The attached board has been identified non-destructively as an ESP32-P4 revision 1.3 with 16 MB flash. Its saved pre-project image identifies itself as `ESP32-P4-Elecrow-Advance`; more importantly, it contains Elecrow's complete, commit-pinned 10.1-inch loading-background asset at flash offset `0x71004`, while the full 7- and 9-inch assets are absent. The working classification is therefore the 10.1-inch/DHE04310D **factory-firmware variant** with very high confidence. This is not a physical-label or PCB-revision claim: `pcb_revision` remains null and every pin map remains unauthorized until the board silkscreen is photographed.

The original 16 MB factory flash has been backed up locally, hashed, and bound to this unit by a SHA-256 of its normalized base identity. The raw identifier is not stored. The binary is intentionally Git-ignored; its metadata is in `hardware/backups/manifest.json`.

The pin-independent `bringup` image has passed on the connected board using the exact pinned ESP-IDF 5.5.3 toolchain: P4 revision v1.3, 16 MiB flash, 32 MiB initialized PSRAM, a 1 MiB PSRAM write/read test, and persistent PASS heartbeats. The tested build configured hex PSRAM at 200 MHz; M0 did not independently measure its bus clock. The app-only write targeted `0x10000`; a later 64 KiB readback from `0x0` matched the same region of the factory backup, and the installed app readback matched the built binary. Evidence is recorded under `hardware/test-runs/`.

The bounded display path is now hardware-tested. A reproducible custom image using Elecrow's commit-pinned V1.0–V1.2-invariant settings and Espressif's locked EK79007 1.0.2 driver initialized the attached 1024×600 RGB565 panel at two DSI lanes/900 Mbps and 51 MHz, then cycled vertical, horizontal, and BER patterns at 25% backlight. The app-only range read back exactly, serial cycling remained healthy, and the user supplied a live image of visible color bars. This pass authorizes no other peripheral and does not restore or reuse the factory UI.

The toolchain lock also fixes the image compatibility range to ESP32-P4 revision 1.0 through 1.99. The build wrapper rejects the incompatible revision-3.x family before a flash is attempted.

The D0 Doom host proof also passes: the immutable vendored doomgeneric source builds with the project-authored adapter under fatal warnings, and the ignored, hash-pinned Doom 1.9 shareware development WAD reaches eight headless frames. D0.5 additionally cross-compiles and whole-archive links all 80 selected engine objects for ESP32-P4 revision 1.x with the pinned IDF; the build-only target contains no WAD or hardware services and repository flash commands reject it. D0 remains host-tested and D0.5 is build-tested only—neither qualifies Doom runtime on the ESP32-P4, display, SD, audio, or controller input. A clean clone without local game data reports the D0 smoke test as skipped while still verifying provenance and building the engine.

The reusable tier-1 gamepad software path now builds under the pinned target: a singleton P4 high-speed USB Host owner, exclusive class leases, Espressif HID transport, the bounded descriptor/report parser, and complete canonical snapshots. Host sanitizer tests cover the fixture gate, lease/teardown state machine, transactional publication, reconnect sessions, and immediate disconnect neutralization. The committed `gamepad_diag` configuration remains electrically inert and both flash modes remain false. The published V1.0-V1.2 schematics prove that J16 carries the P4 HS DP/DM pair, but its two 5.1 kOhm CC resistors advertise a sink and its diode-isolated VBUS path feeds only into the board; it cannot power a controller. A passive OTG adapter or ordinary powered hub is not a safe fix. Physical input testing requires a current-limited, backfeed-isolated J16 data/power shim. The Mac USB tree currently exposes only the WCH serial bridge, not a controller.

## First milestones

1. Reproduce the toolchain and pass the serial/flash/PSRAM bring-up diagnostic.
2. Display baseline complete; record the physical product/PCB labels, then bring up touch, SD card, and audio in separate diagnostics.
3. Build a safe powered USB-host shim and pass generic HID gamepad diagnostics.
4. Run Doom with legal game data from SD, RGB565 video, I²S audio, and the shared input API.
5. Extract only proven procedures into repository skills and hardware acceptance tests.

See [the architecture](docs/ARCHITECTURE.md), [the native console shell](docs/CONSOLE_OS.md), [hardware facts and open questions](docs/HARDWARE.md), [the Doom acceptance design](docs/DOOM.md), and [the acceptance roadmap](docs/ROADMAP.md).

## Reproducible commands

Install the pinned SDK once, or point `P4_IDF_PATH` at an existing matching install:

```sh
make setup
make verify
make build APP=bringup
make build APP=display_diag
make gamepad-host
make gamepad-idf
make console-shell-host
make platform-game-storage-host
make game-sdk-host
make console-os-idf
make console-os-olimex-idf
```

Hardware writes always require an explicit port. For the first pin-independent
diagnostic, flash only the application partition so the factory bootloader and
partition table remain intact:

```sh
make backup PORT=/dev/cu.wchusbserial10
make flash-app APP=bringup PORT=/dev/cu.wchusbserial10
make monitor APP=bringup PORT=/dev/cu.wchusbserial10
make flash-app APP=display_diag PORT=/dev/cu.wchusbserial10
```

Both `make flash-app` and the full-project `make flash` verify the local factory
backup's hash and byte count, require the connected unit's stored identity
hash to match the backup manifest, probe the live ESP32-P4 and flash size, and
refuse to write on a mismatch or when secure boot/flash encryption is enabled.
After writing, they read the built application's exact byte range back and
compare its SHA-256. Readback transfers are capped at 512 KiB, each chunk is
checked against the corresponding source range and retried independently at a
lower baud, and the complete ordered byte count and SHA-256 must match before a
deferred image can launch. Use the full-project target only when a test requires
a new bootloader or partition table. Do not connect a controller with a passive
OTG adapter; see `docs/HARDWARE.md` first.

## Repository layout

```text
apps/          ESP-IDF applications and acceptance firmware
components/    Reusable badge services and board support
games/         Native P4 Game API games and manifests
docs/          Architecture, decisions, and test criteria
hardware/      Board profile, schematics/host-shim notes, backup metadata
scripts/       Reproducible setup/build/flash/monitor commands
third_party/   Exact source pins and third-party licensing policy
.agents/skills Project-scoped Codex workflows learned from verified work
```
