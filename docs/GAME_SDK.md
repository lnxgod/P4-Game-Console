# P4 Game SDK v1

P4 Game SDK v1 is the stable C interface for storage-installed Console OS
games. A game owns bounded gameplay state and draws a 320x200 RGB565 frame.
Console OS owns the panel, touch, audio, timing, USB, filesystem, and app
lifecycle.

## Package format

Native games use `p4-native-elf-v1`. A `.P4G` file contains a fixed 256-byte
little-endian header followed by one stripped ESP32-P4 ELF32 `ET_DYN` payload.
The header carries bounded launcher metadata, capability masks, and the
payload SHA-256.

Before invoking Espressif's pinned `elf_loader` 1.3.1, firmware verifies:

- the entire package layout and reserved bytes;
- ASCII IDs, titles, versions, licenses, and two-level folder names;
- package size, payload digest, and duplicate launcher/game IDs;
- RISC-V ELF class, machine, headers, load ranges, memory limit, entry point,
  section/string bounds, relocation count/types, and referenced symbols;
- an undefined-symbol allowlist limited to the libc calls used by the SDK
  wrapper.

The relocated cartridge receives only a versioned host table containing the
RGB565 surface and callbacks for sanitized input, frame presentation, bounded
tone audio, achievements, and completion. It receives no display, touch,
audio, USB, or filesystem handles.

Native machine code is not a security sandbox. A structurally valid malicious
cartridge can still execute CPU instructions, so install only packages you
trust. This format is not UF2; UF2 is a flashing container, while `.P4G` is a
Console OS runtime package.

The earlier `.P4CART` format remains supported as a distinct open-source
container under `P4/GAMES/*.P4CART`. Console OS bounds and hashes those
`P4CART1` Lua-source packages and shows valid entries in Game Manager. They
must never be renamed to `.P4G`: the reviewed `p4-lua-5.4-v1` sandbox is still
pending, so this compatibility path validates and catalogs carts but does not
execute them yet. See `docs/CONTENT_LIBRARY.md`.

Doom remains a special legacy case. Its engine is linked into the OS, but its
WAD is read from `P4 GAMES`; it uses an exclusive one-way handoff until the
engine has a reviewed reentrant teardown.

## Make and install a game

From the repository root:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
cmake -S games/star_hop -B build-host/star_hop -G Ninja
cmake --build build-host/star_hop
```

The starter compiles on the host immediately. Before adding nontrivial game
logic, follow an existing game's `tests/` and CMake wiring, then run
`ctest --test-dir build-host/star_hop --output-on-failure`.

The creator chooses the next free launcher ID and writes:

```text
games/star_hop/
  CMakeLists.txt
  game.json
  README.md
  src/star_hop.c
```

The Console OS build validates every enabled manifest and produces either:

```text
apps/console_os/build/game-storage-seed/GAMES/STAR_HOP.P4G
apps/console_os/build-olimex-esp32-p4-pc/sd-card/GAMES/STAR_HOP.P4G
apps/console_os/build-waveshare-landscape/sd-card/GAMES/STAR_HOP.P4G
```

Run `make console-os-idf` after the focused game test when a distributable
cartridge or device install is needed. Use `make game-registry-check` for a
manifest or generator change and `make game-sdk-host` for shared API, package,
loader, or cross-game changes. Do not run repo-wide `make check` for an
isolated game change.

On Elecrow, connect the laptop to J16, copy `STAR_HOP.P4G` into the `GAMES`
directory on `P4 GAMES`, eject the volume cleanly, and open Game Manager. On
Olimex Rev.B, power off, move the microSD card to the laptop, copy the cartridge
into `GAMES` (or rebuild the complete card bundle and run
`make install-olimex-sd-card
SD_MOUNT=/Volumes/P4GAMES`), eject it, reinstall it, and power on. Replacing the
file updates the game; removing it in Game Manager uninstalls it. Neither path
requires an OS reflash.

Waveshare uses the same powered-off microSD workflow and keeps the card
read-only while Console OS runs. Use `make install-waveshare-sd-card
SD_MOUNT=/Volumes/P4GAMES` for the validated complete bundle.

Executable `.P4G` packages are never linked into the OTA application. The
launcher and loader support only storage-backed catalog entries, while the
separate P4CART compatibility catalog remains non-executable until its Lua
sandbox is implemented.

`game.json` is the source/package contract. Its important fields are:

- `format`: `p4-native-elf-v1`;
- `api_version`: `1`;
- `version`: a bounded semantic version;
- `package_file`: an uppercase `.P4G` basename stored under `GAMES`;
- unique `id` and `launcher_id`;
- bounded `title`, `subtitle`, `folder`, `license`, and RGB565 accent;
- required and optional capabilities.

Use `scripts/new-game.py --dry-run` to inspect a starter plan. The creator
never overwrites an existing game.

## API at a glance

Include only headers under `components/p4_game_api/include/p4/`:

- `p4/game.h`: descriptor, lifecycle, capabilities, launcher return, and
  service calls;
- `p4/input.h`: Up, Down, Left, Right, A, B, Start, Back, and standard
  on-screen controls;
- `p4/draw.h`: clipped pixels, shapes, text, and RGB565 sprites;
- `p4/audio.h`: the host-owned eight-voice tone and copied-PCM mixer;
- `p4/audio_pack.h`: original reusable PCM effects;
- `p4/feedback.h`: original reusable animated feedback overlays;
- `p4/achievements.h`: the bounded OS-owned session catalog.

`update` receives bounded elapsed time plus complete `held`, `pressed`, and
`released` snapshots. Return `P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed.
`render` receives the caller-owned surface; supplied drawing primitives clip
to its bounds.

Tone and PCM stream audio are optional. `p4_game_play_tone()` and
`p4_game_submit_pcm16_stereo()` may return false when their requested service
is unavailable. Console OS owns the 16 kHz PCM16-stereo session, opening it
only for the foreground game and restoring the reviewed board-safe state when
the game exits. A cartridge never receives a raw audio or GPIO handle.

Achievements are optional and bounded. Call `p4_game_unlock_achievement()`
with a short stable ID, title, and description. The host binds the event to
the running game ID, rejects mismatches, and de-duplicates repeat reports.
The current catalog lasts for the boot session; persistent saves are deferred.

## OS resource inheritance

Games inherit a stable logical console rather than a board definition:

- Every `.P4G` targets a clipped 320x200 RGB565 Game API surface. Console OS
  places that stable surface in its board-specific viewport. On Waveshare 4.3,
  the OS viewport is always 768x480 landscape. A game must not infer scanout
  rotation, pin maps, stride layout, or backlight behavior.
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
current default is 6/10, and session teardown restores the GPIO53 amplifier-safe
state. Games never own I2S, codec I2C, GPIO53, or the backend. The stream call
was already reserved in API v1, so
activating this bounded implementation does not change the native format or
API version.

### Use the standard original feedback pack

The Console API also includes `p4/audio_pack.h` and `p4/feedback.h` as the
source-of-truth reusable pack for native games. Request both `audio-tone` and
`audio-stream` in `game.json`; declare both capabilities; keep one
`p4_game_audio_effect_player_t` in game-owned state; trigger an action,
impact, reward, or fail effect; then service an already-active effect at most
once per update. The trigger itself queues at most one initial block. If
streaming is unavailable, retain a short `p4_game_play_tone()` fallback. The
pack never busy-waits.

Use `p4_game_feedback_draw_audio_effect()` for event-bound, crop-safe, clipped
original action, impact, reward, or fail overlays. It consumes the shared ImageGen atlas in
`components/p4_game_api/`, not a PNG decoder in the game. Asset provenance,
deterministic conversion commands, and the exact regeneration constraints live
in `components/p4_game_api/README.md`.

When making a new generated sound, use a configured original-sound provider
(for example ElevenLabs Sound Effects) and preserve the selected source WAV in
the owning game or reusable component. Record the provider, date, sharing or
license setting, exact prompt, source filename, SHA-256, resampling gain and
duration before committing its generated PCM include. Request a short dry
effect, no music, no voice, and no copyrighted sounds. Do not use a web rip,
commercial-game sample, or untracked generated media as a runtime source.

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
3. Save the selected PNG at `games/<slug>/assets/`, or beside the owning shared
   API component when multiple games use it. Record the ImageGen prompt,
   purpose, grid, license/provenance, and source filename in the owning README;
   keep source-only art out of runtime dependencies.
4. Commit a deterministic converter beside the owning game or shared component
   that produces a bounded RGB565 include. Use nearest-neighbor conversion and
   one explicit transparent chroma key. Runtime code must never decode PNGs or
   allocate an image loader.
5. Test regeneration: the converter output must exactly match the committed
   generated include. Account for `width * height * 2` bytes in the game’s
   static flash budget and draw cells with `p4_draw_sprite_rgb565()`.

## Portable-game rules

- Keep hardware access out of `games/`; reusable services belong in
  `components/`.
- Treat input as a snapshot. Never retain touch pointers or assume one contact.
- Bound all state, loops, sprite dimensions, text lengths, and audio requests.
- Use original or correctly licensed code and assets. Do not copy arcade ROMs,
  maps, sprites, fonts, or sounds.
- Record ImageGen animation-art provenance and use a deterministic RGB565
  conversion path for every animated raster asset.
- Do not commit commercial Doom WADs, WAD-bearing firmware artifacts, or
  recovery images.
- Run the changed game's focused host sanitizer tests. A successful build is
  not hardware acceptance or permission to flash.

Maze Chase, Space Invaders, and Byte Buddy are complete original examples.
Byte Buddy demonstrates virtual-pet care, a mini-game, tones, achievements,
and direct return to the launcher using only code-rendered shapes and P4 APIs.
Calculator, Input Test, and AV Test demonstrate removable utility and
diagnostic cartridges under the System hierarchy.
