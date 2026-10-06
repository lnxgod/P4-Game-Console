# Checkers

Checkers is an original, textured two-player game for P4 Game API v1.
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

Touch is primary: tap a piece and a highlighted destination, or drag the piece
and release it over a legal square. Invalid/outside drops leave the board
unchanged. Tap a selected piece again to cancel unless another jump is required.
Pieces follow the finger while dragging, including the rotated board used by
the white network player. No virtual controller is drawn. Touch gestures and
their release consume synthetic controller bits so each action happens once.

Physical controller/keyboard shortcuts remain available:

- D-pad/arrows: move the board cursor
- A: select a piece or move to the highlighted square
- B: cancel a selection, except during a compulsory continued jump
- Start: begin a new match after game over
- Back: return directly to the launcher
- Touch: move directly on the board, or tap New Match / Exit

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

## Native high-resolution presentation

Version 1.1.0 negotiates **768x480** through optional `video-highres`,
with a complete **320x200** fallback. Touch coordinates remain canonical
320x200 in both modes. Game geometry is rasterized directly into the supplied
surface, with native 24/38px antialiased typography in high resolution and
legible compact bitmap text in fallback; no small framebuffer is enlarged.

The original ImageGen atlas `assets/presentation_imagegen_v2.png` provides
48px maple/walnut board materials and lacquer red/ivory men and crowned kings.
The exact built-in generation prompt, source SHA-256, reviewed crops and
license are recorded in `assets/presentation_provenance.json`. These are
original project assets distributed under MIT, with no commercial game art.
The deterministic `tools/convert_presentation.py` converter uses reviewed
source rectangles, nearest-neighbor sampling, bounded RGB565 arrays and an
explicit transparent key. Its compiled image budget is **27,648 bytes**;
font data and game code are additional. The game never decodes PNGs or creates a low-resolution temporary framebuffer.

Run the converter with Pillow installed:

```sh
python3 tools/convert_presentation.py
```

Focused tests cover padded framebuffer guards and full-surface rendering at
both resolutions, canonical touch targets after rendering, and representative
menus, play and result states. Set `P4_CAPTURE_DIR` to an existing absolute
directory to retain native PPM captures from the presentation test. The
existing rules and mocked multiplayer suites remain unchanged. See
`LOCAL_TESTING.json` for exact automated evidence and pending interactive/
physical-device acceptance; host tests are not hardware acceptance.

## Frame-time budget

The minimum target is 30 presented frames per second (33.333ms per frame).
The renderer copies mirrored material row spans and paints visible regions
without repeatedly painting covered full-screen layers. Canonical touch targets,
rule timing and multiplayer packet formats remain unchanged.

The shared optimized CPU benchmark uses real updates, rendering and audio:

```sh
cmake --build build-host/play-checkers --target p4_game_benchmark
build-host/play-checkers/p4_game_benchmark 2000 768 games/checkers/tests/performance-input.txt
```

The input tape exercises real play. Timings in `LOCAL_TESTING.json` describe
this Mac CPU run, exclude display/transport/device costs, and **do not certify
30 FPS on the ESP32-P4**. The simulator targets 60Hz; physical Tab5 frame-time
and presentation measurements remain required for device acceptance.
