# Tab5 storage: installation contract and implementation status

Read this reference for `m5stack-tab5` installations involving no SD card or
adding a card later. Paths below are relative to the repository root.

## Requested default

The no-SD installation must store the enabled basic games, their resources,
and the verified Doom shareware WAD in internal flash. Teaching Lua demos are
excluded from the current default library; see `docs/GAME_LIBRARY.md`.
Chex is offered only when an SD card is available and is installed only after
the user selects it.

The same firmware should recognize SD on the next startup without a reflash.
Keep internal starter content available alongside SD additions. Keep saves
associated with their original game/storage location; do not silently copy,
replace or merge them. Mount errors must preserve both stores and report which
one is usable. Do not automatically format a card.

The owner confirmed card detection after reboot on 2026-10-04. Insert/remove
with power off, then restart; no OS reflash should be needed. Live
insertion/removal is outside this default.

## Current implementation evidence (2026-10-04)

This workflow is specified; **the existing Tab5 firmware is SD-only**:

- `apps/console_os/partitions-tab5.csv` has two `0x7f0000`-byte OS slots and
  no `game_data` partition.
- `components/platform_game_storage/src/platform_game_storage.c` selects Tab5
  through `P4_GAME_STORAGE_SD_BACKEND`.
- `apps/console_os/CMakeLists.txt` generates an `sd-card/` bundle for Tab5.
- `scripts/verify-console-os-tab5.py` verifies the existing SD-only layout.
- `scripts/flash-console-os-tab5.py` binds that layout and the existing guarded
  first-layout/app-only operations. It is not a migration route for a new
  internal filesystem.
- `docs/boards/M5STACK_TAB5.md` documents verified SD and native-USB transfer
  on units A/B. Automatic internal-plus-SD catalog support is not established.

An instruction in a skill cannot implement these runtime changes. When a no-SD
installation is requested, inspect the current code and evidence first. If it
still matches the above, explain the missing storage implementation, continue
safe content preparation, and establish implementation scope before device
writes. Do not flash the SD-only image and describe it as SD-free. Do not use
an old exact-artifact authorization for a different partition layout.

Update this status when the actual code and verification change; retain the
distinction between software checks and on-device proof.

## Capacity and candidate design

The recorded A/B units have 16 MiB flash and 32 MiB volatile PSRAM. The earlier, pre-curation local
build measured on 2026-10-04 contained (historical planning values):

| Item | Bytes |
| --- | ---: |
| OS image | 1,383,856 |
| 19 enabled native packages | 2,312,976 |
| Required resource sidecar | 1,397,952 |
| Two Lua cartridges | 10,218 |
| Doom shareware WAD | 4,196,020 |
| Basic content plus Doom | 7,917,166 |
| Optional Chex WAD, excluding its patch | 12,361,532 |

These are planning measurements, not fixed package budgets. Recalculate from
the final build and `third_party/game-data.json`.

One candidate layout is two 3 MiB OS slots after the existing `0x20000`
reserved region, leaving `0x9e0000` bytes (9.875 MiB) for a persistent,
wear-levelled game filesystem. This is **not an approved or implemented layout**.
Validate app growth allowance, filesystem overhead, saves and upload/update
staging. Replacing a 4 MiB WAD while retaining the old WAD needs more temporary
space than the roughly 2.3 MiB steady-state remainder. Reuse an already verified,
identical WAD; never delete a valid installed file merely to make an atomic
replacement appear possible.

Implementation must cover storage mounting and location-aware catalogs,
resources, Lua carts, Doom, saves, file transfer and updates. Preserve internal
content when SD is present, define duplicate-ID handling without presenting
duplicate launcher entries, and target SD additions explicitly. A present but
invalid/full card must not destroy or hide the internal collection.

Keep the native USB-C verified transfer path; internal storage does not require
enabling Tab5 USB-A host power or USB Drive/MSC. The existing Elecrow flash
backend is tied to MSC and must be adapted deliberately rather than selected
by changing Tab5's board identity.

A layout migration must preserve affected current flash data, verify the old
layout and active OS slot, and bind the complete new layout/filesystem and
recovery bytes to the exact unit. Test interrupted migration and wrong-device
rejection before any hardware claim.
