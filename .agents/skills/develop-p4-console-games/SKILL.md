---
name: develop-p4-console-games
description: Create, scaffold, categorize, modify, or integrate native C P4 Console OS games and their manifests. Use for new Game API games, launcher game folders/types, game drawing, controls, tone or PCM sound, registry integration, return-to-launcher behavior, or kid-friendly game templates on this ESP32-P4 console.
---

# Develop P4 Console Games

Start from the user's idea, including a free-form concept or genre combination.
Use `docs/GAME_STARTERS.md` to choose an optional scaffold/example only when it
helps; do not require a template menu or constrain the user's mechanics or art.
Infer routine choices, ask only about missing decisions that change gameplay,
and build a playable core before tuning it with the user.

Native C and `.P4G` are the supported game-creation route, including networked
sessions. The former Lua authoring stack and skill are removed. Custom software
2D/3D engines and raycasting are allowed: draw
into the negotiated RGB565 surface through the stable lifecycle and resource
contracts. Shared rendering helpers are optional. Current packaging supports C;
a C++ port requires an explicitly tested C-ABI/toolchain adapter, not an assumed
C++/STL runtime. No language choice guarantees smooth device play.

Build against stable platform APIs, make the launcher category explicit and
keep hardware ownership in platform components. Keep state and work bounded
for the ESP32-P4 runtime.

## Load the contracts

Read `AGENTS.md`, `docs/GAME_SDK.md`, `docs/CONSOLE_OS.md`, and the target
game's `game.json` before editing. Also use:

- `$develop-p4-multiplayer-games` automatically for linked-console multiplayer;
  the user does not need to name the skill.
- `$test-p4-games-locally` for native SDL3 play, sanitizer smoke and tuning.
- `$develop-p4-games` for manifests, packages and content installation.
- `$develop-esp32-p4-platform` for actual toolchain, shared-service or board work.
  Ordinary drawing, tones and normalized controls do not require bring-up.
- `$add-usb-gamepad-support` only for missing/broken shared controller support
  or a physical HID/transport change, not ordinary button mappings.
- The matching board skill for hardware acceptance. Use
  `$test-console-os-builds` and the Elecrow display/audio skills only on their
  exact Elecrow target; Tab5 uses `docs/boards/M5STACK_TAB5.md`.

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
- A manifest may declare 1–16 unique C basenames in `sources`; omit it for the
  conventional single `src/<component>.c` game. Keep every source inside that
  game and inside the same reviewed package/ABI boundary.
- Use fixed storage and bounded loops. Do not bypass the reviewed ELF package
  validator/loader, add a second loader, or add an unbounded recursive folder
  walk.

## Ship the title and icon with the game

For every new game or remix, update the cartridge's own title, version and
launcher artwork. Follow `$develop-p4-games`'s cartridge-owned presentation
contract (`launcher_icon` in `game.json`); verify an install changes the tile
without adding a title/ID mapping to Console OS. Preserve the 0.44 nextgen
launcher design. Artwork for a separate ongoing remix remains that remix's
work; shared-service changes do not authorize replacing all game art.

## Choose a game type

Every enabled native game manifest must contain a `folder` path. Use one or
two uppercase segments, each 1–15 ASCII bytes and matching
`[A-Z0-9][A-Z0-9 -]{0,14}`. Never use a leading/trailing slash, an empty
segment, or more than one slash.

Prefer a broad reusable type under `GAMES`:

| Folder | Use for |
|---|---|
| `GAMES/ARCADE` | Short score, maze, wave or crossing games |
| `GAMES/ADVENTURE` | Exploration, narrative or companion games |
| `GAMES/PLATFORM` | Side-view jumping and platform games |
| `GAMES/SPORTS` | Hockey and other sports |
| `GAMES/CARDS` | Card games and solitaire |
| `GAMES/TABLETOP` | Dice and board games |
| `GAMES/SHOOTERS` | First-person shooters such as Doom and Chex Quest |

Reuse a current type before adding a new category. Add Puzzle or Racing only
when an actual finished game needs one. Reserve `SYSTEM/TESTS` for diagnostics
and `SYSTEM/TOOLS` for utilities. Use player-facing Title Case names and a
short description of the game; omit P4, API versions and implementation terms.
Keep game IDs, package filenames, saves and network protocol identities stable
when renaming. See `docs/GAME_LIBRARY.md` for the current library and quality bar.

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

Use the repository creator for a fresh identity, then borrow only the needed
patterns from `docs/GAME_STARTERS.md`. A scaffold is optional guidance for the
implementation, not a restriction on the game idea:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/PLATFORM
python3 scripts/new-game.py "Card Table" --folder GAMES/CARDS --high-res
```

Use `--dry-run` first when the requested title, slug, ID, accent, or folder is
uncertain. The creator refuses overwrites, chooses a free launcher ID, and
writes the validated folder metadata. New scaffolds are unpublished
(`enabled: false`); use `-DP4_ALLOW_DRAFT_GAME=ON` in the local runner, or
`make play-game GAME=<slug>`, while developing them. Enable the finished game
for packaging after focused validation; keep retired identities reserved by
`games/retired.json`. Do not add a central launcher entry;
configure-time registry generation discovers enabled manifests and emits the
parallel game/folder tables.

For an existing game, update its `game.json` folder directly and keep the
descriptor title/subtitle within their byte bounds. Do not put launcher folder
metadata into `p4_game_descriptor_t`; that descriptor is the stable API ABI.

## Implement against Game API v1

- Use `p4/game.h` for lifecycle/capabilities and return
  `P4_GAME_EXIT_TO_LAUNCHER` on Back.
- Use `p4/input.h` for normalized held/pressed/released controls. By default
  map title/start, menus, gameplay, pause, retry and Back to D-pad/A/B/Start/
  Back wherever the mechanics permit; retain touch as well. Supported USB/BLE
  pads feed this existing OS input path, so a game needs no USB capability,
  driver, pairing screen or raw controller access. Use held for movement and
  transitions for actions. Preserve an explicitly touch-only design, but state
  that limit. Never infer physical Tab5 HID support from the shared API.
- For card and tabletop games, make visible cards, pieces, dice and semantic
  actions the primary touch interface. Support natural legal drag-and-drop and
  tap alternatives; omit a permanent virtual D-pad/A/B overlay when direct touch
  already covers play. Retain normalized physical-controller support. Consume
  touch gestures through release before interpreting synthesized button bits so
  a card drag cannot trigger an invisible controller action. Follow the direct
  interaction section of `docs/GAME_ART.md`.
- Use optional `p4/draw.h` primitives and licensed sprites, or a bounded custom
  software renderer. Keep clipping, stride and resource ownership correct.
- Target native 768x480 RGB565 by default, with optional `video-highres` plus
  `P4_GAME_CAP_VIDEO_HIGH_RES` and a tested 320x200 fallback. The creator now
  requests this automatically. Render directly into the negotiated surface;
  enlarging a finished low-resolution frame is not a visual upgrade. Touch
  and standard control hit regions remain normalized to 320x200 in either mode.
- Read `docs/GAME_ART.md` for the shared presentation and ImageGen contract.
  Use `p4/presentation.h` for cartridge-local antialiased text and materials;
  keep important card ranks, suits, scores and instructions exact and legible.
- Use `p4/visual.h` for fixed-point motion, atlas animation, easing,
  deterministic camera shake, and small caller-owned particle arrays. These
  helpers are optional building blocks for the game's engine; custom rendering
  still uses the OS-owned lifecycle, timing and supplied surface.
- Apply the canonical [ESP32-P4 performance contract](../../../docs/GAME_PERFORMANCE.md)
  from the first playable build: bounded shared raster work, fractional motion,
  pinned RV32 hot-path review and exact-device cadence evidence. Target 60 FPS;
  qualify the actual-device 30 FPS release floor separately from host success.
- Use `p4/audio.h` for host-owned sound.
- Stop all requested sound in the game's `stop` callback.
- Use both P4 cores through OS-owned services, as described in
  `docs/GAME_PERFORMANCE.md`. Games submit bounded PCM/tone commands; the Tab5
  0.54 candidate's shared core-1 audio worker handles mixing/output while the
  foreground core runs the game. Never create private tasks or move cartridge
  callbacks to another core. Measure producer jitter and backpressure; a
  successful synth test alone does not prove continuous device audio.

For linked-console multiplayer, automatically use
`$develop-p4-multiplayer-games`. Declare the optional `multiplayer-session`
capability and validated `multiplayer` profile; consume only the bounded,
non-blocking `p4_game_multiplayer_*` API. Console OS owns registration,
Host/Join, transport selection and the start barrier. The game owns its rules,
messages and offline/peer-loss behavior. `new-game.py --multiplayer` creates
metadata only; it does not implement synchronization.

Use the public APIs for optional saves, achievements, resource sidecars and
dice accessories when the design needs them. See `docs/GAME_STARTERS.md` and
the corresponding Game SDK section before adding a service. Neither `storage`
nor `save` gives a game a filesystem path; missing optional capabilities need
an honest fallback.

For simple sound, request `audio-tone` and call `p4_game_play_tone()`. For a
software mixer, request `audio-stream`, declare
`P4_GAME_CAP_AUDIO_STREAM`, and submit 1–256 frames of signed 16 kHz PCM16
stereo with `p4_game_submit_pcm16_stereo()`. The host copies accepted blocks
into its fixed 512-frame FIFO; drop/degrade a rejected block and never
busy-wait in a callback.

Use original or correctly licensed assets. Never import arcade ROMs, maps,
sprites, fonts, sounds, or commercial Doom WAD content.

## Make high-resolution art usable in firmware

Use ImageGen for new or materially revised raster art. Request crop-safe
sprite atlases or seamless materials sized for their native 768x480 use.
Describe the grid, frame contents, shared baseline, palette and transparency;
keep important text and symbols code-rendered. Inspect converted artwork at
native size. Pixel art may retain hard edges, but it is not the default style
for every game; follow the user's art direction and `docs/GAME_ART.md`.

- Keep the original source PNG under `games/<slug>/assets/` and explain its
  provenance, row/frame layout, and license in that game's README.
- Commit a game-local converter under `games/<slug>/tools/` which takes the
  source PNG to a fixed-size RGB565 include under `src/generated/`. Use
  nearest-neighbor scaling for pixel art or a recorded quality offline filter
  for painted materials, and map transparency to one explicit chroma key. Never decode PNGs, allocate image buffers, or use a texture loader at
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

Verify only the boundaries changed:

- For game behavior, run that game's focused CMake/CTest target and the local
  SDL3 loop. Multiplayer also needs its two-instance session tests.
- For a new/changed manifest or folder, run `make game-registry-check`.
  Registry generation discovers the game automatically.
- For a new launcher category behavior, exercise navigation into the category,
  launch/return and Up; change shared shell tests only if shell behavior changed.
- For shared Game API, loader or package changes, run `make game-sdk-host`;
  for shared shell changes, run `make console-shell-host`.
- For skill/documentation-only changes, validate the skills and references;
  do not rebuild firmware.

When packaging or a firmware candidate is requested, use one matching target:
`make console-os-tab5-idf` (the default), `make console-os-elecrow-idf`,
`make console-os-olimex-idf`,
or `make console-os-waveshare-idf`.
Follow the matching board route for any install. A build is not hardware
acceptance. Game-only updates use the existing content path and need no OS
reflash.

Update `docs/GAME_SDK.md` only when the reusable contract changes. Record
current evidence separately from historical runs. Use `$develop-p4-games`
for package validation and installation.

## Match the intended spatial experience

For a physical marble, tilting tray or water-maze request, establish the camera
and depth in the first playable scene. When the user expects 3D or 3D-like play,
use a native projected scene with visible wall faces, occlusion, shaded actors
and a water surface that responds continuously to the simulation. A flat grid
with decorative ripples does not meet that brief. Keep reusable bounded mesh
rasterization in the shared API; games own the camera and scene. Use the native
cartridge path for this class of real-time game.

Show an actual native-resolution gameplay capture early, before lengthy polish
or packaging. A concept image is not evidence of the renderer. Check motion as
well as a still: interpolate local fixed-step actors, preserve fractional
projection, handle respawns without tweening across walls, and invert the
actual camera for touch targeting. Keep input, physics and multiplayer rules
independent of the view. A rejected visual direction requires a scene/rendering
change, not just another texture on the same presentation.

## Water and rigid-body requests

When water is a gameplay mechanic, separate the 3D renderer from the fluid model.
A projected textured plane does not establish water physics. Choose a bounded
model appropriate to the device, state its limits (for example, a height field
cannot overturn), and prove small-force response, wave propagation after release,
volume conservation, solid boundaries, displacement and forces back on objects.
For multiplayer, distinguish host-authoritative body/rule state from locally
reconstructed surface effects; do not call an unsent fluid field synchronized.

Review a moving reference or interactive demo when the expected interaction is
unclear. Compare an actual source-rendered motion capture with it before packaging;
do not describe a read article or a static image as a watched video. The optional
`p4/shallow_water.h` and `p4/scene3d.h` helpers demonstrate caller-owned bounded
storage, physical volume/momentum and banded native depth rendering. They are
choices, not required templates or evidence of device frame rate. Include depth,
refraction scratch and projected vertices in the 128 KiB state budget.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
