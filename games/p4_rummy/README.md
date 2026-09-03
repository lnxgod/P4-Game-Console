# P4 Rummy

P4 Rummy is an original, quick-playing Rummy variant for one to four human
players. A one-player game adds one CPU opponent. Two to four people can play
locally by passing the console, or join an OS-owned multiplayer room from
separate consoles. A two-console room can add up to two host-controlled CPU
seats; the manifest is ready for future four-player Console OS transports.

## Rules

Each player receives seven cards. On a turn, draw from the face-down stock or
the face-up discard pile, then discard one card. A card just taken from the
discard pile cannot be returned immediately. The first player whose seven-card
hand can be divided completely into melds wins:

- a set is three or four cards of one rank;
- a run is three or more consecutive cards of one suit; and
- an ace can be low in A-2-3 or high after a king.

After 200 turns, the bounded round ends and the lowest-deadwood hand wins.
The CPU keeps melds and prefers draws or discards that lower its deadwood.

## Controls

- Setup: Left/Right changes the number of human players. In a network room,
  the host uses it to add CPU seats. A or Start deals.
- Pass-and-play: hand the console to the named player, then press A to reveal.
- Draw: Left/Right, Up/Down, or B switches between stock and discard. A draws.
- Discard: Left/Right selects a card. A discards it.
- Touch: tap a pile to draw; tap a hand card to select it and tap it again to
  discard. All setup and round-over actions are also touchable.
- Back or the upper-left Exit target returns to the launcher.

## Multiplayer

Console OS owns Host/Join, room discovery, compatibility, physical links, and
the start barrier. The cartridge starts from the connected session and never
opens BLE, UART, USB, display, audio, or storage hardware. The host validates
turn intents and broadcasts a complete 55-byte table snapshot. If the link is
lost, the game returns to a same-device setup using the connected human count.

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
