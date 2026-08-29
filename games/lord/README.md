# LORD — P4 ANSI Door Edition

This is the complete standalone P4 Console port of *Legend of the Red
Dragon*. It installs under `GAMES/ADVENTURE`, renders at 320×200 in a
16-color ANSI/RIP-inspired style, and uses only the stable P4 Game API for
video, controls, tone audio, shared CP437 drawing, and optional durable saves.

Version 1.6.1 includes:

- named hero or heroine characters and Death Knight, Mystical, and Thieving
  professions;
- independent mastery and daily-use counters for all three skill trees;
- the town, 16 weapons, 16 armour choices, healer, eleven training masters,
  ChompCoin bank deposits/withdrawals/transfers, rankings, daily news, and
  stats;
- all 131 monster records from the authorized pinned Synchronet source,
  spanning all twelve levels, with stats intact and graphic victory blurbs
  replaced by kid-safe yielding/fleeing outcomes;
- the 15 forest-event families: the old man, hag, ChompCoin cache, Merry Men, gem,
  flower garden/Hammer Stone, skill teachers, charm sticks, horse trader,
  fairies, Olivia, princess rescue, lost ChompCoin, troll, and DarkCloak
  Tavern;
- combat, running, death/recovery, daily limits, level progression, and the
  level-12 Red Dragon victory/rebirth loop;
- the full kid-friendly Red Dragon Inn play set: sleep, berry fizz, gossip,
  public conversation, Seth and Violet best-friend paths, bard songs, Dragon
  Dice, friendly sparring, announcements, and room status;
- bounded social rewards: the bartender's riddle consumes one friendship
  action, table Dragon Dice keeps its ChompCoin wager without minting badges,
  and ending any active team or best-friend pact removes its one 5-HP bonus;
- an eight-warrior persistent local realm with public sayings, ChompCoin
  transfer, PvP, mail, trust, adventure teams, youth mentoring, and daily
  revival;
- a twelve-message inbox/sent-mail store and an in-cartridge ANSI keyboard for
  character names, letters, announcements, sayings, and conversations;
- charm, gems, young heroes helped, friendship badges, encounters, horse,
  fairy, fairy lore, amulet, and high-spirits state;
- seven bounded ports of the Synchronet LORD add-ons: Aragorn's Math, Barak's
  House, The Grab Bag, The Graveyard, Olodrin's Youth Guild, The Outhouse, and
  Pickle Goddess;
- one fictional currency—ChompCoin—for loot, shops, healing, equipment,
  banking, transfers, and all wagers; existing schema-3 balances migrate
  without conversion;
- the same pinned 8×16 CP437 font as the Console OS BBS, compacted safely for
  real DOS box, arrow, diamond, smile, music, block, and shade glyphs at
  320×200;
- a generated/dithered dragon-and-castle title, twelve code-drawn RIP-style
  scenes, and additional kid-friendly CP437 compositions for the inn, Seth,
  Violet, Dragon Dice, recovery, and victory;
- an explicit, deterministic, CRC-protected version-5 save codec with
  validated version-3 and version-4 migration paths. Version 5 persists
  opaque directory identities, the last applied realm event, and the last
  accepted Mac actor/revision/save generation. The game consumes the immutable
  launch snapshot, queues copied `AUTO` commits, polls completion, and uses
  optimistic host sequences;
- a bounded `LRSY` backend-sync record carrying an opaque actor ID, one-use
  nonce, realm revision, save sequence, CRC, and the complete save payload.
  The cartridge never owns accounts, TLS, sockets, USB, BLE, or server
  transport;
- an optional Mac-hosted realm path over the existing Console OS
  `multiplayer-session` service. It synchronizes full character snapshots,
  compare-and-swap revisions, up to 100 player profiles with eight-entry
  Previous/Next roster pages, bounded presence and warrior stats including
  ChompCoin, and a trusted hourly realm-day refresh over either H1 USB or BLE.
  The Mac hosts each logical session and every console joins it. P4RM v3 can
  upload offline progress when the locally persisted server base still
  matches, and preserves both copies behind `SYNC CONFLICT` when they diverge;
- a P4RM v3-compatible, parent-authorized `ADOPT_LOCAL` welcome path. It binds
  a Mac actor/revision—including revision zero before its first head—as the
  compare-and-swap base, keeps the local committed generation at zero until
  upload succeeds, and remains mutually exclusive with snapshot, ordinary
  accept, and conflict welcomes;
- durable, nonce-idempotent cross-player actions for letters, two-sided
  ChompCoin transfers, encouragement and supplies, consent-based adventure
  teams, shared mentoring, leased asynchronous PvP outcomes, tavern
  conversation, and town announcements. The hub retains numbered events until
  an accepted target snapshot proves application by advancing its durable event
  cursor. A wire acknowledgement records receipt only and never discards the
  event by itself. A dirty offline branch uploads first; LORD shows `SYNCING`
  and briefly pauses input while that immutable upload or a split event body is
  applied. The hub sends only one durable event before waiting for its cursor
  commit, so reconnect and power-loss replay remain exactly once.

The cartridge is fully playable offline. Console OS 0.4.85 first enabled its
OS-owned journaled save service for the authorized Waveshare profile. The
0.4.88 successor adds device-local authentication and a monotonic freshness
anchor, so ordinary SD edits, cross-device copies, and SD-only rollback fail
closed. LORD progress survives exit and relaunch there; other profiles may
still withhold `save` and fall back to the current session. With a LORD P4MP room and the Mac realm hub it labels
the realm `MAC REALM`, synchronizes that actor, lists other hub profiles, uses
the hub's hourly rollover, and delivers classic cross-player actions to the
other profile. A realm-bound character cannot trigger the classic local sleep
reset, even while disconnected; the next trusted Mac realm hour grants its new
day. Local-only characters retain classic inn sleep. Public Internet accounts
and arbitrary third-party IGM
packages still require the OS adapters documented in
[BACKEND_SYNC.md](BACKEND_SYNC.md), [OS_INTEGRATION.md](OS_INTEGRATION.md), and
[osupgrade.md](../../osupgrade.md). Mac setup and the exact implemented scope
are in [LORD_REALM_HUB.md](../../docs/LORD_REALM_HUB.md); the complete
standalone game does not depend on the hub.

Implementation note: the legacy C fields named `gold` and `bank` remain only
to preserve existing save bytes. They now mean carried and vaulted ChompCoin;
there is no second gold currency and no balance conversion.

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
