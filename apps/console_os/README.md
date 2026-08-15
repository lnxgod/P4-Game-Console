# P4 Console OS

This is the FreeRTOS-native console shell for the Elecrow ESP32-P4 10.1-inch
tablet. Fixed system apps and Doom live in the OS image. Other Game API apps
are validated `.P4G` cartridges loaded from persistent storage, so adding or
removing a game does not require an OS reflash.

The home screen keeps the accepted Program Manager-style interface and
organizes built-ins plus the current storage catalog as:

- All Programs (every entry in one scrollable view)
- Games
  - Action: Doom (exclusive foreground handoff)
  - Arcade: installed reentrant Game API cartridges
- System: Colors, Touch, System status, Audio status, File Manager, and Game
  Manager

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

The full project image can generate a reviewed FAT seed, but the one-time
dual-OTA migration deliberately does not write `game_data`, preserving the
live Doom file and other user data. Later OS releases are copied to J16 as
`P4UPDATE.P4U` and installed from Game Manager into the inactive OTA slot.
The consumed update package is removed before reboot when storage ownership
is still available; otherwise it can be removed safely from Game Manager.
Runtime code never auto-formats a damaged volume.

Seed cartridges are generated from validated `games/*/game.json` manifests.
At runtime the launcher catalog comes from validated `.P4G` files. Its
lightweight desktop view shows three columns by two rows, scrolls with vertical
arrows or a one-finger swipe, and supports up to 32 apps. It derives at most
two folder levels from validated package metadata. The skin is drawn with
RGB565 primitives and adds no launcher bitmap asset. Create a native starter
without editing the launcher:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
make console-os-idf
```

Copy the resulting cartridge from
`build/game-storage-seed/` to `P4 GAMES` over J16 and eject. See
`docs/GAME_SDK.md` for the API and package contract. This project does not use
UF2.

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

The first migration needs a guarded J1 write for the bootloader, partition
table, OTA data, and OTA-0 image. It must not write `game_data`. After that,
normal game and OS updates use J16.
The exact-unit route is `scripts/console-os-game-manager-migrate.py`. It
requires clean committed artifacts, reuses the existing complete backup,
creates no new backup, and retains J1 through exact readback and startup
acceptance.
