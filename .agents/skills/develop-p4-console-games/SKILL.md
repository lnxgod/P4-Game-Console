---
name: develop-p4-console-games
description: Create, scaffold, categorize, modify, or integrate native C P4 Console OS games and their manifests. Use for new Game API games, launcher game folders/types, game drawing, controls, tone or PCM sound, registry integration, return-to-launcher behavior, or kid-friendly game templates on this ESP32-P4 console.
---

# Develop P4 Console Games

Build games against the stable platform APIs and make their launcher category
explicit. Keep hardware ownership in platform components and keep every game
bounded enough for the ESP32-P4 runtime.

## Load the contracts

Read `AGENTS.md`, `docs/GAME_SDK.md`, `docs/CONSOLE_OS.md`, and the target
game's `game.json` before editing. Also use:

- `$develop-esp32-p4-platform` for the locked ESP-IDF toolchain or board work.
- `$use-elecrow-p4-display` for panel, framebuffer, or on-device visual work.
- `$use-elecrow-p4-audio` for factory-speaker integration or acoustic tests.
- `$add-usb-gamepad-support` only when USB HID input is in scope.
- `$test-p4-games-locally` for the required pre-firmware play, smoke, and
  gameplay-tuning loop.
- `$test-console-os-builds` for candidate verification, flashing, recovery, or
  manual tablet acceptance.

## Preserve the native model

- Keep format `p4-native-elf-v1` and API version 1 unless a separately
  reviewed ABI migration is requested.
- Treat games as bounded RISC-V ELF cartridges packaged into `.P4G` files and
  installed on persistent game storage. They are not host EXEs or UF2 files;
  adding or removing one does not require an OS reflash while API v1 remains
  compatible. The SDL3 runner compiles the same game sources directly for
  local play, but it does not change the device package format.
- Let Console OS own display, touch, audio hardware, timing, and lifecycle.
  Games use only stable `p4/` APIs and never own raw ESP-IDF peripherals.
- Keep capability flags as internal runtime metadata. Do not expose
  developer-facing `V/T/A/S` labels on kid-facing launcher tiles; render the
  icon, title, description, and folder/app count only.
- Keep reusable services in `components/`; keep game-specific code in
  `games/<slug>/`.
- Use fixed storage and bounded loops. Do not bypass the reviewed ELF package
  validator/loader, add a second loader, or add an unbounded recursive folder
  walk.

## Choose a game type

Every enabled native game manifest must contain a `folder` path. Use one or
two uppercase segments, each 1–15 ASCII bytes and matching
`[A-Z0-9][A-Z0-9 -]{0,14}`. Never use a leading/trailing slash, an empty
segment, or more than one slash.

Prefer a broad reusable type under `GAMES`:

| Folder | Use for |
|---|---|
| `GAMES/ARCADE` | Fixed-screen, maze, score-chase, or classic shooter games |
| `GAMES/ACTION` | Combat, FPS, survival, or fast action games |
| `GAMES/PUZZLE` | Logic, matching, word, or block puzzles |
| `GAMES/PLATFORM` | Side-view jumping and platform games |
| `GAMES/RACING` | Driving, racing, or time-trial games |
| `GAMES/ADVENTURE` | Exploration or narrative games |

Reuse an existing type when it fits. Default to `GAMES/ARCADE` when the game
is ambiguous; do not create a near-duplicate category for one title. Reserve
`SYSTEM` for built-in diagnostics and platform tools, not ordinary games.

The shell derives its hierarchy from manifests:

```text
Root
  All Programs
  Games
    <game type>
      <game>
  System
```

Root tiles and folder tiles are logical views; startup still reports the
number of registered apps, not the number of visible root folders.

## Scaffold a game

Use the repository creator instead of copying an old game:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/PLATFORM
```

Use `--dry-run` first when the requested title, slug, ID, accent, or folder is
uncertain. The creator refuses overwrites, chooses a free launcher ID, and
writes the validated folder metadata. Do not add a central launcher entry;
configure-time registry generation discovers enabled manifests and emits the
parallel game/folder tables.

For an existing game, update its `game.json` folder directly and keep the
descriptor title/subtitle within their byte bounds. Do not put launcher folder
metadata into `p4_game_descriptor_t`; that descriptor is the stable API ABI.

## Implement against Game API v1

- Use `p4/game.h` for lifecycle/capabilities and return
  `P4_GAME_EXIT_TO_LAUNCHER` on Back.
- Use `p4/input.h` for normalized held/pressed/released controls.
- Use `p4/draw.h` for clipped RGB565 primitives or bounded licensed sprites.
- Use `p4/audio.h` for host-owned sound.
- Stop all requested sound in the game's `stop` callback.

For simple sound, request `audio-tone` and call `p4_game_play_tone()`. For a
software mixer, request `audio-stream`, declare
`P4_GAME_CAP_AUDIO_STREAM`, and submit 1–256 frames of signed 16 kHz PCM16
stereo with `p4_game_submit_pcm16_stereo()`. The host copies accepted blocks
into its fixed 512-frame FIFO; drop/degrade a rejected block and never
busy-wait in a callback.

Use original or correctly licensed assets. Never import arcade ROMs, maps,
sprites, fonts, sounds, or commercial Doom WAD content.

## Make animated 8-bit art usable in firmware

When a game needs generated pixel art, request a purpose-built sprite atlas,
not a scenic illustration. Describe the exact grid, every frame in each row,
a shared baseline, a uniform transparent background, limited palette, hard
pixel edges, and no text or cell borders. Inspect the generated image before
using it; regenerate it if the frames are not independently crop-safe.

- Keep the original source PNG under `games/<slug>/assets/` and explain its
  provenance, row/frame layout, and license in that game's README.
- Commit a game-local converter under `games/<slug>/tools/` which takes the
  source PNG to a fixed-size RGB565 include under `src/generated/`. Use
  nearest-neighbor scaling and map transparent pixels to one explicit chroma
  key. Never decode PNGs, allocate image buffers, or use a texture loader at
  runtime.
- Bound every atlas dimension and account for `width * height * 2` bytes in
  the static firmware budget. Draw an individual cell with
  `p4_draw_sprite_rgb565()` using the atlas row stride and its frame offset.
- Advance frames from bounded game state and draw the same cell family in the
  title/attract view and gameplay. Animate only state that the game owns;
  hardware timing remains the host's responsibility.
- Record generated art accurately in `game.json`'s `assets` field. Never
  describe generated sprites as code-rendered-only or as licensed third-party
  art.

## Play and tune locally

Before building a firmware candidate, use `$test-p4-games-locally` on every
new or behavior-changing native game. Run its sanitizer-backed headless smoke,
then open the real game sources in the SDL3 runner:

```sh
make play-game GAME=space_invaders
```

Replace `space_invaders` with the target manifest directory slug. Exercise
movement, A/B, Start, Back, edge conditions, mouse-mapped touch regions, and
tone audio when requested. Iterate on observed gameplay until the local build
is ready, rerunning the headless smoke after each meaningful change.

Do not skip the local loop merely because an ESP-IDF build succeeds. Also do
not treat local play as proof of panel, physical touch, speaker, USB, resource,
launcher, or flash behavior. Legacy Doom uses its separate host-smoke path and
is not compatible with this native Game API runner.

Carry the local-test handoff into the firmware stage: exact game slug and
source revision or content hash, headless CTest result, interactive tester,
behaviors exercised, and tablet-only checks still pending. Do not call a
candidate `local-play-tested` without explicit interactive confirmation, and
do not request a guarded install for an unplayed or failing changed game.

## Verify the hierarchy and game

After local play passes, run the full host checks:

```sh
python3 scripts/generate-game-registry.py --games-root games --check
python3 scripts/tests/test-game-registry.py
python3 scripts/tests/test-new-game.py
make game-sdk-host
make console-shell-host
```

Tests must prove the manifest folder is valid and aligned with its generated
game entry. For a new type, add launcher tests that open Root -> Games -> Type,
launch the game, return to the same type folder, and navigate Up to Root.
Preserve All Programs scrolling and gesture-suppression coverage.

When the request includes a firmware candidate, run only the matching target:
`make console-os-idf`, `make console-os-olimex-idf`, or
`make console-os-waveshare-idf`. Use `$test-console-os-builds` for the exact
Elecrow tablet route and `$develop-waveshare-p4-4.3` for the Waveshare route.
A build is not hardware acceptance and does not authorize a flash.

Update `docs/GAME_SDK.md` only when the reusable contract changes. Record a
new exact build artifact instead of rewriting an executed or historical
record.
