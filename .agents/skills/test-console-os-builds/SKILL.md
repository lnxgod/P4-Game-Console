---
name: test-console-os-builds
description: Build, verify, guarded-flash, recover, or manually qualify the P4 Console OS and native Game API games on the exact Elecrow ESP32-P4 10 in tablet. Use when a session is asked to test a Console OS build, put it on the tablet, validate Maze Chase or another game, check image/sprite rendering, touch, sound, return-to-launcher, or Doom regressions, or write honest hardware acceptance notes. Never substitute a generic idf.py flash for the exact-artifact route.
---

# Test Console OS builds

Test the software and the named tablet as separate stages. Preserve the
currently working image until the successor passes exact readback and retained
startup capture.

## Load the governing contracts

Also use these repository skills:

- `$develop-esp32-p4-platform` for the locked toolchain, board identity,
  backup, write, recovery, and evidence rules.
- `$use-elecrow-p4-display` for launcher or game pixels on the 10 in panel.
- `$use-elecrow-p4-audio` whenever a build can energize native-game or Doom
  sound, even if startup acceptance intentionally keeps the amplifier off.
- `$develop-p4-console-games` when the candidate adds or changes a native game,
  manifest folder/type, controls, drawing, or Game API sound use.
- `$test-p4-games-locally` before building or installing a candidate that adds
  or changes native-game behavior.
- `$add-usb-gamepad-support` only when USB HID is actually in scope.

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
3. **Install a changed build**: create a new exact-artifact successor route,
   preserve the live predecessor, write only the app partition, read it all
   back, and capture startup on the retained UART.
4. **Recover an interrupted install**: inspect the durable ledger first and
   use only the installer bound to that ledger.

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
`$test-p4-games-locally` workflow before `make console-os-elecrow-idf` and before
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

Run the locked checks in this order:

```sh
make verify
make game-sdk-host
make console-shell-host
make check
make console-os-elecrow-idf
python3 scripts/verify-console-os.py apps/console_os/build
```

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

Before any write:

1. Resolve one explicit serial device and prove no monitor owns it.
2. Verify the hashed exact-unit identity, ESP32-P4 revision v1.3, 16 MiB
   flash, 32 MiB PSRAM, factory backup manifest, and current predecessor.
3. Create immutable build evidence for the exact candidate.
4. Create a new exact-unit audio release when the candidate contains the
   factory audio path. Bind the owner's direction, the same known GPIO path,
   reduced volume, recovery bytes, and connected-unit-only topology exception.
5. Create a new authorization, installer wrapper, outer route, and unique 0700
   ignored recovery directory. Use the executed Game API v1 files as a pattern,
   but never modify them to target different bytes.
6. Dry-load the frozen route, validate every digest and geometry field, test
   the exact startup parser with representative markers, and prove wrong
   identity/artifact values fail before opening the UART.

The transaction must retain one exclusive UART from loader entry through:

- live identity, security, flash, and partition checks;
- a sealed live preimage covering the complete successor mutation span;
- one app-partition write only;
- byte-for-byte readback of the complete padded span;
- one application launch; and
- ordered launcher startup capture.

Mark `restore_required` durably before the first flash command. On every caught
post-mutation failure, restore and read back the sealed predecessor before
exiting. Do not use generic `idf.py flash`, a full-project write, an erase, or
`--force`.

## Respond to a stopped transaction

Read the ledger before doing anything else.

- If `install_write_attempt_count` is `0` and `restore_required` is `false`,
  no recovery write is justified. Preserve/archive that attempt and use only a
  hardware reset to relaunch the unchanged predecessor.
- If `restore_required` is `true`, run the exact successor installer's
  `recover` command against its original recovery directory. Do not create a
  substitute preimage.
- If restoration cannot be proven, stop with the ledger active and report that
  recovery is required.

Do not blind-retry a repeated hardware boundary. Diagnose the pinned source and
record the no-write attempts. Any normalization exception must bind the exact
unit, runtime, stub, observed values, and still reject a wrong canonical
identity.

## Qualify the running build

Automated startup must prove the exact app count bound by the new candidate
route, ordered display/touch readiness, monotonically increasing
launcher/touch counters, equal display submission/completion counts, zero
timeouts/failures, no Doom handoff, and amplifier-off state. The installed
predecessor has six entries; the current folder successor still registers
seven apps even though root renders three logical folder tiles. Never reuse a
six-entry startup parser for the seven-app artifact, and never mistake the
three root tiles for the registry count.

Then ask the operator to check only what requires eyes, hands, or ears:

1. Root shows `ALL PROGRAMS`, `GAMES`, and `SYSTEM` with folder icons, no
   corrupt pixels, and no developer-facing `V/T/A/S` capability labels.
2. Open `GAMES`; confirm `ACTION` and `ARCADE`. Open `ARCADE`; confirm
   `MAZE CHASE` and `SPACE INVADERS`. Use `UP` twice and confirm Arcade ->
   Games -> Root without wrapping or launching a tile.
3. Open `ALL PROGRAMS`. Swipe upward in the app area and use the down arrow;
   both reveal the seventh app without launching the tile where the gesture
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

End by stating what passed, what remains manual, what is currently installed,
and the exact next control the operator should tap. Never call pending evidence
a pass.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
