# P4 Yahtzee

P4 Yahtzee is a native, two-to-four-player P4 Game API v1 dice game. It
supports same-device pass-and-play and an optional OS-owned
`multiplayer-session`.
Network mode exchanges compact turn requests and host-authoritative scorecard
snapshots; the cartridge never receives a socket, route, radio, or peer
identity.

Its `game.json` is also the reference declarative profile: turn-based,
two-to-four-player, protocol 4, with a 61-byte message ceiling sized for the
complete four-player host snapshot. Scores use a bounded six-bit packing so
all four scorecards remain inside the Console API's 64-byte message limit.
Console OS derives lobby compatibility and timing from that metadata; Yahtzee
source stays independent of BLE and UART.

For network play, open Multiplayer and select `P4 YAHTZEE` under `GAME`.
Create a room, join its compatible room from each peer, then start from the
host. The lobby launches every cartridge directly into the connected match;
the in-game `LOCAL / NETWORK` menu remains the fallback when the cartridge is
launched normally. Current Console OS transports expose two active network
slots, while the protocol and cartridge state are ready for a future
four-player transport adapter.

## Controls

Touch is primary: tap dice to hold or release them, tap an unused score row to
commit it, and tap Roll/Reroll. The bonus and total rows are display-only.
Tap the player-count arrows before starting local play; tap the pass panel
when the next player is ready. The interface uses semantic touch instructions
and draws no virtual controller. Touch press/hold/release consumes synthesized
controller bits to prevent duplicate holds, scores, or rolls.

Physical controller/keyboard shortcuts remain available:

- **Start** rolls or rerolls the unheld dice, up to three rolls per turn.
- On the local-play menu, **Left/Right** selects two, three, or four players.
- **Left/Right** selects a die; **A** holds or releases it.
- **B** switches between the dice and scorecard.
- In the scorecard, the D-pad selects a category and **A** commits it.
- Touch directly selects dice, score rows, and the on-screen Roll button.
- Touch the persistent upper-left **EXIT** button to return directly to the
  launcher from any game screen.
- **Back** returns to the mode menu, then to the launcher.
- At the final-score screen, **A/Start** begins another match in the same local
  or network session. Any network player may request the host-authoritative
  rematch; **Back** is the explicit way to return to the mode menu.

Each roll requests a short four-voice square/triangle cluster from the
host-owned tone mixer, producing an original dice-clatter effect without a
PCM asset or direct speaker access.

The top strip keeps every active player's total and upper-section progress
visible, with the current player outlined in cyan.
The bottom scorecard splits the six upper categories and seven lower
categories into readable columns with live scoring previews.
After a roll settles, every unused category with a positive projected score
switches to a bright pink `PICK` treatment. Legal zero-point choices remain
neutral, and the focused category gains a cyan border in either case. Committed
rows retain a darker locked treatment with a gold edge and explicit `SET`
marker, so scoring, zero-point, and used categories remain distinct.

## Local verification

```sh
cmake -S tools/p4-game-host -B build-host/play-p4_yahtzee -G Ninja \
  -DP4_GAME=p4_yahtzee
cmake --build build-host/play-p4_yahtzee
ctest --test-dir build-host/play-p4_yahtzee --output-on-failure
make play-game GAME=p4_yahtzee
```

The `.P4G` remains installable under `GAMES/TABLETOP` without an OS reflash.

## Virtual dice accessory

Version 1.3.1 optionally uses the reusable P4MP dice accessory service. On a
matching Console OS build, tap CONNECT on the Core2, then READY for the named
player, shake, and settle. Held dice and the three-roll limit are preserved;
network results still come from the game host. Touch/Start remains available
without an accessory. See `docs/DICE_ACCESSORY.md` for firmware, practice
mode, pairing, and the hardware acceptance status.

For a network match, choose **Multiplayer → Host → P4 YAHTZEE → Match Settings
→ Dice → ON - SHARED CORE2** before opening the room. The single Core2 connects
to the host and is passed between players. Tap Connect after launching, then
Ready and shake on each turn. Both consoles show the host-generated result.
Off disables the accessory for that match; controller/touch rolls remain
available in either mode. Both peers need the updated protocol-4 cartridge.

Real rolls use a separate PCG stream and unbiased six-face sampling. Rendering
never advances that stream. This fixes the former every-other-LCG-output parity
pattern without suppressing naturally repeated dice.

The v2 dice service accepts confirmed Core2 hold/unhold selections for the current player, including the remote player through the shared host. All-held selections can be released; Ready/shake rerolls only unheld dice.

## Native high-resolution presentation

Version 1.4.0 negotiates **768x480** through optional `video-highres`,
with a complete **320x200** fallback. Touch coordinates remain canonical
320x200 in both modes. Game geometry is rasterized directly into the supplied
surface, with native 24/38px antialiased typography in high resolution and
legible compact bitmap text in fallback; no small framebuffer is enlarged.

The original ImageGen atlas `assets/presentation_imagegen_v2.png` provides
112px ivory dice and 96px felt, paper and walnut materials.
The exact built-in generation prompt, source SHA-256, reviewed crops and
license are recorded in `assets/presentation_provenance.json`. These are
original project assets distributed under MIT, with no commercial game art.
The deterministic `tools/convert_presentation.py` converter uses reviewed
source rectangles, nearest-neighbor sampling, bounded RGB565 arrays and an
explicit transparent key. Its compiled image budget is **205,824 bytes**;
font data and game code are additional. Original earlier art remains as
historical source material and is no longer included by the game renderer.

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
without repeatedly painting covered full-screen layers. State, touch targets,
rule timing and multiplayer packet formats remain unchanged.
Dice animation retains elapsed-time remainders across updates; delayed
frames catch up within the bounded 330ms animation without changing real rolls.

The shared optimized CPU benchmark uses real updates, rendering and audio:

```sh
cmake --build build-host/play-p4_yahtzee --target p4_game_benchmark
build-host/play-p4_yahtzee/p4_game_benchmark 2000 768 games/p4_yahtzee/tests/performance-input.txt
```

The input tape exercises real play. Timings in `LOCAL_TESTING.json` describe
this Mac CPU run, exclude display/transport/device costs, and **do not certify
30 FPS on the ESP32-P4**. The simulator targets 60Hz; physical Tab5 frame-time
and presentation measurements remain required for device acceptance.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. The game name
stays visible beside player status during play. Existing touch actions and
setup choices remain direct; no extra launch confirmation is added.
