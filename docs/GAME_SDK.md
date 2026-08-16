# P4 Game SDK v1

P4 Game SDK v1 is the small native C interface used by games inside Console
OS. Console OS owns a 768x480 landscape viewport. Existing native-static games
draw a 320x200 RGB565 compatibility frame which the OS scales into that
viewport. Console OS
owns the panel, touch controller, audio hardware, timing, and app lifecycle.
That separation lets a game use controls and sound without knowing Waveshare
pin maps or calling ESP-IDF peripheral drivers.

## Delivery formats and current status

Doom is not a UF2 or a separately launched desktop-style executable. ESP-IDF
first links RISC-V machine code into `p4_console_os.elf`; its image tools then
produce the flashable `p4_console_os.bin`. Doom is currently statically linked
into that same ELF and receives an exclusive one-way handoff from the launcher.

Native SDK games use the explicit format name `p4-native-static-v1`. They are
also RISC-V machine code statically linked into the Console OS ELF, but use the
reentrant Game API lifecycle and can return to the launcher. Adding a game
currently means rebuilding and safely installing the complete Console OS app
image.

UF2 is a block-oriented flashing container, not a CPU executable format. This
repository does not produce or accept UF2. Removable games use the versioned,
source-included P4 Cart package; renaming an ELF or BIN file does not create one.

### P4Cart Console API (catalog implemented, runtime pending)

`game-platform/` now defines P4 Cart source projects, deterministic container
bytes, the P4 Lua v1 API, transfer framing, validation/pack/unpack tooling, and
the open Bounce Lab template. Console OS now mounts microSD without formatting,
hash-validates a bounded `/P4/GAMES` catalog, and reports valid carts in its
Library page. The host installer stages, syncs, read-back validates, and
atomically activates complete files. It does **not** yet execute Lua or preview
a cart, so cataloged carts are not launchable. Do not rename a native ELF, BIN,
C source, ZIP, or ROM to `.p4cart`. See `docs/CONTENT_LIBRARY.md`.

The intended delivery split is:

- Console OS, Doom, and any other reviewed native engine remain in the
  statically linked `p4_console_os.bin`.
- A P4Cart game targets the 768x480 landscape Console OS canvas through a
  reviewed sandboxed execution backend and cartridge container/packer. It will receive
  logical time, input, drawing, and approved assets through the Console API;
  it will never receive raw ESP-IDF, display, touch, audio, filesystem, USB,
  or storage handles.

The P4 Lua API defines four bounded tone voices through the OS-owned mixer.
That adapter is not implemented yet, so validated carts are not currently
launchable or audible. See `game-platform/README.md` and its API documents for
the exact boundary.

## Make a game

From the repository root:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
make play-game GAME=star_hop
make console-os-waveshare-idf
```

To play and tune any enabled native game locally before rebuilding or flashing
Console OS, use the SDL3 host runner:

```sh
make play-game GAME=space_invaders
make play-game GAME=maze_chase
```

This links the selected game's real C source against the same Game API drawing,
input, lifecycle, and tone-mixer code used by Console OS. Use arrows or WASD
for movement, Space/Z for A, X/Shift for B, Enter/P for Start, and
Escape/Backspace/Q for Back. A mouse click exercises the standard touchscreen
control regions. Local play is software validation, not tablet acceptance.

For a new or behavior-changing native game, this is the required development
order: run the sanitizer-backed host checks, play and tune the selected game
locally, rerun the checks after meaningful changes, and only then build a
firmware candidate. The guarded tablet workflow remains required for physical
display, touch, speaker, USB, resource, launcher, and install acceptance.

The creator picks the next free launcher ID and writes:

```text
games/star_hop/
  CMakeLists.txt
  game.json
  README.md
  src/star_hop.c
```

`game.json` is the launcher/build contract. The registry generator validates
every enabled manifest, rejects duplicate IDs and symbols, rejects direct
hardware/RTOS dependencies, and automatically links the component. Its
required `folder` field contains one or two uppercase
segments of at most 15 ASCII characters each, such as `GAMES/ARCADE` or
`SYSTEM`. Root shows derived `ALL PROGRAMS`, `GAMES`, and `SYSTEM` folders;
`GAMES` then shows type folders such as `ACTION` and `ARCADE`.

The launcher shows three columns by two rows and supports up to 32 registered
apps. Use the vertical arrows or swipe the app area to scroll by rows. Folder
discovery scans that fixed registry without heap allocation, recursion, a
filesystem, or a dynamic executable loader. No central source list needs to
be edited.

Use `--dry-run` to inspect the plan or `--help` for title, slug, folder, color,
and launcher-ID options. New games default to `GAMES/ARCADE`; choose another
bounded path with `--folder`. The generator never overwrites an existing game.

## API at a glance

Include only the stable headers under `components/p4_game_api/include/p4/`:

- `p4/game.h`: descriptor, start/update/render/stop lifecycle, capabilities,
  launcher return, and audio service calls.
- `p4/input.h`: normalized Up, Down, Left, Right, A, B, Start, and Back states,
  plus standard on-screen controls.
- `p4/draw.h`: clipped pixels, rectangles, circles, text, and RGB565 sprites.
- `p4/audio.h`: the host-owned tone plus copied-PCM mixer.
- `p4/achievements.h`: the fixed, in-session achievement catalog used by
  Console OS and host tools. Games declare achievements through
  `p4_game_unlock_achievement()` in `p4/game.h`.

The `update` callback receives bounded elapsed time and complete `held`,
`pressed`, and `released` button snapshots. Return
`P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed. The `render` callback receives
a caller-owned 320x200 surface; every supplied drawing primitive clips to its
bounds.

## Unlock achievements

An active game may call `p4_game_unlock_achievement(context, id, title,
description)` for a short, game-scoped badge. IDs, titles, and descriptions
are bounded; repeated IDs for the same game are treated as an already-unlocked
success. Console OS keeps up to 32 entries in RAM and shows them in its
Achievements page. Do not treat this as a save API: persistence will be added
only with the separately reviewed storage policy.

## OS resource inheritance

Games inherit a stable logical console rather than a board definition:

- The existing native-static compatibility API is clipped RGB565 at 320x200,
  and Console OS scales it into the 768x480 landscape viewport. New P4 Cart
  games target 768x480 directly. Neither format may infer scanout rotation,
  pin maps, stride layout, or backlight behavior.
- Input is a complete normalized snapshot. Use only the Game API buttons,
  touch points, and standard on-screen controls; never retain a touch pointer
  or talk to GT911/USB directly.
- Lifecycle, timing, launcher return, and audio sessions belong to Console OS.
  A game requests a capability in `game.json`, then uses only the matching
  `p4/` function. It never opens I2S, configures GPIO, owns an audio worker,
  or starts a FreeRTOS task.

## Make sound through the host-owned API

Audio is optional and must be original or correctly licensed. Use one of these
two native-API paths:

1. For hops, hits, UI feedback, and other short effects, request `audio-tone`
   and call `p4_game_play_tone()` with a bounded frequency, duration, volume,
   and square or triangle waveform. The host mixes at most eight voices.
2. For original music or more detailed effects, request `audio-stream`, add
   `P4_GAME_CAP_AUDIO_STREAM`, mix signed 16 kHz PCM16 stereo in game-owned
   fixed buffers, and submit 1–256 frames with
   `p4_game_submit_pcm16_stereo()`.

Each accepted stream call copies 1–256 frames of already-mixed signed 16 kHz
PCM16 stereo into a fixed 512-frame FIFO; the caller may reuse its buffer as
soon as the call returns. Stream audio and tones are saturating-mixed before
Console OS writes the shared backend. A `false` result means the optional
service is unavailable, the request is invalid, or the FIFO is full. Drop or
degrade that block—never busy-wait inside a game callback. In every game,
`stop` must call `p4_game_stop_audio()` (directly or through the provided game
helper) before returning to the launcher.

On the Waveshare 4.3, Console OS opens the reviewed ES8311 speaker session at
the selected 1–10 master step only while a requesting native game runs. The
default is 8/10, and session teardown restores the GPIO53 amplifier-safe
state. Games never own I2S, codec I2C, GPIO53, or the backend. The stream call
was already reserved in API v1, so
activating this bounded implementation does not change the native format or
API version.

## Make animated art with ImageGen

New or materially revised raster animation art **must use ImageGen** as its
source. A game that has no raster animation may use code-drawn primitives;
this requirement applies when adding animation frames, sprites, or a sprite
atlas. Do not substitute copied arcade, console, web, or commercial-game art.

Use this workflow:

1. Ask ImageGen for one crop-safe animation atlas: state the exact frame grid,
   cell size, every animation row, a shared baseline, transparent background,
   limited palette, hard pixel edges, and no text, borders, shadows, or scenery
   outside a frame.
2. Inspect every frame before committing. Regenerate it when a frame bleeds
   into another cell, loses the shared baseline, has opaque background pixels,
   or cannot be cropped independently.
3. Save the selected PNG at `games/<slug>/assets/`, record the ImageGen prompt,
   purpose, grid, license/provenance, and source filename in that game's
   README, and keep any source-only art out of runtime dependencies.
4. Commit a game-local deterministic converter in `games/<slug>/tools/` that
   produces a bounded RGB565 include in `src/generated/`. Use nearest-neighbor
   conversion and one explicit transparent chroma key. Runtime code must never
   decode PNGs or allocate an image loader.
5. Test regeneration: the converter output must exactly match the committed
   generated include. Account for `width * height * 2` bytes in the game’s
   static flash budget and draw cells with `p4_draw_sprite_rgb565()`.

## Rules for portable games

- Keep hardware access out of `games/`; reusable services belong in
  `components/`.
- Treat input as a snapshot. Never retain touch pointers or assume one contact.
- Bound all state, loops, sprite dimensions, text lengths, and audio requests.
- Use original or correctly licensed code and assets. Do not copy arcade ROMs,
  maps, sprites, fonts, or sounds.
- Record ImageGen animation-art provenance and use a deterministic RGB565
  conversion path for every animated raster asset.
- Do not commit commercial Doom WADs or WAD-bearing firmware artifacts.
- Run the host sanitizer suite before the pinned ESP-IDF build. A successful
  build is not hardware acceptance or permission to flash.

Maze Chase under `games/maze_chase/` and Space Invaders under
`games/space_invaders/` are complete clean-room examples. Both use only
code-rendered shapes and the P4 API; neither contains arcade ROM, map, sprite,
font, art, or sound assets.
