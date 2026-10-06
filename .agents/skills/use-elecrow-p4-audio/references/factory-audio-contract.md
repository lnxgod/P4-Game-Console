# Factory audio contract for the 10 in variant

## Pinned source

- Vendor: Elecrow, DHE04310D 10.1-inch CrowPanel Advanced.
- Repository commit: `c5a437311b951aaa9d17115bf420877a8f1f7b83`.
- Archive: `factory_sourcecode/V1.0/ESP32-P4-Adcance-brookesia_phone_inch10_1.zip`.
- Archive SHA-256: `73b32c4d4dc89cc0b091388d6a7862d827d6548412717bcb1f36f7adb8da2e28`.

The reviewed board source inside that vendor archive is
`components/espressif__esp32_p4_function_ev_board/esp32_p4_function_ev_board.c`
(SHA-256 `f0aa354307710744f37d57b8ea23942b13d6ae38c26a98ad118c606ae5b11b69`).
Its `bsp_audio_init()` creates I2S1 TX with six 256-frame DMA descriptors,
16 kHz signed PCM16 stereo, standard I2S timing, GPIO21 LRCLK, GPIO22 BCLK,
GPIO23 DOUT, and no I2S1 TX MCLK. It enables the I2S channel before driving
GPIO30 low.

Before TX, the same factory function also creates and enables PDM RX I2S0 at
16 kHz mono with DSR_8S, GPIO24 clock, and GPIO26 input. With the pinned IDF
configuration, GPIO24 runs at 1.024 MHz. The schematic fans GPIO24 toward both
the microphone clock and an ES8311 MCLK resistor. Because the factory still
performs no external ES8311 register writes, this clock is a material full-init
side effect, not proof of configured codec playback. A build that omits PDM RX
is accurately described only as the factory speaker-TX-compatible subset.

The factory speaker constructor calls `bsp_i2c_init()`, but its codec control
implementation is `my_codec.c` (SHA-256
`f80ee68cd9e079725e9ea68d218b04c06438a86a91988e98750dfbc4b319ee18`).
That object reads and writes a RAM register array; it performs no external
ES8311 transaction. Reproducing factory sound therefore means direct I2S1 plus
active-low GPIO30, without inventing an ES8311 register sequence. A complete
initializer replay also keeps the factory's PDM RX I2S0 and its active GPIO24
clock; that is not an I2S1 TX MCLK output and is not proof that the codec was
configured.

## Project implementation

`components/platform_audio_factory` preserves the speaker TX pins, controller,
format, and amplifier polarity. Its current implementation also creates and
enables the bounded PDM RX clock/input lifecycle before TX, without consuming
microphone samples. Preserve that lifecycle when claiming complete factory-init
equivalence. It deliberately improves lifecycle safety:
GPIO30 is latched/read back high first, the full DMA ring is zero-filled,
GPIO30 is pulled low once, exact zeros run for at least 350 ms, and every error
tries to restore and prove the high shutdown state.

The Doom app uses its local counted `platform/audio.h` adapter. This provides a
runtime witness that every backend call crossed the reviewed gateway. No app
code may call `platform_audio_factory_*`, I2S, or GPIO30 directly.

## Evidence boundaries

The exact unit's owner confirmed that the project Doom image produced sound
effects and recognizable MUS music through this path. Console OS Game API v1
then received its own exact-unit audio release and installed successfully. The
path is therefore working and runtime-authorized on the bound device identity
`4ea036…`; it is not merely build-only or blocked there. A changed artifact
still requires exact flash readback, rising delivery counters with zero write
failures, a fresh artifact-bound release, and a human hearing clean effects.
If sound is absent, keep the project path unchanged and diagnose the observed
factory-compatible waveform before introducing any codec, MCLK, alternate
GPIO, or amplifier assumption.

The newer uninstalled Game API v1 implementation accepts copied blocks of
1–256 signed 16 kHz PCM16-stereo frames into a 512-frame software FIFO and
mixes them with its bounded tone voices before the counted platform adapter.
That software path is host/build-tested, not yet acoustically accepted on the
tablet. It does not change the factory initializer or GPIO ownership above.

Published topology still permits two output families to converge. Prefer a
powered-off population/continuity release. An owner-directed exception is valid
only for the bound unit when an immutable record explicitly accepts that risk,
names the exact factory path and GPIOs, preserves rollback bytes, and keeps the
global pin map locked. It does not turn GPIO30 readback into amplifier-state
proof or make the path safe on another board.
