> Historical audit from `codex/repository-cleanup` at `5b585f2c33d0534353d310d88fd211c55e92e07d` in worktree `1f75`.
> Its results and binary hashes apply only to that branch. The canonical
> [consolidated audit](REPOSITORY_CLEANUP_2026_10_06.md) supersedes its
> completion claims and records the final disposition of conflicting findings.

# Repository cleanup audit — 2026-10-06

Baseline: `615321ff45020387fba96572b5b42376b0d1be52` (`origin/main`, PR #13).
Execution branch: `codex/repository-cleanup`, isolated worktree `1f75`.
Machine-readable results: [cleanup evidence](../test-runs/2026-10-06-repository-cleanup-1f75.json).
This is a source/build audit, with no device access, flashing, publishing or merging.

## Completed checklist

- [x] Fetch/check default branch, PRs, local branches, source checkout and worktrees.
- [x] Audit the tracked tree for retired Lua code, dependencies, build hooks,
  games, tests, documentation and skills.
- [x] Trace and remove obsolete code, test wrappers and generated art; retain
  supported diagnostics, source provenance and recovery material.
- [x] Review all 14 skills, their supporting resources and AGENTS.md against
  the implemented native C/.P4G and board workflows.
- [x] Correct stale capability, layout, catalog and command guidance.
- [x] Run focused tests, syntax/reference validation and the primary Tab5 build.
- [x] Record retained work and explicit acceptance limits below.

## Git and concurrent work

GitHub reported `main` as default and all 13 PRs merged, with no open PRs.
The source checkout was clean on `codex/m5stack-tab5` at `0b2376b` when inspected.
Its two commits outside main's ancestry were preserved. So were the active
Tide Maze worktree and all Wacky Wheels sources, tools and ignored local inputs.
The Tide Maze worktree subsequently contained active modified and untracked
game/shared-renderer files; none were changed here.

The delayed launch of this chat overlapped a recovery cleanup chat on
`codex/repository-cleanup-execution`. That chat removed eight local refs during
this audit. Before removal, all eight were ancestors of main and had no attached
worktree: `codex/doom-arena-release`, `codex/doom-multiplayer-engine-barrier`,
`codex/game-manager-usb-updates`, `codex/program-manager-file-manager`,
`codex/usb-game-storage`, `codex/usb-game-storage-device-install`,
`codex/usb-game-storage-program-manager`, and `codex/wireless-multiplayer`.
Their complete commit IDs are retained in the evidence. This worktree performed
no branch deletion; its recheck stopped when it found the first ref already
absent. No remote branch or registered worktree was removed here.

Preserved branches with commits outside main's ancestry at the snapshot:

| Branch | Unique commits |
| --- | ---: |
| `codex/byte-buddy-h1-p4r` | 10 |
| `codex/fix-skyline-leap` | 1 |
| `codex/game-changers-ai` | 2 |
| `codex/game-suite-polish` | 2 |
| `codex/m5stack-tab5` | 2 |
| `codex/tide-maze` | 3 |
| `codex/tide-maze-3d` | 2 |

Merged but registered `codex/byte-buddy-core-update` and
`codex/waveshare-console-os`, local `main`, all detached/prunable worktree
registrations, and both cleanup worktrees were retained. An old/prunable path
is not proof that its branch or recovery state is disposable.

The two cleanup branches overlap. Review their diffs before combining them;
do not blindly apply both. This branch additionally corrects the Elecrow
dual-OTA history, factory PDM/framebuffer references, board capacity guidance,
ES8311 component documentation and the SDL preview's missing artwork fields.

## Tree coverage and disposition

The baseline contains 2,560 tracked files. Inventory covered every top-level
group, with caller/build/manifest tracing for removal candidates. This is not
a proof that every dynamic path is reachable or every optional target works.

| Area | Review and disposition |
| --- | --- |
| Root files, Makefile, locks | Tab5 default and explicit older-board targets checked; AGENTS storage/art guidance clarified; toolchain and dependency locks unchanged |
| `apps/` (314 files) | Console build/component graph, selected profiles, storage seed, SDK defaults and retirement metadata checked; old diagnostics retained as supported/historical workflows |
| `components/` (606) | CMake/source/include references and disabled/uncalled candidates scanned; stale Lua shell display branch/message removed; protocol class 4 rejection retained |
| `games/` (503) | All manifests remain native C; 17 enabled packages; retired identities unchanged; only unused LORD derived title art/converter removed |
| `scripts/` (245) | CLI/build/test consumers and Python definitions examined; two uncalled helpers and two unused capture wrappers removed; current verifiers and retirement guards retained |
| `tools/` (28), `ports/` (14) | Host/realm/Quake build references checked; SDL preview repaired; realm, Quake and Wacky work preserved |
| `.agents/` (34), `docs/` (30), root prose | All skills and supporting resources read; local links and command names checked; obsolete Lua proposal and board/version claims corrected |
| `design/` (67) | Reference galleries/provenance retained; they are design inputs, not firmware-only candidates |
| `hardware/` (307), `test-runs/` (47) | Exact-artifact/recovery records retained unchanged; dated failures and pending acceptance remain authoritative for their artifacts |
| `third_party/` (349), `game-data/` (8) | Vendored source, notices, locks and authorized Pure Hell pack retained unchanged; no downloaded WAD or firmware added |

No tracked `.lua` sources, Lua runtime/toolchain directory or Lua dependency
remains. The earlier removal is recorded in
`test-runs/2026-10-05-lua-source-removal.json`; references there to already-removed
conflict-copy files are historical evidence, not current files to delete.

## Removal evidence

| Removed surface | Evidence |
| --- | --- |
| Shell P4CART display enum/label and “LUA PENDING” notice | Only the obsolete display branch used the shell enum. Native transfers use classes 1–3. Wire class 4 remains reserved and rejected before storage access |
| Host/desktop `.P4CART` demo fixtures | Replaced with native `.P4G` examples; no parser, storage policy or game rules changed |
| `_verify_central_flash_path_legacy` | No caller; the live `verify_central_flash_path` remains. Parsed Python AST is identical after excluding the removed definition |
| `p4g_name_valid` | No caller; all native validation already uses `game_name_valid`. Remaining AST is identical |
| Capture wrappers `0.4.85` / `0.4.86` | No maintained consumer; they only set superseded artifact/version overrides and dispatch `0.4.84`. The used shared capture implementation and historical records remain |
| LORD `lord_title_art.h`, `title-background-ansi.png`, `build_title_art.py` | Only self/converter/document references; no C include or package build dependency. Current game includes `lord_illustrated_title.h`. Original source art and provenance remain |

Retained deliberately:

- `.P4CART` rejection code/tests, seed guard and the retired installer stub:
  they prevent unsupported installation and preserve existing user files.
- The unused comparison parser in `gamepad-diag-capture.py` and two private
  wrappers in `doom-e6-install.py`: these files are bound by the historical D1/E6
  gate/restore inventories. Removing them would invalidate those frozen paths;
  cleanup does not refresh or weaken their exact-source authorizations.
- `nextgen/maze.png`: `pack_nextgen.py` constructs its filename from
  `source.json`; `nextgen.inc` consumes the generated `ng_art_maze` array.
- LORD native/fallback title preview PNGs: generated presentation evidence;
  original art still participates in launcher/provenance workflows.
- Five missing screenshot links in the pinned Doom/Quake upstream READMEs:
  upstream documentation is retained byte-for-byte rather than altering the
  vendor source/provenance bundle.

## Skills review

All 14 frontmatters pass `quick_validate.py`. All 81 Make-command references
in skill resources resolve. Referenced scripts exist; generated/local recovery
paths and source-archive paths were distinguished from checked-in files.

Platform, Elecrow testing/display/audio references and Waveshare skills now
describe the implemented services rather than initial bring-up plans. The
Elecrow reference explicitly records the later dual-OTA migration; its old
single-app `0x10000` install is historical, not a successor template for the
current layout. Test instructions derive catalog counts from the candidate.
Elecrow skill UI metadata names its exact board; Tab5 is still primary.

The native authoring, package, multiplayer, local testing, art, controller and
installos workflows were also reviewed. Their supported C/API boundaries,
controller ownership, multiplayer barriers, direct touch, resolution/performance
contracts, and explicit Chex selection were retained. Installos correctly says
Tab5 is SD-only: no `game_data` partition exists in the Tab5 layout, and its
storage backend/build bundle are SD-backed. AGENTS now states this directly.

## Validation

| Check | Result |
| --- | --- |
| Pinned `make verify` | Pass with existing ESP-IDF 5.5.3, commit `2c211b2`; no setup/upgrade |
| `make console-shell-host` | 7/7, ASan/UBSan |
| `make console-os-host` | 8/8, including SDL dummy-video smoke; repaired pre-existing missing-field build errors |
| Tab5-configured SDL console preview | 8/8 with `-DP4_CONSOLE_HOST_TAB5=ON` |
| `make p4-desktop-host` | 1/1 |
| `make p4-content-host` | 3 CTest cases plus 8 content and 5 retirement Python tests |
| `make game-registry-check` | Pass; 17 enabled native games, 85 native-verifier gate cases, resource/protected-lineage/scaffold checks |
| Transfer/game/USB-drive/wire/clock suite | 28/28 |
| D2.3 capture transport suite | Pass; same-handle, atomic receipt and one-shot checks |
| LORD focused tests | 3/3, including presentation and active traces |
| `make console-os-tab5-idf` | Pass; firmware, update package and 17-cartridge SD bundle verified |
| Source syntax/reference checks | 214 Python files and 69 shell scripts parse; 195 first-party Markdown links resolve, including this audit |
| Skills, diff and preservation checks | 14/14 validators; `git diff --check` clean; no lock/hardware/vendor/gameplay/manifest changes |

Tab5 image: 4,317,712 bytes; SHA-256
`eb04184caede1f3fc96af405f2b78d67f50975976c50a83a9084496d11676654`.
Build-only, `flash_authorized=false`, `hardware_verified=false`. The final
incremental Tab5 rebuild after the unused-file removals produced the identical
image hash and passed the verifier again. Both SDL preview smoke tests also
passed after their final native-directory fixture correction. No game behavior changed, so
no new interactive game acceptance is claimed or required for this cleanup.

Logs remain local under `build-host/repository-cleanup/`; their hashes and
commands are recorded in the linked JSON. The initial plain `make verify`
could not discover the source checkout's SDK; supplying `P4_IDF_PATH` resolved
it. The first SDL build exposed missing artwork initializers; the corrected
build passed. Existing pinned Doom compiler warnings remain, with no new
first-party warnings or global suppressions added.

No full Elecrow/Olimex/Waveshare firmware matrix or historical `make check`
was needed for these shell/tool/document changes. Shared shell tests cover the
maintained shell variants; only the primary Tab5 firmware was rebuilt. No
serial/acoustic/touch/USB-controller/multiplayer cadence or device 30 FPS
acceptance is asserted, and no prior failed hardware result was rewritten.
