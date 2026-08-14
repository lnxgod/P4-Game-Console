# Elecrow factory-path audio backend

This isolated backend follows the complete pinned Elecrow 10.1-inch factory
audio hardware initializer. It owns PDM RX I2S0 and speaker TX I2S1, performs
no I2C transaction or codec probe, and has no automatic fallback.

Repository review record:
`hardware/evidence/elecrow-10.1-factory-audio-semantics.json`.

Reviewed source:

- Repository evidence record
  `hardware/evidence/elecrow-10.1-factory-audio-semantics.json`, which also
  records matching `bsp_audio_codec_speaker_init`, `i2s_data_if`, and
  `bsp_audio_init(NULL)` strings from the preserved factory image
- Elecrow repository commit
  `c5a437311b951aaa9d17115bf420877a8f1f7b83`
- `factory_sourcecode/V1.0/ESP32-P4-Adcance-brookesia_phone_inch10_1.zip`
- Archive SHA-256
  `73b32c4d4dc89cc0b091388d6a7862d827d6548412717bcb1f36f7adb8da2e28`
- Factory BSP source SHA-256
  `f0aa354307710744f37d57b8ea23942b13d6ae38c26a98ad118c606ae5b11b69`
- Factory `my_codec.c` SHA-256
  `f80ee68cd9e079725e9ea68d218b04c06438a86a91988e98750dfbc4b319ee18`
- Vendored Espressif BSP package `4.1.1`, repository commit
  `0064ab931c38699fc993ee161713ac1d1c0943a5`

The vendor `bsp_audio_init` first creates and enables a PDM microphone RX
branch on I2S0: 16 kHz mono PCM, DSR_8S, GPIO24 clock (1.024 MHz under the
pinned ESP-IDF configuration), and GPIO26 input. It then creates the
independent speaker TX branch on I2S1: 16 kHz PCM16 stereo, BCLK 22, LRCLK 21,
DOUT 23, TX MCLK unused, and active-low GPIO30. This component preserves that
ordering and keeps PDM RX enabled without ever consuming microphone data.
GPIO24 is a PDM clock; the board may also fan it through R126/strapping toward
the codec MCLK net, which does not make it an external-codec initialization.
The vendor
`my_codec_ctrl_new()` stores fake registers in RAM; it never accesses an
external ES8311. Consequently, the earlier address-0x18 NACK is compatible
with the factory path and does not explain why the vendor image can play.

Hardening added here:

- GPIO30 is latched high before pad configuration, configured
  `GPIO_MODE_INPUT_OUTPUT`, rewritten high, and physically read back.
- Every later high/low request is read back exactly.
- All six 256-frame TX DMA descriptors are overwritten with zeros before each
  speaker-clock enable.
- The amplifier sees at least 350 ms of exact zero clocks, followed by a
  second low readback, before RUNNING.
- PCM is copied into immutable-input staging and bounded to signed PCM16.
  Backend volume step 10/10 copies the complete signed range
  `[-32768, 32767]` bit-for-bit (maximum absolute magnitude 32768) with unity
  gain and no amplification; lower steps attenuate proportionally. The data is written with an
  exact-byte check and finite 100 ms timeout. Doom's 8-bit DMX effects are
  already quiet after mixer conversion, so an additional 8x backend
  attenuation made a nominally working demo likely sound silent.
- Any incomplete rollback enters `FAILED_SAFE`; start is rejected until a
  complete stop retry succeeds.
- Cleanup never loses an owned I2S handle after a driver failure.
- A short FreeRTOS critical section gives task-context loggers coherent
  telemetry without priority-inversion spinning while
  audio work proceeds: exact GPIO transitions/readbacks, both I2S channel
  lifecycles, 1536 zero frames, measured settle time, bounded-write results,
  full signed-PCM magnitude, rollback proof, and retained resources.

Remaining physical gate: the published schematics do not prove which of the
converging amplifier footprints is populated on the connected unit. Source and
host tests are not hardware verification. The final route is an operator-
directed replay scope for the exact bound unit; it is not physical population
or continuity proof and must not be generalized to another unit. Flash/runtime
acceptance still needs exact readback, serial evidence, and human acoustic
confirmation under the separate authorization.
