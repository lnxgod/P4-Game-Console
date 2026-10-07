# Skyline Leap

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Developer install only; unfinished. **Folder:** `GAMES/WIP`.

**Package:** `SKYLINE.P4G`. **Players:** Solo.

Hidden from standard installs; see [developer installs](../README.md#developer-installs).
Game IDs and saved progress are preserved.

**Local preview:** `make play-game GAME=skyline_leap` from the repository root.

Version 1.1.0. Cross four rooftop routes, collect three signal shards, disable patrol bots, and reach the open exit.

An original yellow-jacket courier, animated purple robots, crystal pickups, exit doors, a detailed ImageGen coastal city background, and riveted metal platforms make the 768x480 view distinct. Courier art follows facing, jump/fall, firing, and hurt states without changing physics. Four route tints distinguish the stages.

Left/Right moves; A or Up jumps; B fires a pulse; Start pauses; A begins/restarts; Back exits. Keyboard bindings in the SDL host are arrows/WASD, Space/Z for A, X/Shift for B, Enter/P for Start, and Escape/Backspace/Q for Back. Touch remains normalized to 320x200 in both display modes.

## Rendering and original art

The game opts into **768x480 RGB565** and retains a 320x200 fallback. It draws directly into the negotiated surface; it does not stretch a rendered low-resolution framebuffer. The high-resolution path uses the shared antialiased Arimo presentation font, native image detail, and translucent standard controls. Fallback labels retain their compact pixel font. Rules, game identity, lifecycle, audio, and platform ownership are preserved.

`assets/hires_atlas_v2.png` is original project artwork produced with the built-in ImageGen tool. `assets/hires_provenance.json` records the exact prompts, generation mode, source hashes, measured crop bounds, and conversion geometry. It contains no imported commercial game assets. Game art/code are MIT; the shared Arimo font is OFL-1.1 with its notice in `third_party/arimo/OFL.txt` (pinned source: `third_party/arimo/source.json`).

`tools/convert_hires.py` takes the crop-safe 4x4 sheet to a bounded RGB565 atlas, using nearest-neighbor sampling and explicit transparent chroma key 0. Character proportions are retained within their measured atlas cells. The skyline image is filtered once at conversion to 384x154; the runtime never decodes PNGs. Total new static game image data: **323,072 bytes**. No runtime asset allocation or texture decoder is used.

```sh
python3 games/skyline_leap/tools/convert_hires.py
```

The older `*_v1.png` sources and old atlas converter remain as historical art and are no longer included by the runtime.

## Local verification

```sh
cmake -S games/skyline_leap -B build-host/skyline_leap -G Ninja
cmake --build build-host/skyline_leap
ctest --test-dir build-host/skyline_leap --output-on-failure
build-host/skyline_leap/skyline_leap_hires_preview build-host/skyline_leap/hires
cmake -S tools/p4-game-host -B build-host/play-skyline_leap -G Ninja -DP4_GAME=skyline_leap
cmake --build build-host/play-skyline_leap
ctest --test-dir build-host/play-skyline_leap --output-on-failure
make play-game GAME=skyline_leap
```

`tools/render_hires.c` renders opening, gameplay, and pause captures at both resolutions from the real game sources, then verifies Back exits. The focused sanitizer test and all three generic SDL host checks pass. `LOCAL_TESTING.json` records source/art hashes and the tested scope. These are host results; hardware display, physical input, speaker acoustics, and device performance remain pending. Interactive play is recorded separately by the integrating agent.

The optimized host CPU benchmark runs 2,000 frames of movement and action inputs from `tests/performance-input.txt`, including the real tone/PCM mixer. At 768x480, total update/audio/render time was **0.265 ms at p99** and **0.405 ms maximum**, with no frame above 33.3 ms; the 320x200 run also had no misses. This excludes display presentation and ESP32 execution, so it does not establish hardware frame rate. The simulator independently targets 60 Hz and reports measured presentation FPS in its title. Full results and input/source hashes are in `LOCAL_TESTING.json`.

```sh
cmake --build build-host/play-skyline_leap --target p4_game_benchmark
build-host/play-skyline_leap/p4_game_benchmark 2000 768 games/skyline_leap/tests/performance-input.txt
build-host/play-skyline_leap/p4_game_benchmark 2000 320 games/skyline_leap/tests/performance-input.txt
```

The board-independent cartridge is built through `make console-os-tab5-idf` for the primary Tab5 target and installed through its verified native USB content path. Game code consumes only the public `p4/` API; the OS owns display, touch, timing, audio, storage, and lifecycle services.
