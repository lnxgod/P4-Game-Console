# Space Invaders

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/ARCADE`.

**Package:** `INVADERS.P4G`. **Players:** Solo.

**Local preview:** `make play-game GAME=space_invaders` from the repository root.

Version 1.1.0. Defend against waves of original alien drones and protect the destructible shields.

A wide battle area uses original purple and teal craft, an animated white/cobalt player ship, a painted nebula and planet backdrop, and metal bunkers that visibly break apart. Four-frame ship animation, short formation tweens, bright projectile cores and impact flares bring movement and damage to life. The wider view maps the original simulation bounds consistently; movement speed, formation steps, firing cadence, shield collisions and scoring retain their existing rules.

The opening screen reuses the original nebula and animated ship art with exact title text and a large Play button. Combat, hazards and RNG wait for Play, A or Start. Release the launch key/contact before firing. Left/Right moves; hold A or B to fire at the bounded 220 ms cooldown; Start pauses; A restarts directly into combat; Back exits from every screen. Keyboard bindings in the SDL host are arrows/WASD, Space/Z for A, X/Shift for B, Enter/P for Start, and Escape/Backspace/Q for Back. Touch remains normalized to 320x200 in both display modes.

## Rendering and original art

The game opts into **768x480 RGB565** and retains a 320x200 fallback. It draws directly into the negotiated surface; it does not stretch a rendered low-resolution framebuffer. The high-resolution path uses the shared antialiased Arimo presentation font, native image detail, and a restrained control strip containing only the actions this game uses. During combat, visible Exit, Pause, Left/Right and A/B controls retain their standard canonical touch regions. On the title, Play occupies canonical x191–296/y153–182 and Exit x0–51/y0–23; touch starts only when the contact begins on the visible control. Hidden gameplay touch regions do not start a game. Fallback labels retain their compact pixel font. Rules, game identity, lifecycle, audio, and platform ownership are preserved.

`assets/hires_atlas_v2.png` is original project artwork produced with the built-in ImageGen tool. `assets/hires_provenance.json` records the exact prompts, generation mode, source hashes, measured crop bounds, and conversion geometry. It contains no imported commercial game assets. Game art/code are MIT; the shared Arimo font is OFL-1.1 with its notice in `third_party/arimo/OFL.txt` (pinned source: `third_party/arimo/source.json`).

`tools/convert_hires.py` takes the crop-safe 4x4 sheet to a bounded RGB565 atlas, using Lanczos reduction from the measured original source crops and explicit transparent chroma key 0. Ship crops are fitted to the existing collision silhouettes. The 16-frame **80×44** atlas is **112,640 bytes**, sized for the native ship footprints instead of enlarging 48×32 cells. `assets/space_backdrop_v3.png` is new original ImageGen artwork; its exact prompt, source/output hashes and conversion settings are in `assets/space_backdrop_provenance.json`. `tools/convert_background.py` produces a 384x192 opaque RGB565 background (**147,456 bytes**), sampled directly into the negotiated surface with an optimized two-pixel native horizontal span. Total static game image data is **260,096 bytes**, excluding the launcher icon and shared font. No runtime asset allocation or texture decoder is used.

```sh
python3 games/space_invaders/tools/convert_hires.py
python3 games/space_invaders/tools/convert_background.py
```


## Local verification

```sh
cmake -S games/space_invaders -B build-host/space_invaders -G Ninja
cmake --build build-host/space_invaders
ctest --test-dir build-host/space_invaders --output-on-failure
build-host/space_invaders/space_invaders_hires_preview build-host/space_invaders/hires
cmake -S tools/p4-game-host -B build-host/play-space_invaders -G Ninja -DP4_GAME=space_invaders
cmake --build build-host/play-space_invaders
ctest --test-dir build-host/play-space_invaders --output-on-failure
make play-game GAME=space_invaders
```

`tools/render_hires.c` renders opening, gameplay, pause, damaged-bunker/edge, and game-over captures at both resolutions from the real game sources, then verifies Back exits. The focused sanitizer test and all five generic SDL host checks pass. Focused coverage includes 5,000 input-fuzz updates per resolution with padded-stride guards, formation tween progress and pause freeze, completion before the fastest formation step, rendering without mutating gameplay, transparent destroyed shield cells, and transparent bunker silhouette corners. `LOCAL_TESTING.json` records source/art hashes and the tested scope. These are host results; hardware display, physical input, speaker acoustics, and device performance remain pending. Interactive play is recorded separately by the integrating agent.

The current motion pass interpolates the player and projectiles between the unchanged 16 ms simulation steps. New projectile samples and respawns reset their presentation endpoints, and pause holds them still. At a real 60 Hz cadence, the largest player step falls from **1024 to 544 Q8 units** (about **20 to 11 native pixels**), removing periodic double-steps without changing speed, collisions or firing cadence. Native projectiles now use narrow bright cores instead of wide enlarged logical rectangles.

The background renderer skips the **177 native rows** completely covered by the opaque HUD/control strip. Shield texture coordinates are calculated once per bounded row/map, eliminating a variable division from each shield-pixel iteration in the pinned RV32 `-Os` assembly. All ten native/fallback captures from that motion-pass revision matched scalar reference loops exactly for those raster changes. Evidence is under `build-host/motion-pass/space_invaders/`; no device cycle measurement is implied.

The earlier motion-pass 2,000-frame host CPU trace combined held fire with pulsed fire and reverses movement every 40 frames. Held A/B regressions each produce five launches in 1,008 ms, never faster than 220 ms or beyond two projectiles; releasing stops new shots. Back wins over A in active, paused, and game-over states. Native/fallback frames changed in **1,992/1,990 frames**. Native total p95/p99/max were **0.099/0.111/0.299 ms** and fallback max was **0.069 ms**, with zero frames over 33.333 ms. This excludes device execution and display presentation. Final hashes and actual play observations are recorded in `LOCAL_TESTING.json`.
```sh
cmake --build build-host/play-space_invaders --target p4_game_benchmark
build-host/play-space_invaders/p4_game_benchmark 2000 768 games/space_invaders/tests/performance-input.txt
build-host/play-space_invaders/p4_game_benchmark 2000 320 games/space_invaders/tests/performance-input.txt
```

The current standalone cartridge is **402,512 bytes**, including its 9,744-byte launcher icon, below the 524,288-byte limit. Its package SHA-256 is recorded in `LOCAL_TESTING.json`. This sizing build does not install or flash anything.

The board-independent cartridge is built through `make console-os-tab5-idf` for the primary Tab5 target and installed through its verified native USB content path. Game code consumes only the public `p4/` API; the OS owns display, touch, timing, audio, storage, and lifecycle services.

## Title presentation verification

The presentation pass adds no raster asset bytes and keeps the existing native/fallback art. Focused sanitizer coverage verifies frozen title gameplay/RNG, A and Start launch, canonical Play and Exit through the actual mapper, held launch suppression across repeated update slices, release-to-fire, invalid/dragged-in title touches, direct retry, and Back priority. Existing held-fire, shield, collision, interpolation and padded-stride regressions remain passing. The final focused suite is 1/1 and SDL suite 5/5.

`build-host/presentation-pass/space_invaders/` contains exact-source title/play/pause/result captures at both sizes, pinned RV32 assembly, package log and active 2,000-frame CPU benchmarks. The trace now presses A, releases at frame1 and begins combat controls at frame2; 1,884 native and 1,881 fallback frames change. Native p95/p99/max: **0.118/0.151/0.355 ms**; fallback max: **0.063 ms**. All frames are below33.333ms on the Mac CPU; this is not device FPS. Latest interactive acceptance and hardware limitations remain in `LOCAL_TESTING.json`.

## Maintained Tab5 native rendering

Current package version: **1.1.1**. The native manifest and C descriptor both
require `video-highres`. Maintained Tab5 play renders directly into a **768×480
RGB565** surface. Canonical **320×200** coordinates remain input units for touch
and controls. The retained fallback renderer and earlier fallback guidance are
for explicitly selected legacy diagnostics.

The target is **60 FPS** with an actual-device release floor of **30 FPS**.
Acceptance requires verification of the actual runtime surface and readable
opening/title, busy gameplay, pause, and results views on the exact Tab5 unit,
with the package and Console OS identities recorded. Those device readability
and cadence checks remain pending until measured; host captures and CPU timing
do not establish device acceptance.

`tools/render_hires.c` captures native 768×480 views by default using the actual
required-capability descriptor. Pass `--legacy` after the output prefix to add
explicitly labeled 320×200 diagnostic captures through a temporary legacy
descriptor copy.

`tools/render_preview.c` also captures native 768×480 by default.
