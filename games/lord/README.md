# LORD — P4 ANSI Door Edition

This is a native, storage-installed P4 Game API v1 port of *Legend of the Red
Dragon*. It appears in the P4 BBS door catalog under `GAMES/ADVENTURE` and
runs inside the normal 320×200 game surface; the BBS shell remains the
launcher and retains ownership of display, input, audio, and storage.

Version 0.2.0 implements:

- ANSI/BBS-inspired text screens and DOS color palette;
- Death Knight, Mystical, and Thieving class skills;
- town, forest, shops, healer, bank, inn, and training master;
- the original eleven training thresholds and item progression;
- encounters across all twelve monster tiers;
- bounded turn-based combat, running, death/recovery, and daily limits;
- the level-12 Red Dragon battle and stronger post-victory rebirth;
- a four-warrior local realm directory with public character records;
- daily player-versus-player duels, rewards, deaths, and revival;
- a six-letter mailbox with read state, preset composition, replies, and
  duel/romance messages;
- affection, compliments, gifts, proposals, marriage, and spouse bonuses;
- built-in Goblin Dice, Old Wizard, and Herbalist IGMs with daily limits;
- a generated-and-dithered 16-color dragon/castle title scene plus code-drawn
  RIP-style town, forest, inn, and battle scenes;
- an explicit, deterministic, CRC-protected version-1 save codec with no raw
  C structure or padding in the serialized payload.

The current Game API does not yet expose writable cartridge storage, realm
transactions, OS text input, trusted day rollover, or external IGM handoff.
Until those services land, the new social systems use clearly labeled local
realm records, mail composition uses bounded preset text, and progress remains
session-only even though the save codec is ready. Exiting the door therefore
starts a new character. The exact OS work and game-side connection points are
in [OS_INTEGRATION.md](OS_INTEGRATION.md) and
[osupgrade.md](../../osupgrade.md).

## Controls

- D-pad or Left/Right: move the highlighted choice
- A or Start: choose / attack / continue
- B: return to the previous in-game screen
- Back (`EXIT`): return immediately to the P4 BBS launcher

## Build and test

From the repository root:

```sh
cmake -S games/lord -B build-host/lord -G Ninja
cmake --build build-host/lord
ctest --test-dir build-host/lord --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-lord -G Ninja \
  -DP4_GAME=lord
cmake --build build-host/play-lord
ctest --test-dir build-host/play-lord --output-on-failure
```

Run `make play-game GAME=lord` for the interactive SDL3 host preview. A normal
Console OS cartridge build discovers `game.json` and produces `LORD.P4G` in
the generated game-storage seed.

## Permission and provenance

This port was authorized for this P4 project by the project owner on
2026-08-18. It uses newly written C and code-rendered ANSI-style UI; no
upstream `.ICN`, `.LRD`, player database, or other runtime data file is
included. Exact source identity and attribution are in [UPSTREAM.md](UPSTREAM.md).
