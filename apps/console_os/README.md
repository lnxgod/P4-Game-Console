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
  Manager, Game Manager, Multiplayer, Controllers, Save Manager, and Terminal
  - Tools: Calculator
  - Tests: Input Test and AV Test

Boot first displays the official Game Changers AI logo, starts SD initialization,
plays a louder classic-PC POST beep with a deliberate pause and separated
hard-disk seek clicks, dials `614-276-3639` with deliberately paced
dual-frequency DTMF pairs, then plays a condensed V.25/V.22bis 2400-baud handshake at boot master
step 10/10. Display and the
branded frame still come first, followed by the minimum codec control bus,
background storage worker, boot audio, and input. The branded
loading screen remains visible with a state-driven SD animation until the
microSD mount reaches a ready or degraded terminal state; serial evidence reports
the measured dial and SD initialization durations. Program Manager's first frame
then includes the discovered P4G catalog. A slow or missing card therefore shows
explicit loading instead of a black panel. The Colors app switches the shell
among Gold, Arcade, Ocean, and Sunset palettes for the current session. There is
no CRT filter.

## Laptop game storage

On Elecrow, Console OS exposes the internal persistent `game_data` FAT volume
through the board's J16 ESP32-P4 USB port as **P4 Game Storage**; J1 remains
the CH340 programming/serial port. While a laptop owns the volume, the
firmware unmounts it, invalidates its file cache, changes the Doom tile to
`USB STORAGE ACTIVE`, and rejects a Doom launch. A clean host eject remounts
it and triggers fresh size/header/SHA-256 validation.

Waveshare 4.3 and Olimex use removable microSD instead. Olimex still requires a
powered-off laptop card reader. On Waveshare, H1 remains the CH343 programming
and serial port; connect the native H2 USB-C device port to a Mac, open
**System**, and press **Turn On USB Mode** to expose the microSD card as
**P4 Game Storage**. Console OS unmounts the card before host ownership and
verifies each host write synchronously. Eject the volume in Finder (or
disconnect H2), then press **Turn Off USB Mode** to remount and rescan it. The
off button stays locked while an attached Mac has not cleanly ejected, and
firmware never formats the card.
Full-card byte addresses remain 64-bit even though the ESP32-P4 ABI is 32-bit,
so laptop writes above the first 4 GB are accepted and verified correctly.
If a fresh or damaged card cannot mount, Console OS keeps it host-owned and
exports it through H2 for explicit laptop recovery instead of trapping it in a
failed app mount. Firmware still never initiates a format.

The controller-first Waveshare image keeps Espressif's locked external-hub
scheduler. Its bounded low-speed-child recovery waits for both device recycle
and the hub's complete EP0/status chain before retrying a downstream reset.
Console OS arms a persistent one-boot marker first; if a candidate fails before
stable runtime, the next boot suppresses recovery and starts in safe Host/HID
mode rather than repeating a boot loop.

The main-screen Waveshare **Controllers** app adds console-owned BLE HID/HOGP
pairing, a controller-only BLE mode toggle, disconnect/forget controls, and a
persistent six-button mapping wizard. Wired USB has active-input priority,
while Doom and every cartridge tier consume the same mapped,
transport-neutral snapshot. Pair the controller before opening a lobby; one
persistent encrypted pad can then coexist with one BLE Doom multiplayer peer.
Modern Bluetooth-capable Xbox Wireless Controllers are the priority
acceptance target; Xbox Wireless Adapter, Xbox 360 wireless, and wired
XInput/GIP are outside this generic BLE HID path. See `docs/CONTROLLERS.md`
before making a model-specific support claim.

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

File Manager can browse the complete visible directory tree without exposing
absolute filesystem paths: select a folder and choose **Open**, then use
**Back** to move to its parent. Folders sort before files and the current SD
path is shown above the list. On Waveshare the app-owned card remains
read-only, so add, rename, move, or remove files through the USB Drive app.

Seed cartridges are generated from validated `games/*/game.json` manifests
into `GAMES/*.P4G`. During the loading screen, the OS mounts the card on a
background task, scans that directory, validates each package, and builds the
first launcher catalog; root `.P4G` files are read only for compatibility with
older cards.
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
microSD while retaining the P4CART compatibility catalog. Build and verify it
with `make console-os-waveshare-idf`. Connect H2, wait for its volume to mount,
and run
`make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES`; the installer
validates every enabled cartridge and preserves unrelated files. It requires
an external USB FAT32 volume named `P4GAMES` and rejects ExFAT. Recover a fresh
or corrupt card with `diskutil eraseDisk MS-DOS P4GAMES MBRFormat /dev/diskN`
only after resolving the exact external disk. Waveshare SD initialization tries
10 MHz, 5 MHz, 1 MHz, then 400 kHz so normal cards mount promptly while the
slow-card recovery path remains available.

The 0.4.6 catalog correction gives the optional legacy P4CART scanner a 24 KiB
PSRAM stack and logs its measured low-water mark. Native package generation now
rejects unsupported ELF imports before a `.P4G` can reach the card; Calculator
uses only the frozen Game API/runtime import set, so all 12 generated packages
pass the same structural ELF policy used by the on-device catalog.

## Doom and audio lifecycle

The home shell never energizes the amplifier. When Doom is selected, the shell
darkens the panel and releases touch, I2C1, and display ownership before
entering the existing Doom composite. Doom then reinitializes those services
and owns the exact 16 kHz PCM16-stereo SFX + procedural MUS path at volume
step 9/10.

This handoff remains deliberately non-reentrant. Confirming Doom's quit prompt
with keyboard Y/Enter or touch Y safely tears down its owned services and
restarts into Console OS; N/Escape or touch N cancels. A future lifecycle
milestone can add in-place `prepare`, `enter`, `leave`, and `resume` hooks
without changing the shell registry API.

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
