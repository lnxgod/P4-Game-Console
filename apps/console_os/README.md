# P4 Console OS

This is the FreeRTOS-native console shell for the Waveshare ESP32-P4-WIFI6
Touch LCD 4.3, with the SDL3 host runner as the fast PC development target.
It is a monolithic ESP-IDF firmware with a small static app registry, not a
desktop process loader. The launcher and built-in pages share OS-owned
display, touch, and audio services.

The home screen currently organizes the static registry as:

- All Programs (every entry in one scrollable view)
- Games
  - Action: Doom
  - Arcade/Platform: manifest-discovered reentrant Game API games
- System: Colors, Touch, System status, Audio, Achievements, Game Manager,
  Multiplayer, File Manager, Save Manager, and Terminal

The launcher is generated from validated `games/*/game.json` manifests. Its
lightweight Program Manager-style home view shows three columns by two rows,
scrolls with vertical arrows or a one-finger swipe, and supports up to 32
apps. It derives at most two folder levels from validated manifest metadata;
there is no heap-backed filesystem or dynamic loader. The entire skin is drawn
with RGB565 primitives; it adds no launcher bitmap asset. Create a native
starter without editing the launcher:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
```

See `docs/GAME_SDK.md` for the API and executable-format explanation. Native
games are RISC-V code statically linked into `p4_console_os.elf`; ESP-IDF emits
the flashable `p4_console_os.bin`. This project does not use UF2.

## Boot and color modes

Boot presents the official Game Changers AI logo on a clean black screen with
an original three-note startup chord and a short progress animation. The logo
asset provenance and its resampled RGB565 form are recorded in
`main/assets/README.md`; it is an authorized brand asset, not a generic game
art dependency.

The **Colors** system page has four shell-only modes: Gold (the Game Changers
default), Arcade, Ocean, and Sunset. A choice takes effect immediately across
the launcher and built-in pages; native games retain their own visual design.
The selected mode is deliberately session-only until persistent settings are
reviewed alongside the storage policy. It does not apply a CRT filter.

## Doom and audio lifecycle

The home shell never energizes the amplifier. Its Audio panel owns a bounded
1–10 master setting, defaults to 8/10, and applies changes when the next game
starts. When Doom is selected, the shell
darkens the panel and releases touch, I2C1, and display ownership before
entering the existing Doom composite. Doom then reinitializes those services
and owns the exact 16 kHz PCM16-stereo SFX + procedural MUS path at the
selected master volume.

This first handoff is deliberately one-way. The imported Doom engine does not
yet have a reviewed reentrant shutdown path, so returning home requires a
restart. A future lifecycle milestone can add explicit `prepare`, `enter`,
`leave`, and `resume` hooks without changing the shell registry API.

Native Game API apps use a different, reentrant path. Console OS retains the
display and touch services, supplies normalized controls and drawing helpers,
and opens the reviewed factory speaker session only while a game requesting
tone or PCM-stream audio is active. The stream service copies bounded 16 kHz
PCM16-stereo blocks into the shared mixer; games still never own I2S, codec
I2C, or GPIO53.
Back exits directly to the launcher and restores the amplifier-safe state.
Native game updates, touch, and audio run every 16 ms; the OS submits every
second rendered frame for a steady 31.25 FPS target close to 30 FPS.

## SD content and multiplayer

At boot, Console OS performs a no-format microSD mount and a bounded read-only
P4 Cart catalog scan. Library reports valid/rejected carts and can retry the
scan without hashing unrelated large game-data files.
The cart Lua runtime is still pending, so valid carts are listed but not
launched. Copy reviewed carts to removable media with
`python3 scripts/p4-content.py cart`; the bounded workflow is in
`docs/CONTENT_LIBRARY.md`. Device-side USB import remains visibly unavailable
until a general-purpose, path-bounded protocol is reviewed.

The Multiplayer page currently reports the allocation-free v1 packet/session
core. The ESP32-C6 Wi-Fi transport and playable lobby remain pending, and games
never receive sockets. See `docs/MULTIPLAYER.md`.

## Local build

The exact ignored Doom shareware WAD documented in the repository is required
at `local-data/doom/doom1.wad`. Then run:

```sh
make game-sdk-host
make console-os-waveshare-idf
```

The app metadata is build-only. A successful build is not permission to flash
or a hardware acceptance result. Bind and authorize the exact new artifact
before preparing another guarded console install.
