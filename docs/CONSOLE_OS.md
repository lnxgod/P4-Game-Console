# P4 Console OS architecture and test notes

## Status

The current Waveshare successor changes the home surface, not the Game API.
Console OS renders an 80x30 CP437/ANSI BBS natively in the centered 768x480
viewport, presents games and folders as numbered doors, and keeps the existing
Windows 3.1-style Program Manager as the selectable Appearance fallback. Touch
hit testing, keyboard, and controller navigation share the same two-column,
three-row door geometry. Native games remain 320x200 RGB565 and are scaled by
the platform display path. The Multiplayer page now selects among Doom, Chex
Quest, and installed native cartridges that declare `multiplayer-session`,
filters rooms
by exact game identity and content hash, and launches the selected game on both
consoles after the host-owned start barrier. Doom keeps its dedicated lockstep
handoff; native cartridges receive only the bounded, transport-neutral Game
API session callbacks.

Console OS 0.4.71 adds a transport-neutral controller broker and a Waveshare
Controllers panel. The OS can pair, reconnect, disconnect, or forget one
encrypted BLE HID/HOGP pad while retaining wired USB priority. BLE controller
and multiplayer clients share one NimBLE/ESP-Hosted owner and a two-link
budget; games and Doom continue to read only canonical snapshots. Modern
Bluetooth-capable Xbox Wireless Controllers are the priority named target,
but model-specific hardware acceptance remains pending. See
`docs/CONTROLLERS.md`.

Console OS 0.4.44 synchronizes multiplayer Doom at two boundaries. A
session-tokenized launcher handshake lets either player start both consoles
and holds them briefly before the one-way hardware handoff. A separate Doom
engine barrier then advertises readiness only after each engine enters network
configuration and requires the peer's READY plus an acknowledgment of its own
before tic delivery is enabled. Both barriers are bounded; host tests cover
their pure state machines, while two-console gameplay remains a separate
hardware acceptance.

The current source successor keeps game cadence in the OS at a target 30 Hz,
but passes cartridges measured wall-clock frame time instead of a fabricated
constant. Deltas are clamped to the Game API bound, the scheduler waits only
for the remainder of the current frame, and late frames are logged rather than
followed by rapid catch-up updates. The allocation-free `p4/visual.h` helpers
then let cartridges add fixed-point motion, atlas animation, shake, and
particles without taking over display or timing.

Console OS 0.4.36 is the BBS readability successor. Printable ASCII carrying
the ANSI bold attribute gains one right-hand pixel inside the existing 9x16
cell. This slightly enlarges headings, door names, and controls while retaining
the exact 80x30 grid, CP437 line art, 768x480 framebuffer, and pointer hit
geometry. It is host-previewed and requires a fresh exact-artifact authorization
plus on-panel readability/touch acceptance before any hardware claim.

The first visible Waveshare frame is now an ANSI adaptation of the official
Game Changers AI lightbulb/circuit mark. Boot advances through visible POST,
disk seek, `ATDT 6142763639`, V.22bis training, SD door-directory sync, and
`CONNECT 2400` phases before drawing the home board. This replaces generic
loading text without hiding real storage progress. The bounded ANSI and BBS
APIs, host previews, transport policy, and open-content rules are documented
in `docs/BBS.md`. The USB-host firmware candidate is build-verified but has
not yet received a new on-device acceptance record.

The original FreeRTOS-native shell was installed on the exact bound 10.1-inch tablet on
2026-08-14. Its 4,928,880-byte app image was verified by complete padded-span
readback, launched once on the retained UART, and passed a 30-second launcher
capture. A further 160-second receive-only observation reached 15,600 touch
polls with zero touch, display, or timeout failures. The shell kept the
amplifier unenergized. The user subsequently confirmed that the launcher was
visible and that selecting Doom launched it. That confirmation does not by
itself claim a separately observed Console-OS-to-Doom acoustic pass.

The accepted Program Manager successor uses a bounded folder registry with
All Programs, Games/Action, Games/Arcade, and System views. Its nested
navigation, scrolling, Maze Chase, Space Invaders, Doom handoff, sound, and
return behavior were manually accepted on this exact tablet before the USB
storage work. The integrated build below preserves that renderer and input
model while adding laptop-accessible persistent game data.

The integrated Program Manager and USB game-storage build was installed on the
same bound tablet on 2026-08-14. The complete 7 MiB app span passed readback,
the existing game-data partition was unchanged by the app-only transaction,
and retained-UART startup reported storage ready, seven apps, a completed
1024x600 display submit, 600 successful touch polls, and an unenergized
amplifier. J16 then enumerated as a writable 9,289,728-byte FAT16 volume with
512-byte sectors. Laptop copy/remove, clean FAT verification, Doom identity,
and clean eject passed. The exact record is
`hardware/test-runs/2026-08-14-console-os-program-manager-usb-install.json`;
its operator-visible UI and Doom-launch fields remain pending until explicitly
confirmed on the panel.

That accepted build used statically registered apps. The current successor
keeps the same shell and Doom handoff while moving reentrant games into
validated storage cartridges.

The next app-only successor adds File Manager under System. It uses USB only
as the transport: the laptop adds files while J16 owns FAT, then a clean eject
returns ownership to Console OS. The on-device app lists at most 32 sorted root
entries, hides host metadata, pages five rows at a time, refreshes explicitly,
and removes one regular file only after a second confirmation. Directories,
paths, traversal tokens, control characters, overlong names, mount
transitions, USB ownership, and the terminal Doom lease all fail closed. This
successor is host-, build-, and retained-UART-startup-tested on the exact unit;
it is not fully hardware-tested until its UI and file operations are observed
on the panel and through a clean J16 cycle.

The no-new-backup, app-only route is frozen in
`scripts/console-os-file-manager-install.py`. It accepts only the already
installed Program Manager plus USB application span and the exact final File
Manager artifact, while preserving the partition table and live `game_data`
bytes.
The 2026-08-14 transaction passed complete app-span readback and startup with
eight registered apps while preserving the live volume byte-for-byte. See
`hardware/test-runs/2026-08-14-console-os-file-manager-install.json`.

The current successor adds Game Manager, microSD-loaded `.P4G` cartridges,
and dual-slot `.P4U` launcher updates while retaining the same window-manager
renderer. Maze Chase and Space Invaders are no longer linked into the OS
binary: they are seed packages copied into `GAMES` on `P4 GAMES`. The app image
is consequently under 1 MiB instead of carrying a duplicate 4.2 MiB Doom WAD.
This successor is host-tested and build-verified; its one-time partition
migration and cartridge launches still require the hardware record below.

The reconciled 0.4.0 candidate also adds the official Game Changers AI boot
logo, an original A4-C#5-E5 startup chord, four session-only shell color
modes, a bounded session achievement catalog, and the original Byte Buddy
virtual-pet cartridge. It deliberately adds no CRT filter and retains the
bounded local multiplayer session core from the overhaul.
The long-lived shell and catalog staging objects use static storage so the
24 KiB ESP-IDF main-task stack stays bounded across slow microSD scans.

The 0.4.1 boot correction makes the official logo the first visible frame,
plays the startup chord at the 8/10 master level, and reaches the interactive
launcher before starting removable-storage initialization on a dedicated
task. A slow, missing, or invalid card therefore changes only the asynchronous
storage status and catalog; it cannot hold the core OS on a black panel.

The 0.4.2 loading and loudness correction keeps that official frame visible
while the dedicated storage task initializes microSD, cycles a `READING SD
CARD` indicator from the real task state, and presents Program Manager only
after storage reports ready or degraded. Boot, native-game, and Console OS Doom
handoff audio all use the selected 10/10 master step.

The 0.4.3 boot and removable-media correction starts that storage task as soon
as the official frame is visible, then dials `614-276-3639` with the standard DTMF
frequency pairs at 10/10 while initialization runs. Hardware logs expose the
actual dial and SD initialization durations. A fresh or corrupt filesystem is
left host-owned for explicit recovery through Waveshare H2; firmware never
formats it. Executable games remain exclusively directory-driven from
microSD `GAMES/*.P4G` (plus root compatibility), never linked into the OTA app.

The 0.4.5 recovery correction follows the dial with a standards-shaped
V.25/V.22bis 2400-baud call, answer, carrier, guard, and training sequence
while the branded loading frame remains visible. USB MSC
byte-range validation is 64-bit, so writes anywhere on the 32 GB microSD are
valid on the ESP32-P4's 32-bit ABI; only the small internal-flash backend is
allowed to narrow an already bounded address. SD writes remain split into
DMA-safe 4 KiB transactions and synchronously read back before host success.
The Waveshare SD clock tries 10 MHz, 5 MHz, 1 MHz, then 400 kHz, retaining safe
fallback while avoiding multi-minute FAT scans on normal cards.

The 0.4.6 catalog correction raises the optional legacy P4CART worker's PSRAM
stack from 12 KiB to 24 KiB and records its low-water mark. The package builder
also rejects ELF imports outside the frozen cartridge runtime set, preventing a
game that the device catalog would reject from being copied into `GAMES`.

The 0.4.7 USB ownership correction makes Waveshare H2 storage opt-in. The
dedicated **USB Drive** app performs the app-to-host handoff; after Finder
ejects the volume (or H2 disconnects), **Turn Off USB Mode** remounts the card
and triggers a fresh game scan. Automatic cable-driven ownership changes are
disabled, and USB-off remains locked while an attached host has not issued a
clean eject, so macOS and Console OS can never mount FAT concurrently.

The 0.4.8 File Manager correction replaces the flat root-only view with safe
relative-directory navigation. Directories sort before files, the current SD
path remains visible, OPEN enters the selected directory, and BACK walks to
the parent before returning to Program Manager. Row selection no longer
rescans the card. Dot segments, absolute paths, repeated separators, control
characters, and overlong paths remain rejected at the storage boundary. The
Waveshare app-owned mount stays read-only; its File Manager browses while the
USB Drive app remains the supported way to edit card contents.

The 0.4.14 controller-first correction combines the supported Host/HID and
TinyUSB MSC roles in one image without concurrent ownership. Console OS boots
with H2 dedicated to a powered-hub controller. Opening USB Drive neutralizes
input and fully stops Host/HID before MSC starts; returning requires clean
macOS eject or disconnect, uninstalls TinyUSB, remounts and rescans microSD,
then restarts Host/HID. A failed transition leaves H2 quiesced.

The 0.4.15 hardware-recovery correction scopes the ESP-IDF internal/DMA
reserve to 32 KiB for the composite Waveshare Host/HID/hub/TinyUSB image.
The earlier 64 KiB setting exhausted DMA-capable internal regions before
`app_main` and caused a fail-fast reboot loop. The Waveshare release verifier
now rejects a controller-first image unless the bootable reserve is pinned.

The 0.4.16 controller-first isolation correction removes the unqualified
ESP-Hosted Wi-Fi/SDIO transport from the H2 acceptance image. Its constructor
ran before `app_main`, consumed the memory needed by removable storage, and
then asserted while sharing the SDMMC host. The ordinary Waveshare profile
retains the passive-scan candidate; the controller-first profile is limited to
display, touch, audio, microSD, USB Host/HID, and the explicit USB Drive role.

The 0.4.17 recovery correction makes USB Drive allocation genuinely lazy.
Controller mode mounts the microSD FAT filesystem directly for Console OS;
only the explicit USB Drive transition unmounts that VFS, stops Host/HID,
creates the DMA-backed MSC object, and installs TinyUSB device mode. Returning
deletes MSC before remounting and rescanning. The Waveshare display path no
longer toggles the DSI pattern generator on every frame, preserves the last
visible frame on a refresh timeout, and gives boot frames a bounded retry
instead of converting one missed VSYNC into a permanent black halt.

Hardware showed that 0.4.18's reset-attempt setting did not retry a failed USB
descriptor enumeration: it applies only when the downstream port reset itself
fails. Console OS 0.4.19 then tried at most two complete OS-owned Host/HID
recycles. Exact-unit testing proved that restarting the P4 host stack cannot
reset an externally powered hub, so every fresh host graph encountered the
same disabled downstream port. The attempt exhausted safely without a boot
loop, but it did not enumerate the controller.

Console OS 0.4.20 moves the bounded recovery to the failed downstream-port
boundary. The controller-first build verifies the exact locked
`espressif/usb` 1.5.0 `ext_port.c`, generates a build-directory overlay, and
compiles that one translation unit in place of the untouched managed source.
After enumeration disables a still-connected port, the overlay waits for the
failed USBH device object to be freed and then resets only that port. It permits
two retries, resets the budget on physical reconnect, and leaves normal
hot-plug ready after exhaustion. The release verifier reproduces the overlay,
proves only the generated source was compiled, and retains the 500 ms
reset-recovery interval. Host/HID still never overlaps TinyUSB MSC.

Exact-unit testing rejected that 0.4.20 experiment. Its first retry overlapped
an external-hub control transfer, the hub request returned
`ESP_ERR_INVALID_STATE`, and `usbh_dev_close` asserted with one control
transfer still in flight. Console OS 0.4.21 is the recovery boundary: it
compiles the original locked external-port source, disables automatic
enumeration retries, and preserves ordinary controller hot-plug plus the
mutually exclusive USB Drive app. No further retry may run from the port
recycle callback.

Console OS 0.4.22 keeps the byte-exact 1.5.0 hub scheduler and replaces only
the exact locked `ext_port.c` in the controller-first build. Its retry remains
pending after device-free until `status_lock`, `status_outdated`, and
`waiting_recycle` are all clear, so the hub's existing feature-completion and
GetStatus chain has released the one shared EP0 URB. Two host models cover
both callback orderings. Before Host/HID starts, Console OS commits an NVS
one-boot marker; if the previous boot did not survive 600 main-loop frames,
the next boot suppresses retries and follows the stable 0.4.21 behavior. This
exact-unit run passed scheduler serialization and boot-loop containment, but
the attached low-speed child still failed `CHECK_SHORT_DEV_DESC`.

Console OS 0.4.23 therefore generates an additional exact-input overlay of the
locked USB 1.5.0 HCD source. It follows Espressif's P4 forced-full-speed
correction: restore FS/LS-only mode and the 30 MHz UTMI clock before every root
reset, then set the post-enable UTMI clock and one-millisecond frame interval
from the observed device speed. The same NVS decision gates both this
low-level reapplication and the downstream retry, so an incomplete first boot
falls back to the prior stable path. The verifier rejects modified managed
sources, a substituted hub scheduler, a non-reproducible overlay, or a build
containing the revoked split-transaction experiment. Retained UART and named
hot-plug tests remain required for controller acceptance.

Console OS 0.4.24 keeps that guarded USB candidate and fixes pointer-source
arbitration in the launcher. An absent USB mouse no longer injects an invalid
pointer frame before each GT911 poll, so controller-first mode cannot
continually disarm physical touch. The native BBS marks each door as tappable
and provides bounded touch Previous/Next controls for directories longer than
six entries. This is host-tested only until a fresh exact-artifact release is
installed and direct door taps are observed on the Waveshare panel.

Console OS 0.4.25 fixes a controller-triggered black-screen halt. The Waveshare
DPI submit path sometimes copies a valid frame but misses the later refresh
acknowledgement window and returns `ESP_ERR_TIMEOUT` with the backlight still
safe. Interactive launcher redraws now classify only that timeout as a missed
acknowledgement, log `FRAME_ACK_MISSED`, and continue; hard display errors keep
the existing fail-closed behavior. The Waveshare verifier pins this policy so
a future input change cannot silently restore the fatal timeout path.

Console OS 0.4.26 corrects the exact `0079:0011` SNES controller profile so
physical A is the canonical accept/South button, physical B is back/East, and
Y/X retain their labeled West/North positions. The Waveshare Doom handoff now
reads the already-running OS-owned gamepad snapshot, preserving USB host
ownership and hot-plug neutralization while adding D-pad and button controls.
A one-time volume-policy migration restores the boot sequence to 10/10 and
games (including Doom) to 9/10; later Control Panel changes remain persistent.

Console OS 0.4.27 extends the Waveshare missed-refresh-acknowledgement policy
to storage cartridges. A copied game frame that returns `ESP_ERR_TIMEOUT`
with the backlight preserved is counted and allowed to continue, while every
hard display error still fails closed. This prevents the first Byte Buddy
frame, or a later animation frame, from being misclassified as a cartridge
render failure after the launcher has already accepted the same panel timing
condition.

The 0.4.9 startup and Doom lifecycle correction rejects an unexpected embedded
logo byte count and changes boot audio to a quiet 4/10 POST beep, short hard-disk
seek, fast phone dial, and condensed V.22bis handshake. Native games and Doom
open at 9/10. Doom's Y/N prompt also accepts touch/keyboard
Enter/Escape; confirmed quit runs the existing safe composite teardown and
restarts into Console OS rather than falling back into Doom's infinite loop.

The 0.4.10 brand correction removes the mistakenly sourced `.com` sales-site
wordmark. The boot surface now centers the square arcade-console mark published
by `gamechangersai.org`, with its source URL, source SHA-256, converted RGB565
SHA-256, dimensions, and build-time byte count pinned fail-closed.

The 0.4.11 boot-audio correction raises the boot codec and tones to 9/10,
lengthens the POST beep and its following pause, separates the hard-drive seek
clicks so they read as individual mechanisms, and dials `614-276-3639` using
the standard ten-digit DTMF frequency pairs before the modem handshake.

The 0.4.12 loudness correction raises only the boot codec and tone levels to
10/10. Native games and Doom remain at the established runtime level of 9/10.

The 0.4.13 parity build slows the ten-digit DTMF sequence to 160 ms tones,
90 ms ordinary digit gaps, and 220 ms pauses after the `614` and `276`
groups. This makes the number readable at the full 10/10 boot level without
lengthening the later V.22bis handshake. The timing lives in the shared
Console OS layer and is identical on Waveshare 4.3 and the Elecrow 10 in
variant.

The current compatibility successor also restores the original P4 Cart
Library boundary without replacing the native `.P4G` loader. A bounded PSRAM
background task scans `P4/GAMES/*.P4CART`, validates complete `P4CART1`
container geometry plus whole-file and per-payload hashes, and adds valid carts
to the launcher. At launch, Console OS revalidates and hashes the selected
source, then runs it in the source-locked Lua 5.4.8 sandbox with bounded heap,
instructions, host calls, drawing, tone audio, and OS-owned exit handling.
Source carts never enter the native ELF loader. Saves currently persist across
relaunches within one boot; durable SD commits remain pending.

## Board-specific runtime

The Elecrow build remains the default and preserves the accepted
window-manager renderer, touch/audio paths, Doom handoff, and J16 laptop MSC
workflow. The additive Olimex Rev.B build uses that same shell and native-game
loader with these substitutions:

| Service | Elecrow 10 in | Olimex Rev.B | Waveshare 4.3 |
|---|---|---|---|
| Display | 1024x600 DSI, 3x viewport | 1280x720 HDMI, 3x viewport | 480x800 ST7701 rotated to 800x480, 768x480 viewport |
| Persistent content | internal FAT over J16 | removable microSD | removable microSD; app read-only or exclusive H2 MSC host |
| Input | GT911 touch | USB-A pad + keyboard + mouse | GT911 touch + externally powered H2 USB HID + one bonded BLE HID pad |
| Audio | reviewed factory speaker path | ES8311 to 3.5mm jack | ES8311 speaker path behind runtime gate |
| Doom | exclusive touch handoff | exclusive pad/keyboard/mouse handoff | exclusive touch handoff |
| Programming / transfer | J1 UART / J16 MSC | native USB-C Serial/JTAG / card reader | H1 CH343 UART + verified file transfer / runtime-switch H2 MSC |

For Olimex, build and verify once with `make console-os-olimex-idf`. The build
creates `apps/console_os/build-olimex-esp32-p4-pc/sd-card/` with all enabled
`.P4G` cartridges under `GAMES/`, the locally supplied exact shareware `DOOM1.WAD`,
and `UPDATE/P4UPDATE.P4U`. Power the board off and move the card to a laptop,
then copy the known bundle with:

```sh
make install-olimex-sd-card SD_MOUNT=/Volumes/P4GAMES
```

The installer refuses broad or unmounted destinations, validates source and
destination hashes, and preserves unrelated card files. Console OS never
formats this card and does not support live removal. The USB-C connector is
not a mass-storage endpoint.

For Waveshare, run `make console-os-waveshare-idf`, connect the native H2
USB-C device port to the laptop, open System, and press **Turn On USB Mode**.
After the card volume mounts, run
`make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES`. Eject it in Finder,
then press **Turn Off USB Mode** to remount and rescan. The same guarded
installer copies all enabled packages and declared resource sidecars (including
`GAMES/BYTEBUD.P4G` and `GAMES/BYTEBUD.P4R`), verifies every hash, and leaves
unrelated card data untouched. It requires an external
USB FAT32 volume named `P4GAMES`; ExFAT is rejected because the pinned firmware
mount does not support it. Recover a card with `diskutil eraseDisk MS-DOS
P4GAMES MBRFormat /dev/diskN` only after resolving the exact external disk. H1
remains programming UART and also carries bounded, SHA-256-verified transfers
at a negotiated 921600 baud. `scripts/p4-transfer.py push GAME.P4G --port ...`
defaults to the native P4G class, validates both ends, stages and reads back the
write, and invalidates the native catalog so the game appears without reboot.
H2 and the app never own the filesystem concurrently, and runtime formatting
remains forbidden.

Boot does not hash Doom or Chex on its critical path. After SD mount and native
catalog readiness, the launcher becomes interactive while those doors remain
unverified. Selecting one starts the exact full-file SHA-256 and caches success
only for that uninterrupted mounted-storage generation. Chex additionally
requires its exact `.DEH` companion; its pinned WAD has a valid `PWAD` header,
which is accepted only by the Chex path. Remount, USB Drive ownership, or reboot
invalidates the in-memory result. A persisted size/timestamp/sample receipt is
never sufficient for executable game-data readiness.

Olimex launcher controls are: D-pad or arrow/WASD to navigate, gamepad A or
Z/Space/Enter to accept, gamepad B or X/Escape/Backspace to go back, and R/F5
to refresh. A boot mouse drives the shell pointer; left click activates, right
click goes back, middle click refreshes, and the wheel moves selection. Native
games receive the normalized gamepad/keyboard buttons, with mouse left/right
mapped to A/B. A disconnect publishes a neutral input snapshot before the
next foreground update.

## Foreground model

```text
boot
  -> platform_game_storage mounts board storage; Elecrow may hand FAT to J16
  -> platform_display owns DSI/panel/backlight
  -> platform_i2c_shared owns I2C1
  -> platform_touch borrows I2C1 for GT911
  -> console_shell renders the home screen and built-in pages
       |-- All Programs (scrollable flat view)
       |-- Games
       |     |-- Action -> Doom
       |     `-- installed microSD P4G games grouped by manifest folder
       |-- System -> diagnostics / Colors / Audio / Achievements
       |     |-- Tools -> Calculator
       |     |-- Tests -> Input Test / AV Test
       |     |-- File Manager -> bounded root list / confirmed delete
       |     |-- Game Manager -> P4G status/remove, P4CART launch, OS update
       |     |-- Save Manager -> bounded OS-owned slot catalog
       |     |-- Multiplayer -> local session core / transport status
       |     |-- Controllers -> USB/BLE status + pair/disconnect/forget
       |     `-- Terminal -> local commands / touch or physical keyboard
       |-- storage cartridge selected
       |     -> reload, re-hash, validate, and relocate into PSRAM
       |     -> console retains display/touch ownership
       |     -> game receives normalized controls + RGB565 surface
       |     -> optional bounded audio session starts for the game
       |     `-> Back stops the game/audio and returns home
       `-- Doom or Chex selected
             -> verify the exact WAD (and Chex DEH) on demand for this mount
             -> retain exclusive game-storage lease until restart
             -> backlight dark
             -> destroy touch borrower
             -> destroy shared I2C1 owner
             -> deinitialize display
             -> enter Doom composite
                  -> Doom recreates display/touch
                  -> Doom alone initializes SFX + MUS audio
```

This first Doom transition is exclusive and one-way. Restarting returns to the
launcher. Returning directly from Doom will require a reviewed engine teardown
that releases its VFS blob, audio worker/backend, touch client, I2C bus, video,
display, overlays, and exit callbacks in a deterministic order.

## Shared service contract

| Resource | Launcher owner | Doom owner | Transition rule |
|---|---|---|---|
| MIPI-DSI display | `platform_display` | `platform_display` | Launcher deinitializes it before Doom starts |
| RGB565 surface | 320x200 shell buffer | Doom video adapter | Never shared concurrently |
| I2C1 GPIO45/46 | `platform_i2c_shared` | `platform_i2c_shared` | Touch borrower is destroyed before bus owner |
| GT911 touch | `platform_touch` | `platform_touch` + Doom input | Invalid/malformed frames neutralize input |
| Speaker audio | none on home; reviewed session for native games | Doom audio adapter/factory backend | Only one foreground owner; close must re-prove amplifier shutdown |
| Game-data FAT | launcher or laptop, never both | terminal game lease | Clean eject returns ownership; host access is revoked and WAD verification is invalidated |
| Doom/Chex data | validated logical-root `/DOOM1.WAD` or `/CHEX.WAD` + `/CHEX.DEH` | read-only VFS adapter | Exact known identities only; full SHA runs on demand once per uninterrupted mount; Chex alone accepts its pinned PWAD header |
| File Manager | `console_shell` view plus `platform_game_storage` operations | unavailable | Lists/deletes only while the app owns FAT; host and game ownership reject every operation |
| Game cartridge | validated microSD package bytes, then relocated PSRAM image | unavailable | Catalog and launch revalidate SHA/ELF; cartridge receives only the host callback table; OTA contains no `.P4G` payload |
| Game resource | optional same-name `.P4R`, validated and held read-only in PSRAM during launch | unavailable | 8 MiB total bound, exact game-ID binding and payload SHA-256; game receives only the immutable payload view and no filesystem handle |
| Signal scanner | Waveshare-only `platform_signal_scan`; one foreground cartridge receives sanitized snapshots | dormant/unavailable | Passive background scans publish at most eight session-tokenized results; games receive no radio handle, BSSID, credentials, or socket |
| Controller input | `platform_gamepad` selects a complete USB or BLE HID snapshot | same canonical snapshot | Wired USB priority; encrypted identity-bound BLE fallback; disconnect immediately neutralizes input; games receive no host, GATT, bond, or address handle |
| OS update | inactive OTA slot | unavailable | USB stops during streaming; boot slot changes only after final image verification |

The launcher surface is standard RGB565 at 320x200. The proven display service
scales it 3x into a 960x600 viewport with 32-pixel black margins on the
1024x600 panel. Touch coordinates outside that viewport cannot activate UI.

## App registry

`components/console_shell/include/console/shell.h` is the launcher boundary.
Each app has a nonzero unique ID, bounded title/subtitle, one required folder
path, capability flags, accent color, enabled state, and either a built-in page
or external handoff. Fixed built-ins are combined with a bounded runtime
catalog of validated packages. Folder paths contain one or two uppercase
segments, each at most 15 bytes. The shell itself owns no heap, filesystem, or
recursion; storage and loading remain reusable platform services.

The shell accepts at most five contacts and shows a three-column, two-row
viewport. Bounded up/down controls and vertical one-finger swipes scroll whole
rows without wrapping; a recognized swipe suppresses tile launch. The
Program Manager chrome, program/folder icons, scrollbar, and status bar are
RGB565 primitives and require no bitmap asset.

To add another built-in app:

1. Add a page enum and bounded renderer/input behavior to `console_shell`.
2. Register one descriptor in `apps/console_os/main/console_os_main.c`.
3. Add host navigation, malformed-input, and framebuffer-bound tests.

To add a reentrant native game, run `scripts/new-game.py`, choose a bounded
manifest path such as `GAMES/ARCADE`, and implement it against the headers in
`components/p4_game_api/include/p4/`. The build emits one `.P4G` file plus an
optional same-name `.P4R` resource sidecar. Copy the declared files into
`GAMES` on `P4 GAMES` and eject J16; no OS installation is needed. See
`docs/GAME_SDK.md`.

The standard SD bundle also includes Calculator under `SYSTEM/TOOLS` plus
Input Test and AV Test under `SYSTEM/TESTS`. They use the same P4G boundary as
games and are removable without changing the launcher firmware.

## Sound behavior

The Audio page reports the compiled handoff contract only. It does not start
I2S, drive GPIO30 low, or generate a test tone. A native game may request the
Game API tone capability; Console OS then owns a bounded audio session for that
foreground game and closes it before returning home. Doom retains its working
exclusive path:

- 16,000 Hz signed PCM16 stereo
- Doom sound effects plus WAD MUS procedural synthesis
- up to 16 music voices
- Console OS handoff volume step 9/10
- factory I2S1 speaker TX route, with the pinned factory PDM-clock side effect
- no external MIDI device or soundfont

Do not make the home shell, a native game, and Doom own the factory backend at
the same time.

## Reproducible software checks

From the repository root:

```sh
make console-shell-host
make platform-game-storage-host
make gamepad-host
make game-sdk-host
make console-os-idf
make console-os-olimex-idf
make console-os-waveshare-idf
```

The host targets use AddressSanitizer and UndefinedBehaviorSanitizer. They test
launcher pagination and registry bounds, framebuffer guards/stride padding,
physical-to-logical touch mapping, press/release semantics, malformed frames,
storage handoff/cache generations and terminal game leases,
the bounded tone mixer and platform audio lifecycle, Maze Chase state and
randomized input, plus manifest/scaffolder rejection behavior.
The storage host suite also exercises root-name and relative-path validation,
folder-first bounded listings, nested navigation, hidden-entry exclusion,
directory classification, capacity truncation, traversal rejection,
regular-file deletion, and directory-delete refusal. The shell suite covers
File Manager paging, selection, open/up navigation, refresh,
confirmation/cancel, non-removable directories, unavailable storage, and
malformed snapshots.
Game/package tests additionally cover malformed headers, reserved fields,
ELF/string-table bounds, real seed cartridges, deterministic P4CART packing,
complete P4CART/payload hashes, Game Manager remove/install confirmation, and
the platform-neutral `.P4U` envelope parser.

The IDF target builds with the locked ESP-IDF 5.5.3 and managed component
versions, checks ESP32-P4 revision 1.x bounds, and runs
`scripts/verify-console-os.py`. The verifier confirms the WAD identity and Git
ignore status, build-only flash policy, handoff cleanup order, component graph,
partition/flash geometry, FAT seed identity, required USB-device/storage
symbols, and absence of USB-host/SD/codec entry points in the final ELF.

The one-time Game Manager migration cannot be app-only because the former
7 MiB app range becomes `ota_0` (`0x20000..0x38ffff`) plus `ota_1`
(`0x390000..0x70ffff`) and OTA data at `0x10000`. The guarded J1 transaction
writes only the bootloader, partition table, OTA data, and OTA-0 image; it does
not write `0x710000..0xffffff`, so the live `P4 GAMES` volume is preserved.
Subsequent OS updates use `UPDATE/P4UPDATE.P4U` over J16 and target only the inactive
slot. Bootloader rollback remains pending until storage, display, and the first
ready frame succeed.

## Guarded hardware acceptance

The first install used the exact-unit route rather than generic `idf.py flash`:

1. The predecessor recovery journal was closed and bound by digest.
2. The new installer captured the complete live 4,931,584-byte app-span
   preimage before writing.
3. It wrote only the factory app partition and verified the complete padded
   span byte-for-byte.
4. It launched once and retained the same exclusive UART for startup capture.
5. The launcher proved display completion, GT911 polling, and amplifier-off
   state with no rollback required.

The original MVP install record is
`hardware/test-runs/2026-08-14-console-os-mvp-install.json`; the integrated
Program Manager/USB update is recorded in
`hardware/test-runs/2026-08-14-console-os-program-manager-usb-install.json`.
The original launcher's visibility and launcher-to-Doom transition were later
confirmed by the operator. Do not convert the integrated build's pending
operator-visible UI, Doom, or audio fields into pass claims without a new
observation on the panel.

WADs, WAD-bearing firmware binaries, and local recovery images remain local
and must never be pushed to GitHub.
