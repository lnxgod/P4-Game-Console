---
name: use-elecrow-p4-audio
description: Add, change, diagnose, flash, or test speaker sound on the Elecrow ESP32-P4 10 in variant (10.1-inch DHE04310D). Use for the factory speaker-TX I2S1/GPIO30 path, the factory PDM clock side effect, Doom sound effects, silent audio, clicks/pops, volume, DMA underruns, amplifier shutdown, or audio runtime acceptance. Do not invent an ES8311 register sequence for the pinned factory behavior.
---

# Use the Elecrow P4 10 in audio path

Reproduce the pinned factory behavior behind reusable platform APIs. Keep its
speaker TX branch distinct from the PDM receiver that the complete factory
audio initializer also starts; do not infer a codec transaction from schematic
labels when the exact factory source is the requested behavior.

## Load the exact contract

Read these files before changing or enabling audio:

1. `hardware/board-profile.json`
2. `hardware/evidence/elecrow-10.1-factory-audio-semantics.json`
3. `hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json`
4. `components/platform_audio_factory/include/platform_audio_factory/audio.h`
5. `references/factory-audio-contract.md`

Also use `$develop-esp32-p4-platform` for the pinned toolchain, exact-unit
authorization, guarded app write, recovery, and evidence rules. Combined
display firmware also uses `$use-elecrow-p4-display`.

The normal release is a powered-off population/continuity proof that the two
schematic amplifier-output families cannot contend. If the exact-unit owner
instead explicitly directs replay of a known-working factory path and accepts
that unresolved topology risk, require a separate immutable authorization that
binds the one device identity, pinned factory source, prior nondamaging run,
allowed GPIOs, recovery image, and explicit risk acceptance. Never promote that
exception to another unit, PCB revision, or general pin map.

## Preserve the factory-compatible path

- Use I2S1 as master at 16 kHz, signed PCM16 stereo.
- Use LRCLK GPIO21, BCLK GPIO22, and DOUT GPIO23. The I2S1 TX MCLK output is
  unused. Do not generalize that statement to the whole initializer: its
  GPIO24 PDM clock is active at about 1.024 MHz and may fan out on the PCB's
  codec-MCLK net.
- When claiming complete factory audio-init equivalence, also create its PDM RX
  I2S0 path: 16 kHz mono, DSR_8S, clock GPIO24 at 1.024 MHz, input GPIO26. Doom
  may discard microphone samples, but omitting this path is only a
  factory-speaker-TX-compatible subset.
- Use GPIO30 as active-low speaker enable. Games never drive it directly.
- Do not probe or configure an ES8311 and do not emit codec I2C traffic. The
  pinned factory `my_codec` control object is an in-memory register shim.
- Use `components/platform_audio_factory`; applications consume the counted
  `platform/audio.h` adapter and do not own raw I2S or GPIO handles.
- Keep Doom music disabled. Feed its native 16 kHz effects at backend volume
  step 10/10 for unity gain without amplification. Do not call that 10 percent;
  the factory UI's usual stored/default volume applies separate attenuation.

## Start and stop without pops

Keep the factory electrical path while using the stricter project lifecycle:

1. Latch GPIO30 high before changing its direction and prove the pad reads high.
2. Reproduce the factory initializer order: create and enable PDM RX I2S0 on
   GPIO24/GPIO26, then create I2S1 TX on GPIO21/GPIO22/GPIO23.
3. Preload the complete 1536-frame TX DMA ring with exact zeros.
4. Enable the TX clocks while the amplifier remains shut down.
5. Drive GPIO30 low once, keep zeros flowing for at least 350 ms, prove low
   again, and only then publish RUNNING.
6. Accept bounded PCM writes only after the service enters RUNNING.

On any create, start, write, stop, or cleanup failure, request and read back
GPIO30 high first. Retain ownership when cleanup is not proven, publish
FAILED_SAFE, and reject restart until recovery succeeds. Never add a fallback
codec or alternate pin route.

## Build and verify

Run the focused host and artifact checks before any write:

```sh
make verify
make platform-audio-factory-host
make doom-touch-audio-host
make build APP=doom_embedded_touch_audio
python3 scripts/verify-doom-embedded-touch-audio.py \
  apps/doom_embedded_touch_audio/build build-only
```

Require the final ELF/map audit to prove the app-facing adapter is the only
caller of every `platform_audio_factory_*` entry point, the runtime gate bytes
match the authorization, USB is absent, and no external codec transaction path
entered the graph. If the complete factory initializer is selected, also prove
the exact PDM RX GPIO24/GPIO26 graph and prohibit consuming microphone data.

## Install and qualify sound

Use only the guarded app-partition route for the exact bound unit:

```sh
make flash-app APP=doom_embedded_touch_audio PORT=/dev/cu.<port>
```

Require one retained UART descriptor from live identity/security/flash/partition
checks through write, complete padded-span readback, and one post-readback
launch. Capture afterward without transmitting or resetting.

Runtime evidence must show:

- exact composite/touch/audio gate values `1/1/1`;
- `SOUND_READY` with exact PDM-RX-first/TX-second initialization, the complete
  zero preload, both low-pad readbacks, and the measured settle interval;
- increasing audio frames and platform-audio call counts;
- zero audio write failures and no `SOUND_DEGRADED` or fault marker;
- sustained video and touch polling; and
- a person confirming audible, undistorted Doom effects.

Serial counters prove data reached the backend, not that a speaker produced
sound. Record acoustic confirmation separately and never infer it from GPIO30,
I2S, or frame counters alone. GPIO pad readback likewise proves only the P4 pad
level, not a downstream amplifier's electrical state.
