# P4 Yahtzee

P4 Yahtzee is a native, two-player P4 Game API v1 dice game. It supports
same-device pass-and-play and an optional OS-owned `multiplayer-session`.
Network mode exchanges compact turn requests and host-authoritative scorecard
snapshots; the cartridge never receives a socket, route, radio, or peer
identity.

Its `game.json` is also the reference declarative profile: turn-based,
two-player, protocol 2, with a 48-byte message ceiling sized for the complete
45-byte host snapshot. Console OS derives lobby compatibility and timing from
that metadata; Yahtzee source stays independent of BLE and UART.

For two-console play, open Multiplayer on both devices and select `P4 YAHTZEE`
under `GAME`. Create a room on one console, join its compatible room on the
other, then start from the host. The lobby launches both cartridges directly
into the connected network match; the in-game `LOCAL / NETWORK` menu remains
the fallback when the cartridge is launched normally.

## Controls

- **Start** rolls or rerolls the unheld dice, up to three rolls per turn.
- **Left/Right** selects a die; **A** holds or releases it.
- **B** switches between the dice and scorecard.
- In the scorecard, the D-pad selects a category and **A** commits it.
- Touch directly selects dice, score rows, and the on-screen Roll button.
- Touch the persistent upper-left **EXIT** button to return directly to the
  launcher from any game screen.
- **Back** returns to the mode menu, then to the launcher.
- At the final-score screen, **A/Start** begins another match in the same local
  or network session. Either network player may request the host-authoritative
  rematch; **Back** is the explicit way to return to the mode menu.

Each roll requests a short four-voice square/triangle cluster from the
host-owned tone mixer, producing an original dice-clatter effect without a
PCM asset or direct speaker access.

The top strip keeps both players' totals and upper-section progress visible.
The bottom scorecard splits the six upper categories and seven lower
categories into readable columns with live scoring previews.
After a roll settles, every unused category with a positive projected score
switches to a bright pink `PICK` treatment. Legal zero-point choices remain
neutral, and the focused category gains a cyan border in either case. Committed
rows retain a darker locked treatment with a gold edge and explicit `SET`
marker, so scoring, zero-point, and used categories remain distinct.

## Original generated art

`assets/p4_yahtzee_dice_atlas_imagegen_v1.png` was generated for this game
with OpenAI ImageGen. It is an original 3x2 atlas containing the six standard
dice faces on transparent cells; no third-party game art is used. The prompt
requested polished 16-bit pixel art, warm ivory faces, navy pips, cyan edge
light, a restrained gold highlight, and exact 1-6 pip layouts.

`tools/png_to_dice_atlas.py` crops the six cells, applies a fixed transparency
threshold, resizes with nearest-neighbor sampling, and emits the bounded
RGB565 include at `src/generated/p4_yahtzee_dice_atlas.inc`. Runtime code does
not decode PNGs or allocate image buffers.

## Local verification

```sh
cmake -S tools/p4-game-host -B build-host/play-p4_yahtzee -G Ninja \
  -DP4_GAME=p4_yahtzee
cmake --build build-host/play-p4_yahtzee
ctest --test-dir build-host/play-p4_yahtzee --output-on-failure
make play-game GAME=p4_yahtzee
```

The `.P4G` remains installable under `GAMES/ARCADE` without an OS reflash.
