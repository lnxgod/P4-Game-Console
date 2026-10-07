# Art and spatial rendering

Read the artwork section when adding sprites or materials. Read the spatial
section for projected marble, tilting-tray or water-maze scenes. Use the
Game Art skill for the complete presentation and provenance contract.

## Make high-resolution art usable in firmware

Use ImageGen for new or materially revised raster art. Request crop-safe
sprite atlases or seamless materials sized for their native 768x480 use.
Describe the grid, frame contents, shared baseline, palette and transparency;
keep important text and symbols code-rendered. Inspect converted artwork at
native size. Pixel art may retain hard edges, but it is not the default style
for every game; follow the user's art direction and `docs/GAME_ART.md`.

- Keep the original source PNG under `games/<slug>/assets/` and explain its
  provenance, row/frame layout, and license in that game's README.
- Commit a game-local converter under `games/<slug>/tools/` which takes the
  source PNG to a fixed-size RGB565 include under `src/generated/`. Use
  nearest-neighbor scaling for pixel art or a recorded quality offline filter
  for painted materials, and map transparency to one explicit chroma key.
  Never decode PNGs, allocate image buffers, or use a texture loader at runtime.
- Bound every atlas dimension and account for `width * height * 2` bytes in
  the static firmware budget. Draw an individual cell with
  `p4_draw_sprite_rgb565()` using the atlas row stride and its frame offset.
- Advance frames from bounded game state and draw the same cell family in the
  title/attract view and gameplay. Animate only state that the game owns;
  hardware timing remains the host's responsibility.
- Record generated art accurately in `game.json`'s `assets` field. Never
  describe generated sprites as code-rendered-only or as licensed third-party
  art.

## Match the intended spatial experience

For a physical marble, tilting tray or water-maze request, establish the camera
and depth in the first playable scene. When the user expects 3D or 3D-like play,
use a native projected scene with visible wall faces, occlusion, shaded actors
and a water surface that responds continuously to the simulation. A flat grid
with decorative ripples does not meet that brief. Reuse bounded rendering
helpers where useful; a game-local C renderer may own the camera, projection
and scene. Put reusable services in `components/`. Preserve the stable Game API
lifecycle, supplied surface, resource bounds and performance contract for every
rendering path. Use the native cartridge path for this class of real-time game.

Show an actual native-resolution gameplay capture early, before lengthy polish
or packaging. A concept image is not evidence of the renderer. Check motion as
well as a still: interpolate local fixed-step actors, preserve fractional
projection, handle respawns without tweening across walls, and invert the
actual camera for touch targeting. Keep input, physics and multiplayer rules
independent of the view. A rejected visual direction requires a scene/rendering
change, not just another texture on the same presentation.
