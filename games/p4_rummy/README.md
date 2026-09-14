# P4 Rummy 500

P4 Rummy 500 is an original, quick-playing Rummy variant for one to four players.
Offline play puts one person against one to three CPU opponents and starts the
table immediately without a pass-and-play gate. Human multiplayer uses an
OS-owned P4MP room on separate consoles. A two-console room can add up to two
host-controlled CPU seats; the manifest is ready for future four-player
Console OS transports.

P4 Rummy is an enabled core seed cartridge: Console OS includes
`P4_RUMMY.P4G` in every generated game-storage bundle while keeping it
replaceable through the normal Game Manager and H1 update paths. It opts into
the advanced 768x480 graphics surface on supported consoles and retains the
portable 320x200 renderer elsewhere. Cards, suit marks, table chrome, and text
are drawn directly at the negotiated resolution; touch remains in the stable
320x200 input coordinate space.
Every card uses its complete rank, including `10`; the table view does not
substitute a `T` abbreviation.

## Rules and scoring

Each player receives seven cards. On a turn, draw from the face-down stock or
the face-up discard pile, play any valid melds, then discard one card. Played
melds stay visible on the table, and later turns may add matching cards to an
existing set or either end of a run. A card just taken from the discard pile
cannot be returned immediately, but it may be used in a meld. The first player
to play or discard their last card ends the round:

- a set is three or four cards of one rank;
- a run is three or more consecutive cards of one suit; and
- an ace can be low in A-2-3 or high after a king.

Each player scores the value of every card they meld or lay off, then subtracts
the value of every card left in hand when the round ends. Number cards score
face value; jacks, queens, and kings score 10; aces score 15 except that an ace
in an A-2-3 low run scores 1. Scores may be negative. Further rounds are
played until at least one player reaches 500; the highest total wins, and a
tie at the top requires another round.

This compact P4 variant deals seven cards to every player, uses the top discard
only, and has no jokers or discard-pile digging. After 200 turns, the bounded
round ends, applies the same meld-minus-hand scoring, and names the
lowest-deadwood hand as the round winner. The CPU plays available melds and
prefers draws or discards that lower its deadwood.

## Controls

- Setup: Left/Right changes the number of CPU opponents. In a network room,
  the host uses it to add CPU seats. A or Start deals immediately.
- Draw: Left/Right, Up/Down, or B switches between stock and discard. A draws.
- Play: Left/Right focuses a card, B marks or unmarks it, and Start plays the
  marked cards as a new set/run or onto an existing meld. A discards the
  focused card.
- Touch/mouse: tap DEAL, tap the stock or discard pile to draw, then tap hand
  cards to mark them. Tap MELD to play the marked cards or DISCARD to end the
  turn with the focused card. Cards left in your hand are held automatically.
  All setup and round-over actions are also touchable.
- Back or the upper-left Exit target returns to the launcher.

## Multiplayer

Console OS owns Host/Join, room discovery, compatibility, physical links, and
the start barrier. Protocol 3 carries bounded hands, table melds, signed scores,
and match state in one 64-byte host-authoritative snapshot. The cartridge
starts from the connected session and never opens BLE, UART, USB, display,
audio, or storage hardware. The host validates turn intents and broadcasts the
complete table snapshot. If the link is lost, the game returns to offline setup
with one person and one CPU opponent.

## Host checks

```sh
cmake -S games/p4_rummy -B build-host/p4_rummy -G Ninja
cmake --build build-host/p4_rummy
ctest --test-dir build-host/p4_rummy --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-p4_rummy -G Ninja \
  -DP4_GAME=p4_rummy
cmake --build build-host/play-p4_rummy
ctest --test-dir build-host/play-p4_rummy --output-on-failure
```

Console OS discovers `game.json` and packages `P4_RUMMY.P4G`. Install that
cartridge through Game Manager, a guarded SD workflow, or the verified H1
transfer path; a game-only update does not require an OS flash.
