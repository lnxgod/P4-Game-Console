---
name: esp32-add-game
description: Create, port, modify, package, install, or test storage-installed games for P4 Console OS. Use for games/*, P4 Game API v1, game.json manifests, .P4G cartridges, launcher metadata, native 768x480 RGB565 rendering with 320x200 fallback, normalized controls, tone audio, Game Manager installation or removal, or adding a game to the Program Manager catalog.
---

# ESP32 - Add Game

Build games against the stable P4 Game API, then install their `.P4G` files
through the Elecrow `P4 GAMES` USB volume, an Olimex/Waveshare microSD bundle,
or the Waveshare H1 / Tab5 native USB verified live-transfer paths. Keep Console OS in charge of
hardware, storage, and lifecycle services so adding or removing a game never
requires an OS reflash.

M5Stack Tab5 is the primary target: `m5stack-tab5`,
`make console-os-tab5-idf`, native USB-C transfer with the card left inserted.
Target names are exact: `elecrow-crowpanel-advanced-10` is the Elecrow 10 in
device, `olimex-esp32-p4-pc` is the Olimex ESP32-P4-PC Rev.B development board,
and `waveshare-esp32-p4-wifi6-touch-lcd-4.3` is the Waveshare 4.3 in console.
A package remains board-independent, but its Console OS build and copy workflow
must match the physical target. Any new target must first appear in
`hardware/boards/console-os-port-contract.json`; never infer a board from its
display connector or reuse another board's identity.

Use `$esp32-make-game` for free-form gameplay/source authoring and
`$esp32-test-game` for the SDL3 edit-play loop. Add
`$esp32-multiplayer` automatically for linked-console games; it owns
game-protocol guidance while Console OS owns rooms and links. Optional examples
live in `docs/GAME_STARTERS.md`. Supported controllers enter through the normal
`controls` API; never add USB/HID code to a cartridge. Use this skill for the
native package, catalog, storage, installation, and removal boundary. Native
C is the supported creation path. The old Lua games, tools and skill are
removed. Follow `docs/GAME_SDK.md` for custom engine freedom and current C
source/toolchain limits.

## Load the game contract

Use the canonical [ESP32-P4 performance contract](../../../docs/GAME_PERFORMANCE.md)
when authoring or qualifying a game. It covers bounded rendering, active traces
and the actual-device 30 FPS release floor; host tests and package transfers
remain distinct from device qualification.

Read these files before changing a game:

1. `AGENTS.md`
2. `docs/GAME_SDK.md`
3. `components/p4_game_api/include/p4/game.h`
4. The closest example under `games/`, normally `maze_chase` or
   `space_invaders`

Read `$esp32-fix-console`, `$esp32-waveshare`, a matching
board display/audio skill, or `$esp32-controllers` only when the request
changes or diagnoses that platform boundary. Ordinary drawing, normalized
button handling, and tone playback through existing `p4/` APIs do not require
board bring-up or peripheral diagnostics.

## Create or modify a game

For a new game, inspect the plan before creating files:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
python3 scripts/new-game.py "Card Table" --folder GAMES/CARDS --high-res
```

The generator chooses a free non-retired launcher ID, never overwrites a game,
and starts with `enabled: false`. Preview drafts with `make play-game GAME=<slug>`.
Publish only after the quality checks in `docs/GAME_LIBRARY.md` pass. It creates the manifest and CMake entry at the game root, runtime code
under `src/`, and a README. Add deterministic host tests under `tests/` and an
optional preview tool under `tools/` as the game grows; follow an existing game
for their CMake wiring.

For an existing game, preserve its public ID unless the task intentionally
creates a different title. Prefer small, deterministic game-state transitions
that can be exercised without display, audio, USB, or filesystem hardware.

## Cartridge-owned launcher presentation

New games and game remixes must ship their updated launcher title and artwork
inside the cartridge. Console OS 0.45 reads the title from the `.P4G` header
and prefers the optional `.p4icon` section for the tile; installing a game or
changing its art must not require an OS reflash or an OS-side title/ID table.
Preserve the 0.44 nextgen interface layout and styling when extending this path.

Keep `game.json`'s title/subtitle/version accurate. Finish the game's icon art
as part of its remix, then run `python3 scripts/pack-game-icon.py <art.png>
--output games/<slug>/assets/launcher.p4i` and set
`"launcher_icon": "assets/launcher.p4i"` in that manifest. Keep the source art,
license/provenance, and packed icon with the game. Read `docs/GAME_SDK.md`'s
"Cartridge launcher artwork" contract for exact bounds. This is title artwork,
not a screenshot of implementation details or generic placeholder art.

Verify the built cartridge contains the icon, install it, and confirm the
launcher title and tile refresh from the new cartridge without a firmware
change. Missing/invalid optional icons use the existing fallback; a fallback
does not count as an upgraded icon for a new/remixed release. Stay within the
512 KiB package limit including the icon. Do not recreate other games' icons
unless that artwork work is requested; respect an existing parallel remix.

## Preserve runtime and package rules

- Include only headers under `components/p4_game_api/include/p4/`. Never take
  raw display, touch, audio, USB, SD, or filesystem handles from a game.
- Render a complete RGB565 frame using optional clipped `p4/draw.h` primitives
  or a bounded custom software renderer on the supplied surface.
  Target 768x480 by default: declare optional `video-highres` in `game.json`
  and `P4_GAME_CAP_VIDEO_HIGH_RES` in the descriptor, with a tested 320x200
  fallback selected from `surface->width`/`height`. New scaffolds do this
  automatically. Follow `docs/GAME_ART.md` for native-detail layouts, exact
  text/symbols, ImageGen assets and byte budgets. Touch input stays canonical
  320x200 in both modes. Keep state at or below `P4_GAME_MAX_STATE_BYTES` and bound loops,
  coordinates, sprite dimensions, text, timers, and audio requests.
- Consume complete `held`, `pressed`, and `released` input snapshots. Return
  `P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed.
- Treat tone audio as optional and tolerate `p4_game_play_tone()` returning
  false. Put reusable services in `components/`, never in a game.
- Use the shared OS multicore services described in `docs/GAME_PERFORMANCE.md`.
  Tab5 OS 0.54 moves native audio output onto P4 core 1 while game callbacks
  remain on core 0; cartridges must not create their own hardware-owning tasks.
  Bounded PCM submissions can fail under backpressure. Preserve buffer lifetimes
  and test uneven delivery, stop/restart and teardown. Actual core IDs, underrun
  counters and device cadence are required before claiming a multicore speedup.
- Keep `game.json` authoritative. Retain format `p4-native-elf-v1`, API version
  1, a unique game ID and launcher ID, an uppercase root `.P4G` filename, a
  folder of at most two uppercase segments, and accurate version, license,
  asset, and capability declarations. Use the optional bounded `sources` list
  for a multi-file game instead of creating a second package path.
- For a multiplayer game, declare optional `multiplayer-session` and validate the
  declarative `multiplayer` profile described in `docs/GAME_SDK.md`. Package
  the canonical profile into the reviewed `.P4G` header extension; never
  create a transport-specific sidecar or let a game choose BLE/UART/USB.
  Console OS owns the full Host/Join flow: Host selects the game and settings,
  while Join resolves an advertised room to the exact installed cartridge.
  Cartridge code starts with an already-sanitized session and must not draw a
  second lobby, scan peers, or ask the player to choose Host/Join again.
- Use original or correctly licensed code and assets. Never commit commercial
  Doom WADs, WAD-bearing firmware, generated cartridges containing a WAD, or
  recovery images.

## Test only what changed

Choose the smallest proof that covers the modified boundary:

- Documentation or skill text: validate that document or skill and check its
  referenced paths. Do not build firmware.
- One game's C source or tests: configure, build, and test only that game:

  ```sh
  cmake -S games/<slug> -B build-host/<slug> -G Ninja
  cmake --build build-host/<slug>
  ctest --test-dir build-host/<slug> --output-on-failure
  ```

  Run the final command when the game defines tests. Add deterministic tests
  for nontrivial behavior by following an existing game's CMake wiring.

- A manifest, generator, or registry change: run `make game-registry-check`.
- A shared game API, package format, loader contract, or change spanning every
  maintained game: run `make game-sdk-host`.
- A distributable cartridge or Console OS integration change: after focused
  host checks pass, run exactly one matching build: `make console-os-tab5-idf` for
  Tab5, `make console-os-elecrow-idf` for
  Elecrow, `make console-os-olimex-idf` for Olimex Rev.B, or
  `make console-os-waveshare-idf` for Waveshare 4.3.

Do not run repo-wide `make check` by default. Reserve it for an explicit user
request, a pinned toolchain or dependency change, or a genuinely cross-cutting
change spanning maintained applications. Do not repeat an unchanged build,
flash, or hardware diagnostic. Stop when the risk-matched checks pass and
report unrelated failures without expanding the task.

## Package and install without flashing

For protected games, first apply [Protected game payloads](../../../docs/GAME_SDK.md#protected-game-payloads). Red Dragon requires the exact payload approved by the installed OS; a fresh category-only rebuild can change linked shared code. Use its paired artifact, preserve its save identity, and verify registration after transfer. Ordinary package validation is not a protected-lineage check.

The board-specific Console OS builds write each enabled cartridge to:

```text
apps/console_os/build-tab5/sd-card/GAMES/<PACKAGE>.P4G
apps/console_os/build/game-storage-seed/GAMES/<PACKAGE>.P4G
apps/console_os/build-olimex-esp32-p4-pc/sd-card/GAMES/<PACKAGE>.P4G
apps/console_os/build-waveshare-landscape/sd-card/GAMES/<PACKAGE>.P4G
```

To install or update on Elecrow, connect the laptop to J16, copy the `.P4G`
file into `GAMES/` on `P4 GAMES`, verify the destination byte count or hash,
and eject cleanly. On Olimex, power the board off, move the microSD card to a
laptop reader, copy the cartridge into `GAMES/`, verify it, eject, reinstall,
and power on. The Olimex USB-C programming port is not storage and live card
removal is unsupported.

On Waveshare, keep controller-host mode as the default. Open the on-device USB
Drive app to stop Host/HID, unmount the card, and hand H2 to TinyUSB MSC. After
the FAT32 `P4GAMES` volume appears, run
`make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES`, eject it cleanly,
and return from USB Drive mode so Console OS remounts and rescans. A powered-off
card-reader copy is also valid. Never let the Mac and Console OS own the
filesystem at the same time. Open Game Manager to refresh and launch the game.
Do not flash the OS for a game-only update.

Prefer H1 when the badge is running and only a cartridge needs to move. The
default transfer class is `p4g`:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.wchusbserial...
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.wchusbserial... --no-replace
```

The host rejects unsafe names, oversize files, malformed package geometry, API
mismatches, and payload-digest failures before sending. Console OS repeats the
bounded package validation, SHA-256 verification, atomic staging, and readback
under `/GAMES`; a successful P4G upload invalidates and reloads the native game
catalog without rebooting. Use `--class exchange` only for the separate
File Transfer exchange area, never to bypass native cartridge validation.

On M5Stack Tab5 use `make console-os-tab5-idf`, then keep the card in the
powered tablet and use its connected USB-C Serial/JTAG port:

```sh
python scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port /dev/cu.usbmodem1101
python scripts/p4-usb-content.py doom --port /dev/cu.usbmodem1101
```

Use the explicit current port for A or B, not an assumed enumeration order.
`push-bundle` validates and installs native `.P4G` and `.P4R` resources over one
connection. Individual resources use `push --class p4r`. Each format has its own
directory and validator; never use `exchange` to bypass it. Native USB may
reboot on open on macOS; the tools
tolerate startup. Content activation reboots; native file
activation refreshes the appropriate catalog. USB Drive/MSC remains disabled on
Tab5. The default USB-A HID/XUSB candidate has a separate board-owned host-power
path; consult the Tab5 board document for exact-artifact and named-controller
acceptance. Native USB-C content transfer does not depend on USB-A host mode.
Do not ask for a card reader when the USB path is available.

For a hardware acceptance, perform one named run that launches the changed
game, exercises its changed behavior, and returns to the launcher with Back.
Record the exact cartridge hash and device used. A host test or successful
package build is not hardware verification. When multiplayer behavior changed,
exercise both OS roles, exact-game room resolution, connected launch, bounded
message exchange, and the game's offline/peer-loss fallback without claiming
an unmeasured frame rate.

## Definition of done

A game change is complete when its manifest remains valid, its focused host
tests pass, a requested `.P4G` validates, and any claimed device behavior has
one recorded hardware acceptance. State any remaining package, copy, or device
check plainly instead of running broader unrelated tests.

## Remove a known cartridge over Tab5 USB

Use `python3 scripts/p4-transfer.py remove /absolute/path/EXACT.P4G --port <port>`
when the user requests removal. Supply the exact locally validated copy; the
device checks its byte count and SHA-256 before deleting that named cartridge.
A changed or unknown file is rejected. For sidecars supply both `.P4R` and `.P4G`;
the client removes the resource first.
This does not delete saved progress, WADs or arbitrary files. Older firmware
without remove support must first receive a separately authorized OS update.

## Close the performance loop after a complaint

A user report of severe lag or unacceptable presentation rejects that installed
candidate's gameplay acceptance. Record the report against its known package
and device; do not overwrite it with a transfer PASS or merely leave the old
result as unmeasured. Preserve the rejected artifact for comparison.

Follow GAME_PERFORMANCE.md to isolate update, drawing, input and presentation
costs, rebuild the complete cartridge source closure, and repeat representative
play on the intended device. A requested diagnostic installation can proceed
within existing authorization, but label it as a candidate. A successful copy
and registration never closes a lag complaint. Keep the defect open until
measured cadence and physical responsiveness/visual acceptance support the fix;
report missing operator play or device timing explicitly without claiming it
is resolved. Do not add an OS flash to a game-only fix.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.

## Standard and development content

Standard bundles exclude disabled manifests and every `GAMES/WIP` title.
Byte Buddy, Red Dragon and Skyline Leap require an explicit
`make install-dev GAME=<slug> PORT=<port>`; see the
[developer install guide](../../../games/README.md#developer-installs).
Tide Maze remains in the normal bundle by the owner's request; preserve its
open device-lag acceptance. Feature Blast Circuit, with Wacky Wheels as an
installed fallback. Preserve hidden games' source, package IDs and saves.
