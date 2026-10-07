# Frog Hop

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/ARCADE`.

**Package:** `FROGHOP.P4G`. **Players:** Solo.

**Local preview:** `make play-game GAME=frog_hop` from the repository root.

Version 1.1.1. Guide the frog across traffic and floating logs, then fill all five lily-pad homes.

The native view adds an 80 ms visual hop with crisp lime frog animation, four distinct car designs, bark-textured logs, flowered home pads, an original flowing-water texture, clover-covered banks, granular asphalt, pebbled shore, and a larger original frog illustration on the title panel. Pause and result panels dim the live scene while preserving context. Vehicle art spans the original collision widths and HUD labels avoid the Exit and Start buttons.

Arrows or the touch D-pad hop. A or Start begins; Start pauses; Back exits. Keyboard bindings in the SDL host are arrows/WASD, Space/Z for A, X/Shift for B, Enter/P for Start, and Escape/Backspace/Q for Back. Touch remains in canonical 320×200 input units on the native surface and in explicit legacy diagnostics.

## Rendering and original art

Version 1.1.1 requires `video-highres` in the manifest and C descriptor and renders directly at **768×480 RGB565** on maintained Tab5. The 320×200 source path remains only for explicitly requested legacy maintenance and labeled host diagnostics. It draws directly into the negotiated surface; it does not stretch a rendered low-resolution framebuffer. The high-resolution path uses the shared antialiased Arimo presentation font, native image detail, and translucent standard controls. Explicit legacy diagnostics retain their compact pixel font. Game identity, audio, and platform ownership are preserved. The collision recovery timer now advances while movement is locked, fixing an existing first-collision freeze; the impact burst is distinct from lily-pad homes.

`assets/hires_atlas_v2.png` is original project artwork produced with the built-in ImageGen tool. `assets/hires_provenance.json` records the exact prompts, generation mode, source hashes, measured crop bounds, and conversion geometry. It contains no imported commercial game assets. Game art/code are MIT; the shared Arimo font is OFL-1.1 with its notice in `third_party/arimo/OFL.txt` (pinned source: `third_party/arimo/source.json`).

`tools/convert_hires.py` takes the crop-safe 4x4 sheet to a bounded RGB565 atlas, using Lanczos reduction from the measured original source crops and explicit transparent chroma key 0. Vehicle crops are fitted to existing collision silhouettes; character proportions are retained. Gameplay sprites use **184,320 bytes**. `tools/convert_environment.py` adds **108,544 bytes** for four 128×80 RGB565 ImageGen material tiles and a 128×104 title frog sampled from the original high-resolution source; combined image data is **292,864 bytes**. `assets/environment_provenance.json` records the exact original prompt and conversion bounds. No runtime asset allocation or texture decoder is used.

```sh
python3 games/frog_hop/tools/convert_hires.py
python3 games/frog_hop/tools/convert_environment.py
```

The older `*_v1.png` sources and old atlas converter remain as historical art and are no longer included by the runtime.

## Local verification

```sh
cmake -S games/frog_hop -B build-host/frog_hop -G Ninja
cmake --build build-host/frog_hop
ctest --test-dir build-host/frog_hop --output-on-failure
# Default captures use the maintained 768×480 surface.
build-host/frog_hop/frog_hop_hires_preview build-host/frog_hop/hires
# Add explicitly labeled legacy diagnostic captures only when needed.
build-host/frog_hop/frog_hop_hires_preview build-host/frog_hop/diagnostic --legacy
cmake -S tools/p4-game-host -B build-host/play-frog_hop -G Ninja -DP4_GAME=frog_hop
cmake --build build-host/play-frog_hop
ctest --test-dir build-host/play-frog_hop --output-on-failure
make play-game GAME=frog_hop
```

`tools/render_hires.c` renders opening, gameplay and pause captures at 768×480 by default; `--legacy` adds labeled 320×200 diagnostics from the real game sources, then verifies Back exits. The focused sanitizer test and all five generic SDL host checks pass. `LOCAL_TESTING.json` records source/art hashes and the tested scope. These are host results; hardware display, physical input, speaker acoustics, and device performance remain pending. Interactive play is recorded separately by the integrating agent.

The current motion pass interpolates traffic and logs between the unchanged 20 ms world steps, with the carried frog using the same offset. Vehicle colors remain stable through lane wrapping. Water has a separate continuous clock, so hopping cannot jump the texture phase; pause freezes the scene. At a 60 Hz sample cadence, the lane trace changes from **10 repeated positions per second to zero**, and its largest Q8 displacement drops from **512 to 435**. The 1 ms regression also covers wrap continuity, frog/log alignment, and recovery.

Native repeating material rows now use six or seven bounded `memcpy` spans instead of per-pixel masked addressing. The pinned RV32 `-Os` assembly and scalar-reference proof are in `build-host/motion-pass/frog_hop/`; all six native/fallback captures from that motion-pass revision matched the scalar material loop exactly. This is an instruction-path comparison, not measured device cycles.

The earlier motion-pass optimized host CPU benchmark ran 2,000 frames with `tests/performance-input.txt`, including movement, actions and the real tone/PCM mixer. The updated scene changed in **1,615 frames**; it now freezes during game-over waits instead of animating them. Native total p95/p99/max were **0.444/0.477/0.533 ms** and fallback max was **0.122 ms**, with zero frames over 33.333 ms. These are Mac CPU results and do not establish tablet FPS. That earlier cartridge size is historical; the recorded 1.1.0 presentation candidate was **487,396 bytes**. Final hashes, actual play observations and remaining device checks are recorded in `LOCAL_TESTING.json`.
```sh
cmake --build build-host/play-frog_hop --target p4_game_benchmark
build-host/play-frog_hop/p4_game_benchmark 2000 768 games/frog_hop/tests/performance-input.txt
```

The board-independent cartridge is built through `make console-os-tab5-idf` for the primary Tab5 target and installed through its verified native USB content path. Game code consumes only the public `p4/` API; the OS owns display, touch, timing, audio, storage, and lifecycle services.

## Persistent header verification

The native and fallback HUD retains FROG HOP above clearly labelled score, lives and filled homes, alongside the round number. It stays inside the original18-canonical-pixel HUD; course geometry, touch controls, collisions and motion are unchanged. Real-source captures include UINT32_MAX score, round255 and all five homes to check worst-case spacing. No raster assets were added.

`build-host/presentation-pass/frog_hop/` binds native/fallback title, gameplay, pause and header-limit captures, focused sanitizer1/1, SDL5/5, pinned RV32 assembly and a 2,000-frame active CPU trace. Native p95/p99/max: **0.441/0.461/0.530 ms**; fallback max: **0.121 ms**. Both sizes change1,615frames; game-over waits are deliberately static. These are desktop CPU measurements, not hardware frame-rate acceptance.

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

Checkpoint `41dfbe6` records focused ASan/UBSan and native SDL smoke checks
for its source revision. These historical host results do not qualify the
merged Console OS or establish physical device acceptance.
