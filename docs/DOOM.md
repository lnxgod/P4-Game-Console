# Doom acceptance design

Doom is the first end-to-end client of the badge platform. It must exercise the same video, storage, audio, timing, and controller APIs available to every later game; it must not own board drivers.

## Engine and source policy

Use upstream [doomgeneric](https://github.com/ozkl/doomgeneric) at the exact commit recorded in `third_party/source-lock.json`. Its narrow platform boundary is a good fit for an embedded acceptance app. Use [ESP32P4DOOM](https://github.com/alexkid77/ESP32P4DOOM) only as a reviewed implementation reference for ESP32-P4 framebuffer scaling, memory, SD, and audio patterns.

Do not import the ESP32P4DOOM repository or its history wholesale: its tree includes a Doom WAD, and its board layer targets Espressif hardware rather than this CrowPanel. Import doomgeneric from its clean upstream and preserve its GPL notices.

Linking doomgeneric makes the distributed Doom firmware a GPL-covered work. Keep `apps/doom` as an acceptance application over separately reusable platform components, publish its corresponding source/build inputs with any distributed image, and perform a license audit before a public release.

## Local game data

The current development default is the unmodified Doom v1.9 shareware IWAD at the ignored path `local-data/doom/doom1.wad`. It remains outside Git and outside firmware images. Its expected identity is recorded in `third_party/game-data.json`; tests fail closed if a data entry with a required hash does not match.

The source distribution for the shareware IWAD was id Software's `doom19s.zip`, historically distributed through the `/idgames/idstuff/doom` archive. The exact archive and local WAD acquisition URLs and both sets of hashes are recorded in `third_party/game-data.json`. This project does not mirror or redistribute either the archive or IWAD. Keep the original shareware package notices with any independently redistributed copy and reassess distribution rights before a public badge release.

[Freedoom 0.13.0](https://github.com/freedoom/freedoom/releases/tag/v0.13.0) remains the fully redistributable public-release/reproducibility option. A user may also pass a legally owned WAD explicitly.

The intended first search order on a FAT32 microSD is:

1. `/sdcard/doom1.wad` for the current shareware development case
2. `/sdcard/freedoom1.wad`
3. `/sdcard/freedoom2.wad`

If neither exists, report the attempted paths and stop cleanly. Do not add an embedded, SPIFFS, download, or commercial-WAD fallback. Preserve the Freedoom BSD-3-Clause notice when distributing its data.

## Platform boundary

The doomgeneric adapter supplies only engine-facing operations:

- initialize and present a 320×200 framebuffer;
- read time and sleep/yield without owning the scheduler;
- consume normalized platform input and emit Doom key events;
- use standard file operations routed to the mounted storage service;
- submit mixed PCM through the platform audio service.

The display service owns RGB565 conversion, aspect policy, PPA scaling, scanout buffers, and backlight. Start with an integer-safe 4:3 game viewport centered in 1024×600; make stretch/full-screen a later option. The gamepad adapter samples one canonical snapshot per Doom tic and must never see USB handles or raw HID bytes.

## Bring-up stages

### D0 — engine host proof

D0 is implemented as a silent, headless native build. The adapter uses a 320×200 framebuffer, renders a finite number of frames, prints a diagnostic framebuffer checksum, and exits. It does not contain display, audio, input, USB, SD, or CrowPanel pin code.

The vendored engine is upstream commit `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`, root tree `413539bdaa1521af167d9b34e9db0cd193367624`. `make doom-provenance` verifies the locked manifest hash, every vendored file against its upstream Git blob ID, and the presence of the GPL license/readmes. To reproduce the import from a clean local checkout, or by fetching the locked repository when no checkout is supplied:

```sh
make doom-vendor DOOMGENERIC_SOURCE=/absolute/path/to/doomgeneric
make doom-vendor
```

The vendor command is deliberately non-destructive: it refuses to replace a divergent vendor tree or manifest.

Build and run the D0 proof with:

```sh
make doom-host
make doom-smoke WAD=/absolute/path/to/freedoom1.wad DOOM_FRAMES=8
```

Without `WAD=`, the smoke test uses the ignored `local-data/doom/doom1.wad` shareware development copy and verifies its size and SHA-256 against `third_party/game-data.json`. `WAD` may instead name another readable IWAD/PWAD; an entry in the manifest with the same local path is verified when present. A WAD inside the repository must be covered by `.gitignore`. The smoke test runs in a temporary working directory, so generated configuration never enters the source tree. It passes only after the engine presents the requested number of frames and emits `P4_DOOM_D0 PASS`.

This is `host-tested` evidence only. It proves the clean engine builds and reaches rendered frames with the supplied game data; it does not prove gameplay, pixel correctness, cross-platform checksum determinism, ESP32-P4 compilation, memory placement, display scanout, audio, SD, or controller input.

### D0.5 — ESP32-P4 cross-build and whole-archive link

D0.5 is complete as `build-tested` evidence. The pinned ESP-IDF 5.5.3 toolchain cross-compiles all 80 sources in the same feature-reduced doomgeneric engine set for `esp32p4` revision 1.0 through 1.99. The engine component is linked with `--whole-archive`; the verifier proves that the selected source list, archive, and final linker map each contain the same 80 objects. This is stronger than compiling a library that the linker could later discard.

The project-owned ESP-IDF seam supplies bounded timing and neutral-input hooks but does not call `doomgeneric_Create`. `app_main` reports its build-only status and returns. It owns no display, storage, audio, SD, USB, or board pins, and no WAD is embedded. The repository flash wrapper rejects both full-project and app-only writes for this target before it probes hardware because `apps/doom/app-metadata.json` marks them unauthorized.

Build and verify the link contract with:

```sh
make doom-idf
```

The recorded reproducible image is 475,776 bytes (`0x74280`) in a 1 MiB app partition, leaving 572,800 bytes. Two independent build directories produced identical BIN and ELF files. The clean pinned-compiler build surfaced 43 warnings in immutable legacy upstream code plus one newlib linker warning that `mkdir` is unavailable; the project-authored seam remains under ESP-IDF's fatal-warning policy. Warning counts are recorded diagnostics, not a compiler-independent threshold. The exact artifact hashes and warning-class counts are in `test-runs/2026-08-12-doom-d05-idf-build.json` and are checked against a local build by `scripts/doom/verify-idf-build.py`.

D0.5 was not flashed or executed. It does not prove boot, WAD loading, memory placement/headroom, rendering, SD, audio, USB, input, or gameplay. Those remain on-device work beginning at D1.

### D1 — silent first frame

- Confirm the physical panel identity and PCB revision, then qualify the display diagnostic first.
- Mount SD in the board's conservative mode and validate the WAD header/size.
- Allocate Doom's zone and framebuffers from PSRAM with measured internal-heap headroom.
- Render a stable title/menu or first frame without touch, USB, or audio enabled.

### D2 — playable input

- Prove USB Host electrical safety with the powered fixture.
- Pass the standalone gamepad diagnostic for one named generic HID controller.
- Map movement, turn, fire, use, run, weapon, pause, escape, enter, and menu directions from `gamepad_core`.
- Keep UART/USB keyboard fallback during bring-up.
- Verify unplug-while-held produces releases by the next Doom tic.

### D3 — sound and persistence

- Reuse Doom's mixer but send PCM through the CrowPanel's direct I²S/NS4168 service.
- Sound effects are complete. The E6 successor adds the real WAD MUS tracks
  with a bounded 140 Hz, 16-voice procedural synth mixed on the audio worker;
  use `docs/DOOM_MUSIC_TEST.md` for its separate timing/acoustic acceptance.
- Put saves/configuration on SD through the storage service and verify recovery from interrupted writes.

### D4 — endurance acceptance

- Record frame/tic timing, underruns, heap minima, PSRAM use, stack high-water marks, and controller latency.
- Run repeated cold boots, hotplug/reconnect, level transitions, saves/loads, and at least a 60-minute gameplay soak.
- Reboot or fail cleanly on unrecoverable resource errors; never spin silently.

## Initial budgets to measure

These are measurement gates, not assumed guarantees:

- 320×200 8-bit Doom framebuffer: 64,000 bytes before conversion.
- 1024×600 RGB565 scanout buffer: 1,228,800 bytes per full buffer.
- Doom zone target from the reference port: approximately 6 MiB in PSRAM.
- Preserve internal SRAM for ISR/DMA-capable allocations and stacks; do not let generic `malloc` placement hide DMA constraints.

Finalize buffer count, pixel conversion strategy, target frame rate, and audio buffer depth from diagnostics on the confirmed panel.
