# P4 Console OS architecture and test notes

## Status

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

The shell deliberately uses a monolithic firmware with statically registered
apps. ESP-IDF and FreeRTOS provide tasks, timers, memory, and drivers; this MVP
does not pretend to be a desktop OS with dynamically loaded executables.

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

## Foreground model

```text
boot
  -> platform_game_storage mounts persistent FAT and starts USB MSC on J16
  -> platform_display owns DSI/panel/backlight
  -> platform_i2c_shared owns I2C1
  -> platform_touch borrows I2C1 for GT911
  -> console_shell renders the home screen and built-in pages
       |-- All Programs (scrollable flat view)
       |-- Games
       |     |-- Action -> Doom
       |     `-- Arcade -> Maze Chase / Space Invaders
       |-- System -> Colors / Touch / System / Audio status / File Manager
       |     `-- File Manager -> bounded root list / refresh / confirmed delete
       |-- native API game selected
       |     -> console retains display/touch ownership
       |     -> game receives normalized controls + RGB565 surface
       |     -> optional bounded audio session starts for the game
       |     `-> Back stops the game/audio and returns home
       `-- Doom selected
             -> stop USB device, remount FAT, and re-hash DOOM1.WAD
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
| Game-data FAT | launcher or laptop, never both | terminal game lease | Clean eject returns ownership; host access is revoked and WAD re-hashed before Doom |
| Doom WAD | validated `/game-data/DOOM1.WAD` | read-only VFS adapter | Exact ignored shareware identity only; host changes invalidate cache |
| File Manager | `console_shell` view plus `platform_game_storage` operations | unavailable | Lists/deletes only while the app owns FAT; host and game ownership reject every operation |

The launcher surface is standard RGB565 at 320x200. The proven display service
scales it 3x into a 960x600 viewport with 32-pixel black margins on the
1024x600 panel. Touch coordinates outside that viewport cannot activate UI.

## App registry

`components/console_shell/include/console/shell.h` is the launcher boundary.
Each app has a nonzero unique ID, bounded title/subtitle, one required folder
path, capability flags, accent color, enabled state, and either a built-in page
or external handoff. Folder paths contain one or two uppercase segments, each
at most 15 bytes. The shell derives its views by scanning the fixed registry
of at most 32 apps; it owns no heap, filesystem, recursion, or dynamic loader.
Root exposes All Programs plus unique top-level folders.

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
manifest path such as `GAMES/ARCADE`, implement it against the headers in
`components/p4_game_api/include/p4/`, and leave its validated manifest enabled.
Configure-time generation discovers and registers both the game and its folder
metadata. See `docs/GAME_SDK.md`. Doom remains a special legacy handoff until
its engine has a reviewed reentrant teardown.

## Sound behavior

The Audio page reports the compiled handoff contract only. It does not start
I2S, drive GPIO30 low, or generate a test tone. A native game may request the
Game API tone capability; Console OS then owns a bounded audio session for that
foreground game and closes it before returning home. Doom retains its working
exclusive path:

- 16,000 Hz signed PCM16 stereo
- Doom sound effects plus WAD MUS procedural synthesis
- up to 16 music voices
- backend volume step 6/10
- factory I2S1 speaker TX route, with the pinned factory PDM-clock side effect
- no external MIDI device or soundfont

Do not make the home shell, a native game, and Doom own the factory backend at
the same time.

## Reproducible software checks

From the repository root:

```sh
make console-shell-host
make platform-game-storage-host
make game-sdk-host
make console-os-idf
```

The host targets use AddressSanitizer and UndefinedBehaviorSanitizer. They test
launcher pagination and registry bounds, framebuffer guards/stride padding,
physical-to-logical touch mapping, press/release semantics, malformed frames,
storage handoff/cache generations and terminal game leases,
the bounded tone mixer and platform audio lifecycle, Maze Chase state and
randomized input, plus manifest/scaffolder rejection behavior.
The storage host suite also exercises root-name validation, sorted bounded
listings, hidden-entry exclusion, directory classification, capacity
truncation, traversal rejection, regular-file deletion, and directory-delete
refusal. The shell suite covers File Manager paging, selection, refresh,
confirmation/cancel, non-removable directories, unavailable storage, and
malformed snapshots.

The IDF target builds with the locked ESP-IDF 5.5.3 and managed component
versions, checks ESP32-P4 revision 1.x bounds, and runs
`scripts/verify-console-os.py`. The verifier confirms the WAD identity and Git
ignore status, build-only flash policy, handoff cleanup order, component graph,
partition/flash geometry, FAT seed identity, required USB-device/storage
symbols, and absence of USB-host/SD/codec entry points in the final ELF.

The first migration from the pre-USB layout cannot be app-only: the factory app
shrinks to 7 MiB and `game_data` occupies `0x710000..0xFFFFFF`. A reviewed
full-project image seeds the FAT volume. The Program Manager successor was then
installed app-only and preserved that live partition byte-for-byte. Another
full-project flash deliberately overwrites it with the seed. On-device
acceptance now covers J16 enumeration, copy/remove, FAT repair and clean
reverification, WAD persistence, and clean eject. Abrupt disconnect recovery
and launch rejection while the laptop owns the volume remain separate negative
tests.

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
