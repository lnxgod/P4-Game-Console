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

Each player receives seven cards. On a turn, draw one card from the face-down
stock or choose any visible card in the ordered face-up discard pile. Taking a
deeper discard also takes every card above it. When digging into the pile, the
chosen card is marked automatically and must be played in a meld before the
turn can end. Play any valid melds, then discard one card. Played melds stay
visible on the table, and later turns may add matching cards to any player's
existing set or either end of a run. A card just chosen from the discard pile
cannot be returned immediately. The first player to play or discard their last
card ends the round:

- a set is three or four cards of one rank;
- a run is three or more consecutive cards of one suit; and
- an ace can be low in A-2-3 or high after a king.

Each player scores the value of every card they meld or lay off, then subtracts
the value of every card left in hand when the round ends. Number cards score
face value; jacks, queens, and kings score 10; aces score 15 except that an ace
in an A-2-3 low run scores 1. Scores may be negative. Further rounds are
played until at least one player reaches 500; the highest total wins, and a
tie at the top requires another round.

This P4 variant deals seven cards to every player and has no jokers. Hands are
bounded by the complete 52-card deck, and large hands use touchable eight-card
pages. After 200 turns, the bounded round ends, applies the same
meld-minus-hand scoring, and names the lowest-deadwood hand as the round
winner. The CPU plays available melds and prefers draws or discards that lower
its deadwood.

## Controls

- Setup: Left/Right changes the number of CPU opponents. In a network room,
  the host uses it to add CPU seats. A or Start deals immediately.
- Draw: Up/Down or B switches between stock and the discard pile. Left/Right
  moves through the discard pile, and A or Start takes the selected card plus
  every card above it.
- Play: Left/Right focuses a hand card, B marks or unmarks it, Up/Down chooses
  a table meld, and Start plays the marked cards. A discards the focused card.
- Touch/mouse: tap DEAL, tap the stock to draw one, or tap any displayed pile
  card to take it and the cards above it. Pile arrows reveal older or newer
  discards when more than five are present. Tap hand cards to mark them. Tap
  PLAY CARDS for automatic placement, or tap a visible P1-P4 meld to lay the
  marked cards onto that exact meld. Tap DISCARD to end the turn. Large hands
  show touchable page arrows. All setup and round-over actions are touchable.
- Back or the upper-left Exit target returns to the launcher.

## Multiplayer

Console OS owns Host/Join, room discovery, compatibility, physical links, and
the start barrier. Protocol 4 carries full hands, the ordered discard pile,
table melds, signed scores, and match state as a pair of bounded
64-byte host-authoritative messages. The cartridge starts from the connected
session and never opens BLE, UART, USB, display, audio, or storage hardware.
The host validates turn intents and broadcasts the complete table snapshot.
If the link is lost, the game returns to offline setup with one person and one
CPU opponent.

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
