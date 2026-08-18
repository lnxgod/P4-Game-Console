# Console API shared feedback pack

This component owns the reusable, host-safe feedback pack for native Console
API games. It contains original audio and animation assets only; games use its
bounded `p4/` interfaces rather than decoding media or acquiring hardware.

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

`assets/animation/p4_console_effects_atlas_v2.png` is the active original
built-in ImageGen output with an alpha channel. It is a transparent 4x4 source
sheet: cyan directional action burst, orange impact burst, gold reward
sparkle, and violet power-down pulse, with four time-ordered frames per row.
It deliberately uses high-contrast silhouettes and generous cell padding so
the feedback remains readable on the Console OS 320x200 surface. The runtime
never opens the PNG.

The built-in ImageGen prompt was:

> Create a single original, clean-room 4 columns by 4 rows pixel-art sprite
> sheet. Every cell is a distinct, centered 40 by 40 pixel animation frame;
> leave 6 pixels of genuinely transparent padding inside every cell and keep
> each effect readable when reduced to a 320x200 16-bit handheld screen. Row
> 1: cyan directional action pulse that travels right and blooms. Row 2: orange
> impact burst that expands then fades. Row 3: gold reward sparkle that rises
> and resolves. Row 4: violet fail/power-down pulse that collapses inward.
> Crisp high-contrast 16-bit console pixel art; no text, logos, borders,
> characters, trademarks, or watermark.

The selected v2 source SHA-256 is
`9527cc5dace3e2275bdae75971e65810a16c1a7d5a871b97da241075be7fc073`.
The prior `p4_console_effects_atlas_v1.png` and `feedback_atlas.inc` remain in
the repository as provenance for the initial shared pack; v2 supersedes them
at runtime.

`tools/png_to_feedback_atlas.py` nearest-neighbor converts that source to a
160x160 RGB565 include. Alpha below 128 becomes the explicit `0x0000` chroma
key. `p4_game_feedback_draw()` clips the selected 40x40 cell through
`p4_draw_sprite_rgb565()`.

```sh
python3 components/p4_game_api/tools/png_to_feedback_atlas.py \
  components/p4_game_api/assets/animation/p4_console_effects_atlas_v2.png \
  components/p4_game_api/src/generated/feedback_atlas_v2.inc
```
