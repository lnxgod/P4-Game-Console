# LORD — P4 ANSI Door Edition

This is the complete standalone P4 Console port of *Legend of the Red
Dragon*. It installs under `GAMES/ADVENTURE`, renders at 320×200 in a
16-color ANSI/RIP-inspired style, and uses only the stable P4 Game API for
video, controls, tone audio, shared CP437 drawing, and optional durable saves.

Version 1.1.0 includes:

- named male or female characters and Death Knight, Mystical, and Thieving
  professions;
- independent mastery and daily-use counters for all three skill trees;
- the town, 16 weapons, 16 armour choices, healer, eleven training masters,
  bank deposits/withdrawals/transfers, rankings, daily news, and stats;
- all 131 monster records from the authorized pinned Synchronet source,
  spanning all twelve levels;
- the 15 forest-event families: the old man, hag, gold sack, Merry Men, gem,
  flower garden/Hammer Stone, skill teachers, charm sticks, horse trader,
  fairies, Olivia, princess rescue, lost gold, troll, and DarkCloak Tavern;
- combat, running, death/recovery, daily limits, level progression, and the
  level-12 Red Dragon victory/rebirth loop;
- the full Red Dragon Inn play set: sleep, bartender drinks and gossip,
  public conversation, Seth, Violet, bard songs, blackjack, sleeping-player
  attacks, announcements, and room status;
- an eight-warrior persistent local realm with public sayings, bank transfer,
  PvP, mail, courtship, marriage/divorce, children, and daily revival;
- a twelve-message inbox/sent-mail store and an in-cartridge ANSI keyboard for
  character names, letters, announcements, sayings, and conversations;
- charm, gems, children, encounters, horse, fairy, fairy lore, amulet, and
  high-spirits state;
- seven bounded ports of the Synchronet LORD add-ons: Aragorn's Math, Barak's
  House, The Grab Bag, The Graveyard, Olodrin's Orphans, The Outhouse, and The
  Pickle Goddess;
- the same pinned 8×16 CP437 font as the Console OS BBS, compacted safely for
  real DOS box, arrow, heart, block, and shade glyphs at 320×200;
- a generated/dithered dragon-and-castle title plus twelve code-drawn
  ANSI/RIP-style scenes with CP437 texture and framing;
- an explicit, deterministic, CRC-protected version-3 save codec. The game
  consumes the immutable launch snapshot, queues copied `AUTO` commits, polls
  completion, and uses optimistic host sequences.

The cartridge is fully playable offline. On a Console OS profile that offers
`save`, progress survives exit and relaunch. On profiles that deliberately
withhold writable storage, it remains playable for the current session and
labels the realm local. Shared remote BBS accounts, real remote opponents,
remote consent transactions, trusted server-day rollover, and arbitrary
third-party IGM packages still require the optional OS adapters documented in
[OS_INTEGRATION.md](OS_INTEGRATION.md) and
[osupgrade.md](../../osupgrade.md); the complete standalone game does not
depend on them.

## Controls

- D-pad: move through menus; move in two dimensions on the ANSI keyboard
- A or Start: choose, attack, enter a character, or continue
- B: return to the previous in-game screen; on the keyboard, cancel entry
- Back (`EXIT`): queue any dirty save and return to the launcher

The keyboard contains letters, digits, space, punctuation, `DONE`, and `DEL`.

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

Run `make play-game GAME=lord` for the interactive SDL3 preview. To capture
all screens without a visible desktop, create a directory and run the unit
binary with `LORD_CAPTURE_DIR=/absolute/path`; it emits deterministic PPM
frames after the same sanitizer-backed render-bound checks.

A normal Console OS cartridge build discovers `game.json` and produces
`LORD.P4G` in the generated game-storage seed. See
[FEATURE_MATRIX.md](FEATURE_MATRIX.md) for the upstream parity audit.

## Permission and provenance

The project owner authorized this port from the pinned Synchronet source on
2026-08-18. The new C implementation imports only data covered by that
permission and uses newly generated/project-rendered art. Exact source hashes,
add-on paths, and regeneration details are in [UPSTREAM.md](UPSTREAM.md) and
[assets/README.md](assets/README.md).
