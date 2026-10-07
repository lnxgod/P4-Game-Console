# Game presentation standard

Every game on maintained Tab5 must render directly into a **768×480 RGB565**
game framebuffer. Native cartridges require `video-highres` in the manifest's
`required_capabilities` and `P4_GAME_CAP_VIDEO_HIGH_RES` in the C descriptor's
required capabilities. The creator emits both requirements by default;
verify they remain aligned after edits. A 320×200 game framebuffer, an enlarged
completed low-resolution frame, or a title-specific resolution downgrade does
not meet this standard. Pixel art is welcome; compose the scene, glyphs and
controls at the native render resolution.

Check the actual selected surface before changing textures. For device
acceptance, bind a `CARTRIDGE_START ... surface=768x480` record and native-size
active-play capture to the exact cartridge, OS and unit. Optional metadata,
an enlarged window or a scaled screenshot alone cannot prove native rendering.
If the runtime selects 320×200, investigate the manifest, descriptor, OS
`P4_CONSOLE_NATIVE_*_LOW_RES` overrides and surface allocation with **Fix Console**.
Do not simplify artwork around that downgrade and call the visual defect fixed.

Legacy low-resolution ABI/source paths remain preserved for explicitly
requested legacy maintenance; they are not a maintained Tab5 presentation mode.
Engine ports use their owning workflow and must verify their actual game
framebuffer as well. Historical low-resolution results remain historical.

Input and simulation remain independent of render resolution. Native touch
coordinates are always 320×200. Scale drawing or convert a touch point once,
never both. Preserve hitboxes, movement rates, saves, rules and network state
when changing art. Native C is the supported authoring path. Shared helpers
are optional: custom software 2D/3D engines and raycasters may write the
negotiated RGB565 surface within the stable API and resource bounds. Choose
the rendering style for the game, then qualify it against
[the same ESP32-P4 cadence contract](GAME_PERFORMANCE.md).

## Readability and materials

Use deliberate spacing, high contrast and coherent art direction for each
game. Draw small labels, card ranks, suits, scores and control prompts as exact
code or text; never bake important instructions or numbers into generated art.
Use shape, outline or symbol as well as color for important state. Check selected,
disabled, face-down, damaged and overlapping states against the actual material.

`p4/presentation.h` provides inline native helpers: rounded rectangles, clipped
RGB565 sprite scaling and antialiased ASCII text from the pinned Arimo font.
Its font data is linked once per cartridge by the existing SDK and packager;
it adds no runtime imports or ABI change. Coordinates are actual surface pixels;
`p4_ui_x/y` map legacy 320×200 geometry. `p4_ui_text` and `p4_ui_text_width` use
the same pixel-height metrics (28-pixel cell = 24-pixel type). Prefer roughly
22–28-pixel cells for ordinary high-resolution labels; shorten or reflow text
that does not fit. Measure widths rather than assuming fixed-width glyphs.
The shared coverage atlas costs 31,920 bytes plus 95 advances per cartridge that uses
it. Its reproducible generator is
`components/p4_game_api/tools/generate_presentation_font.py`; font source,
pinned hash and OFL license are under `third_party/arimo/`.

## Visual release standard

Arcade, platform and sports remixes should have the cohesive detail of a
polished TurboGrafx-16 or PlayStation-era release or better in the chosen
2D or 3D style: distinct characters, finished environments, clear material and
depth cues, readable hazards, designed title/pause screens, and fluid motion. Review the whole
scene at native size, including the floor, backgrounds, shields, platforms
and unused margins. A high-resolution surface or detailed sprite alone does
not satisfy this standard. Expand cramped playfields where useful while
preserving rules and the canonical touch contract.

Card and tabletop games may use simpler art. Prioritize sharp ranks/suits,
clean spacing, strong contrast, large touch targets and readable score sheets.
Keep genre-appropriate restraint instead of adding texture that hides play.
Record visual review separately from measured performance: maintain the
60 FPS target and 30 FPS floor while adding detail.

## Direct interaction for table games

Card, dice and board games should use the table itself: tap cards, dice and
scores, drag cards or pieces to legal destinations, and keep only meaningful
actions such as Draw, Fold, Roll and New Deal. Do not cover the table with a
virtual D-pad or A/B controller. Keep physical controller and keyboard support
through normalized input, with mappings in help rather than permanent clutter.
Consume touch gestures before synthesized gamepad bits, including their release
frame; otherwise an invisible old controller region can trigger the wrong
action. Show a lifted object and clear legal destinations while dragging, and
cancel invalid or interrupted drops without changing game rules or saves.

## ImageGen source art

Use ImageGen for new or materially revised raster sprites, textures and
animation. Request original, crop-safe assets at sizes that retain detail at
768×480. Specify the atlas grid, cell contents, palette, baseline, padding,
transparency and camera angle. Use a transparent background for cutouts and
an opaque seamless surface for materials. Inspect each cell before conversion.
Avoid tiny sprites enlarged across the screen or decorative noise under text.
Code-native geometry, precise symbols and UI typography remain code-native.

Preserve the source PNG, exact prompt, generation date and SHA-256 in the
owning game's `assets/` directory with an honest provenance notice. Keep a
deterministic converter under `tools/` and a bounded generated RGB565 include
under `src/generated/`. Use nearest-neighbor for intentionally pixelated art;
use a quality offline resampling filter for painted materials. Record the
filter, crop, output dimensions and transparent key. Inspect converted output
for key-color fringes, lost contrast and cropped silhouettes. Runtime code
must not decode PNGs or allocate image loaders.

Budget every atlas as width × height × 2 bytes, plus any alpha data. Prefer
reusable tiles and sensible sprite sizes to a full-screen texture for every
state. Record generated byte totals, final cartridge size and source hash;
keep package/load limits unchanged. Verify converter regeneration is identical.

## Acceptance

Run focused game tests and sanitizer smoke with padded-stride guard checks
at native resolution. Retain legacy-size bounds checks as compatibility tests
only. Inspect native-size title, gameplay, menus and
result screens. Play the real SDL game with keyboard and mouse/touch. Check
text clipping, card overlap, animation, selected states, pause/retry and Back.
Exercise existing session tests when a multiplayer game's renderer changes.
Keep `host-tested`, interactive local play and physical-device acceptance
separate. A new texture or successful build alone is not a polished game.

## Motion and frame budget

Follow the canonical [ESP32-P4 performance contract](GAME_PERFORMANCE.md)
while designing art and animation: bounded shared raster work, fractional
motion, active input traces and exact-device cadence evidence. Native games
target 60 FPS with an actual-device 30 FPS release floor. Keep visual review,
host CPU measurements and physical-device qualification separate.


## Complete game presentation

Every enabled game ships its own validated launcher_icon; a generic launcher
gamepad is not finished artwork. Keep the name and icon recognizable in the
library, the selected-game loading screen, and the game's opening view. Use a
branded title or useful setup screen before time-critical action begins, a clear
in-game name/header without crowding the field, and legible pause/result/retry
states. Card and board games may open directly to their ready table after the
branded launcher transition; do not add a redundant confirmation or permanent
virtual controller to touch-first play. Preserve network start barriers and
never pause only one participant's simulation.

Inspect native-resolution opening, active play, pause, and results. Check quiet
backgrounds against clear actor, obstacle, pickup and hazard silhouettes; texture
detail must not hide paths or game state. Review movement and overlapping effects,
not only a neutral board. Legacy fallback captures establish compatibility only.
Cover-only
work is not evidence of improved gameplay graphics or actual-device cadence.
Future artwork replacements must travel with the cartridge rather than require
a new title/ID map in Console OS. Built-in Doom/Chex use OS-owned cover assets;
their game data stays separate. Retained WIP titles still require their own icons.
