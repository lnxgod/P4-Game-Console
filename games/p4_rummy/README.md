# Rummy 500

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/CARDS`.

**Package:** `P4_RUMMY.P4G`. **Players:** Solo vs CPUs; 2–4 linked.

**Local preview:** `make play-game GAME=p4_rummy` from the repository root.

Linked counts describe the game profile; see [transport and hardware limits](../README.md#test-status-and-multiplayer).

Rummy 500 is an original, quick-playing Rummy variant for one to four players.
Offline play puts one person against one to three CPU opponents and starts the
table immediately without a pass-and-play gate. Human multiplayer uses an
OS-owned P4MP room on separate consoles. A two-console room can add up to two
host-controlled CPU seats. Tab5 Local Wi-Fi supports up to four humans where
the OS admits that roster; Bluetooth and USB serial remain two-console links.
This protocol capacity does not establish physical four-device acceptance.

P4 Rummy is an enabled core seed cartridge: Console OS includes
`P4_RUMMY.P4G` in every generated game-storage bundle while keeping it
replaceable through the normal Game Manager and H1 update paths. Version
2.2.1 requires the native 768×480 graphics surface on maintained Tab5. Cards,
suit marks, table chrome and text are drawn directly into that surface;
touch remains in the canonical 320×200 input coordinate space. Legacy rendering
source remains available for explicitly requested legacy maintenance and
labeled host diagnostics.
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

The table uses semantic card actions without a virtual controller overlay.

- Tap DEAL, then tap stock to draw one card or a displayed discard to take that
  card and everything above it. Pile arrows reveal older/newer discards.
- Tap hand cards to mark a set/run. Tap PLAY CARDS for automatic placement, or
  drag one of the marked cards onto PLAY CARDS. The lifted card shows the
  selected-card count. Dragging an unmarked card includes it with the current
  selection for that drop.
- To lay off onto a specific table meld, drag a card/selection onto that meld.
  Tapping the meld with cards marked remains supported.
- Drag a single card to the face-up discard pile or DISCARD button to end the
  turn. Tapping DISCARD still uses the focused card. Required-meld and
  just-picked-discard restrictions remain enforced.
- A green drop outline is legal; red rejects the move. Releasing outside a
  target or on an illegal target leaves hand, selection, score and turn
  unchanged. Touch page arrows handle large hands; dragging does not reorder
  cards or move cards across pages.
- Tap setup controls and round results directly. Exit returns to the launcher.

Keyboard/controller input remains available without controller decoration:
Left/Right changes setup or card focus; A/Start deals or draws; Up/Down/B changes
the draw source. During play, B marks a card, Up/Down targets a meld, Start plays
the selection and A discards. Back exits. Synthetic mapper buttons are ignored
during touch and release so dragging across old controller regions has no
extra effect.

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

## Native-resolution presentation

Version 2.2.1 requires `video-highres` in the manifest and C descriptor.
The maintained Tab5 surface is **768×480 RGB565**. Fonts, rounded card edges,
and mathematical suit silhouettes are rasterized directly into that surface;
no completed 320×200 frame is enlarged. Canonical 320×200 touch coordinates
remain input units. The compact legacy renderer is preserved only for
explicitly requested legacy maintenance and labeled host diagnostics.
Input, rules, card identities, saves, and multiplayer messages remain unchanged.
Exact ranks, numbers, and suits are drawn by code, so generated art cannot
change card meaning. Explicit legacy diagnostics retain the proven compact font.

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
python3 games/p4_rummy/tools/convert_table_materials.py
```

Focused sanitizer tests cover the required native surface and explicit legacy
diagnostic copies, padded framebuffer strides, render captures, and Back
lifecycle. Existing rule and multiplayer tests remain in place. Optional screenshots come from the actual game renderer:

```sh
P4_CARD_CAPTURE_DIR=/absolute/existing/output/directory \
  ctest --test-dir build-host/p4_rummy --output-on-failure
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

## Direct-touch verification

`tests/test_touch_drag.c` checks canonical touch at both 320x200 and 768x480,
including lifted-card framebuffer guards, valid and invalid drops, stale-gesture
cancellation, existing keyboard actions, and synthetic controller isolation.
The focused rule/network tests remain unchanged. `TOUCH_TESTING.json` records
this pass separately from the preceding art/package acceptance.

```sh
cmake --build build-host/play-p4_rummy --target p4_game_benchmark
build-host/play-p4_rummy/p4_game_benchmark 2000 768 games/p4_rummy/tests/touch-performance-input.txt
```

The maintained benchmark uses `768`; the required descriptor rejects a
320×200 run. Earlier fallback timings remain historical evidence, and explicit
legacy fixture copies preserve that source path. The benchmark measures Mac
CPU update, audio and rendering during continuous drag; display/device timing,
physical touch and linked-console acceptance remain separate checks.

## Current maintained Tab5 contract

Version **2.2.1** requires `video-highres` in both `game.json` and the C
descriptor. Maintained Tab5 gameplay renders each frame directly into a
**768×480 RGB565** surface. Canonical 320×200 touch coordinates are input
units only; they do not select a render resolution. Any 320×200 fallback
guidance retained above applies only to explicitly requested legacy diagnostics.

The global target is **60 FPS**, with an **actual-device 30 FPS release floor**.
Acceptance requires the exact cartridge/package, Console OS artifact and Tab5
unit, the observed runtime surface, and cadence measured during busy gameplay.
Readable title/ready, play, pause and results views must be checked on that same
device. These physical readability and cadence gates remain pending until
measured for this exact candidate. A merge or firmware installation does not
close these physical acceptance gates.

Checkpoint `41dfbe6` records focused ASan/UBSan and native SDL smoke checks
for its source revision. These historical host results do not qualify the
merged Console OS or establish physical device acceptance.
