---
name: esp32-elecrow-sound
description: "Use for explicitly requested legacy Elecrow ESP32-P4 10 in variant (10.1-inch DHE04310D) speaker-service work: factory I2S1/GPIO30/PDM initialization, silent audio, clicks/pops, volume, DMA underruns or acoustic acceptance. Ordinary game audio through stable P4 APIs uses Make Game and Test Game."
---

# ESP32 - Elecrow Sound

Reproduce the pinned factory behavior behind reusable platform APIs. Keep its
speaker TX branch distinct from the PDM receiver that the complete factory
audio initializer also starts; do not infer a codec transaction from schematic
labels when the exact factory source is the requested behavior.

If a game only calls the existing P4 audio APIs, use `$esp32-make-game` and
`$esp32-test-game` for authoring and focused tests. Do not run this
factory-audio workflow unless the request changes the platform audio path or diagnoses actual device sound.

## Load the exact contract

Read these files before changing or enabling audio:

1. `hardware/board-profile.json`
2. `hardware/evidence/elecrow-10.1-factory-audio-semantics.json`
3. `hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json`
4. `components/platform_audio_factory/include/platform_audio_factory/audio.h`
5. `references/factory-audio-contract.md`

Also use `$esp32-fix-console` for the pinned toolchain, exact-unit
authorization, guarded app write, recovery, and evidence rules. Combined
display firmware also uses `$esp32-elecrow-screen`.

The normal release is a powered-off population/continuity proof that the two
schematic amplifier-output families cannot contend. If the exact-unit owner
instead explicitly directs replay of a known-working factory path and accepts
that unresolved topology risk, require a separate immutable authorization that
binds the one device identity, pinned factory source, prior nondamaging run,
allowed GPIOs, recovery image, and explicit risk acceptance. Never promote that
exception to another unit, PCB revision, or general pin map.

The repository already records that exception for the bound tablet identity
`4ea036…`: Doom SFX and recognizable MUS music were operator-confirmed, and
an exact-unit factory-audio release was recorded for the historical Console
OS Game API v1 artifact. Do not
describe this tablet's audio as runtime-blocked. A changed artifact still needs
its own immutable release and guarded install route; the exception remains
non-reusable and does not authorize another unit.

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
- Native Console OS games request `P4_GAME_CAP_AUDIO_TONE`,
  `P4_GAME_CAP_AUDIO_STREAM`, or both. Stream clients submit 1–256 already
  mixed 16 kHz PCM16-stereo frames; the host copies whole accepted blocks into
  the shared FIFO bounded by `P4_GAME_AUDIO_STREAM_BUFFER_FRAMES` in
  `components/p4_game_api/include/p4/audio.h` (currently 2048 frames). The host
  is the only code that writes the platform adapter. A full FIFO returns
  `false`; games drop/degrade rather than spin.
- On the exact-unit E6 path, mix validated Doom WAD MUS lumps with native 16 kHz
  effects above the stable platform boundary using the bounded procedural synth.
  It requires no external MIDI hardware, codec traffic, or SoundFont and does
  not claim bit-exact OPL emulation. Feed the combined stream at backend volume
  step 6/10 for linear final-output attenuation. Do not call the step a percent;
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

For a music-enabled acceptance, require rising MUS event, note, and mixed-frame
counters, non-zero music PCM, zero MUS parse failures, and separate human
confirmation of recognizable title/E1M1 music plus simultaneous sound effects.
Software counters never prove acoustic output.

## Interpret attenuated telemetry correctly

The counted adapter observes PCM before the factory backend applies its volume
step; backend telemetry observes PCM afterward. For backend step `v` in 1..10:

- require `backend_peak >= floor(adapter_peak * v / 10)` once every
  adapter-counted write is known to have completed in the backend;
- cap the backend peak with `platform_audio_factory_peak_for_volume(v)`;
- never require post-attenuation non-zero counts to equal or exceed the adapter
  counts, because integer attenuation can quantize small samples to zero;
- when snapshots are taken independently, bound any backend surplus by
  `backend_frames_written - adapter_frames_forwarded` and the corresponding
  sample delta rather than pretending the snapshots are atomic.

For step 6/10, a pre-volume peak of 15599 truthfully becomes 9359 and the
absolute backend limit is 19660. Treat that as expected attenuation, not a
silent-audio failure. Make retained-UART capture return and persist success as
soon as the required exact startup sequence and minimum valid records pass;
unnecessarily waiting exposes a completed proof to later cable disconnects.

On any create, start, write, stop, or cleanup failure, request and read back
GPIO30 high first. Retain ownership when cleanup is not proven, publish
FAILED_SAFE, and reject restart until recovery succeeds. Never add a fallback
codec or alternate pin route.

## Build and verify proportionally

Run only the checks that cover the changed seam:

- Tone calls or game sound timing: use that game's focused host tests.
- The shared game tone mixer or adapter: run its focused component test.
- `platform_audio_factory`: run `make platform-audio-factory-host`.
- The combined Doom audio integration or its reviewed artifact graph: run the
  complete sequence below.

Do not run repo-wide `make check`, unrelated display/gamepad/USB tests, or the
combined Doom build for an isolated game or component change. For the complete
integration path, run:

```sh
make verify
make platform-audio-factory-host
make doom-touch-audio-host
make build APP=doom_embedded_touch_audio
python3 scripts/verify-doom-embedded-touch-audio.py \
  apps/doom_embedded_touch_audio/build build-only
```

Do not repeat an unchanged build or image. After the focused checks pass, use
one hardware acceptance for the changed acoustic behavior and repeat only when
the firmware or test conditions change.

For the combined Doom integration, require the final ELF/map audit to prove
the app-facing adapter is the only
caller of every `platform_audio_factory_*` entry point, the runtime gate bytes
match the authorization, USB is absent, and no external codec transaction path
entered the graph. If the complete factory initializer is selected, also prove
the exact PDM RX GPIO24/GPIO26 graph and prohibit consuming microphone data.

## Install and qualify sound

Read [the recorded installation history](../esp32-elecrow-test/references/current-unit.md)
and follow `$esp32-elecrow-test` for a current-layout, exact-artifact sound
successor. Changed audio requires its separately recorded audio release,
exact-unit authorization and guarded installer route before a write. A past
factory-audio or display acceptance does not supply these records.

The command below is preserved historical E5 touch-only replay at `0x10000`,
with audio disabled; it cannot qualify sound. Use it only for an explicitly
requested, authorized replay of its exact original source, artifact, unit and
layout. Never reuse it on the recorded dual-OTA `0x20000` layout or change its
frozen gate to install an audio-enabled successor:

```sh
make flash-app APP=doom_embedded_touch_audio PORT=/dev/cu.<port>
```

If no matching successor authorization and guarded route is recorded, stop
before writing and prepare them separately. For an authorized successor,
require one retained UART descriptor from live identity/security/flash/partition
checks through write, complete padded-span readback, and one post-readback
launch. Capture afterward without transmitting or resetting.

For an audio-enabled Doom E6-style successor, runtime evidence must show the
following. Console OS candidates use their own reviewed launcher/audio startup
contract; acoustic acceptance remains separate:

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

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
