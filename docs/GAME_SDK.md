# P4 Game SDK v1

P4 Game SDK v1 is the small native C interface used by games inside Console
OS. A game owns gameplay state and draws a 320x200 RGB565 frame. Console OS
owns the panel, touch controller, audio hardware, timing, and app lifecycle.
That separation lets a game use controls and sound without knowing Elecrow pin
maps or calling ESP-IDF peripheral drivers.

## What format Doom actually uses

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
repository does not currently produce or accept UF2. A future removable-game
format should be a signed/versioned P4 package with a reviewed loader and
resource limits; renaming an ELF or BIN file to `.uf2` would not provide that.

## Make a game

From the repository root:

```sh
python3 scripts/new-game.py "Star Hop"
make game-sdk-host
make console-os-idf
```

The creator picks the next free launcher ID and writes:

```text
games/star_hop/
  CMakeLists.txt
  game.json
  README.md
  src/star_hop.c
```

`game.json` is the launcher/build contract. The registry generator validates
every enabled manifest, rejects duplicate IDs and symbols, and automatically
links the component. The launcher shows six apps per page and supports up to
32 registered entries. No central source list needs to be edited.

Use `--dry-run` to inspect the plan or `--help` for title, slug, color, and
launcher-ID options. The generator never overwrites an existing game.

## API at a glance

Include only the stable headers under `components/p4_game_api/include/p4/`:

- `p4/game.h`: descriptor, start/update/render/stop lifecycle, capabilities,
  launcher return, and audio service calls.
- `p4/input.h`: normalized Up, Down, Left, Right, A, B, Start, and Back states,
  plus standard on-screen controls.
- `p4/draw.h`: clipped pixels, rectangles, circles, text, and RGB565 sprites.
- `p4/audio.h`: the host-owned, eight-voice square/triangle tone mixer.

The `update` callback receives bounded elapsed time and complete `held`,
`pressed`, and `released` button snapshots. Return
`P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed. The `render` callback receives
a caller-owned 320x200 surface; every supplied drawing primitive clips to its
bounds.

Tone audio is optional. Call `p4_game_play_tone()` and accept a `false` result
when sound is unavailable. On the authorized 10.1-inch unit, Console OS opens
the reviewed 16 kHz PCM16-stereo factory speaker session at volume step 6/10,
mixes at most eight voices, and restores proven active-high amplifier shutdown
when the game leaves or any backend operation fails. Game code never owns I2S,
GPIO30, or the audio backend.

The API has a reserved PCM stream capability, but Console OS v1 intentionally
does not expose it yet. Games should request only capabilities they use and
must still function when an optional capability is absent.

## Rules for portable games

- Keep hardware access out of `games/`; reusable services belong in
  `components/`.
- Treat input as a snapshot. Never retain touch pointers or assume one contact.
- Bound all state, loops, sprite dimensions, text lengths, and audio requests.
- Use original or correctly licensed code and assets. Do not copy arcade ROMs,
  maps, sprites, fonts, or sounds.
- Do not commit commercial Doom WADs or WAD-bearing firmware artifacts.
- Run the host sanitizer suite before the pinned ESP-IDF build. A successful
  build is not hardware acceptance or permission to flash.

Maze Chase under `games/maze_chase/` is the complete clean-room example. It
uses only code-rendered shapes and the P4 API; it does not contain Pac-Man ROM,
map, sprite, sound, or artwork data.
