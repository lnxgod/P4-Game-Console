# Game library

Tab5 is the primary Console OS target. The enabled native bundle contains
12 main-library games, two available works in progress, and three utilities.
Doom v1.9 shareware is the default engine-data title; provisioning includes its
verified WAD download when missing. Chex Quest is an optional SD add-on and
requires an explicit opt-in plus its verified WAD and patch. Game data stays
local. New game creation and distribution are native-only; custom 2D/3D
renderers are allowed through the stable Game API. The old Lua runtime, games,
authoring tools and skill are removed. Unpublished drafts do not ship by default.

## Find a game

The launcher sorts categories and games alphabetically, ignoring letter case.
Folders precede apps; All Programs remains the first root shortcut. Sorting
changes only presentation: launching and saved progress still use stable IDs.

| Category | Games |
|---|---|
| Arcade | Blast Circuit, Frog Hop, Maze Chase, Space Invaders, Tide Maze |
| Cards | Color Clash, Rummy 500, Solitaire, Texas Hold'em |
| Shooters | Doom; verified shareware WAD installed by default |
| Optional | Chex Quest; explicit SD opt-in, verified WAD and patch required |
| Sports | Air Hockey |
| Tabletop | Checkers, Yahtzee |
| WIP (Work in progress) | Byte Buddy, Red Dragon |

Byte Buddy 5.0.0, Red Dragon 1.9.0 and Maze Chase 1.1.1 (the Pac-Man-style
game) are native C `.P4G` games. The initial-launch remixes update Byte Buddy's
city/panels and Red Dragon's native ANSI presentation while preserving their
game identities and saved progress. See their game READMEs for exact acceptance.

Byte Buddy and Red Dragon stay enabled and available under
`GAMES/WIP`. This is a development status, not a claim that they are complete or
an instruction to remove them. Their game IDs, cartridge names, saved progress
remain unchanged. Category changes travel in their rebuilt
cartridges. The optional Chex category is an OS catalog change; seeing its tile
does not prove its game data is present or verified.

Calculator lives in System / Tools. Input Monitor and Sound & Motion live in
System / Tests; Sound & Motion checks animation and tones, while the built-in
Sensors page shows the hardware motion sensor. The Tab5 Games view excludes these utilities; Settings / System provides their
launch buttons. The classic shell groups system tools in one Control Panel,
including in All Programs. Overview
combines battery, storage, motion and clock status; Preferences adjusts saved
startup/game sound directly. Controls, Storage and Connections group everyday
actions; Advanced holds detailed diagnostics. Back returns to the same section.
Internal IDs and destructive-action confirmations stay unchanged.

Blast Circuit joins Arcade as a locally play-tested and build-tested
cartridge candidate: four distinct runners, three arenas, an editor and
host-to-peer custom-level sharing, symmetric arenas with permanent
pillars, three-to-five starting barriers, one/two-hit and lingering-fire crates,
and a four-minute playlist of three public-domain Bach arrangements. Its
cartridge passes the console package/ELF validator. Speaker quality, persistent device saves and
real four-device matches remain unverified; it is not included in the
historical ranking below. See [its verification record](../games/blast_circuit/LOCAL_TESTING.json).

## Historical review

The table below preserves an earlier provisional source-review ranking of
completeness, replay value and fit for a touch-first Tab5. It is not a current
release-readiness ranking; the historical WIP entries were unfinished. All 13 retained native games passed an eight-frame
SDL3 AddressSanitizer/UndefinedBehaviorSanitizer smoke run. That is a runtime
sanity check, not a scored play session or proof that every game works on A/B.
The order below does not override alphabetical launcher sorting.

| Rank | Game | Why keep it / next limitation |
|---|---|---|
| 1 | Byte Buddy | WIP: persistent companion and Signal City; retained for further development. |
| 2 | Red Dragon | WIP: substantial adventure and persistent realm; needs a full progression and presentation pass. |
| 3 | Color Clash | Complete color-card loop with CPU opponents and readable card art. |
| 4 | Rummy 500 | Full meld/scoring loop with CPU play; card readability deserves device testing. |
| 5 | Solitaire | Complete familiar solo card game; a clear fit for touch. |
| 6 | Texas Hold'em | CPU seats, betting and side-pot rules; more complex to learn and test. |
| 7 | Air Hockey | Direct touch play and a CPU fallback; touch feel matters more than a successful build. |
| 8 | Yahtzee | Complete dice and score-card rules, same-device pass-and-play; needs more than one local player. |
| 9 | Skyline Leap | Removed from the install list at the owner’s request; unfinished platforming is not suitable for the library. |
| 10 | Space Invaders | Focused, complete wave shooter with scoring and restart; smaller scope but a sensible arcade game. |
| 11 | Frog Hop | Crossing hazards, moving logs, five goals and increasing rounds; compact but complete. |
| 12 | Maze Chase | A compact classic maze loop; limited variety, still useful as a small finished arcade game. |
| 13 | Checkers | Proper captures, kings, chained jumps, win/draw rules; no CPU, so it is mainly for two people. |

Doom is a separate platform benchmark. A has operator-confirmed startup sound,
Doom music and effects on the speaker repair. B sound and new lifecycle checks
need their own evidence. Linked-console protocols in a game's source do not
prove that every Tab5 transport is available; C6 radio and USB-A HID remain
outside the accepted port.

## Removed from the product library

- Star Sprout: both incomplete student prototypes are absent from Tab5 A and B.
  Their exact identities and hashes are reserved in `games/retired.json`; see
  [the removal receipts](../test-runs/2026-10-05-star-sprout-removal.json).

- Skyline Leap: disabled in the default install list and removed from the tablets
  at the owner’s request because the game is unfinished. Source and its existing
  identity are retained for possible development; it must meet the complete-loop
  and fluid-action release requirements before being enabled again.

- Asteroids, Asteroids 2 and Breakout: removed at the owner's request. Their
  source history remains in Git; their public IDs, launcher IDs and filenames
  are permanently reserved in `games/retired.json`.
- Bounce Lab and QR Dodge: removed from the repository and both tablets.
  The remaining Lua authoring stack, including its fixtures, tools and skill,
  is removed rather than retained as a migration path.
- `P4 GAME API V1` was a repeated developer subtitle, not another game.
  Retained games now describe what the player does instead.

Renaming never changes cartridge filenames, game/save IDs or network protocol
IDs. In particular `LORD.P4G` now displays Red Dragon, and existing `P4_*.P4G`
files display their ordinary game names.

## Keep future additions worthwhile

Apply the [ESP32-P4 performance contract](GAME_PERFORMANCE.md) from the first
playable build: native 768×480 with fallback, 60 FPS target and actual-device
30 FPS release floor. A locally tested or installed candidate is not yet a
performance-qualified release. Keep unmeasured candidates/WIP honestly labelled;
record exact package, OS, unit and sustained active-play cadence before claiming
release readiness. Action games must move continuously and respond fluidly on the tablet; visible
whole-tile jumps, repeated stalls and unfinished demo-like play fail the release
bar. Keep grid rules internal and interpolate visible actors. Card and board
games may retain deliberate turn-based steps. Fix a failing action title or
remove it from the install list and installed library; do not keep it merely to
increase the game count. Existing source can remain disabled for development.

1. Start with the player's idea. Use a reference only where it helps; do not
   make every game a reskin of the same demo.
2. Create a draft with `scripts/new-game.py`; its `enabled: false` keeps it
   out of release bundles. `make play-game GAME=<slug>` previews drafts locally.
3. Finish a complete loop: start, meaningful controls, challenge or progress,
   an appropriate outcome, retry/continue, and reliable return to the launcher.
   A sandbox or companion can have an open-ended loop instead of a win screen.
4. Use a readable Title Case name (15 ASCII bytes maximum), a specific short
   description, and one of the current categories. Keep diagnostics in System.
5. Play the real sources locally, test important rules and failure paths, and
   check touch, optional audio, pause/restart and Back. Controllers consume the
   OS-normalized input API; games never own device drivers.
6. Replace placeholder descriptions, set `enabled: true`, validate/package,
   then install through native USB-C. Record device acceptance separately.

## USB update and removal

Use the [Tab5 guide](boards/M5STACK_TAB5.md) and
[Game SDK](GAME_SDK.md). Keep the card inserted:

```sh
python3 scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <port>
python3 scripts/p4-transfer.py remove /absolute/path/EXACT.P4G --port <port>
```

The remove command validates the local file and requires the same name, size
and SHA-256 on the device. It refuses changed or unknown versions, arbitrary
files and symlinks. Supply a matching `.P4R` before its `.P4G` when present;
saved progress and game WADs are preserved.
Use firmware with this protocol extension; old firmware rejects the request.

Tide Maze 0.1.0 is a locally exercised development candidate with three flooded
marble labyrinths and two-console co-op. Its new optional six-axis motion service
requires a motion-aware OS (Tab5 0.50 candidate); physical tilt, audio, launcher
refresh and linked-tablet acceptance remain pending. See
[its verification record](../games/tide_maze/LOCAL_TESTING.json).
