# P4 Console OS architecture and test notes

## Status

The installed 4.3-inch desktop-services/Quake renderer successor is 5,550,784
bytes with SHA-256
`b64b93a943711ca9330e8f065b2ab71f33c1f4fd6e90bb7403d7d1c3768566c9`.
It keeps the 768x480 landscape contract, File Manager, Save Manager,
Terminal/on-screen keyboard, and Solitaire. Quake remains only an optional
easter egg: its complete engine runs on a dedicated 96 KiB PSRAM task stack,
and WinQuake's additional roughly 128 KiB software-renderer scratch now lives
in PSRAM-backed BSS instead of an automatic frame. One display refresh timeout
drops a frame and recovers on the next successful submit; three consecutive
timeouts still stop Quake fail-closed.

The exact app-only guarded install and on-device digest verification passed on
2026-08-16 without another factory backup, SD write, bootloader write, or
partition-table write. A receive-only post-install capture held Console OS
stable for more than seven minutes, validated the exact PAK, and recorded
42,600 touch polls with zero runtime failures. Quake was not launched during
that watch, so it is not yet accepted or safe-claimed. See
`hardware/test-runs/2026-08-16-waveshare-console-os-quake-renderer-install.json`.

Its installed predecessor (`ce54c906...`) reached Quake's first frame before
faulting in `R_EdgeDrawing`: upstream allocated about 128 KiB of edge and
surface scratch on the 96 KiB engine task. The earlier `f0a7a613...` build had
instead faulted in `Con_CheckResize` on Console OS's 24 KiB main stack. These
are retained failure records, not acceptance evidence.

The active product targets are the SDL3 PC runner and the Waveshare
ESP32-P4-WIFI6-Touch-LCD-4.3. The current installed source candidate builds to
5,508,000 bytes with SHA-256
`0918b38a2e5c010ddc8f615e01e148dda32dfbd2354d06d3b66f9502f605a88c`.
It contains 15 static entries: Doom, the SD-gated Quake easter egg, seven
native games, and six built-in pages. The build verifier passes for the fixed
768x480 landscape contract, no-format SD catalog, Quake data exclusion,
loopback-only embedded Quake network path, and 31 FPS native render target.
On 2026-08-16 its guarded app-only install and complete on-device digest check
passed. Serial acceptance reached the 15-app launcher with the 64 KiB internal
DMA reserve intact, then powered and mounted the inserted microSD through the
Waveshare LDO4 path without formatting. The bounded catalog found zero carts
and no Quake PAK. Visual game and Quake acceptance remain pending.

The installed 2026-08-15 predecessor was 5,112,832 bytes with SHA-256
`d2bc620d213d4ed240251b97003c54cc5987418b07a1a26494b59841d971e426`.
Its serial evidence proved the 800x480 landscape profile, 768x480 viewport,
12-app registry, touch polling with zero failures, 8/10 default master volume,
and 31 FPS target. Those results do not transfer to the new SD/Quake candidate.

### Archived 10.1-inch test history

The original FreeRTOS-native shell was installed on the exact bound 10.1-inch tablet on
2026-08-14. Its 4,928,880-byte app image was verified by complete padded-span
readback, launched once on the retained UART, and passed a 30-second launcher
capture. A further 160-second receive-only observation reached 15,600 touch
polls with zero touch, display, or timeout failures. The shell kept the
amplifier unenergized. The user subsequently confirmed that the launcher was
visible and that selecting Doom launched it. That confirmation does not by
itself claim a separately observed Console-OS-to-Doom acoustic pass.

The P4 Game API v1, paginated manifest registry, and original Maze Chase game
described below were installed on the same exact tablet on 2026-08-14. The
4,941,776-byte app artifact passed a complete 4,947,968-byte padded-span
readback and retained-UART startup acceptance for the six-entry launcher.
Display and GT911 counters advanced without failures while the launcher kept
the amplifier off. The operator then confirmed the launcher worked and played
Maze Chase, accepting its launch, visuals, and controls. Manual acceptance of
its tones, Back-to-launcher behavior, and the retained Doom handoff remains
pending.

The badge-free folder successor is now installed on the bound tablet. It keeps
Space Invaders, scrolling, the primitive-drawn retro launcher, and active Game
API v1 PCM streaming, and presents a bounded two-level folder hierarchy. Its
sealed 4,951,552-byte BIN has SHA-256
`636ee38221197c0c7b02c5d04ea19ddfac0a3c6dbd9b2719d8bb21dfb1658a7d`;
the complete 4,964,352-byte installed/readback span has SHA-256
`ad863f0b1b8377692a22168cad5601b3f3f49fa5b7ab126b12d4df98c83a8f96`.
The owner visually accepted the folder home screen and removal of the
developer-facing `V/T/A/S` capability letters, reporting that it was “perfect”
and “running good,” then explicitly confirmed “i did test it all” and “all
games work.” That accepts nested navigation/scrolling, both native games and
their sound/return behavior, Doom sound and music, System pages, and
restart-to-launcher on this exact build. The exact install record is
`hardware/test-runs/2026-08-14-console-os-folder-clean-install.json`.

The prior flat seven-entry image and the earlier 4,951,936-byte folder build
with capability badges are historical predecessors. The latter was never
installed; its exact software record remains
`test-runs/2026-08-14-console-os-folder-hierarchy-build.json`.

The shell deliberately uses a monolithic firmware with statically registered
apps. ESP-IDF and FreeRTOS provide tasks, timers, memory, and drivers; this MVP
does not pretend to be a desktop OS with dynamically loaded executables.

The current desktop-services successor adds Windows 3.1-inspired File Manager,
Game Manager, Save Manager, Terminal/touch keyboard pages, and the original
Solitaire game while preserving the 768x480 landscape contract. File listing,
binary size formatting, sorting, save-slot metadata, terminal editing, and SSH
transport state live in the reusable `p4_desktop` component. The exact
Waveshare SD authorization remains read-only, so write/delete/USB-export and
real SSH transport controls are visibly gated. See `docs/DESKTOP_OS.md`.

## Foreground model

```text
boot
  -> platform_display owns DSI/panel/backlight
  -> platform_i2c_shared owns I2C1
  -> platform_touch borrows I2C1 for GT911
  -> console_shell renders the home screen and built-in pages
       |-- All Programs (scrollable flat view)
       |-- Games
       |     |-- Action -> Doom
       |     |-- Arcade -> native games discovered from enabled manifests
       |     `-- Platform -> Skyline Leap
       |-- System -> Colors / Touch / System / Audio / Library / Multiplayer
       |-- native API game selected
       |     -> console retains display/touch ownership
       |     -> game receives normalized controls + RGB565 surface
       |     -> optional bounded audio session starts for the game
       |     `-> Back stops the game/audio and returns home
       |-- Quake selected after exact SD PAK validation
       |     -> retain OS display/touch/I2C/storage ownership
       |     -> run the read-only adapter on the 768x480 canvas
       |     `-> restart Console OS after engine return
       `-- Doom selected
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

| Resource | Launcher owner | Legacy foreground owner | Transition rule |
|---|---|---|---|
| MIPI-DSI display | `platform_display` | `platform_display` | Launcher deinitializes it before Doom starts |
| RGB565 surface | 768x480 OS landscape viewport; current launcher/native compatibility buffer is 320x200 | Doom video adapter | Never shared concurrently |
| I2C1 GPIO7/8 | `platform_i2c_shared` | `platform_i2c_shared` | Touch/codec borrowers are destroyed before bus owner |
| GT911 touch | `platform_touch` | `platform_touch` + Doom input | Invalid/malformed frames neutralize input |
| Speaker audio | none on home; reviewed ES8311 session for native games | Doom ES8311 adapter | Only one foreground owner; close must re-prove GPIO53 amplifier shutdown |
| SD card | no-format mount + bounded read-only catalog + fixed-path atomic USB receiver | Quake reads one hash-gated PAK | Transfer and games are mutually exclusive; unmount before Doom handoff |
| Game data | Quake PAK remains separate on SD | Doom uses immutable embedded WAD | Neither is committed; Quake is never embedded |

The Console OS display contract is a 768x480 landscape viewport with 16-pixel
left/right margins on the 800x480 landscape panel, then a rotation into the
native 480x800 scanout. The current launcher/native-game renderer uses a
320x200 compatibility surface inside that OS viewport. New P4 Cart games
target 768x480 directly. Touch coordinates outside the viewport cannot
activate UI.

## App registry

`components/console_shell/include/console/shell.h` is the launcher boundary.
Each app has a nonzero unique ID, bounded title/subtitle, one required folder
path, capability flags, accent color, enabled state, and either a built-in
page or external handoff. Folder paths contain one or two uppercase segments,
each at most 15 bytes. The shell derives its logical views by scanning the
fixed registry of at most 32 apps; it owns no heap, filesystem, recursion, or
dynamic loader. Root exposes All Programs plus the unique top-level folders.

The shell accepts at most five contacts and shows a three-column, two-row
viewport. Bounded up/down controls and vertical one-finger swipes scroll whole
rows without wrapping; a recognized swipe suppresses tile launch. The
Program Manager-style chrome, program/folder icons, scrollbar, and status bar
are RGB565 primitives and require no bitmap asset.

To add another built-in app:

1. Add a page enum and bounded renderer/input behavior to `console_shell`.
2. Register one descriptor in `apps/console_os/main/console_os_main.c`.
3. Add host navigation, malformed-input, and framebuffer-bound tests.

To add a reentrant native game, run `scripts/new-game.py`, choose a bounded
manifest path such as `GAMES/ARCADE`, and implement it against the headers in
`components/p4_game_api`. Configure-time generation discovers and registers
both the game and its folder metadata. See `docs/GAME_SDK.md`. Doom remains a
special legacy handoff until its engine has a reviewed reentrant teardown.

Library scans only `/P4/GAMES` without recursion, caps work and results, and
fully validates container and payload hashes. It does not execute carts; the
Lua sandbox remains pending. The OS-owned H1 USB-UART receiver accepts no
arbitrary paths, stages fixed content names, and read-back validates content
before atomic activation. See `docs/CONTENT_LIBRARY.md`.

Multiplayer currently consists of an OS-owned bounded packet/session core and
status page. Wi-Fi transport, lobby payload codecs, and gameplay integration
remain pending. Games never own sockets. See `docs/MULTIPLAYER.md`.

## Sound behavior

The Audio page controls the OS-owned master volume from 1–10, defaults to
8/10, and applies the selected level when the next game starts. It does not
start I2S, energize the amplifier, or generate a test tone while the launcher
is active. A native game may request tone
audio, PCM-stream audio, or both. Console OS then owns one bounded foreground
session and closes it before returning home. The PCM service copies each
accepted 1–256-frame signed 16 kHz PCM16-stereo block into a fixed 512-frame
FIFO, saturating-mixes it with tone voices, rejects whole blocks on overflow,
and never exposes I2S, codec I2C, or GPIO53 to the game. Doom retains its working exclusive
path:

- 16,000 Hz signed PCM16 stereo
- Doom sound effects plus WAD MUS procedural synthesis
- up to 16 music voices
- OS master volume, default 8/10
- Waveshare ES8311 I2S1 speaker route with GPIO53 amplifier control
- no external MIDI device or soundfont

Do not make the home shell, a native game, and Doom own the factory backend at
the same time.

Native games receive 16 ms update/input/audio service ticks. Console OS
renders and submits every second tick, producing a stable 31.25 FPS target
without starving the 16 kHz audio stream. Existing native-static games remain
on their 320x200 compatibility surface; new P4 Cart games use 768x480. Games
never contain board-specific display, touch, codec, GPIO, I2S, or USB code.

## Reproducible software checks

From the repository root:

```sh
make console-shell-host
make game-sdk-host
make console-os-idf
```

The host targets use AddressSanitizer and UndefinedBehaviorSanitizer. They test
root/type folder derivation, Up navigation, folder-preserving app return,
launcher row scrolling/swipe suppression and registry bounds, framebuffer
guards/stride padding, physical-to-logical touch mapping, press/release
semantics, malformed frames, copied PCM lifetime/FIFO wrap/overflow/underrun,
tone-plus-stream clipping, the platform audio lifecycle, native game state and
randomized input, plus manifest/scaffolder folder rejection behavior.

The IDF target builds with the locked ESP-IDF 5.5.3 and managed component
versions, checks ESP32-P4 revision 1.x bounds, and runs
`scripts/verify-console-os.py`. The verifier confirms the WAD identity and Git
ignore status, build-only flash policy, handoff cleanup order, component graph,
partition/flash geometry, required ES8311/SD/catalog/Quake symbols, Quake data
exclusion, loopback-only embedded networking, and absence of USB entry points
in the final ELF.

## Guarded hardware acceptance

The current Waveshare install reused the already preserved complete factory
backup, checked the exact live device identity plus bootloader and partition
table, wrote only the app at `0x10000`, and passed esptool's on-device digest
verification. Its short serial boot check passed with no panic, display error,
touch failure, or amplifier activation on the launcher. See
`hardware/test-runs/2026-08-15-waveshare-console-os-volume-frame-install.json`.

The records below are retained historical 10.1-inch test evidence and are not
part of the active PC/Waveshare qualification path.

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
`hardware/test-runs/2026-08-14-console-os-mvp-install.json`; the Game API v1
successor record is
`hardware/test-runs/2026-08-14-console-os-game-api-v1-install.json`. The
launcher's earlier visibility and launcher-to-Doom transition were confirmed
by the operator. The successor's exact install and automated launcher runtime
passed, and the operator played Maze Chase successfully. Maze Chase sound,
exit/launcher return, and the successor's Doom regression remain pending. Do
not convert those checks into pass claims without the operator's observation.

The latest badge-free folder successor was installed through its exact-unit
guarded route, not through generic `idf.py flash`. The route preserved the
complete flat-launcher predecessor, wrote the factory app partition exactly
once, verified the complete 4,964,352-byte successor span in ten chunks,
revalidated the partition table, and launched once without reopening the UART.
The retained startup capture passed with seven registered apps, ordered
display/touch markers, three display submissions and completions, 600 touch
polls, zero failures, and the amplifier off. The owner then accepted the full
launcher, folder, game, audio, return, and restart checklist. See
`hardware/test-runs/2026-08-14-console-os-folder-clean-install.json`.

WADs, WAD-bearing firmware binaries, and local recovery images remain local
and must never be pushed to GitHub.
