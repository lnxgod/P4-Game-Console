# Repository cleanup audit — 2026-10-06

The behavior-preserving cleanup is complete on `codex/repository-cleanup-execution`,
based on `origin/main` at `615321ff45020387fba96572b5b42376b0d1be52`.
The canonical Tab5 firmware and 17-cartridge native bundle build and verify.
No hardware was flashed, no remote branch was deleted, and nothing was published
or merged. Detailed hashes, branch tips, PRs and check results are in
[the evidence record](../test-runs/2026-10-06-repository-cleanup.json).

- [x] **Git baseline, PRs and local work.** Fetched `origin`; GitHub confirms
  `main` is default and all PRs #1–#13 are merged, with no open PRs. An already
  merged PR does not make subsequent branch commits obsolete: the source
  `codex/m5stack-tab5` checkout was clean at `0b2376b`, but its two Byte Buddy /
  content-evidence commits are absent from this baseline. They remain intact.
  This cleanup uses its own attached managed worktree.
- [x] **Lua retirement.** Searched the complete tracked tree, including hidden
  skills, manifests, dependencies, build hooks, tools and documentation. No Lua
  game, interpreter, authoring stack or skill remains. Removed the launcher's
  stale “P4CART LUA PENDING” promise, unused shell transfer class and old capacity
  comment; changed desktop/preview fixtures to native `.P4G` examples and current
  storage folders. Corrected the old scanner description and future IGM proposal.
  Wire class 4 remains reserved and rejected; native-only guards and tests still
  prevent old cartridges from being installed or mutated. Historical evidence
  and users' existing archived files are preserved.
- [x] **Unused testing tools.** Removed the uncalled substring parser from
  `gamepad-diag-capture.py`, the uncalled legacy central-flash verifier from
  `verify-audio-direct-diag.py`, and the unused `p4g_name_valid` wrapper. Removed
  the 0.4.85/0.4.86 capture wrappers: they only set obsolete fixed version/hash
  values and invoke the retained configurable 0.4.84 capture tool; no maintained
  command, test or installer consumes them. Their only tracked references were
  historical whole-tree hash inventories. Those inventories are unchanged.
- [x] **Dead code, assets and configuration.** Removed LORD's superseded
  `lord_title_art.h`, its generated ANSI preview and unused `build_title_art.py`.
  Current C sources use `lord_illustrated_title.h`; original source artwork,
  prompts, provenance and rejection/acceptance records remain. Fixed the SDL
  Console OS preview's stale descriptor initializers, which failed the existing
  `-Werror` build after artwork fields were added. No game rules, active game
  rendering, manifests, IDs, saves or network protocols changed.
- [x] **All repository skills and AGENTS.md.** Read all 14 skills, their
  references and UI metadata; checked commands/paths against current code and
  Make targets. Corrected Tab5 USB-A guidance, the optional multiplayer
  capability, Elecrow-only capacity wording, repetitive/conflicting test scope,
  historical app-count claims and a cross-skill reference. Clarified implemented
  Waveshare display/audio routing and Elecrow framebuffer/PDM behavior. Marked
  the Elecrow installation snapshot with its date and made its skill UI explicitly
  Elecrow-specific. No abandoned skill resource was found. `installos` still
  asks about SD, provisions verified Doom, offers Chex only with explicit SD
  selection, and accurately reports the unimplemented Tab5 internal-storage route.
- [x] **Validation.** Existing parser, restore, transfer, native-source, registry,
  host preview and affected game checks pass. The full Tab5 target and bundle
  verifier pass with the pinned SDK and unchanged component locks. The one
  historical Elecrow artifact-dependent test is listed below as unexercised.
- [x] **Evidence and review.** Three implementation commits separate Lua/preview
  cleanup (`8ae63bb`), unused tools/assets (`7819597`) and skills (`3b28df9`).
  This audit and its JSON record preserve the removal rationale, original hashes,
  source binding, retained work and acceptance limits. Logs and the full patch
  remain ignored under `build-host/repository-cleanup/`.

The inventory covered all **2,560 baseline tracked files**. Consumer tracing
included dynamic Python imports, CLI/Make/CMake entrypoints, manifest-generated
asset names and immutable digest dependencies. It is a structural and consumer
audit, not a formal proof that every possible runtime branch is reachable.

| Scope | Baseline files | Disposition |
| --- | ---: | --- |
| `.agents`, root instructions/build/config/docs | 42 | All skills/instructions, defaults, references and locks reviewed; targeted corrections above |
| `apps`, `components` | 920 | Build/source consumers traced; shell remnants corrected; board and reusable-service boundaries retained |
| `games` | 503 | Manifest/source/generated-asset consumers traced; obsolete LORD outputs removed; active game work preserved |
| `scripts`, `tools` | 273 | Test/workflow/import/hash consumers traced; only proven unused surfaces removed |
| `docs`, `design` | 97 | Maintained links and Lua claims checked; design galleries and manifest-driven sources retained |
| `hardware`, `test-runs` | 354 | Historical acceptance, rejected candidates, bindings and recovery evidence preserved |
| `third_party`, `ports`, `game-data` | 371 | Pinned provenance, ports, licenses and existing original game data retained unchanged |

Eight local branch refs were deleted only after verifying ancestry to the
baseline and absence from every worktree registration. Full recovery SHAs are
in the JSON record.

| Deleted local branch | Tip |
| --- | --- |
| `codex/doom-arena-release` | `3670f7892d74` |
| `codex/doom-multiplayer-engine-barrier` | `a36e523a8412` |
| `codex/game-manager-usb-updates` | `55395edd4ca8` |
| `codex/program-manager-file-manager` | `a767e1a533d7` |
| `codex/usb-game-storage` | `50502ce24b68` |
| `codex/usb-game-storage-device-install` | `d35bf86685b6` |
| `codex/usb-game-storage-program-manager` | `5348cd2025e6` |
| `codex/wireless-multiplayer` | `5ec54214193d` |

The unmerged Byte Buddy H1, Skyline, game-suite, game-changers, Tab5 and Tide Maze
tips remain. Both Byte Buddy checkouts, the active Tide Maze checkout, the source
checkout used by “Analyze Whackey Racers,” and the separate active cleanup
checkout remain intact. All remote refs and all eight missing/prunable worktree
registrations remain; some retain unmerged or detached historical work. Local
`main` was not reset or advanced behind another checkout's back.

Some apparently unused items have real consumers. The E6 installer's two
uncalled helper functions remain because the **whole file hash** is required by
seven frozen installer/verifier paths. A trial removal exposed that dependency;
the exact original bytes were restored and the affected checks rerun. Current
game/console host runners, Realm hub tests, hardware fixtures, old exact-artifact
installers and recovery transports remain supported verification/evidence paths.
The Maze illustration and LORD native/fallback previews have manifest/converter
consumers. Vendored README image links refer to five absent upstream screenshots;
editing pinned upstream bytes to repair those cosmetic links is outside this
cleanup. There are **zero broken maintained relative Markdown links**.

| Check | Final result |
| --- | --- |
| `make verify` | PASS — existing ESP-IDF 5.5.3, commit `2c211b236707889e8400c4dc5644dd5c4ee071e0`, existing Python 3.11 environment |
| `make game-registry-check` | PASS — 17 enabled native games; source/seed/board/resource/lineage/generator checks |
| `make console-shell-host`, `make p4-desktop-host` | PASS — 7/7 and 1/1 |
| `make console-os-host` | PASS — 8/8, including SDL dummy ASan/UBSan smoke |
| `make p4-content-host` | PASS — 3/3 CTest; 8 content and 5 native-bundle tests |
| `cmake -S games/lord -B build-host/lord -G Ninja`, build, CTest | PASS — 3/3 including presentation and gameplay trace |
| All 46 `scripts/tests/test-*.py` entrypoints | 45 PASS; 1 lacks the separate historical Elecrow build artifact |
| `test-project-env.sh`, `test-app-readback.sh` | PASS |
| Skill validator | PASS — 14/14; referenced paths and metadata reviewed |
| Syntax / links / whitespace | PASS — 463 JSON, 214 project Python, 69 project shell files; maintained Markdown links; `git diff --check` |
| `make console-os-tab5-idf` | PASS — all target host dependencies; full firmware and 17-cartridge bundle verified |

The worktree initially lacked a selected SDK/Python environment; pointing to the
existing pinned checkout and its `idf5.5_py3.11_env` resolved that without an
installation or upgrade. Two USB-overlay tests needed managed components; they
passed after the Tab5 build populated the locked dependencies. The preview
initializer error was corrected and its complete target passed afterward.

`test-console-os-game-manager-migrate.py` requires verified historical Elecrow
artifacts under `apps/console_os/build`; that directory is absent in this fresh
worktree. It remains in the existing Elecrow build workflow. Tab5 output cannot
satisfy that exact-board test, and no sealed evidence was rewritten to make it
pass. Additional board builds, interactive gameplay and hardware testing are not
claimed. No gameplay behavior changed; actual-device cadence and existing failed
device acceptance remain unchanged.

The verified Tab5 candidate is 4,317,760 bytes, SHA-256
`2a2cde5dd8cf46e1366f318f0db1a5c4755078b8c5af2cafefe8e70b835f7959`.
It was built from the cleanup code before the local commits, so its embedded
build label is the baseline commit; the JSON records the modified-source binding.
It has **no flash authorization or hardware acceptance**. WADs, firmware,
cartridges, build products and recovery images were not added to Git.
