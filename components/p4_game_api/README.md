# Console API shared feedback pack

This component owns the reusable, host-safe feedback pack for native Console
API games. It contains original audio and animation assets only; games use its
bounded `p4/` interfaces rather than decoding media or acquiring hardware.

## Allocation-free visual helpers

`p4/visual.h` adds small reusable pieces for polished games without creating a
scene engine: signed Q16.16 movement, smoothstep easing, looping or ping-pong
atlas timing, clipped/flipped/integer-scaled RGB565 sprites, deterministic
camera shake, and bounded caller-owned particles. Cartridge packaging compiles
these helpers into each game that uses them, so Game API v1 and its frozen
runtime import allowlist do not change. `games/asteroids` is the reference.

`p4/draw.h` also exposes bounded 8×8 compact and native 8×16 CP437 glyph/text
drawing. The bytes come from the single pinned font in `components/p4_cp437`,
which is shared with Console OS's 80×30 ANSI/BBS renderer. The helpers paint
explicit foreground and background colors, clip to the 320×200 surface, never
allocate, and compile into a cartridge without adding a host callback or raw
terminal access. LORD is the reference for compact DOS box, arrow, shade, and
symbol glyphs.

## Original audio provenance

The four stereo WAV sources under `assets/audio/source/` were generated through
ElevenLabs Sound Effects with sharing disabled on 2026-08-16. Each selected
source is original project audio under this repository's MIT license:

- `p4_action_original_v1.wav` — activation blip, electric snap, forward whoosh
- `p4_impact_original_v1.wav` — metallic pop, digital crackle, descending thud
- `p4_reward_original_v1.wav` — rising glassy chimes and sparkle
- `p4_fail_original_v1.wav` — low digital drop, muted power-down wobble

The exact source prompts were:

- Action: `Original universal P4 console game action: crisp compact activation blip with a quick electric snap and a short forward whoosh, dry mono arcade effect, 0.35 seconds, no music, no voice, no copyrighted sounds.`
- Impact: `Original universal P4 console game impact: compact metallic pop with a tiny digital crackle and a short descending arcade thud, dry mono game effect, 0.45 seconds, no music, no voice, no copyrighted sounds.`
- Reward: `Original universal P4 console game reward: three bright glassy digital chimes rising quickly into a crisp sparkle, dry mono arcade game effect, 0.7 seconds, no music, no voice, no copyrighted sounds.`
- Fail: `Original universal P4 console game fail: gentle low digital drop, short muted power-down wobble, soft final click, dry mono arcade game effect, 0.6 seconds, no music, no voice, no copyrighted sounds.`

The selected WAV SHA-256 values are `3c0660cefe9fdc7ee5e85df9c2ff0df6db9b7a079c29ab935072213ba0bcd48f`
(action), `f10097ecc8183c0c2176b1309fa83d40b264f96650b5e4929cecac1a66ef89e7`
(impact), `d86e1549140c0f523c0d21a5aa9bee1fcf208f9f2c25a32538e84b561cb8c6e7`
(reward), and `402462f00d0c41e117ef5845e29763e34089fc55c3abc3c3d144d4ae6d68d11a`
(fail).

`tools/wav_to_audio_pack.py` deterministically applies a 0.55 gain, converts
to 16 kHz mono PCM16, and bounds the source clips to 0.65, 0.72, 0.80, and
0.48 seconds respectively. `src/audio_pack.c` expands the selected sample to
stereo only in a 256-frame stack buffer before calling the existing copied-PCM
API. This never gives a game an I2S handle or a decoding task.

Regenerate after deliberately replacing a source:

```sh
python3 components/p4_game_api/tools/wav_to_audio_pack.py \
  components/p4_game_api/assets/audio/source \
  components/p4_game_api/src/generated/audio_pack.inc
```

## ImageGen animation provenance

`assets/animation/p4_console_effects_atlas_v1.png` is original built-in
ImageGen output with an alpha channel. It is a transparent 4x4 source sheet:
cyan action burst, orange impact burst, gold reward sparkle, and violet
power-down pulse, with four time-ordered frames per row. The runtime never
opens the PNG.

The built-in ImageGen prompt was:

> Create a single original, clean-room 4 columns by 4 rows pixel-art sprite
> sheet. Each cell is a distinct 40 by 40 pixel animation frame, with generous
> transparent separation between cells. Row 1: cyan action burst growing across
> four frames. Row 2: orange impact burst expanding and fading across four
> frames. Row 3: gold reward sparkle rising and resolving across four frames.
> Row 4: violet fail/power-down pulse collapsing across four frames. Genuinely
> transparent background; crisp 16-bit console pixel art; no text, logos,
> borders, characters, trademarks, or watermark.

`tools/png_to_feedback_atlas.py` nearest-neighbor converts that source to a
160x160 RGB565 include. Alpha below 128 becomes the explicit `0x0000` chroma
key. `p4_game_feedback_draw()` clips the selected 40x40 cell through
`p4_draw_sprite_rgb565()`.

```sh
python3 components/p4_game_api/tools/png_to_feedback_atlas.py \
  components/p4_game_api/assets/animation/p4_console_effects_atlas_v1.png \
  components/p4_game_api/src/generated/feedback_atlas.inc
```
