# GameChangersAI OS startup

The 0.44 startup uses a midnight navy background, a restrained cyan perspective
grid, the reviewed transparent GameChangersAI joystick mark, and separate live
version/status lettering. Tab5 renders at the full 1280×720 landscape resolution;
the portable renderer also retains 768×480 and 1152×720 input support.
The caller must do one complete repaint before requesting status-only updates.
The RGB565 + alpha8 asset is sized and hashed at firmware build time.

The Tab5 audio is an original one-second, five-hit treasure fanfare. Four
rising three-note chords lead into a five-voice D-major finish with a low root.
The rhythm is four short strikes and a held fifth: da-na-na-na-naa. Warm synth
harmonics and a sustained envelope replace the earlier single bell-note run.
The fixed-point oscillator renders identical 16 kHz stereo PCM on host and
board, with eight-ms attacks and soft releases. No sampled game recording,
transcribed melody, ATDT/digit dialing or modem noise is included. Saved volume
is applied once by the existing board service; 0 skips the sound.

The boot logo stays visible while the short cue plays; audio does not wait on
animation frames. Tab5 presents the finished desktop in one frame without the
old connection hold or six-step wipe. Storage and game validation still finish
before the fully populated launcher, with their existing checks intact.

This component owns no audio, display, input or storage hardware. The app
retains the board service lifecycle, audio cleanup and display fences.
Actual speaker sound, uninterrupted playback and display timing need Tab5
acceptance; the WAV and rendered frames are host previews only.

## Local verification

```sh
make console-startup-host
make console-shell-host
cmake -S components/platform_display -B build-host/platform_display -G Ninja
cmake --build build-host/platform_display
ctest --test-dir build-host/platform_display --output-on-failure
P4_TAB5_USB_HOST=1 make console-os-tab5-idf
```

The startup test writes `build-host/console_startup/boot.ppm` and `fanfare.wav`.
It checks logo size rejection, row/stride bounds, incremental rendering,
PCM duration/headroom/DC level, stereo identity and arbitrary chunk boundaries.
Shell tests cover native Tab5 navigation, all game-library pages, bounded rendering,
touch cancellation, service actions and confirmation safety. Separate tests preserve
the compact and Waveshare renderers; the legacy Tab5 BBS interface is disabled.

See `apps/console_os/main/assets/gamechangers_mark_v042.json` for the built-in
ImageGen prompt/provenance and `third_party/arimo/` for font provenance/license.

## Tab5 flight boot (OS 0.50)

The Tab5 renderer uses the reviewed alpha joystick sprite and a time-based perspective fly-in with orbital lights. Stationary status and three activity dots replace the marquee. A bounded startup worker owns the UI framebuffer during initialization and yields at a 60 ms target cadence. Home joins the worker before drawing, with no minimum animation duration or final hold. Fatal diagnostics also acquire ownership before reusing the framebuffer. Audio remains the original one-second chord fanfare.

Run `make console-startup-host` for ASan/UBSan validation. It compares incremental frames with full redraws at multiple elapsed times and supported sizes, checks completion independently of time, and guards malformed buffers and row padding. Repack with `components/console_startup/tools/pack_flight_assets.py` using Python with Pillow; PNG and font source digests are pinned. Preview output is under `design/tab5-nextgen/boot-flight/`. Host preview timings do not establish tablet frame rate.
