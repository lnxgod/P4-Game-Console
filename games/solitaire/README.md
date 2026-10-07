# Solitaire

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/CARDS`.

**Package:** `SOLITAIR.P4G`. **Players:** Solo.

**Local preview:** `make play-game GAME=solitaire` from the repository root.

An original clean-room Klondike-style card game for P4 Game API v1. It draws crisp native-resolution cards, suit silhouettes, focused-card
previews, and direct card manipulation over original ImageGen felt and card backs.

- Drag a face-up card or run onto a highlighted tableau or foundation. Cards
  follow your finger; only a legal release commits a move. An invalid drop
  returns the cards to their original pile without changing the deal.
- Tap the stock to draw/recycle. You can also tap a card to select it, then
  tap its destination; tap the selected card again to cancel.
- Exit and New deal are the only touch action buttons. There is no on-screen
  D-pad or A/B overlay. Touching their former regions only manipulates cards.
- A physical controller or keyboard still works: D-pad moves focus (Up reaches
  deeper face-up runs), A draws/selects/drops, B quick-moves to a foundation or
  cancels, Start deals again, and Back returns to Console OS.

The expanded tableau fills the screen below the stock and foundations. Cards
are taller, covered cards compress more tightly, and face-up runs get larger
rank/suit strips. A full-card preview in the spare top-row slot helps read
very deep piles when pressing/selecting a card or navigating with a controller. Drag previews keep
whole runs visible and mark legal destinations. Gesture loss or a second
finger cancels a drag; crossing Exit/New deal during a drag cannot trigger it.
A high-resolution host negotiates 768x480 and draws directly into that surface.
The 320x200 fallback remains supported. Touch targets stay canonical 320x200.

## Native-resolution presentation (1.1.0)

The preferred surface is 768x480 RGB565 with an optional `video-highres`
capability and a complete 320x200 fallback. Fonts, rounded card edges, and
mathematical suit silhouettes are rasterized at the negotiated resolution;
no 320x200 framebuffer is enlarged to produce the high-resolution game.
Rules, card identities and lifecycle remain unchanged; direct touch gestures
replace the virtual gamepad.
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
python3 games/solitaire/tools/convert_table_materials.py
```

Focused sanitizer tests include both negotiated surfaces, padded framebuffer
strides, render captures, and Back lifecycle. Existing rule tests remain in place. Gesture regressions exercise the actual
input mapper, legal/illegal stack drops, stock taps, lost/multiple contacts,
header actions, and unchanged keyboard support. Optional screenshots come from the actual game renderer:

```sh
P4_CARD_CAPTURE_DIR=/absolute/existing/output/directory \
  ctest --test-dir build-host/solitaire --output-on-failure
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
