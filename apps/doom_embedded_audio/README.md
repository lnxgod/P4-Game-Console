# Doom embedded audio E2 composite

This is a new, build-only composite. It does not replace or modify the exact
`doom_embedded` E1 image or its evidence. It combines the same ignored local
Doom 1.9 shareware WAD/read-only VFS and display/video path with the isolated
`doom_engine_audio`, audited `doom_audio` SFX runtime, and direct-I2S
`platform_audio` API.

The app deliberately has neutral input. The panel's USB-C host data port
cannot safely power a controller without a protected, current-limited,
backfeed-isolated fixture, so USB is not initialized here.

## Lifecycle contract

- `platform_audio_force_safe_shutdown()` is the literal first executable
  statement, before any startup log, power, bus, or clock action.
- `platform_display` exclusively acquires and releases LDO3/LDO4 and starts
  dark; audio never owns those rails.
- Audio is created with no control bus at 16 kHz and 10 percent bring-up
  volume, then bound before `doomgeneric_Create()`.
- Sound effects are enabled; music remains disabled with `-nomusic`.
- The early Doom exit callback forces the amplifier safe first, always asks
  the runtime to stop, and attempts to unbind. It destroys the backend only
  after successful unbind or when binding never succeeded. It then calls
  `platform_audio_recover()` to prove that a failed create left no hidden I2S
  owner. A timeout, failed destroy, or failed recovery retains the borrowed
  backend, sample memory, display, and LDO3/LDO4 instead of racing a worker or
  removing rails from a live/unknown owner.
- Display teardown follows audio teardown and always requests a dark
  backlight first.

The E2 memory policy uses a 1 KiB `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold so
the roughly 533 KiB SFX precache is eligible for PSRAM. Serial heap snapshots
cover pre/post backend creation and the first post-engine frame. Periodic
runtime counters expose command drops, write failures, and the worker's
self-published minimum stack headroom in bytes. A `UINT32_MAX` worker value
means the task has not yet sampled itself; the app never borrows a task handle.

## Current gate

All flash and runtime authorization flags are false. The direct-I2S backend is
reviewed and the composite's canonical plus two independent clean builds are
byte-identical. The dedicated build-only gate is:

```sh
python3 scripts/verify-doom-embedded-audio.py \
    apps/doom_embedded_audio/build build-only
```

`app-flash` mode is deliberately fail-closed until the separate D2.3 reusable
backend acoustic acceptance passes, an exact connected-unit E2 one-shot
authorization record is bound, and this verifier is added to the central
no-reset readback/single-deferred-launch path. Full-project flash is never an
accepted verifier mode. The first E2 hardware acceptance must retain Doom
video statistics, show zero audio command drops/write failures, capture heap
and stack telemetry, and have a person directly confirm recognizable SFX.
