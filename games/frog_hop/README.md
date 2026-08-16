# Frog Hop

Frog Hop is an original, native P4 Game API v1 frog-crossing arcade game.
Guide the frog across moving cars and floating logs, then fill all five home
pads. The launcher discovers `game.json` automatically and places it under
`GAMES/ARCADE`.

Use arrows or the touchscreen D-pad to hop one tile at a time. Press A or
Start on the animated title screen to begin, Start to pause, and the on-screen
Exit control to return to the launcher. It has bounded tone cues for hopping,
crashes, goals, and a cleared round.

`assets/frog_hop_animation_atlas_v1.png` is original, generated 8-bit frame
art. It is a transparent 4x4 atlas: frog hop, car movement, log bob, then
lily-pad/ripple frames. Convert it deterministically before editing its
generated include:

```sh
python3 games/frog_hop/tools/png_to_animation_atlas.py \
  games/frog_hop/assets/frog_hop_animation_atlas_v1.png \
  games/frog_hop/src/generated/frog_hop_animation_atlas.inc
```

The game uses only the stable `p4/` headers. Keep board drivers and raw
ESP-IDF peripheral ownership in platform components.
