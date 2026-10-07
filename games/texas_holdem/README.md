# Texas Hold'em

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/CARDS`.

**Package:** `TEXAS_HOLDEM.P4G`. **Players:** 2–4 local human/CPU seats; 2–4 linked.

**Local preview:** `make play-game GAME=texas_holdem` from the repository root.

Linked counts describe the game profile; see [transport and hardware limits](../README.md#test-status-and-multiplayer).

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

## Native-resolution presentation (1.2.0)

The preferred surface is 768x480 RGB565 with an optional `video-highres`
capability and a complete 320x200 fallback. Fonts, rounded card edges, and
mathematical suit silhouettes are rasterized at the negotiated resolution;
no 320x200 framebuffer is enlarged to produce the high-resolution game.
Input, rules, card identities, saves, and multiplayer messages remain unchanged.
Exact ranks, numbers, and suits are drawn by code, so generated art cannot
change card meaning. Small fallback labels retain the proven compact font.

`assets/table-materials-imagegen.png` is original artwork generated with the
built-in ImageGen tool for this upgrade. It contains emerald and midnight felt
plus navy/gold and violet/silver card-back material. The exact final prompt,
source SHA-256, crop layout, and selected quadrants are recorded in
`assets/table-materials-provenance.json`. The selected felt is 128x128 RGB565
and the selected card-back material is 96x96 RGB565: **51,200 static bytes**.
The illustration converter uses area filtering to preserve fine fibers and
ornamental lines; there is no runtime image decoding or heap allocation.
The artwork is distributed under the project's MIT license. The shared
Arimo typography uses the separately recorded SIL OFL license.

Reproduce the material include with Pillow installed:

```sh
python3 games/texas_holdem/tools/convert_table_materials.py
```

Focused sanitizer tests include both negotiated surfaces, padded framebuffer
strides, render captures, and Back lifecycle. Existing rule and multiplayer
tests remain in place. Optional screenshots come from the actual game renderer:

```sh
P4_CARD_CAPTURE_DIR=/absolute/existing/output/directory \
  ctest --test-dir build-host/texas_holdem --output-on-failure
```

`PRESENTATION_TESTING.json` records this host-validation pass. Native panel,
physical touch, on-device frame timing, speakers, and real linked consoles
still require device acceptance. No firmware was installed by this pass.

The felt renderer copies clipped native texture-row spans instead of computing
an address and wrapping mask for every framebuffer pixel. A 2,000-frame
isolated host comparison produced identical pixels while reducing median
felt-render time from 80 microseconds to 10 microseconds. Reproducible action
traces and complete update/audio/render timings are recorded in
`PRESENTATION_TESTING.json`. The measured host runs stayed below the 33.333 ms
budget; these CPU measurements exclude display transfer and do not certify
30 FPS on the ESP32-P4 hardware.

The table uses direct touch controls for setup, private-card reveal and betting;
controller-only prompts no longer occupy the table. Touches are handled before
the shared mapper’s virtual buttons, so Raise cannot accidentally act as Fold.
Physical keyboard/controller mappings remain available.
