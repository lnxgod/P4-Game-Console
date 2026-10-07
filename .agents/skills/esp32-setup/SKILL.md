---
name: esp32-setup
description: "Set up or provision P4 Console OS with exact-revision precompiled Tab5 packages, native games and verified Doom shareware. Use for initial installation, storage selection and first boot; other boards require explicitly requested legacy maintenance. Ask about microSD; current Tab5 game storage is SD-only. Use esp32-add-game for compatible game-only updates."
---

# ESP32 - Set Up

Use this skill first for an OS installation or first-time console setup in this
repository. It coordinates the board-specific build, content and guarded install
workflows; it does not replace their device bindings or hardware checks.
M5Stack Tab5 is the maintained target; use other boards only for explicitly
requested legacy maintenance.

## Establish the installation choices

Use information already supplied in the conversation; do not ask again.

1. Identify the physical board and exact unit. Ask only if the target is unclear;
   do not infer a board from a serial-port name.
2. Ask: **"Will this console have a microSD card installed?"** Offer Yes / No.
   Do not silently infer the owner's intended storage from a currently mounted
   card. Continue read-only preparation while waiting.
3. If Yes, offer **"Would you also like Chex Quest?"** Default to No.
   Do not offer or install Chex on the no-SD route.

The normal installation includes the basic game bundle **and Doom v1.9
shareware**. Treat obtaining and installing the WAD as part of the job, not a
manual prerequisite to hand back to the user. Respect an explicit request to
omit a game. Doom is the only default engine-data title. Chex Quest is an
optional add-on under `GAMES/OPTIONAL`; its launcher tile does not mean its
resources are installed. Do not fetch, stage or transfer Chex unless the user
explicitly opts in and has SD storage. Preserve an already-installed copy
unless removal is separately requested.

For basic games, use the enabled native manifests under `games/` and their
required resource sidecars, following [library policy](../../../docs/GAME_LIBRARY.md).
Incomplete prototypes stay out of default bundles; retain an explicitly requested
WIP title with its status visible. System tools and diagnostics retain
`SYSTEM/TOOLS` and `SYSTEM/TESTS` folders. Measure the final native bundle rather
than hard-coding a game count or size. Do not silently drop required resources
or Doom to make a layout fit.

## Prepare a fresh checkout

Run commands from the repository root. Use the checked-in skills and scripts;
no personal skill directory or machine-specific SDK path is required. For a
Tab5 installation, prefer `make prebuilt` and `make install-tools`, then
activate `.tools/install-python/bin/activate`. Read
[prebuilt installation](../../../docs/INSTALL_PREBUILT.md) for the lightweight
clone, cached exact-revision package, manifest-bound guarded flash and content
paths. No SDK/compiler is needed for this path. If the exact package is absent,
report that fact and choose the matching CI artifact or a source build; never
substitute an older release or silently start a long compile.

For a source build, read `toolchain.lock.json`; use `make setup` only when its pinned tools are absent,
then `make verify` as described by the
[platform workflow](../esp32-fix-console/references/workflow.md).
Use that environment's Python for serial transfer (pyserial is required); the
data-preparation helper below uses only the Python standard library and Git.
Preserve SDK/component locks, existing saves, installed data and preferences.

## Choose a supported storage route

Read `AGENTS.md`, `toolchain.lock.json`, the selected board profile and its
board document. Use `../esp32-fix-console/SKILL.md` for the locked
build/install route and `../esp32-add-game/SKILL.md` for content validation and
transfer. Use other board/testing skills only where their actual scope applies;
the Elecrow acceptance workflow is not a Tab5 installer.

**Current Tab5 game storage requires microSD.** Read
[the storage status](references/tab5-storage.md) before promising a no-SD
installation or automatic internal-plus-SD expansion.

| User choice | Current Tab5 outcome |
| --- | --- |
| Has microSD | SD-backed basic games and Doom; Chex only if selected |
| No microSD | Prepare and verify local content, then report the unimplemented internal-storage route; do not call an SD-only flash a completed no-SD install |
| Adds microSD later | The current SD backend can mount a supported card at startup; internal starter content and combined catalogs are not implemented |

For SD setup, verify a supported FAT card mounts and has room for the measured
bundle, data, saves and temporary upload files. Mount failure does not authorize
formatting or deleting files. Insert/remove the card with power off, then restart;
live insertion/removal is not established. A future internal-storage route needs
its own implementation, capacity checks, migration and exact-device evidence.

For other boards, verify their own storage capabilities and installation route.
The Tab5 capacity calculation does not authorize repartitioning an Elecrow,
Olimex or Waveshare device.

## Prepare the game data automatically

Run the checked-in [preparation helper](../../../scripts/prepare-game-data.py)
as part of setup; do not hand acquisition back as a manual prerequisite:

```sh
python3 scripts/prepare-game-data.py
```

It reads the exact identities and HTTPS sources from
[`third_party/game-data.json`](../../../third_party/game-data.json), reuses a
hash-valid `local-data/doom/doom1.wad`, or downloads and verifies the missing or
invalid input. Downloads and decompression are bounded; verified files replace
local destinations atomically. Failed acquisition preserves existing files.
Every destination must be Git-ignored. The default is Doom v1.9 shareware:
4,196,020 bytes, SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.

Only after the user chooses Chex **and** SD storage, run:

```sh
python3 scripts/prepare-game-data.py --chex --sd
```

This prepares Doom plus both pinned Chex files. It verifies the Chex archive
before reading its WAD and obtains the same-version patch bytes from the raw
Debian data URL corresponding to the manifest's source page. Both Chex files
must verify before any downloaded file is activated. An HTML page is not a
valid patch. Chex is never selected merely because a local copy already exists.

Save the JSON output with the setup evidence: `local-data-ready` means verified
local inputs, **not installed content**. Keep the manifest notices. Never commit,
embed, mirror or redistribute these data files from the project. The helper
performs no build, flash or transfer. For an isolated acquisition check use
`--output-root build-host/game-data-check`; normal transfer commands consume the
default `local-data/doom/` paths, not that test directory.

If acquisition fails, report the specific failure and keep the affected content
incomplete. Do not activate partial data, substitute an unpinned mirror or call
an empty Doom tile an installed game. Respect an explicit request to omit Doom;
do not substitute Freedoom for this pinned runtime without its compatibility
review.

## Build, install and verify

Measure the actual OS image, game files, filesystem overhead and free space
needed for saves and atomic uploads. Retain two appropriately sized OS update
slots where supported. Account for temporary replacement files and firmware
update staging, not only steady-state content size.

Before writing a new unit, preserve its complete flash and bind its size/hash
and hashed device identity in the backup manifest. For an existing unit, verify
the recorded backup and preserve current content affected by a layout migration.
Use the exact-board guarded installer with immutable artifact hashes and an
explicit port. A partition change needs a layout-aware migration and verified
recovery coverage; an app-only install cannot create internal game storage.
Honor installation authorization already given in the session without asking
for the same permission again.

For a prebuilt Tab5 SD route, use the verified package's `firmware/` and
`content/` directories and `--prebuilt` guarded install as documented in
`docs/INSTALL_PREBUILT.md`. The local authorization must bind the manifest
digest as well as all image hashes; preserve all device/backup/predecessor
checks. A source build instead uses the locked environment and:
```sh
make console-os-tab5-idf
```
This target already runs the focused Tab5 artifact verifier. Use the standalone
verifier when inspecting an existing build rather than repeating it by default.
Preserve chosen peripheral feature selections when preparing a successor for
an existing exact-unit authorization; use the matching
[platform route](../esp32-fix-console/SKILL.md).

Use `scripts/flash-console-os-tab5.py` with the exact unit, authorization digest
and port as documented in `docs/boards/M5STACK_TAB5.md`. Its current guarded
route supports registered units A and B only. A new Tab5 needs its own backup,
identity binding and guarded onboarding support; never relabel it as A/B or
inherit another unit's authorization. Continue local preparation until its
write route is supported. After a verified install, load content over the
connected native USB-C cable:
```sh
python scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <explicit-port>
python scripts/p4-usb-content.py doom --port <explicit-port>
```
For a prebuilt bundle, replace `apps/console_os/build-tab5/sd-card` with
`build-host/prebuilt/tab5/<source-commit>/content` in `push-bundle`.
Only after the SD user selects Chex:
```sh
python scripts/p4-usb-content.py chex --port <explicit-port>
```
These are existing **SD-backed** commands, not proof of a no-SD route. Use the
documented successor commands once internal storage support exists.

Complete focused package/build checks. Routine authorized Tab5 firmware writes
use device checksum verification; use `--verification full-readback` only for
recovery, diagnostics or an explicit request, and record the method that ran.
Content transfers retain verified device readback receipts. Bind any selected
or already-installed protected cartridge, including Red Dragon, to the
compatible OS lineage; keep required `.P4R` resources with their cartridges.
This does not add development games to a standard installation. Record the
exact OS, game/data hashes, unit and transfer receipts. A failed optional Chex
WAD or patch transfer leaves Chex incomplete, even when the other file succeeded.

## Complete initial configuration and acceptance

After the authorized install, with that exact unit running the launcher:

1. Confirm SD mounted, catalog entries and covers are present, and free space
   permits saves and later atomic updates. An icon alone does not prove data
   readiness. Keep existing saves and preferences during upgrades.
2. Read the device clock and, for first-time setup, synchronize UTC from the
   host's correct clock with verified hardware readback:
   ```sh
   python scripts/p4-transfer.py clock --port <explicit-port>
   python scripts/p4-transfer.py clock --sync --port <explicit-port>
   ```
   The current UI displays UTC. Record failed or invalid clock status honestly.
3. In **Control Panel > Preferences**, confirm startup/game volume and Appearance.
   Apply preferences already supplied by the owner; otherwise preserve current
   values. Verify mute and an audible setting when acoustic testing is authorized.
   Do not erase saved preferences to manufacture a first-boot experience.
4. Exercise touch navigation, back/return, one native game, Doom launch/play/exit,
   and save persistence after restart. If Chex was selected, test it only after
   both data files have verified transfer receipts. Record sound and physical
   input feedback separately from frame-cadence measurements.
5. Configure/test a controller or linked-console session only when requested;
   use the relevant repository skill and the selected firmware's actual support.
   Data downloads happen on the host and do not require inventing device Wi-Fi
   credentials or enabling unverified radio/USB-host features.

Report installed OS/content, storage used/free, local preparation versus device
readback, clock/preferences, Doom and optional Chex status, and any acceptance
still pending. A successful build or transfer does not prove smooth gameplay.
The future no-SD route additionally needs absent-card boot/play/save tests and
later-card/removal/conflicting-ID tests from the storage reference; do not mark
those complete on current SD-only firmware.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.

## Standard and development content

Standard bundles exclude disabled manifests and every `GAMES/WIP` title.
Byte Buddy, Red Dragon and Skyline Leap require an explicit
`make install-dev GAME=<slug> PORT=<port>`; see the
[developer install guide](../../../games/README.md#developer-installs).
Tide Maze remains in the normal bundle by the owner's request; preserve its
open device-lag acceptance. Feature Blast Circuit, with Wacky Wheels as an
installed fallback. Preserve hidden games' source, package IDs and saves.
