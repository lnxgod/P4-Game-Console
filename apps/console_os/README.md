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
- Control Panel: Appearance, Touch, System status, Audio, Achievements, File
  Manager, Game Manager, Multiplayer, Controllers, Save Manager, and Terminal
  - Tools: Calculator
  - Tests: Input Test and AV Test

Boot first displays the official Game Changers AI logo, starts SD initialization,
plays a louder classic-PC POST beep with a deliberate pause and separated
hard-disk seek clicks, dials `614-276-3639` with deliberately paced
dual-frequency DTMF pairs, then plays a condensed V.25/V.22bis 2400-baud handshake at boot master
step 10/10. Display and the
branded frame still come first, followed by the minimum codec control bus,
background storage worker, exact P4G catalog worker, boot audio, and input. The branded
loading screen remains visible with a state-driven SD animation until the
microSD mount reaches a ready or degraded terminal state; serial evidence reports
the measured dial and SD initialization durations. Catalog validation overlaps
the sound sequence on a low-priority PSRAM task, and Program Manager's first frame
still includes the exact validated P4G catalog. A slow or missing card therefore shows
explicit loading instead of a black panel. The Windows 3.1 Program Manager is
the default launcher. Appearance can switch the current session to the optional
BBS door view or the Windows, Ocean, and Sunset palettes. There is no CRT
filter.

The shell and native Game API runtime use a fractional 60 Hz scheduler on the
100 Hz FreeRTOS clock. Native audio follows the matching 266/267-frame cadence
at 16 kHz. High-churn storage and diagnostic counters are sampled at 5 Hz so a
controller or multiplayer page cannot force continuous full-screen redraws.
Console OS 0.4.96 defaults to the readable Windows 3.1 launcher, displays the
compatible `SYSTEM/...` namespace as **Control Panel** on the root screen, and
preserves a session theme across live catalog refreshes. Console OS 0.4.95
advances launcher motion on that UI scheduler: touch tracks
fractional rows, release snaps with a bounded fling, and buttons/controllers
use an interruptible cubic ease. When selected, the Waveshare BBS slides only
its clipped ANSI door viewport into the existing 768x480 framebuffer, avoiding
a second frame allocation.

Console OS 0.4.98 coordinates the shared NimBLE GAP procedure before opening
Multiplayer. A disconnected saved controller's pending reconnect yields
automatically, BLE Host/Join starts only after that scan is idle, and leaving
Multiplayer restores the saved reconnect policy. An already-connected
encrypted controller continues to coexist with one multiplayer peer.

Console OS 0.4.101 keeps the expanded 0.4.100 catalog and places its two
catalog buffers plus the larger shell registry in external RAM on boards that
authorize external BSS. This preserves the internal 32 KiB DMA reserve needed
at startup.

Console OS 0.4.100 raises the validated native cartridge catalog from 16 to
32 entries and the combined shell registry from 32 to 64 entries. The current
19-game bundle can therefore remain fully visible alongside built-in system
apps instead of silently omitting games after the sixteenth package.

Console OS 0.4.99 preserves the default 320x200 cartridge surface and adds
negotiated 768x480 RGB565 rendering for detail-heavy native games. Waveshare
presents that high-resolution surface through its existing 1:1 accelerated
content path. Touch remains normalized to 320x200, so input and controller
contracts do not fork when a game selects higher visual detail.

Console OS 0.4.97 keeps that 768x480 launcher contract but rotates it at exact
1:1 scale with the ESP32-P4 PPA into the centered 480x768 panel viewport. Solid
UI fills reuse a completed row and the home page no longer clears the complete
frame twice. Runtime `STATS` expose accelerated submits and failures so panel
performance is visible over H1 instead of inferred from a host preview.

Console OS 0.4.94 keeps flash/OTA inspection on the main task's internal stack
while the 32 KiB PSRAM worker scans only removable-SD game data. This prevents
cache-disabled OTA reads from invalidating the worker's external stack during
startup. Console OS 0.4.92 simplified multiplayer setup without removing controls.
After Host or Join is chosen, the Host page has only three focus stops: game,
Match Settings, and the large create/start action. The existing mode, map,
skill, monster, respawn, time-limit, game, and wired-link controls live on the
separate Match Settings page. Opening Multiplayer lazily prefers BLE for the
game link; a failed or unavailable BLE start leaves wired UART selectable and
does not affect boot or the controller-first H2 USB role.

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
**Control Panel**, open **System**, and press **Turn On USB Mode** to expose the microSD card as
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
view shows three columns by two rows, scrolls smoothly with vertical arrows,
controller input, or a one-finger drag, and supports up to 32 apps. It derives at most
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
