---
name: test-p4-games-locally
description: Play, smoke-test, diagnose, and tune native P4 Console OS Game API games on the development Mac with the repository SDL3 host runner. Use when Codex needs to preview a game before a firmware build or flash, iterate on movement, difficulty, controls, touch regions, drawing, lifecycle, or tone audio, run sanitizer-backed local checks, or explain which results still require the ESP32-P4 tablet. Do not use this runner for legacy Doom or as a substitute for guarded on-device acceptance.
---

# Test P4 Games Locally

Use the repository host runner to shorten the edit-play-tweak loop while
linking the selected game's real C sources against the shared Game API.

## Load the local contract

Read `AGENTS.md`, `docs/GAME_SDK.md`, the target `games/<slug>/game.json`, and
the target source before testing. Also use `$develop-p4-console-games` when
changing game code or its manifest.

Require a `p4-native-elf-v1` manifest with API version 1, a valid entry symbol
and C sources under `games/<slug>/src/`. New generator drafts use
`enabled: false`; pass `-DP4_ALLOW_DRAFT_GAME=ON` to the local runner to preview
one without publishing it in the catalog. `make play-game` already selects
that draft option. Never bypass unknown-format, invalid-symbol or missing-source
checks; enable a finished game only when ready for validated packaging.

## Run the fast loop

From the repository root, configure and run the sanitizer-backed headless
smoke test first:

```sh
cmake -S tools/p4-game-host -B build-host/play-space_invaders -G Ninja \
  -DP4_GAME=space_invaders -DP4_ALLOW_DRAFT_GAME=ON
cmake --build build-host/play-space_invaders
ctest --test-dir build-host/play-space_invaders --output-on-failure
```

Replace `space_invaders` with the manifest directory slug. Do not add a
per-game runner or copy game sources into `tools/`; the generic target must
compile the real sources in place.

Start interactive play only when a visible desktop session is available:

```sh
make play-game GAME=space_invaders
```

Use arrows or WASD for movement, Space or Z for A, X or Shift for B, Enter or
P for Start, and Escape, Backspace, or Q for Back. Use mouse clicks to exercise
the standard tablet touch regions. Quit the window before rebuilding, then
repeat after each meaningful gameplay change.

## Tune from observed behavior

Ask what feels wrong only after the user has a playable build. Translate the
feedback into a narrow source change, such as movement speed, fire cadence,
enemy timing, collision bounds, score pacing, colors, touch behavior, or tone
duration. Preserve bounded state and Game API ownership rules.

After each change:

1. Rebuild and run the headless CTest smoke.
2. Reopen the interactive runner and exercise the changed behavior.
3. Check Start and Back, edge movement, rapid button presses, mouse/touch
   regions, restart behavior, and audio when relevant.
4. Run the target's focused host tests when present, then run
   the wider `make game-sdk-host` only when a shared API/package change warrants
   it; use the selected board's normal build dependencies for firmware.

Treat a sanitizer error, nonzero exit, invalid manifest, frozen game loop, or
failure to return on Back as a local test failure. Fix it before an ESP-IDF
build unless the user explicitly asks only for diagnosis.

## Hand off the local result

Before handing a changed game to the firmware workflow, report:

- the exact game slug and source revision or content hash tested;
- the headless CTest command and result;
- who performed the interactive play and which controls, touch regions,
  lifecycle paths, and audio behavior they actually exercised; and
- every behavior still pending on the physical tablet.

Use `local-play-tested` only after interactive play is explicitly confirmed.
Use `host-tested` for automated sanitizer/unit success without implying that a
person played the game. Never allow an unplayed or failing local candidate to
advance to a guarded device install.

## Keep acceptance claims honest

Local play can validate game rules, RGB565 drawing through the shared API,
normalized keyboard and mouse input, lifecycle behavior, tone-mixer requests,
and common memory or undefined-behavior failures.

Local play cannot validate ESP32-P4 performance or memory pressure, MIPI-DSI
panel output, physical GT911 touch, factory-speaker acoustics, USB host input,
power behavior, flash safety, or launcher integration on the tablet. Never
convert local success into hardware acceptance or permission to flash. Use the matching board's guarded install/acceptance route:
`$test-console-os-builds` for its exact Elecrow target, the Waveshare platform
skill for Waveshare, and `$develop-esp32-p4-platform` with
`docs/boards/M5STACK_TAB5.md` for Tab5.

For multiplayer changes, also use `$develop-p4-multiplayer-games` and the
selected game's two-instance session harness. This single-game SDL runner does
not provide a real Host/Join link or prove two-console synchronization.

Legacy Doom does not implement the reentrant Game API and is outside this SDL3
runner. Use its pinned `make doom-smoke WAD=/absolute/path/to/freedoom1.wad`
host proof where applicable, then follow the separate guarded tablet route.
