# Consolidated repository cleanup audit — 2026-10-06

The source cleanup is complete on **`codex/repository-cleanup-execution`**,
based on `origin/main` at `615321ff45020387fba96572b5b42376b0d1be52`.
The consolidated implementation is **`94edf8ca0bd13bddcb69635ac0530f519c09258b`**;
the following evidence commit changes only this report and its
[JSON record](../test-runs/2026-10-06-repository-cleanup.json).
No hardware was accessed or flashed, and nothing was published or merged.

The other completed cleanup branch, `codex/repository-cleanup` at
`5b585f2c33d0534353d310d88fd211c55e92e07d`, remains clean and recoverable in
worktree `1f75`. Its [labeled historical report](REPOSITORY_CLEANUP_2026_10_06_1F75.md)
and [unchanged JSON evidence](../test-runs/2026-10-06-repository-cleanup-1f75.json)
are preserved. Those build hashes describe that branch only. The earlier
canonical report remains recoverable at `78d3d1a`; this audit supersedes its
incorrect Waveshare audio routing and gamepad-parser removal conclusions.

## Requirement-by-requirement result

| Requirement | Final evidence and disposition |
| --- | --- |
| Baseline, PRs, branches and worktrees | Main baseline and all 13 merged PRs recorded; final GitHub recheck finds no open PRs. Eight proven merged, unattached local refs were removed earlier. All remote refs, active/unmerged work and registered worktrees remain. |
| Retired Lua stack | Complete tracked-tree audit finds no Lua game, runtime, dependency, authoring tool or skill. Removed stale shell enum/display branch/promise and converted fixtures to native `.P4G`. Reserved wire class 4 and native-only rejection tests remain. |
| Unused tools and dead assets | Removed two uncalled Python helpers, two superseded capture wrappers and three inactive LORD art/converter files. No live consumers remain. Exact-file recovery dependencies and historical records are preserved. |
| All skills and AGENTS.md | All 14 skills and supporting resources reviewed. Final corrections cover native multiplayer, Tab5 SD/USB workflows, Waveshare audio/display routing, Elecrow migration/PCM/framebuffer history, and proportional testing. Fourteen schema validators pass; code tracing substantiates corrected claims. |
| Protected contracts | No change against baseline to toolchain/component locks, hardware profiles/backups/authorizations, retired IDs, manifests, gameplay/save/network code, vendor source, ports or game-data. |
| Validation | New recovery/native-only checks, shell and both preview modes pass. Consolidated Tab5 build and 17-cartridge verifier pass. Earlier unaffected checks remain source-bound historical evidence; one Elecrow artifact-dependent test remains unexercised. |
| Reviewability | All 26 paths differing between the two completed branches have an explicit disposition in JSON. Original branches/reports remain recoverable; final implementation and artifact hashes are bound separately. |

## Scope and consolidation decisions

The inventory covers all **2,560 baseline tracked files**: root/instructions
and `.agents` 42; apps/components 920; games 503; scripts/tools 273;
docs/design 97; hardware/test-runs 354; third_party/ports/game-data 371.
Tracing included Make/CMake, dynamic imports, command entrypoints, manifests,
generated assets and whole-file digest consumers. This is a structural and
consumer audit, not a formal runtime-reachability proof.

The branch comparison retained justified changes from both implementations:

- Corrected Waveshare audio to the actual chain: Console OS selects the counted
  adapter in `apps/doom_embedded_touch_audio/components/platform_audio`, which
  calls `components/platform_audio_es8311`. The component README now separates
  that active backend from its historical Elecrow codec investigation. ES7210
  microphone support remains unclaimed.
- Imported the recorded Elecrow dual-OTA migration: its app is at `0x20000`;
  the earlier `0x10000` folder image is historical. The migration's startup PASS
  does not close its pending J16 cartridge/OTA/operator acceptance.
- Preserved the canonical native-game skill's **optional** `multiplayer-session`
  capability and enabled Tab5 USB-A host candidate. The other branch's required
  capability and disabled-host wording conflicted with `GAME_SDK.md` and
  `scripts/build.sh`.
- Clarified AGENTS.md's current Tab5 SD requirement and art-skill routing;
  corrected Console OS version 0.56 and Tab5's one-time 3/10 audio defaults.
  Preserved existing WAD, protected-content and exact-board restrictions.
- Added two missing artwork initializer tails in the Tab5 preview, completing
  the earlier generic-preview repair. Both previews now use `GAMES`, `SAVES`
  and `MAZE.P4G`; Games shows `SELECT A NATIVE P4G GAME`.
- Extended native-only source checks to reject both retired runtime directories.
  Kept canonical precise LORD title-source provenance and the explicitly future
  native IGM proposal. Equivalent prose and whitespace variants were not duplicated.

## Removals and deliberate retention

| Removed item | Consumer proof |
| --- | --- |
| `_verify_central_flash_path_legacy` | No callers; active `verify_central_flash_path` remains. Other Python AST is identical to baseline. |
| `p4g_name_valid` | No callers; active validation uses `game_name_valid`. Other Python AST is identical to baseline. |
| Capture wrappers `0.4.85` and `0.4.86` | No maintained caller; only obsolete fixed version/hash overrides around retained configurable `0.4.84` implementation. |
| LORD `lord_title_art.h`, `title-background-ansi.png`, `build_title_art.py` | No active include/build/converter consumer; current C uses `lord_illustrated_title.h`. Original art, prompts and provenance remain. |
| Shell P4CART enum/display label and Lua-pending notice | Only unreachable shell presentation consumed the enum; protocol rejection remains at the transfer boundary. |

The gamepad capture parser's trial removal was **reversed**. Although uncalled,
its containing file is revalidated by recovery gate inventories. It is now
byte-identical to baseline, SHA-256
`941792e627fc49e303353d38578ae1b30426f99cc15973bc568ebac01af6a473`.
The two dormant E6 installer helpers likewise remain because seven frozen
installer/verifier paths consume that complete file's hash. No sealed digest
was changed to accommodate cleanup.

Retained native-only guards protect archived user files and reject unsupported
cartridges. Historical hash inventories, recovery transports, accepted and
rejected hardware evidence, current host/Realm tools and manifest-driven Maze
and LORD artwork remain. Five missing screenshot links in vendored READMEs
remain unchanged with their pinned upstream bytes. All 199 maintained relative
Markdown links resolve.

## Branch and work preservation

These eight deleted local refs remain recoverable through their merged tips:

| Local branch | Tip |
| --- | --- |
| `codex/doom-arena-release` | `3670f7892d74` |
| `codex/doom-multiplayer-engine-barrier` | `a36e523a8412` |
| `codex/game-manager-usb-updates` | `55395edd4ca8` |
| `codex/program-manager-file-manager` | `a767e1a533d7` |
| `codex/usb-game-storage` | `50502ce24b68` |
| `codex/usb-game-storage-device-install` | `d35bf86685b6` |
| `codex/usb-game-storage-program-manager` | `5348cd2025e6` |
| `codex/wireless-multiplayer` | `5ec54214193d` |

Final recheck confirms each is an ancestor of the baseline and has no worktree
attachment. Full SHAs are in JSON. No additional refs were deleted during
consolidation. All retained branch tips preserve their original commits, all
remote refs are unchanged, and all eight prunable registrations remain.

The source checkout is still clean at `0b2376b`, including its two commits not
in the main baseline. Tide Maze's ongoing modified/untracked work, both Byte
Buddy checkouts, Skyline/game-suite/game-changers branches, Wacky Wheels work
and both cleanup checkouts remain intact. Local `main` was not moved.

## Validation and artifact binding

Only checks warranted by the consolidation were rerun:

| Check | Result |
| --- | --- |
| Gamepad diagnostic gate/state/install/restore/flash-route tests | All five entrypoints pass after restoring exact capture-script bytes; no hardware access. |
| Native-only source contract | PASS; 17 enabled native games and strengthened retired-directory guards. |
| Shell host | 7/7 PASS, ASan/UBSan. |
| Generic SDL preview | 8/8 PASS, including dummy-video smoke. |
| Tab5 SDL preview | 8/8 PASS with `P4_CONSOLE_HOST_TAB5=ON`, including dummy-video smoke. |
| Skills, JSON, Python and links | 14/14 skills, 465 JSON files, 212 maintained Python files, 199 maintained links; no failures. Python count excludes both vendor and port trees. |
| Removal/protection checks | Both remaining Python ASTs match after removing only named definitions; no live removed-item consumers; protected paths unchanged; whitespace clean. |
| Locked Tab5 build and verifier | PASS from clean implementation commit `94edf8c`; all 17 cartridges and update package verified. |

The system Python lacked PyYAML; skill validation reused the existing pinned
IDF Python environment without installation. The firmware build reused the
already verified ESP-IDF 5.5.3 checkout at commit `2c211b2` and its existing
`idf5.5_py3.11_env`. Focused host checks had passed, so the build wrapper and
Tab5 verifier ran directly without repeating unrelated Make dependencies.

The build log confirms `console_shell.c` recompiled. The resulting application
is still **4,317,760 bytes**, SHA-256
`2a2cde5dd8cf46e1366f318f0db1a5c4755078b8c5af2cafefe8e70b835f7959`.
The regenerated **4,318,016-byte** `P4UPDATE.P4U` records build ID
`94edf8ca0bd1`, SHA-256
`44ceaaeb95abc77cff3b03794988f5b27796c2b7bb9546e80164400f09c79b59`.
The application project version is 0.56. JSON binds the consolidated source
tree, diff, code snapshot, logs and package hashes; identical application bytes
do not substitute for that source binding. Artifacts and logs remain ignored
under `apps/console_os/build-tab5` and `build-host/repository-cleanup`.

Earlier canonical validation remains recorded separately: pinned environment,
registry, desktop/content/LORD tests, two shell scripts and 45 of 46 script
entrypoints passed. `test-console-os-game-manager-migrate.py` exited with the
missing separate historical Elecrow artifact prerequisite. It remains
unexercised, not a PASS or a Tab5-compatible test.

No new interactive gameplay, alternate-board build matrix, serial, touch,
acoustic, controller, multiplayer or device-cadence acceptance is claimed.
Existing failed and pending acceptance remains unchanged. The candidate has
**no flash authorization or hardware acceptance**. No firmware, cartridge,
WAD, recovery image or generated build output was added to Git.
