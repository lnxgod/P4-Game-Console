---
name: esp32-game-art
description: Create or upgrade P4 Console game sprites, textures, launcher artwork and native-resolution UI assets. Use for game art direction, ImageGen asset production, deterministic RGB565 packing and visual review; use the native game skill for gameplay changes.
---

# ESP32 - Game Art

Read `AGENTS.md`, `docs/GAME_ART.md`, `docs/GAME_PERFORMANCE.md` and
`docs/LAUNCH_QUALITY.md`. Inspect the current game and its asset converter before
choosing new art. Preserve gameplay, saves, hitboxes and normalized controls.

Design the complete native 768×480 scene for every maintained Tab5 game:
launcher, opening, active play, menus/pause and results. Establish the actual
runtime surface first; a 320×200 framebuffer or per-title OS downgrade is a
resolution defect, not the art target. Follow **Fix Console** to correct it.
Do not produce a low-resolution visual workaround or upscale a completed frame.

Use crisp exact text for labels, scores and cards. Keep floors/backgrounds
quiet enough that actors, paths, obstacles, pickups and hazards read immediately;
use clear silhouettes, outlines and non-color symbols. Inspect native-size
moving/overlapping states rather than judging texture detail in isolation.
Preserve direct touch interaction and canonical 320×200 input coordinates.
ANSI remixes retain their terminal identity with native glyphs and borders.
Legacy fallback views are compatibility evidence only.

Use the environment's built-in ImageGen tool for new raster illustrations,
sprites and textures. Specify dimensions, camera, palette, atlas cells, padding
and crop-safe bounds; request actual alpha for cutouts. View an existing local
image before editing it. Use editable SVG/code geometry for exact symbols or
an established vector design. Do not pass an unsuccessful generation off as a
finished asset or silently switch to a paid API requiring private credentials.

Save selected source art inside the owning game's assets directory, with its
exact prompt, generator, date, SHA-256 and provenance. Built-in Doom/Chex covers
live under `components/console_shell/assets/game-covers`. Match the actual game
and era; no newer-sequel artwork on a classic-game tile. Keep third-party game
data separate and preserve its acquisition/license policy.

Use the game's deterministic converter and `scripts/pack-game-icon.py`; retain
the editable input and conversion settings. Budget RGB565/alpha/indexed bytes,
cartridge size and working memory before adding detail. Never decode PNGs or
allocate image loaders in the game loop. Re-run conversion and verify identical
output, then inspect packed native-resolution views for clipping, contrast,
glyph readability, transparency fringes and animation continuity.

Use `esp32-test-game` for real SDL play and focused sanitizer checks.
Artwork and host timings cannot qualify the device frame rate. Record actual
P4 cadence and physical feedback separately before declaring release quality.
