---
name: esp32-add-game
description: Use when packaging, validating, installing, updating, removing or cataloging native C .P4G games for P4 Console OS, including game.json package and launcher metadata, resource sidecars, storage transfer and Game Manager registration. Gameplay/source authoring uses Make Game; local preview uses Test Game.
---

# ESP32 - Add Game

Package games against the stable P4 Game API, then install their `.P4G` files
through M5Stack Tab5 native USB-C with the microSD card left inserted. Tab5 is
the default maintained target: `m5stack-tab5`, `make console-os-tab5-idf`.
Keep Console OS in charge of hardware, storage and lifecycle services; compatible
game-only updates do not require an OS reflash.

Use [Legacy installation](references/legacy-installation.md) only for explicitly
requested Elecrow, Olimex or Waveshare maintenance. Packages are board-independent,
but the Console OS build, physical identity and transfer route must match.
Any new target must first appear in `hardware/boards/console-os-port-contract.json`;
never infer a board from its display connector or reuse another board's identity.

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

## Prepare a cartridge

Use `$esp32-make-game` for a new title or source change. For scaffold commands and
initial manifest wiring, read [Cartridge preparation](references/cartridge-preparation.md).
Keep new scaffolds unpublished (`enabled: false`) until `docs/GAME_LIBRARY.md`
quality checks pass; preserve public IDs, retired identities and save identities.

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
  Require native 768x480 for every maintained Tab5 game: declare
  `video-highres` in `game.json`'s required capabilities and
  `P4_GAME_CAP_VIDEO_HIGH_RES` in the descriptor's required capabilities.
  The creator emits both requirements by default; verify they remain aligned
  after edits. Never accept a 320x200 framebuffer or per-title downgrade as
  the maintained presentation mode. Follow `docs/GAME_ART.md` for native
  layouts, text/symbols, ImageGen assets and byte budgets. Touch input stays
  canonical 320x200; preserve legacy fallback source only for explicit legacy
  maintenance. Keep state at or below `P4_GAME_MAX_STATE_BYTES` and bound loops,
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

The Tab5 Console OS build writes enabled cartridges to
`apps/console_os/build-tab5/sd-card/GAMES/<PACKAGE>.P4G`. Use the matching
legacy output and transfer route only for an explicitly requested legacy board.

The host rejects unsafe names, oversize files, malformed package geometry, API
mismatches, and payload-digest failures before sending. Console OS repeats the
bounded package validation, SHA-256 verification, atomic staging, and readback
under `/GAMES`; a successful P4G upload invalidates and reloads the native game
catalog without rebooting. Use `--class exchange` only for the separate
File Transfer exchange area, never to bypass native cartridge validation.

On Tab5, keep the card in the powered tablet and use its connected USB-C
Serial/JTAG port. For a one-game update, push only that game's validated `.P4G`:

```sh
python3 scripts/p4-transfer.py push apps/console_os/build-tab5/sd-card/GAMES/CHECKERS.P4G \
  --port <port>
```

Replace `CHECKERS.P4G` with the requested package and use the explicit current
port for the intended unit, not an assumed enumeration order. If the game has a
matching `.P4R`, validate and push that resource first with `--class p4r`; see
[Bundles and resources](references/bundles-and-resources.md). Use `push-bundle`
only for a requested bundle installation, and provision Doom data only when
that content is requested or required by the selected setup.

Native USB may reboot on open on macOS; the tools tolerate startup. Cartridge
activation refreshes the catalog. USB Drive/MSC remains disabled on Tab5.
USB-C transfer does not depend on USB-A host mode and does not require a card
reader when this path is available. Consult `docs/boards/M5STACK_TAB5.md` for
USB-A host-power and exact-artifact/controller acceptance.

For a hardware acceptance, perform one named run that launches the changed
game, exercises its changed behavior, and returns to the launcher with Back.
Record the exact cartridge hash, OS and device used, and verify
`CARTRIDGE_START ... surface=768x480` plus readable native-resolution active
play. For linked play, record that surface on both host and guest. A transfer,
host test, optional capability or enlarged screenshot is not that evidence. When multiplayer behavior changed,
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
