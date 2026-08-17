# P4 Console OS

This is the FreeRTOS-native console shell for the ESP32-P4 console boards.
The OTA image contains the launcher, fixed system apps, platform services, and
the legacy Doom engine only. Every executable Game API cartridge—including
Byte Buddy, Calculator, Input Test, and AV Test—loads from microSD, so adding
or removing one never requires an OS reflash.

The home screen keeps the accepted Program Manager-style interface and
organizes built-ins plus the current storage catalog as:

- All Programs (every entry in one scrollable view)
- Games
  - Action: Doom (exclusive foreground handoff)
  - Arcade: installed reentrant Game API cartridges
- System: Colors, Touch, System status, Audio status, Achievements, File
  Manager, Game Manager, Multiplayer, Save Manager, and Terminal
  - Tools: Calculator
  - Tests: Input Test and AV Test

Boot first displays the official Game Changers AI logo and an original
A4-C#5-E5 startup chord; it does not copy the THX/Dolby recording. The Colors
app switches the shell among Gold, Arcade, Ocean, and Sunset palettes for the
current session. There is no CRT filter.

## Laptop game storage

On Elecrow, Console OS exposes the internal persistent `game_data` FAT volume
through the board's J16 ESP32-P4 USB port as **P4 Game Storage**; J1 remains
the CH340 programming/serial port. While a laptop owns the volume, the
firmware unmounts it, invalidates its file cache, changes the Doom tile to
`USB STORAGE ACTIVE`, and rejects a Doom launch. A clean host eject remounts
it and triggers fresh size/header/SHA-256 validation.

Waveshare 4.3 and Olimex use removable microSD instead. Console OS keeps that
card read-only at runtime. Power the console off and use a laptop card reader
for app, WAD, save, and update-file transfer; their USB programming connection
is not presented as game-storage MSC.

The current Doom integration accepts `DOOM1.WAD` at the drive root, with the
exact shareware identity recorded in `third_party/game-data.json`. Copying or
removing that file does not require a firmware rebuild. Eject the drive before
launching Doom. Doom launch stops the USB device, remounts the volume, and
rehashes the WAD before taking a terminal game lease; USB cannot remount below
the running engine.

The Elecrow full project image can generate a reviewed FAT seed, but its
one-time dual-OTA migration deliberately does not write `game_data`,
preserving live content. Later OS releases are placed at
`UPDATE/P4UPDATE.P4U` through the board's supported storage-transfer route and
installed from Game Manager into the inactive OTA slot. Program Manager
exposes ready/invalid update status on the Game Manager app before it is
opened.
The consumed update package is removed before reboot when storage ownership
is still available; otherwise it can be removed safely from Game Manager.
Runtime code never auto-formats a damaged volume.

Seed cartridges are generated from validated `games/*/game.json` manifests
into `GAMES/*.P4G`. At boot the OS scans that directory, validates each
package, and builds the launcher from the result; root `.P4G` files are read
only for compatibility with older cards.
No `.P4G` cartridge is linked into the OTA application. The complete runtime
launcher catalog comes from validated microSD files. Its lightweight desktop
view shows three columns by two rows, scrolls with vertical
arrows or a one-finger swipe, and supports up to 32 apps. It derives at most
two folder levels from validated package metadata. The skin is drawn with
RGB565 primitives and adds no launcher bitmap asset. Create a native starter
without editing the launcher:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
make game-sdk-host
make console-os-idf
```

Copy the resulting cartridge from `build/game-storage-seed/GAMES/` into
`GAMES` on `P4 GAMES` over J16 and eject. See
`docs/GAME_SDK.md` for the API and package contract. This project does not use
UF2.

Game Manager also restores the original open P4 Cart catalog. Source-included
`P4CART1` files live under `P4/GAMES/*.P4CART`; the firmware scans them on a
bounded background task, verifies their complete container and payload hashes,
and lists valid carts alongside P4G packages. This does not rename or execute
them: the original project did not finish the required sandboxed Lua 5.4
backend. The generated storage bundle includes the MIT-licensed Bounce Lab
reference cart so this compatibility path is reproducible.

The Waveshare 4.3 build obtains its complete executable cartridge catalog from
a read-only-at-runtime microSD card while retaining the P4CART compatibility
catalog. Build and verify it with `make console-os-waveshare-idf`.
With the board powered off, move the card to a laptop and run
`make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES`; the installer
validates every enabled cartridge and preserves unrelated files.

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
