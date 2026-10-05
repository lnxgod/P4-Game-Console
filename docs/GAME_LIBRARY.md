# Game library

Tab5 is the primary Console OS target. The default native bundle contains
13 games and three utilities. Doom and Chex Quest are separate engine entries
whose game data stays local. Teaching templates and unfinished drafts do not
ship in the default library.

## Find a game

The launcher sorts categories and games alphabetically, ignoring letter case.
Folders precede apps; All Programs remains the first root shortcut. Sorting
changes only presentation: launching and saved progress still use stable IDs.

| Category | Games |
|---|---|
| Adventure | Byte Buddy, Red Dragon |
| Arcade | Frog Hop, Maze Chase, Space Invaders |
| Cards | Color Clash, Rummy 500, Solitaire, Texas Hold'em |
| Platform | Skyline Leap |
| Shooters | Chex Quest, Doom; require their separate local game data |
| Sports | Air Hockey |
| Tabletop | Checkers, Yahtzee |

Calculator lives in System / Tools. Input Monitor and Sound & Motion live in
System / Tests; Sound & Motion checks animation and tones, while the built-in
Sensors page shows the hardware motion sensor. System tools share one Control Panel, including in All Programs. Overview
combines battery, storage, motion and clock status; Preferences adjusts saved
startup/game sound directly. Controls, Storage and Connections group everyday
actions; Advanced holds detailed diagnostics. Back returns to the same section.
Internal IDs and destructive-action confirmations stay unchanged.

## Current ranking

This is a provisional source-review ranking of completeness, replay value and
fit for a touch-first Tab5. All 13 retained native games passed an eight-frame
SDL3 AddressSanitizer/UndefinedBehaviorSanitizer smoke run. That is a runtime
sanity check, not a scored play session or proof that every game works on A/B.
The order below does not override alphabetical launcher sorting.

| Rank | Game | Why keep it / next limitation |
|---|---|---|
| 1 | Byte Buddy | Persistent companion, progression, care, upgrades and original art; strongest long-session project. |
| 2 | Red Dragon | Substantial adventure, persistent realm and ANSI presentation; needs a full on-device progression pass. |
| 3 | Color Clash | Complete color-card loop with CPU opponents and readable card art. |
| 4 | Rummy 500 | Full meld/scoring loop with CPU play; card readability deserves device testing. |
| 5 | Solitaire | Complete familiar solo card game; a clear fit for touch. |
| 6 | Texas Hold'em | CPU seats, betting and side-pot rules; more complex to learn and test. |
| 7 | Air Hockey | Direct touch play and a CPU fallback; touch feel matters more than a successful build. |
| 8 | Yahtzee | Complete dice and score-card rules, same-device pass-and-play; needs more than one local player. |
| 9 | Skyline Leap | Four-stage platform adventure with a distinct theme; difficulty tuning remains a play-test task. |
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

- Asteroids, Asteroids 2 and Breakout: removed at the owner's request. Their
  source history remains in Git; their public IDs, launcher IDs and filenames
  are permanently reserved in `games/retired.json`.
- Bounce Lab and QR Dodge: removed from the repository and both tablets.
  Package/QR tests use a minimal inert fixture, never an installed demo.
  The default Lua seed list is empty.
- `P4 GAME API V1` was a repeated developer subtitle, not another game.
  Retained games now describe what the player does instead.

Renaming never changes cartridge filenames, game/save IDs or network protocol
IDs. In particular `LORD.P4G` now displays Red Dragon, and existing `P4_*.P4G`
files display their ordinary game names.

## Keep future additions worthwhile

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
`.P4CART` removal is supported too. Saved progress and game WADs are preserved.
Use firmware with this protocol extension; old firmware rejects the request.
