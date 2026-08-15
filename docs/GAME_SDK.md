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
tone audio, and completion. It receives no display, touch, audio, USB, or
filesystem handles.

Native machine code is not a security sandbox. A structurally valid malicious
cartridge can still execute CPU instructions, so install only packages you
trust. This format is not UF2; UF2 is a flashing container, while `.P4G` is a
Console OS runtime package.

Doom remains a special legacy case. Its engine is linked into the OS, but its
WAD is read from `P4 GAMES`; it uses an exclusive one-way handoff until the
engine has a reviewed reentrant teardown.

## Make and install a game

From the repository root:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
make console-os-idf
```

The creator chooses the next free launcher ID and writes:

```text
games/star_hop/
  CMakeLists.txt
  game.json
  README.md
  src/star_hop.c
```

The Console OS build validates every enabled manifest and produces:

```text
apps/console_os/build/game-storage-seed/STAR_HOP.P4G
```

Connect the laptop to J16, copy `STAR_HOP.P4G` to the root of `P4 GAMES`,
eject the volume cleanly, and open Game Manager. The game appears in the
folder from its package metadata. Replacing that file updates the game;
removing it in Game Manager uninstalls it. No OS reflash is needed.

`game.json` is the source/package contract. Its important fields are:

- `format`: `p4-native-elf-v1`;
- `api_version`: `1`;
- `version`: a bounded semantic version;
- `package_file`: an uppercase root `.P4G` filename;
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
- `p4/audio.h`: the host-owned eight-voice square/triangle tone mixer.

`update` receives bounded elapsed time plus complete `held`, `pressed`, and
`released` snapshots. Return `P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed.
`render` receives the caller-owned surface; supplied drawing primitives clip
to its bounds.

Tone audio is optional. `p4_game_play_tone()` may return false when sound is
unavailable. On the authorized 10.1-inch unit, Console OS opens the reviewed
16 kHz PCM16-stereo speaker session only for the foreground game, then restores
the proven active-high amplifier shutdown state. The reserved PCM-stream
capability is not exposed in v1.

## Portable-game rules

- Keep hardware access out of `games/`; reusable services belong in
  `components/`.
- Treat input as a snapshot and never retain touch pointers.
- Bound state, loops, sprite dimensions, text, and audio requests.
- Use original or correctly licensed code and assets.
- Never commit commercial Doom WADs, WAD-bearing binaries, or recovery images.
- Run host sanitizers and the pinned ESP-IDF verifier before copying a package
  to hardware.

Maze Chase and Space Invaders are complete clean-room examples using only
code-rendered shapes and P4 APIs.
