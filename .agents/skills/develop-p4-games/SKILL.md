---
name: develop-p4-games
description: Create, port, modify, package, install, or test storage-installed games for P4 Console OS. Use for games/*, P4 Game API v1, game.json manifests, .P4G cartridges, launcher metadata, 320x200 RGB565 rendering, normalized controls, tone audio, Game Manager installation or removal, or adding a game to the Program Manager catalog.
---

# Develop P4 Console games

Build games against the stable P4 Game API, then install their `.P4G` files
through the Elecrow `P4 GAMES` USB volume or the offline Olimex microSD card.
Keep Console OS in charge of hardware, storage, and lifecycle services so
adding or removing a game never requires an OS reflash.

Target names are exact: `elecrow-crowpanel-advanced-10` is the Elecrow 10 in
device, while `olimex-esp32-p4-pc` is the Olimex ESP32-P4-PC Rev.B development
board. A package remains board-independent, but its Console OS build and copy
workflow must match the physical target. Any new target must first appear in
`hardware/boards/console-os-port-contract.json`; never infer a board from its
display connector or reuse another board's identity.

## Load the game contract

Read these files before changing a game:

1. `AGENTS.md`
2. `docs/GAME_SDK.md`
3. `components/p4_game_api/include/p4/game.h`
4. The closest example under `games/`, normally `maze_chase` or
   `space_invaders`

Read `$develop-esp32-p4-platform`, `$use-elecrow-p4-display`,
`$use-elecrow-p4-audio`, or `$add-usb-gamepad-support` only when the request
changes or diagnoses that platform boundary. Ordinary drawing, normalized
button handling, and tone playback through existing `p4/` APIs do not require
board bring-up or peripheral diagnostics.

## Create or modify a game

For a new game, inspect the plan before creating files:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
```

The generator chooses a free launcher ID and never overwrites an existing
game. It creates the manifest and CMake entry at the game root, runtime code
under `src/`, and a README. Add deterministic host tests under `tests/` and an
optional preview tool under `tools/` as the game grows; follow an existing game
for their CMake wiring.

For an existing game, preserve its public ID unless the task intentionally
creates a different title. Prefer small, deterministic game-state transitions
that can be exercised without display, audio, USB, or filesystem hardware.

## Preserve runtime and package rules

- Include only headers under `components/p4_game_api/include/p4/`. Never take
  raw display, touch, audio, USB, SD, or filesystem handles from a game.
- Render a complete 320x200 RGB565 frame with the clipped `p4/draw.h`
  primitives. Keep state at or below `P4_GAME_MAX_STATE_BYTES` and bound loops,
  coordinates, sprite dimensions, text, timers, and audio requests.
- Consume complete `held`, `pressed`, and `released` input snapshots. Return
  `P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed.
- Treat tone audio as optional and tolerate `p4_game_play_tone()` returning
  false. Put reusable services in `components/`, never in a game.
- Keep `game.json` authoritative. Retain format `p4-native-elf-v1`, API version
  1, a unique game ID and launcher ID, an uppercase root `.P4G` filename, a
  folder of at most two uppercase segments, and accurate version, license,
  asset, and capability declarations.
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
  host checks pass, run exactly one matching build: `make console-os-idf` for
  Elecrow or `make console-os-olimex-idf` for Olimex Rev.B.

Do not run repo-wide `make check` by default. Reserve it for an explicit user
request, a pinned toolchain or dependency change, or a genuinely cross-cutting
change spanning maintained applications. Do not repeat an unchanged build,
flash, or hardware diagnostic. Stop when the risk-matched checks pass and
report unrelated failures without expanding the task.

## Package and install without flashing

The board-specific Console OS builds write each enabled cartridge to:

```text
apps/console_os/build/game-storage-seed/<PACKAGE>.P4G
apps/console_os/build-olimex-esp32-p4-pc/sd-card/<PACKAGE>.P4G
```

To install or update on Elecrow, connect the laptop to J16, copy the `.P4G`
file to the root of `P4 GAMES`, verify the destination byte count or hash, and
eject cleanly. On Olimex, power the board off, move the microSD card to a laptop
reader, copy the cartridge to its root, verify it, eject, reinstall, and power
on. The Olimex USB-C programming port is not storage and live card removal is
unsupported. Open Game Manager to refresh and launch it. Do not flash the OS
for a game-only update and never let the host and firmware mount the writable
filesystem at the same time.

For a hardware acceptance, perform one named run that launches the changed
game, exercises its changed behavior, and returns to the launcher with Back.
Record the exact cartridge hash and device used. A host test or successful
package build is not hardware verification.

## Definition of done

A game change is complete when its manifest remains valid, its focused host
tests pass, a requested `.P4G` validates, and any claimed device behavior has
one recorded hardware acceptance. State any remaining package, copy, or device
check plainly instead of running broader unrelated tests.
