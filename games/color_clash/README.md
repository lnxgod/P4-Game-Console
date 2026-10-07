# Color Clash

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/CARDS`.

**Package:** `COLOR_CLASH.P4G`. **Players:** Solo vs CPUs; 2–4 linked.

**Local preview:** `make play-game GAME=color_clash` from the repository root.

Linked counts describe the game profile; see [transport and hardware limits](../README.md#test-status-and-multiplayer).

Color Clash is an original shedding-card game for two to four players, with a
classic balanced deck and original branding. Match the discard by color or
symbol, or play a Wild. Skip, Reverse, Draw Two, Wild Draw Four, and the unique
Gamechanger card change the round. The first player to empty their hand wins.

Each human multiplayer participant uses a separate P4 console. Console OS
owns the Host/Join room and selected transport, then launches the cartridge
with an already-sanitized P4MP session. The host validates compact player
intents and broadcasts revisioned, target-tagged messages no larger than 64
bytes. Public
round state uses a compact 27-byte snapshot; private hands use ordered chunks
of at most 54 cards, supporting the full deck without unbounded packets. Each
client applies only messages addressed to its player slot, so its screen shows
its own hand and public opponent card counts. In a two-console room the
authoritative host fills seats three and four with computer players. Tab5
Local Wi-Fi can admit three or four humans, leaving fewer or no CPU seats.
Bluetooth and USB serial remain limited to two consoles; physical four-device
acceptance is separate from the protocol tests.
Only the host advances CPU turns and broadcasts their resulting state.

There is deliberately no pass-and-play mode. A normal launcher start provides
offline practice against one to three deterministic bots, preserving a useful
fallback without exposing alternating human hands on one screen.

## Rules

- The 109-card deck uses purple, gold, red, and Tiffany blue. Each color has
  one 0, two each of 1-9, and two each of Skip, Reverse, and Draw Two. The deck
  also has four Choose Color Wilds, four Wild Draw Fours, and one Gamechanger.
- Match the active color or the top card's symbol. Choose Color Wild is always
  legal.
- Draw Two makes the next player draw two and lose their turn.
- Wild Draw Four lets its player choose red, gold, Tiffany blue, or purple;
  the next player draws four and loses their turn. It is legal only when the
  player has no colored card matching the current active color.
- Reverse changes direction; in a two-player match it acts like Skip.
- If the drawn card is playable, it becomes selected and the player may play
  it or pass. An unplayable drawn card ends the turn. Actions cannot stack.
- Playing down to one card opens an UNO call window. The player may call UNO
  for themself; any other player may call them first, making the caught player
  draw two cards. The window closes when the next normal turn action succeeds.
- The color chooser labels all four choices: RED, GOLD, TIFF, and PURP.
- Color Aid defaults to Symbols, assigning a circle to red, diamond to gold,
  triangle to Tiffany blue, and square to purple. High Contrast adds a dark
  badge and the letters R/G/T/P to every colored card. The same marks appear
  on Wilds, the deck, the active-color status, and the color chooser.
- Playing Gamechanger opens a player chooser. Every hand rotates by the same
  offset so the player who used it receives the chosen player's former hand.
  If that received hand has one or two cards, only the Gamechanger player draws
  up to exactly three. Gamechanger cannot be played as a final card.

## Controls

The default table shows semantic **Play**, **Draw/Pass**, **UNO**, and **Exit**
actions, with no virtual controller or controller-button prompts.

- Drag a hand card upward onto the central discard pile or Play button. The
  lifted card follows the finger/mouse; a green target allows the drop and a
  red target rejects it. Releasing elsewhere returns the card unchanged.
- A stationary tap selects a card; tapping it again or tapping Play plays it.
  Swipe horizontally within the hand to reveal off-screen cards. Cards stay
  in their existing visual order; dragging does not rearrange the hand.
- Tap the deck or Draw to draw. If the drawn card can be played, drag that card
  to the pile, tap Play, or tap Pass. Other cards cannot be substituted for it.
- Tap a color after a Wild, or a player after a Gamechanger. Existing legality
  rules, including the Wild Draw Four restriction, apply to drops too.
- With two cards, UNO+PLAY plays the selected legal card and calls UNO
  atomically. Call UNO and Catch remain available during their normal window.
- Tap menu rows to adjust players and Color Aid; tap the active-color indicator
  during play to cycle the local aid. Exit always returns to the launcher.

Keyboard and controller controls remain available without on-table prompts:
Left/Right selects, A plays, B/Start draws or passes, Up/Down changes Color Aid,
and Back returns from practice to its menu or exits a network session. Start
still prioritizes an open UNO call or UNO+PLAY when applicable. Left/Right and
A operate choice dialogs. Touch gestures suppress the mapper's synthetic
controller buttons until release, so invisible controller regions cannot
play, draw, or exit accidentally.

The runtime combines original RGB565 drawing primitives, shared font glyphs,
and the reviewed ImageGen frame atlas; it has no copied commercial card art,
radio driver, socket, or raw hardware access.

Color Clash 1.8.2 requires `video-highres` and renders directly at 768×480
with its native card atlas, antialiased typography, and original ImageGen table
material and deck back. Legacy source retains its compact atlas for explicit
legacy diagnostics. Touch coordinates remain in the Game API's canonical
320×200 input space, so card, swipe, Draw, UNO and Exit hit regions keep their
established meaning.

## Card art and logo provenance

`assets/color_clash_card_frames_imagegen_v3.png` is the selected OpenAI
ImageGen source atlas. It was generated as a strict 3x2 sheet: red, gold, and
Tiffany-blue frames on row one; purple, Wild, and Gamechanger frames on row
two. The selected source is 1024x1536 RGB with SHA-256
`237823be2c22c401a56fe49254ced442ad32788c95b605b63247c2a82ade7ab5`.
The v3 edit preserved five cells and replaced every green ornament in the
bottom-center Wild frame with royal purple, leaving an exact red, gold,
Tiffany-blue, and purple palette. The prompt required blank centers, crop-safe
silhouettes, hard 16-bit pixel edges, no text, symbols, logos, watermarks, or
cell bleed. The earlier v2 source remains beside it as provenance.

The Gamechanger card composites the existing authorized official Game
Changers AI mark from
`apps/console_os/main/assets/gamechangers_ai_logo.rgb565`. That 112x112 RGB565
asset was retrieved from
`https://www.gamechangersai.org/assets/gamechangers-128.png` at the console
owner's request; its repository-pinned SHA-256 is
`48ee7b2a15a744547884ec6ea7f462277ab60e5dde4d0805bf39db9c0b2bd892`.
ImageGen never redraws the logo.

Regenerate both runtime atlases and their QA previews with Pillow:

```sh
python3 games/color_clash/tools/png_to_card_frames.py \
  games/color_clash/assets/color_clash_card_frames_imagegen_v3.png \
  apps/console_os/main/assets/gamechangers_ai_logo.rgb565 \
  games/color_clash/src/generated/color_clash_card_frames.inc \
  games/color_clash/assets/color_clash_card_frames_runtime_preview_v3.png \
  --high-res-preview \
  games/color_clash/assets/color_clash_card_frames_high_res_preview_v3.png
```

The legacy 28x42 and 42x62 sprites use nearest-neighbor reduction to preserve
their established pixel-art appearance. The high-resolution 67x101 and
101x149 sprites use high-quality downsampling plus a light sharpening pass,
which preserves the ornate frame detail instead of enlarging the 320x200
sprites at runtime.

## Host verification

```sh
cmake -S games/color_clash -B build-host/color_clash -G Ninja
cmake --build build-host/color_clash
ctest --test-dir build-host/color_clash --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-color_clash -G Ninja \
  -DP4_GAME=color_clash
cmake --build build-host/play-color_clash
ctest --test-dir build-host/play-color_clash --output-on-failure
```

## Native-resolution presentation

Version 1.8.2 requires `video-highres` in the manifest and C descriptor.
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
python3 games/color_clash/tools/convert_table_materials.py
```

Focused sanitizer tests cover the required native surface and explicit legacy
diagnostic copies, padded framebuffer strides, render captures, and Back
lifecycle. Existing rule and multiplayer tests remain in place. Optional screenshots come from the actual game renderer:

```sh
P4_CARD_CAPTURE_DIR=/absolute/existing/output/directory \
  ctest --test-dir build-host/color_clash --output-on-failure
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
cmake --build build-host/play-color_clash --target p4_game_benchmark
build-host/play-color_clash/p4_game_benchmark 2000 768 games/color_clash/tests/touch-performance-input.txt
```

The maintained benchmark uses `768`; the required descriptor rejects a
320×200 run. Earlier fallback timings remain historical evidence, and explicit
legacy fixture copies preserve that source path. The benchmark measures Mac
CPU update, audio and rendering during continuous drag; display/device timing,
physical touch and linked-console acceptance remain separate checks.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. The game name
stays visible beside player status during play. Existing touch actions and
setup choices remain direct; no extra launch confirmation is added.

## Current maintained Tab5 contract

Version **1.8.2** requires `video-highres` in both `game.json` and the C
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
