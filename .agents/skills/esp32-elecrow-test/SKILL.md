---
name: esp32-elecrow-test
description: "Use only for explicitly requested legacy Elecrow ESP32-P4 10 in tablet builds, exact-unit guarded installation/recovery, Console OS qualification, native-game device acceptance or Doom regressions. Tab5 validation uses Fix Console and its own board contract."
---

# ESP32 - Elecrow Test

This skill is for the Elecrow 10 in variant only. Tab5 uses
`docs/boards/M5STACK_TAB5.md` and `$esp32-fix-console`; Waveshare uses
`$esp32-waveshare`. Never inherit this route's authorization or offsets.
Test the software and the named tablet as separate stages. Preserve the
existing recovery artifacts until the successor passes exact readback and
retained startup capture. Do not create or refresh firmware backups as part of
flashing; backups run only as a separately requested operation and are never a
flashing prerequisite. Recovery can rebuild old source.

## Load the governing contracts

Also use these repository skills:

- `$esp32-fix-console` for the locked toolchain, board identity,
  write, recovery, evidence and no-backup rules.
- `$esp32-elecrow-screen` for launcher or game pixels on the 10 in panel.
- `$esp32-elecrow-sound` whenever a build can energize native-game or Doom
  sound, even if startup acceptance intentionally keeps the amplifier off.
- `$esp32-make-game` when the candidate adds or changes a native game,
  manifest folder/type, controls, drawing, or Game API sound use.
- `$esp32-test-game` before building or installing a candidate that adds
  or changes native-game behavior.
- `$esp32-controllers` only when USB HID is actually in scope.

Before acting, read `toolchain.lock.json`, `hardware/board-profile.json`,
`AGENTS.md`, and `docs/CONSOLE_OS.md`. Read
`references/current-unit.md` before any hardware test, recovery decision, or
claim about what is currently installed.

## Classify the request

Choose exactly one initial scope:

1. **Software build test**: run host tests, build, and artifact verification;
   do not open the programming UART.
2. **Test the installed image**: use passive serial observation and manual
   checks; do not reflash an artifact that is already installed.
3. **Install a changed build**: create a new exact-artifact successor route
   without firmware backups, use reviewed build artifacts for applicable
   predecessor checks, write only the app partition, verify the candidate and
   capture startup on the retained UART.
4. **Recover an interrupted install**: inspect its receipt and use a reviewed
   artifact rebuilt from old source. For a historical snapshot transaction,
   inspect its original ledger and use the installer bound to that ledger.

A build hash change invalidates every prior flash authorization. Executed
routes, authorizations, recovery directories, and hardware records are
historical objects; never overwrite or silently reuse them.

## Run the software stage

Preserve user changes and inspect the tree first:

```sh
git status --short --branch
git diff --check
```

For every candidate that adds or changes a native Game API game, complete the
`$esp32-test-game` workflow before `make console-os-elecrow-idf` and before
creating any successor install route. Require both the sanitizer-backed
headless smoke and explicit interactive play of the exact game revision.
Record the slug, source revision (or content hash when the tree is dirty),
tester, exercised controls/touch/lifecycle/audio, and tablet-only checks still
pending.

If a visible desktop or interactive tester is unavailable, run safe headless
checks and report the candidate as `host-tested`, but do not classify it as
`local-play-tested`, authorize it, or write it to the device. A platform-only
change that cannot alter native-game behavior does not require interactive
gameplay; keep the ordinary host and hardware checks. Legacy Doom follows its
separate pinned host-smoke and tablet route.

Confirm the ignored Doom input before building the WAD-bearing Console OS:

```sh
test "$(stat -f %z local-data/doom/doom1.wad)" = 4196020
echo '1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  local-data/doom/doom1.wad' | shasum -a 256 -c -
git check-ignore -q local-data/doom/doom1.wad
```

For a complete Elecrow integration candidate, use the locked build target:

```sh
make verify
make console-os-elecrow-idf
```

The target includes its shell, storage, SDK and artifact checks. For an isolated
component change, run its focused host target first. Run `make check` only for
an explicit full-suite request, lock/toolchain changes or changes spanning
maintained applications; it includes historical exact-artifact checks and
unrelated diagnostic builds. Documentation-only edits need no firmware build.

Do not conceal a broad historical-suite failure by editing sealed evidence.
Run and report focused current tests separately, identify whether the failure
predates the candidate, and keep the candidate classified below hardware-tested
until the device stage passes.

After the last build command, hash the BIN, ELF, bootloader, and partition
table and record their byte counts. Never rebuild after binding those hashes.
Seal only the authorized BIN owner-read-only immediately before a guarded
install; the transport rejects a writable artifact.

## Handle images and game assets

The v1 API supports bounded, clipped RGB565 sprites through `p4/draw.h`.
Code-generated shapes and embedded correctly licensed RGB565 arrays are both
valid. Validate width, height, stride, byte order, clipping, flash/PSRAM cost,
and licensing. Keep image conversion deterministic and host-tested.

Follow `docs/GAME_ART.md` for native 768x480 game presentation and the tested
320x200 fallback. This exact Elecrow route retains the platform's negotiated
surface; do not claim Tab5/Waveshare high-resolution support as Elecrow evidence.
Check real art contrast, text, sprites, touch alignment and memory/performance
on the selected device. Record ImageGen sources and deterministic conversion.
Never import Pac-Man ROMs, maps, sprites, sounds, or artwork. Never commit a
commercial WAD, a WAD-bearing firmware image, or local recovery data.

## Install only through a successor route

Firmware backups, snapshots and backup manifests are never required for a
flash. Use a reviewed backup-free successor route; frozen snapshot-based
installers remain historical/recovery tooling. An install request does not
select a backup. Recovery can use firmware rebuilt from the reviewed old source.
For same-layout updates on a dual-OTA unit, qualify the implemented `.P4U`
inactive-slot updater; its physical J16/OTA acceptance remains pending.

Before any write:

1. Resolve one explicit serial device and prove no monitor owns it.
2. Verify the hashed exact-unit identity, ESP32-P4 revision v1.3, 16 MiB
   flash, 32 MiB PSRAM, live layout, security state and applicable predecessor
   against reviewed build artifacts.
3. Create immutable build evidence for the exact candidate.
4. Create a new exact-unit audio release when the candidate contains the
   factory audio path. Bind the owner's direction, the same known GPIO path,
   reduced volume and connected-unit-only topology exception.
5. Create a new authorization and backup-free installer route with an ignored
   receipt directory. Preserve existing historical installers and recovery
   evidence; do not inherit their snapshot requirements.
6. Dry-load the new route, validate every digest and geometry field, test
   the exact startup parser with representative markers, and prove wrong
   identity/artifact values fail before opening the UART.

The transaction must retain one exclusive UART from loader entry through:

- live identity, security, flash, and partition checks;
- any applicable predecessor comparison against reviewed build artifacts,
  without reading or saving a firmware snapshot;
- one app-partition write only;
- byte-for-byte readback of the complete padded span;
- one application launch; and
- ordered launcher startup capture.

Record write start and failure in the install receipt. On a failed write, retain
the loader and evidence for explicit recovery using a reviewed artifact rebuilt
from old source; do not capture firmware or require automatic snapshot rollback.
Do not use generic `idf.py flash`, a full-project write, an erase, or `--force`.

## Respond to a stopped transaction

For a new backup-free route, read its receipt and diagnose the failed write
before a separately authorized rebuild/recovery. No backup is required.

For an interrupted historical snapshot-based transaction only, read its
original ledger before doing anything else:

- If `install_write_attempt_count` is `0` and `restore_required` is `false`,
  no recovery write is justified. Preserve/archive that attempt and use only a
  hardware reset to relaunch the unchanged predecessor.
- If `restore_required` is `true`, recovery can rebuild the reviewed old source
  and use a new exact-unit artifact-bound route. Preserve the original ledger.
  An explicitly selected existing snapshot may instead use the historical
  installer's `recover` command and original directory; never invent a substitute
  preimage or require a backup.
- If restoration cannot be proven, retain the ledger and report the unresolved
  recovery result; prepare the reviewed rebuild without capturing firmware.

Do not blind-retry a repeated hardware boundary. Diagnose the pinned source and
record the no-write attempts. Any normalization exception must bind the exact
unit, runtime, stub, observed values, and still reject a wrong canonical
identity.

## Qualify the running build

Automated startup must prove the exact app count bound by the new candidate
route, ordered display/touch readiness, monotonically increasing
launcher/touch counters, equal display submission/completion counts, zero
timeouts/failures, no Doom handoff, and amplifier-off state. Derive the expected
app count from the candidate's built-ins and validated native catalog. The
six/seven-app counts in the August folder records are historical, not a current
catalog contract. Root folder tiles are not registered-app counts.

Then ask the operator to check only what requires eyes, hands, or ears:

1. Root shows `ALL PROGRAMS`, `GAMES`, and `SYSTEM` with folder icons, no
   corrupt pixels, and no developer-facing `V/T/A/S` capability labels.
2. Open `GAMES`; confirm the candidate's manifest-derived categories and native
   titles. Open `ARCADE` and launch a validated cartridge. Use `UP` twice and confirm Arcade ->
   Games -> Root without wrapping or launching a tile.
3. Open `ALL PROGRAMS`. Swipe upward in the app area and use the down arrow;
   both reveal additional apps without launching the tile where the gesture
   began. Swipe/back-arrow to the first row and confirm the boundary does not
   wrap.
4. Maze Chase renders and the on-screen direction controls are playable.
5. Its tones are clean and acceptably quiet.
6. The upper-left `EXIT` control returns to the Arcade folder and audio shuts
   down.
7. Space Invaders renders its full formation and shields; Left/Right move,
   A or B fires, Start pauses, waves advance, and `EXIT` returns safely.
8. Open `SYSTEM`; confirm Colors, Touch, System, and Audio status. A built-in
   page's Home control must return to the System folder.
9. If a native test game declares `audio-stream`, require audible output and
   serial `pcm_blocks`/`pcm_frames` progress with zero `pcm_rejected`; counters
   alone are not acoustic acceptance. Do not claim native PCM hardware-tested
   when the installed games exercise only tones.
10. Doom still launches; SFX and recognizable MUS music remain audible.
11. A restart returns to Console OS.

Treat “I played [game]” as acceptance of that game's launch, visuals, and
playable controls. Do not infer sound, exit, Doom, or restart unless the
operator says so. Serial audio counters prove delivery to the backend, not
acoustic output.

## Record and hand off

Create a new JSON record under `hardware/test-runs/` with the exact artifact,
device identity hash, mutation/readback hashes, route hashes, ledger state,
startup markers, and explicit manual observations. Keep local raw captures,
preimages, WADs, and firmware ignored.

Use these classifications precisely:

- `host-tested`: native sanitizer/unit tests passed.
- `local-play-tested`: the exact native-game revision also passed explicitly
  confirmed interactive SDL3 play; list only the behaviors actually exercised.
- `build-tested`: the locked ESP-IDF image compiled, linked, and verified.
- `hardware-tested`: exact readback and named serial acceptance passed.
- `operator-confirmed`: only the listed visual, control, or acoustic checks
  were explicitly confirmed by a person.

End by stating what passed, what remains manual and the last evidenced install.
For an active hardware test, name the next relevant control. A software-only
test does not establish the currently installed image. Never call pending
evidence a pass.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
