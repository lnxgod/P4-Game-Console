# P4 Console OS

This is the first FreeRTOS-native console shell for the Elecrow ESP32-P4
10.1-inch tablet. It is a monolithic ESP-IDF firmware with a small static app
registry, not a desktop process loader. The launcher and built-in pages share
the reviewed platform display and touch services.

The home screen keeps the accepted Program Manager-style interface and
organizes the static registry as:

- All Programs (every entry in one scrollable view)
- Games
  - Action: Doom (exclusive foreground handoff)
  - Arcade: Maze Chase and Space Invaders (reentrant Game API games)
- System: Colors, Touch, System status, and Audio status

## Laptop game storage

Console OS exposes the persistent `game_data` FAT volume through the board's
J16 ESP32-P4 USB port. Its USB product is **P4 Game Storage** and its FAT volume
label is **P4 GAMES**. J1 remains the CH340 programming/serial port. While a
laptop owns the volume, the firmware unmounts it, invalidates its file cache,
changes the Doom tile to `USB STORAGE ACTIVE`,
and rejects a Doom launch. A clean host eject remounts it for the app and
triggers a fresh size/header/SHA-256 validation.

The current Doom integration accepts `DOOM1.WAD` at the drive root, with the
exact shareware identity recorded in `third_party/game-data.json`. Copying or
removing that file does not require a firmware rebuild. Eject the drive before
launching Doom. Doom launch stops the USB device, remounts the volume, and
rehashes the WAD before taking a terminal game lease; USB cannot remount below
the running engine.

The full project image includes a reviewed FAT seed. After that first guarded
partition-table install, use app-only firmware updates to preserve files.
Flashing a full project image again intentionally restores the seed volume and
overwrites laptop changes. Runtime code never auto-formats a damaged volume;
the System page instead reports the repair state.

The launcher is generated from validated `games/*/game.json` manifests. Its
lightweight desktop view shows three columns by two rows, scrolls with vertical
arrows or a one-finger swipe, and supports up to 32 apps. It derives at most
two folder levels from validated manifest metadata; there is no heap-backed
filesystem or dynamic executable loader. The skin is drawn with RGB565
primitives and adds no launcher bitmap asset. Create a native starter without
editing the launcher:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
```

See `docs/GAME_SDK.md` for the API and executable-format explanation. Native
games are RISC-V code statically linked into `p4_console_os.elf`; ESP-IDF emits
the flashable `p4_console_os.bin`. This project does not use UF2.

## Doom and audio lifecycle

The home shell never energizes the amplifier. When Doom is selected, the shell
darkens the panel and releases touch, I2C1, and display ownership before
entering the existing Doom composite. Doom then reinitializes those services
and owns the exact 16 kHz PCM16-stereo SFX + procedural MUS path at volume
step 6/10.

This first handoff is deliberately one-way. The imported Doom engine does not
yet have a reviewed reentrant shutdown path, so returning home requires a
restart. A future lifecycle milestone can add explicit `prepare`, `enter`,
`leave`, and `resume` hooks without changing the shell registry API.

Native Game API apps use a different, reentrant path. Console OS retains the
display and touch services, supplies normalized controls and drawing helpers,
and opens the reviewed factory speaker session only while a game requesting
tone audio is active. Back exits directly to the launcher and restores the
amplifier-safe state.

## Local build

The exact ignored shareware WAD documented in the repository is required at
`local-data/doom/doom1.wad`. Then run:

```sh
make console-shell-host
make platform-game-storage-host
make game-sdk-host
make console-os-idf
```

The app metadata is build-only. A successful build is not permission to flash
or a hardware acceptance result. This change needs a new guarded full-project
install because the partition table changed; preserve the complete factory
flash first and bind/authorize the exact artifacts before writing them.
