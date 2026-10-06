# LORD — P4 ANSI Door Edition

This is the complete standalone P4 Console port of *Legend of the Red
Dragon*. It installs under `GAMES/ADVENTURE`, renders at 320×200 in a
16-color ANSI/RIP-inspired style, and uses only the stable P4 Game API for
video, controls, tone audio, shared CP437 drawing, and optional durable saves.

Version 1.8.1 includes:

- named hero or heroine characters and Death Knight, Mystical, and Thieving
  professions;
- independent mastery and daily-use counters for all three skill trees, with
  Reckless Blow, Mystic Renewal, and Shadowstep as distinct battle techniques;
- the town, 16 weapons, 16 armour choices, healer, eleven training masters,
  ChompCoin bank deposits/withdrawals/transfers with no passive interest,
  a Hall that ranks the local hero plus the current up-to-eight-warrior
  directory page by dragon deeds first, then level, experience, and PvP wins,
  daily news, and stats;
- all 131 monster records from the authorized pinned Synchronet source,
  spanning all twelve levels, with stats intact, individual CP437 portraits,
  deterministic two-frame blink/breathe/flutter/jolt/spark/morph animation,
  and
  graphic victory blurbs replaced by kid-safe yielding/fleeing outcomes;
- the 15 forest-event families: the old man, hag, ChompCoin cache, Merry Men, gem,
  flower garden/Hammer Stone, skill teachers, charm sticks, horse trader,
  fairies, Olivia, princess rescue, lost ChompCoin, troll, and DarkCloak
  Tavern;
- intent-driven tactical combat: enemies telegraph Strike, Power, Guard, or
  Quick, and the player chooses Strike, Guard, class Technique, Feint, Run, or
  Stats. The XP curve estimates 241 tier-average monster victories—about
  twelve battles from fifteen daily forest actions after events—for a roughly
  21-day path to level 12, followed by the Red Dragon victory/rebirth loop.
  A knockout resolves its zero-carried-ChompCoin and 10%-XP loss in the same
  durable mutation as the lethal hit, so rebooting cannot skip the penalty;
- the full kid-friendly Red Dragon Inn play set: sleep, berry fizz, gossip,
  public conversation, Seth and Violet best-friend paths, bard songs, Dragon
  Dice with player-chosen Roll/Hold/Leave decisions and a target of 18,
  friendly sparring, announcements, and room status. A round stakes 5
  ChompCoin and returns 8 on a win, deliberately keeping optimal play at
  negative expected value; only the first win each day grants one charm and
  high spirits, and schema-5 saves persist that daily reward bit;
- bounded social rewards: the bartender's riddle consumes one friendship
  action, table Dragon Dice keeps its ChompCoin wager without minting badges,
  every five earned friendship badges reduce incoming battle damage by two up
  to eight, and ending any active team or best-friend pact removes its one
  5-HP bonus;
- an eight-warrior persistent local realm with public sayings, ChompCoin
  transfer, PvP, mail, trust, adventure teams, youth mentoring, and daily
  revival;
- hub-hosted Adventure Clubs for up to eight players each, using sixteen fixed
  kid-safe names instead of unmoderated text. Members complete one
  class-flavored rally per realm day, earn cooperative Banner Stars, compare
  seasonal standings, cheer another club, and enter friendly Banner Clashes.
  The Mac calculates every club point and clash result from accepted character
  heads; clubs never mint personal XP, combat stats, deeds, or ChompCoin;
- a twelve-message inbox/sent-mail store and an in-cartridge ANSI keyboard for
  character names, letters, announcements, sayings, and conversations;
- charm, gems, young heroes helped, friendship badges, encounters, horse, and
  fairy state with active rewards: a post-dragon amulet improves the
  triple-damage critical chance, fairy lore guarantees a forest escape, and
  high spirits add 15% attack damage until the next day or knockout;
- seven bounded ports of the Synchronet LORD add-ons: a real four-choice
  Aragorn's Math challenge with 5- or 20-ChompCoin wagers, Barak's House, The
  Grab Bag, The Graveyard, Olodrin's Youth Guild, The Outhouse, and Pickle
  Goddess;
- one fictional currency—ChompCoin—for loot, shops, healing, equipment,
  banking, transfers, and all wagers; existing schema-3 balances migrate
  without conversion;
- the same pinned 8×16 CP437 font as the Console OS BBS, compacted safely for
  real DOS box, arrow, diamond, smile, music, block, and shade glyphs at
  320×200;
- a generated/dithered dragon-and-castle title and 28 original CP437 location
  compositions: illustrated menu strips, twelve full-height ANSI gallery
  exhibits, and distinct scenes for all seven IGM locations. Forges, armour,
  the vault, mail, the hearth and bard, friendship tables, club banners, and
  forest trails replace empty menu space; ordinary message screens reuse
  their return location's art. Existing animated creature portraits, inn
  panels, recovery, and victory artwork remain intact;
- an explicit, deterministic, CRC-protected version-5 save codec with
  validated version-3 and version-4 migration paths. Version 5 persists
  opaque directory identities, the last applied realm event, and the last
  accepted Mac actor/revision/save generation. The game consumes the immutable
  launch snapshot, queues copied `AUTO` commits, polls completion, and uses
  optimistic host sequences;
- a durable-save barrier for paid Dragon Dice and Aragorn rounds. It commits
  the stake—and Aragorn's daily-use bit—before generating or displaying the
  dice or puzzle, and blocks all input until `COMMITTED`. A queue or terminal
  status failure cancels and refunds the still-hidden round. Profiles without
  the optional `save` service remain playable, but cannot claim this durable
  anti-preview protection;
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
  matches, and preserves both copies behind `SYNC CONFLICT` when they diverge.
  The hub orders all accepted profiles before pagination by dragon deeds,
  level, experience, PvP wins, fewer PvP losses, and stable identity ties. A
  backward-compatible kind-22 deeds sidecar supplies the quest field without
  changing the existing summary or stats packets. The cartridge Hall still
  shows only the local hero plus the current up-to-eight-entry directory page,
  not a single realm-wide board;
- exact offline Red Dragon reconciliation: a matching-base deed is accepted
  only when the hub's authoritative head was already level 12. If the accepted
  head is still below level 12, sync once after reaching level 12 before
  finishing the Dragon. After that level-12 head is accepted, the entire
  encounter may happen offline; no separate pre-fight `seen_dragon` upload is
  required, but the one-deed rebirth still has to match the canonical result;
- a P4RM v3-compatible, parent-authorized `ADOPT_LOCAL` welcome path. It binds
  a Mac actor/revision—including revision zero before its first head—as the
  compare-and-swap base, keeps the local committed generation at zero until
  upload succeeds, and remains mutually exclusive with snapshot, ordinary
  accept, and conflict welcomes;
- durable, nonce-idempotent cross-player actions for letters, two-sided
  ChompCoin transfers, encouragement and supplies, consent-based adventure
  teams, shared mentoring, leased asynchronous PvP outcomes, tavern
  conversation, town announcements, and Adventure Club create/join/leave,
  rally, clash, and cheer operations. The hub retains numbered events until
  an accepted target snapshot proves application by advancing its durable event
  cursor. A wire acknowledgement records receipt only and never discards the
  event by itself. A dirty offline branch uploads first; LORD shows `SYNCING`
  and briefly pauses input while that immutable upload or a split event body is
  applied. The hub sends only one durable event before waiting for its cursor
  commit, so reconnect and power-loss replay remain exactly once.

The cartridge is fully playable offline. Console OS 0.4.85 first enabled its
OS-owned journaled save service for the authorized Waveshare profile. The
historically accepted Console OS 0.4.88 / LORD 1.6.1 host/build baseline adds
device-local authentication and a monotonic freshness anchor, so ordinary SD
edits, cross-device copies, and SD-only rollback fail
closed. That baseline was not installed on Pink or Green. The Console
OS 0.4.90 / LORD 1.8.0 tree was initially recorded as an unsealed, unflashed
successor candidate. This 1.8.1 graphics refresh does not itself establish
new exact-device acceptance. LORD progress survives exit and relaunch when
the OS supplies the accepted save service; other profiles may still withhold
`save` and fall back to the current session. With a LORD P4MP room and the Mac
realm hub, it labels the realm `MAC REALM`, synchronizes that actor, lists other
hub profiles, uses the hub's hourly rollover, and delivers classic cross-player
actions to the other profile. A realm-bound character cannot trigger the
classic local sleep reset, even while disconnected; the next trusted Mac realm
hour grants its new day. Local-only characters retain classic inn sleep, but
neither local sleep nor a server rollover mints bank interest. Public Internet
accounts and arbitrary third-party IGM packages still require the OS adapters documented in
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
frames for all 43 screen types, twelve gallery exhibits, and seven IGM
locations after the same sanitizer-backed render-bound checks. The tests
also require visible foreground art in each menu strip, distinct gallery
and IGM illustrations, and unchanged game state across rendering. Art uses
only the existing CP437 renderer; save bytes and multiplayer messages are
unchanged by this refresh.

A normal Console OS cartridge build discovers `game.json` and produces
`LORD.P4G` in the generated game-storage seed. See
[FEATURE_MATRIX.md](FEATURE_MATRIX.md) for the upstream parity audit.

## Permission and provenance

The project owner authorized this port from the pinned Synchronet source on
2026-08-18. The new C implementation imports only data covered by that
permission and uses newly generated/project-rendered art. Exact source hashes,
add-on paths, and regeneration details are in [UPSTREAM.md](UPSTREAM.md) and
[assets/README.md](assets/README.md).

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/lord/tools/pack_launcher.py` (offline Pillow only).
The icon changes the protected payload digest: release it only with its
reviewed paired Console OS lineage, preserving the game ID and save namespace.

## Native ANSI presentation (1.9.0)

The optional `video-highres` capability now requests a native 768×480 surface.
The renderer keeps the canonical 320×200 command/touch layout and paints text,
panels and glyphs directly into the negotiated surface. It does not stretch an
already rendered frame. Native screens use ivory text, midnight panels, brass
accents and garnet selections, with a persistent Red Dragon identity, clearer
command keys, an ImageGen-illustrated dragon opening, and carefully spaced menus.
CP437 art samples all 16 source rows at native size, including short scenery
cells; the shared pinned CP437 font and Arimo presentation atlas are linked
inside the cartridge. The opening has separate 736×306 and 304×126 indexed
images with RGB565 palettes, totaling 264,544 bytes, and no runtime asset decoder.
The existing original launcher illustration and its provenance are retained.

The low-resolution renderer remains available at 320×200. Minor text spacing
fixes keep player records, the recipient prompt, dice messages and the victory
prompt clear of panel borders. Rules, progression, input handling, creature
animation timing, realm protocol, public ID, save slot `AUTO` and LDSV schema 5
are unchanged. Start still activates the current command; this turn-based
game has no independent pause state.

Reproduce the focused sanitizer and presentation checks:

```sh
cmake -S games/lord -B build-host/lord-launch -G Ninja
cmake --build build-host/lord-launch
ctest --test-dir build-host/lord-launch --output-on-failure
cmake -S tools/p4-game-host -B build-host/play-lord-launch -G Ninja -DP4_GAME=lord
cmake --build build-host/play-lord-launch
ctest --test-dir build-host/play-lord-launch --output-on-failure
```

`lord_presentation_tests` covers all 43 screens, ordinary and maximum values,
last-page selections, both resolutions and padded framebuffer strides, and
checks that rendering changes neither state nor encoded saves. Set
`LORD_PRESENTATION_CAPTURE_DIR` to an existing absolute directory for PPMs.
`lord_trace_tests` replays `tests/performance-input.txt` for 2,000 frames and
requires identical native/fallback state and final saves, real combat and
menu transitions, plus Back exit. Use the same tape with `p4_game_benchmark`
at 768 and 320 to measure active local CPU cost.

This remains a protected successor candidate in `GAMES/WIP`. A new payload
requires the coordinating OS release to regenerate and review the exact
protected-game lineage; do not replace the installed cartridge independently
or carry acceptance from an earlier payload. Device cadence, real panel/touch,
speaker, controllers and live realm acceptance remain separate gates. The
current evidence is in `test-runs/2026-10-05-lord-native-remix.json`.
