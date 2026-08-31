# Checkers

Checkers is an original, code-rendered two-player game for P4 Game API v1.
An ordinary launcher start is same-device play; a Console OS Host/Join start
uses the already-established turn-based multiplayer session. The cartridge
never selects or owns BLE, UART, USB, display, touch, audio, or storage.

## Rules

- Red moves first. Men move and capture diagonally forward; kings move both
  directions.
- Captures are compulsory. A capturing piece must continue jumping while a
  legal jump remains.
- A man is crowned on the far row, and that turn ends immediately.
- A player wins when the opponent has no pieces or no legal move.
- Eighty half-moves without a capture or promotion is a draw.

## Controls

- D-pad/arrows: move the board cursor
- A: select a piece or move to the highlighted square
- B: cancel a selection, except during a compulsory continued jump
- Start: begin a new match after game over
- Back: return directly to the launcher
- Touch: tap a piece, destination, restart button, or Exit

The network protocol is host-authoritative. Clients send bounded move or
restart intents tied to the current revision; the host validates them with the
same rules used by local play and publishes a complete 38-byte snapshot. A
lost peer falls back to same-device play without discarding the current board.

## Host checks

```sh
cmake -S games/checkers -B build-host/checkers -G Ninja
cmake --build build-host/checkers
ctest --test-dir build-host/checkers --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-checkers -G Ninja \
  -DP4_GAME=checkers
cmake --build build-host/play-checkers
ctest --test-dir build-host/play-checkers --output-on-failure
```

Console OS discovers `game.json` and packages `CHECKERS.P4G`. Install that
file through Game Manager, the board's guarded SD workflow, or the verified H1
transfer path; a game-only update does not require an OS flash.
