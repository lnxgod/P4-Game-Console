# Skyline Leap

Skyline Leap is an original, four-stage native P4 Game API v1 rooftop
platform game. Guide a courier over short rooftop routes, collect three signal
shards, stomp wind-up patrol bots, and reach the open beacon. It is a
clean-room platform-game tribute: it does not use Mario, Nintendo characters,
names, stages, art, sound, music, or level layouts.

The launcher discovers `game.json` automatically and puts it in
`GAMES/PLATFORM`. Use arrows or the touchscreen D-pad to move, A or Up to
jump, B to launch a pulse that disables patrol bots, Start to pause, and the Exit control to return to the
launcher. The campaign contains four fixed stages, a three-life run, restart,
title, pause, route-clear, and end screens, plus bounded original tone cues.

## Cartridge

Skyline Leap's source, artwork, manifest, and package identity live together
in this folder. Its manifest produces `SKYLINE.P4G`; after an Elecrow Console
OS build, the USB-share-ready cartridge is generated at
`apps/console_os/build/game-storage-seed/GAMES/SKYLINE.P4G`. Copy that file to
the root of the console's `P4 GAMES` USB share when the device is connected. The
generated cartridge is a build artifact, so the repository tracks this source
folder rather than a stale binary copy.

## ImageGen art provenance

`assets/skyline_leap_animation_atlas_v1.png` is original ImageGen output with
an alpha channel. It is a 4x4 source atlas: courier run/jump frames, courier
state frames, patrol-bot frames, then shard/keycard/spring/beacon props. The
runtime never opens this PNG. `tools/png_to_animation_atlas.py` nearest-neighbor
scales it to a 160x100 RGB565 atlas with transparent pixels mapped to chroma
key `0x0000`, producing the committed fixed-size include.

The built-in ImageGen prompt was:

> Create one transparent, crop-safe 4x4 pixel-art sprite atlas for an embedded
> retro platform game: an original rooftop courier in eight motion/state poses,
> a round wind-up patrol bot in four poses, and four original collectible/goal
> props. Use hard pixels, a limited navy/teal/coral/gold palette, shared
> baselines, no text, no borders, no scenery, and no copyrighted characters,
> logos, pipes, mushrooms, turtles, question blocks, or castle motifs.

Regenerate the runtime include after intentionally replacing the source PNG:

```sh
python3 games/skyline_leap/tools/png_to_animation_atlas.py \
  games/skyline_leap/assets/skyline_leap_animation_atlas_v1.png \
  games/skyline_leap/src/generated/skyline_leap_animation_atlas.inc
```

The source image was generated through the built-in ImageGen tool. It is
original project art under this game's MIT license; no third-party game assets
are included.

`assets/preview_title_v1.png` and `assets/preview_gameplay_attack_v1.png` are
host-rendered inspection captures made by `tools/render_preview.c`; they are
not runtime assets.
