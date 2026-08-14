# Doom sound-effect integration probe

This permanently non-flashable app cross-compiles the opt-in sound path from
the pinned Doom engine to the current `platform_audio` API. It does not start
Doom, create a control bus, initialize the direct-I2S backend, acquire display
rails, or touch GPIO/I2S. The promoted `platform_audio` dependency is the
direct-I2S backend; this link probe carries no codec, control-bus, or I2C
dependency.

The reusable sound code lives in `apps/doom/components/doom_audio`. The
probe-local `components/doom_engine_audio` is a quarantined FEATURE_SOUND
engine variant; it deliberately leaves the reviewed silent `doom_engine`
component and exact `doom_embedded` artifact inputs unchanged. Together they
provide:

- a bounded parser for Doom's unsigned 8-bit mono DMX sound lumps;
- an eight-voice fixed-point mixer producing 16 kHz signed PCM16 stereo;
- a 32-entry SPSC command ring whose Doom-facing operations never wait;
- an audio worker that alone performs blocking `platform_audio_write_frames`;
- fail-quiet handling for invalid lumps, a full command ring, write failure,
  and shutdown timeout;
- a pinned-engine `DG_sound_module`; music intentionally stays disabled.

Run the host sanitizer suite and pinned target link build with:

```sh
make doom-audio-host
make doom-audio-idf
```

## Composite Doom integration after the audio hardware gate passes

Do not enable this in `doom_embedded` until the corrected standalone audio
diagnostic has hardware evidence and the composite image receives its own
review. The exact currently proven silent image must remain available.

The exact Elecrow factory source uses a fake in-memory codec control object and
direct I2S1 speaker output; it does not perform ES8311 hardware I2C control.
The powered ES8311 diagnostic also observed no ACK. Therefore the composite
must follow the independently accepted direct-I2S `platform_audio` backend,
not the obsolete ES8311 candidate, unless later physical evidence proves a
different populated path.

1. Add `apps/doom/components/doom_audio` and a reviewed copy of this probe's
   `components/doom_engine_audio` to the composite app's
   `EXTRA_COMPONENT_DIRS`; require `doom_engine_audio` instead of the silent
   `doom_engine`. Add the accepted `platform_audio` backend requirements. Use
   that backend's committed lock; do not add `esp_codec_dev` or a control bus
   unless its hardware path has separately passed. Never retrofit FEATURE_SOUND
   into the reviewed shared `doom_engine`, because doing so invalidates the
   frozen `doom_embedded` evidence.
2. Enable `CONFIG_DOOM_AUDIO_ENGINE_ADAPTER=y` and the accepted backend's
   narrowly reviewed build option. The mixer/runtime itself has no codec,
   I2C, MCLK, GPIO, or rail dependency.
3. Make `platform_audio_force_safe_shutdown()` the first hardware-facing app
   action. Then initialize `platform_display`; the display service remains the
   sole owner of LDO3 at 2.5 V and LDO4 at 3.3 V.
4. After `platform_display_init()` succeeds, create `platform_audio` at 16,000
   Hz using only the hardware-tested backend. For the direct-I2S candidate,
   this means no control bus or codec object. Audio must never acquire or
   release LDO3/LDO4. Its `platform_audio_write_frames()` implementation must
   use a finite I2S write timeout shorter than the Doom runtime's 250 ms stop
   budget and must reject partial writes; otherwise a blocked worker cannot be
   shut down safely.
5. Bind the muted handle before `doomgeneric_Create()`:

   ```c
   const doom_audio_runtime_config_t doom_audio = {
       .platform_audio = audio,
       .sample_rate_hz = DOOM_AUDIO_OUTPUT_RATE_HZ,
       .display_owns_ldo3_ldo4 = true,
   };
   ESP_ERROR_CHECK(doom_audio_runtime_bind(&doom_audio));
   ```

   `I_InitSound()` calls the project `DG_sound_module`, which starts the worker
   and unmutes through `platform_audio`. Remove `-nosound`, but retain
   `-nomusic`.
6. Register the app's guarded audio cleanup with `I_AtExit(..., true)` before
   `doomgeneric_Create()`. Doom registers `S_Shutdown()` later, so its LIFO
   callback normally runs first and stops the worker. There is also a fatal
   error window between `I_InitSound()` starting the worker and `S_Init()`
   registering that callback, and a failed sound-module Init is never
   registered for upstream Shutdown. Therefore the app callback first calls
   `platform_audio_force_safe_shutdown()`, then calls
   `(void)doom_audio_runtime_stop()` unconditionally (an INVALID_STATE result
   is expected if it is already BOUND), and then attempts
   `doom_audio_runtime_unbind()`. Destroy the borrowed `platform_audio` handle,
   its backend object, or cached sample storage only after unbind succeeds;
   timeout means the worker may still own them. Keep display teardown separate;
   audio never releases its rails.
7. If backend creation or binding fails before a worker exists, force GPIO30
   safe, destroy the partial app-owned objects, append `-nosound`, and preserve
   Doom video. If the sound module's runtime start fails, let it fail quiet and
   use the guarded cleanup above; never destroy a possibly borrowed handle
   until unbind proves that no worker owns it. A later worker write failure
   forces the amplifier off and stops reporting voices as active; it must not
   block or terminate the Doom tick.

The initial composite acceptance should require recognizable Doom sound
effects, retained video/frame statistics, zero command drops/write failures,
and exact app readback evidence. Music remains explicitly out of scope.
