# Obsolete test follow-up — 2026-10-06

This follow-up starts at `a89342dc543c4ce05c6d2503b9fea6556064cade`
in the isolated `codex/obsolete-tests-follow-up` worktree. Implementation
commit: `55cf5cd`. The original cleanup checkout is preserved. Tab5 remains
the primary target; Elecrow firmware work is excluded, including after the
user's clarification that Elecrow is not relevant.

[Machine-readable evidence](../test-runs/2026-10-06-obsolete-test-cleanup.json)
contains the complete inventory, per-script production/support references,
source checks, test results and changed-file hashes. This is a static audit of
all maintained test surfaces plus execution of the relevant existing checks,
not a claim that every unchanged suite or hardware path was executed.

## Removed and narrowed coverage

| Surface | Decision and evidence |
| --- | --- |
| `scripts/tests/test-native-only-sources.py` | Removed. It asserted deleted Lua directories/symbol sources stayed absent and hard-coded three enabled game identities. The current registry validates native format, safe C source lists and source existence; its existing tests and the real 17-game catalog still pass. |
| `scripts/tests/test-p4cart-seed-registry.py` and `scripts/p4cart_seed_registry.py` | Removed together. The former generator had become an exit/check-only compatibility guard, called only by this test and `game-registry-check`. No build, installer or verifier still consumed it. Current board verifiers retain their metadata, linked-component and seed-bundle rejection gates. |
| `scripts/tests/test-capture-waveshare-console-os-scroll-boundary.py` and its capture script | Removed together. The required `P4_CONSOLE_OS SCROLL_GESTURE_GDMA_FRAME_BOUNDARY` marker has no producer in maintained firmware. The tool had no maintained caller besides its synthetic test; remaining mentions are historical source inventories and previous audit results. |
| Native SD bundle test | Renamed to [test-native-sd-bundle.py](../scripts/tests/test-native-sd-bundle.py). Removed only the test of the exit-only old Lua installer stub. Kept native package/resource/update validation, mixed-bundle refusal before target access, preservation of archived user cartridges and corrupt-payload rejection. The compatibility stub itself is unchanged. |
| Content/transfer tests | Removed assertions about absent old Python attributes. Kept rejection before serial/filesystem mutation, invalid format/class/magic handling, atomic-copy behavior, native/resource ordering and preservation of old user content. |
| Native board verifier tests | Removed source-token presence/absence checks and constant legacy report-field checks. Kept 73 cases exercising current verifier gates, including rejected metadata, linked Lua components/symbols, mixed bundles and storage-role transitions. |
| Elecrow Game Manager migration artifact test | Retained as an explicit `make console-os-game-manager-migration-check`; removed from the normal Elecrow build recipe. The completed one-time migration is not a prerequisite for ordinary successor builds. Its documented recovery route still exists, so missing local artifacts were not treated as evidence that the feature is dead. No Elecrow build or artifact acceptance was attempted. |

No replacement tests merely assert these deletions. There are no remaining
maintained code/runner consumers of the deleted paths. Historical evidence
and prior cleanup reports retain their original text.

## Audit coverage

The base inventory contains **306 test/support source files across 67 suite
owners**, plus **69 CMake files with 164 `add_test` declarations**. Declarations
with loops or variables can expand to multiple tests. All 293 resolved literal
C/C++ references in the audited test CMake files exist. Seven variable-based
references belong to the intentionally generated Wacky port.

| Maintained surface | Result |
| --- | --- |
| `scripts/tests`, shell helpers and fixtures | Every entrypoint was classified against its current imported tool/CLI and runner. Three obsolete entrypoints were deleted; 44 self-contained entrypoints passed. The retained migration artifact check remains separately scoped. The fake esptool and native starter-motion fixtures still have consumers. |
| 40 component test owners | Retained. Current components own their implementations and CMake registrations: native API/package/storage/update boundaries, rendering/UI, input, radio/multiplayer, timing/audio and board services. Classic Console Shell tests still serve the non-nextgen profiles. |
| 16 native game test owners | Retained. Every suite has a current native manifest and C sources. Disabled/draft status does not retire a supported development game. Presentation, save, direct-touch, multiplayer and lifecycle tests remain. |
| Seven app test owners | Retained. Console cartridge timing, Core2 dice accessory, Doom/audio and diagnostic policies still have production implementations. No historical test name alone was treated as proof of obsolescence. |
| SDL3 game and console hosts | Retained current smoke, input, audio timing and failure/exit tests. The console smoke is registered directly against its runner. |
| Realm hub | Retained protocol, persistence and multi-client campaign tests for the current server. |
| Wacky and Quake host harnesses | Retained. Their generated port or local game data are explicit prerequisites. The Wacky candidate is active development; Quake registers usage and optional data-backed smoke checks. Missing local content is not retirement. |
| Make/CMake/shell runners | Reviewed registrations and source references. Updated the two affected Make paths and isolated the migration check. `scripts/check.sh` remains unchanged because it participates in frozen diagnostic gate inventories. No GitHub Actions workflow is tracked. |
| Skills and documentation | Scanned the 58 tracked instruction/Markdown files under `.agents/skills`, `docs`, plus AGENTS/README. No skill recommends a deleted test. Corrected Tab5's stale claim that the E5 test requires an open flash gate; documented the explicit migration artifact check. |

## Why older safety tests remain

The user-facing focus is Tab5. Retaining old recovery dependencies does not
add Elecrow builds or hardware work to this change.

- **E3** still tests the closed generic flash gate and one-shot reservation
  lifecycle used by `scripts/flash.sh` and
  `scripts/doom-e3-one-shot-state.py`. It uses temporary state and synthetic
  authorization copies; it neither reopens the committed gate nor needs the
  old firmware artifact.
- **E5/E6** protect current retained-UART transaction/recovery code. E6 loads
  the E5 installer and restore helper with frozen whole-file digests.
  `console-os-install.py` and `console-os-usb-install.py` in turn load the
  exact E6 transport. The Program Manager updater, File Manager updater and
  migration path extend that chain. Their tests exercise descriptor/identity
  binding, short reads, partial writes, full-span readback, durable recovery
  and launch refusal. E5/E6 verifiers/installers also explicitly include test
  files in required evidence inventories.
- **Gamepad diagnostic recovery** still supplies frozen restore/transport
  helpers. Its gate inventory includes `scripts/check.sh`, the fake esptool
  fixture and its tests. No whole-file binding was rewritten.
- **Native-only boundaries** remain tested in the real C wire parser,
  native package validator, current host transfer/content commands, SD
  installer and board verifiers. Unsupported Lua inputs remain rejected.

The three broad Tab5 0.55/0.56 authorization inventories mention some removed
test/tool paths as historical source snapshots. They are preserved unchanged;
this cleanup does not reissue or extend any installation authorization.

## Verification

- **44/44** self-contained script entrypoints passed using the existing
  ESP-IDF Python 3.11 environment with PyYAML 6.0.3.
- `make p4-content-host`: **3/3** CTest cases, including the real native-only
  wire parser under address/undefined sanitizers; **7** content and **4** SD
  bundle Python cases passed.
- `make p4-game-package-host`: **1/1** CTest case passed.
- `make game-registry-check`: passed, including **17** enabled native games,
  **73** board-verifier cases, resources, protected lineage, scaffolding and
  warm-boot capture tests.
- Make dry runs confirmed ordinary Elecrow builds have zero migration-check
  calls and the explicit artifact-check target has exactly one. This is
  routing verification only, not migration acceptance.
- Frozen helper/test bytes checked against the base remain identical;
  removed-path consumer review and `git diff --check` passed.

The overlay tests needed three ignored, pinned USB 1.5.0 source files. Copies
from the existing local dependency cache matched their exact recorded hashes;
no SDK/component upgrade occurred.

No app, component, game, port, host runtime, dependency lock or firmware input
changed. No firmware build, flash, hardware access, publication, or merge into
main/the source checkout was performed. Full repository builds and unrelated
unchanged host suites were not rerun.
