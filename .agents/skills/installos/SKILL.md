---
name: installos
description: Default repository workflow for installing or provisioning P4 Console OS. Ask about microSD, prepare basic games and verified Doom shareware, select supported internal-flash or SD storage, and offer Chex Quest only with SD. Use for OS installation and first setup; use develop-p4-games for game-only updates.
---

# Install Console OS

Use this skill first for an OS installation or first-time console setup in this
repository. It coordinates the board-specific build, content and guarded install
workflows; it does not replace their device bindings or hardware checks.

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
omit a game.

For basic games, use the enabled native manifests under `games/`, their required
resource sidecars. Lua teaching samples are not part of the default bundle;
install a finished Lua cart only when requested. Measure the final bundle rather than hard-coding a game count or size.
Do not silently drop required resources or Doom to make a layout fit.

## Choose a supported storage route

Read `AGENTS.md`, `toolchain.lock.json`, the selected board profile and its
board document. Use `../develop-esp32-p4-platform/SKILL.md` for the locked
build/install route and `../develop-p4-games/SKILL.md` for content validation and
transfer. Use other board/testing skills only where their actual scope applies;
the Elecrow acceptance workflow is not a Tab5 installer.

| User choice | Required installation outcome |
| --- | --- |
| No microSD | Persistent internal-flash game storage containing basic games and Doom; no card required to boot, play or retain saves |
| Has microSD | Basic games and Doom available, SD enabled for additional content, and Chex only if selected |
| Adds microSD later | Detect a supported card at the next startup without reflashing; retain access to internal games and preserve saves |

For Tab5, read [references/tab5-storage.md](references/tab5-storage.md) before
planning a no-SD install or promising automatic SD expansion. It distinguishes
the intended behavior from the existing SD-only implementation.

Use one firmware capable of internal storage and later SD expansion when that
route is implemented. An SD card should expand the available catalog, not make
the internal starter games disappear. Preserve existing files; mounting a card
does not authorize formatting, copying over saves or migrating data. The owner confirmed
detection after reboot: insert/remove with power off, then restart.
Do not promise live insertion/removal without separate implementation and tests.

For other boards, verify their own storage capabilities and installation route.
The Tab5 capacity calculation does not authorize repartitioning an Elecrow,
Olimex or Waveshare device.

## Prepare the game data

1. Read the exact file identities and acquisition URLs from
   `third_party/game-data.json`. Keep game data in ignored local paths.
2. Reuse `local-data/doom/doom1.wad` only after verifying its byte count and
   SHA-256. If absent or invalid, acquire the pinned shareware using the
   repository's instructions in `AGENTS.md`. Download to a temporary local
   staging path, check command failures, decompress/extract only the intended
   input, verify it, then move the verified file into place. Never activate a
   partial download or substitute an unverified mirror.
3. The required Doom identity is 4,196,020 bytes and SHA-256
   `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
   Run `git check-ignore -q local-data/doom/doom1.wad` before building.
   Preserve the original shareware notices.
4. If SD is available **and** Chex was selected, obtain and verify both
   `chex.wad` and `chex.deh` from the pinned manifest. Chex is optional;
   a failed Chex acquisition must not be reported as installed.
5. Keep WADs, generated storage images, packages containing game data and recovery
   images out of Git. Doom stays a separately provisioned data file; do not
   embed it into the OS executable or change the repository's distribution policy.

If acquisition fails, report the concrete failure and keep the installation
incomplete for the affected content. Do not call an empty Doom launcher tile a
successful Doom installation. Do not substitute Freedoom for the pinned runtime
without the separately reviewed compatibility change.

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

For the current Tab5 SD route, use the locked environment and:
```sh
make console-os-tab5-idf
python scripts/verify-console-os-tab5.py apps/console_os/build-tab5
```
Use `scripts/flash-console-os-tab5.py` with the exact unit, authorization digest
and port as documented in `docs/boards/M5STACK_TAB5.md`. After a verified install,
load content over the connected native USB-C cable:
```sh
python scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <explicit-port>
python scripts/p4-usb-content.py doom --port <explicit-port>
```
Only after the SD user selects Chex:
```sh
python scripts/p4-usb-content.py chex --port <explicit-port>
```
These are existing **SD-backed** commands, not proof of a no-SD route. Use the
documented successor commands once internal storage support exists.

Complete focused package/build checks and exact write readback. Record boot,
game catalog and storage evidence for the exact unit. For no-SD acceptance,
boot with the card physically absent, launch a basic game and Doom, return to
the launcher and verify saves after restart. For later-card support, also test
a populated card, an empty supported card, conflicting game IDs and a restart
after card removal; verify internal games and saves remain accessible.

Report what was installed, storage used/free, Doom and optional Chex status,
and which behavior is build-tested, hardware-tested or operator-confirmed.
Never label a skill specification or a successful compile as working hardware.
