# Texas Hold'em

Texas Hold'em is an original native P4 Game API v1 poker game for two to four
human or CPU-controlled seats. A normal launcher start opens a four-seat
pass-and-play table. A
Console OS Multiplayer launch uses the already-established transport-neutral
session and starts directly at the table lobby screen.

The host chooses one equal starting stack for every seat: 500, 1,000, 2,000,
or 5,000 chips, plus the number of CPU seats. Local play supports zero to three
CPUs while keeping Player 1 human. In a network room, the connected humans keep
their assigned seats and the host can fill the remaining seats with CPUs. Both
table settings are synchronized before the first deal.
Blinds are 10/20. Betting uses fold, check/call, and one-big-blind raises,
including call all-ins, chip-conserving side pots, split pots, and the normal
best five-card hand out of seven.

Version 1.1.1 keeps the full big-blind opening price when the big blind is
short-stacked and clears all per-hand masks and cards before publishing a
match-over network snapshot.

## Controls

- Setup: Up/Down selects starting chips or CPU players; Left/Right changes the
  selected setting; A or Start deals.
- Pass-and-play: hand the console to the named player, then press A to reveal.
- Betting: Left/Right selects Fold, Check/Call, or Raise; A confirms.
- Start is a check/call shortcut; B is a fold shortcut.
- Showdown: A or Start advances to the next hand. The host owns this action in
  network play.
- Back, or the upper-left Exit touch target, returns to the launcher.
- Touch can select the stack, deal, reveal, betting actions, and next hand.

The protocol is host-authoritative. Only the host evaluates and performs CPU
actions. Clients send bounded human action intents tied to the current revision
and the host publishes a validated 63-byte table snapshot. CPU cards remain
face-down in the UI until showdown. The manifest supports two to four seats and
the tests exercise both a four-human link and two network humans plus two CPUs.
Current Console OS transports expose two live network slots, which can now be
expanded into a full four-seat table with two host-controlled CPUs.

Console OS owns Host/Join, room discovery, compatibility, and the physical
link. The cartridge never opens BLE, UART, USB, display, audio, or storage
hardware. If a peer is lost, committed bets are refunded and the table falls
back to pass-and-play with the current chip stacks.

## Host checks

```sh
cmake -S games/texas_holdem -B build-host/texas_holdem -G Ninja
cmake --build build-host/texas_holdem
ctest --test-dir build-host/texas_holdem --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-texas_holdem -G Ninja \
  -DP4_GAME=texas_holdem
cmake --build build-host/play-texas_holdem
ctest --test-dir build-host/play-texas_holdem --output-on-failure
```

Console OS discovers `game.json` and packages `TEXAS_HOLDEM.P4G`. Install the
cartridge through Game Manager, a guarded SD workflow, or the verified H1
transfer path; a game-only update does not require an OS flash.
