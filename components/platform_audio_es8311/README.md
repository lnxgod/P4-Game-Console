# ES8311 platform audio

The active backend is selected for Waveshare 4.3 by this component's CMake
board gate and the counted adapter under
`apps/doom_embedded_touch_audio/components/platform_audio`. It borrows the
board-owned I2C bus, uses the profile's I2S pins and active-high GPIO53 amplifier,
and maps volume steps 0..10 to codec volume 0..100. Read
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md` for dated hardware evidence;
this component does not implement an ES7210 microphone service.

## Historical Elecrow codec diagnostic

The original build-only policy describes the expected Elecrow 10.1-inch path:

`I2S1 + GPIO24 MCLK -> ES8311 at 0x18 -> NS4263B -> J4/J6 speakers`

It borrows the shared GPIO45/GPIO46 I2C master bus and never creates or
deletes that bus. A missing ES8311 is a hard failure; there is deliberately no
factory-style direct-I2S/NS4168 fallback. GPIO30 stays high through probe,
clock setup, codec setup, mute and volume readback, and zero priming. Start
holds the codec muted while GPIO30 is low and exact zeros are transmitted for
at least 350 ms before unmute. Every error requests GPIO30 high immediately.

This historical policy is **not Elecrow runtime authorization**. The bound unit previously
did not ACK at `0x18`, and the optional NS4168 and expected NS4263B outputs
converge on the same speakers. Do not flash or execute a GPIO30-low path until
the power-off population/continuity inventory required by
`hardware/evidence/elecrow-10.1-audio-path-review.json` is complete. Current
Elecrow Console OS uses `platform_audio_factory`, not this codec backend. The
Waveshare route does not authorize Elecrow's GPIO30 path.

## Master volume API

The backend exposes a bounded master step of 0..10 through
`platform_audio_es8311_set_volume()` and `platform_audio_es8311_get_volume()`;
zero is a true mute and produces exact zero PCM.
Creation always starts muted with the configured step. While running, a set
operation mutes and verifies the codec before changing its register and only
unmutes after readback succeeds; failures leave the backend in its fail-safe
muted state. Persistence is owned by `platform_console_settings`: callers
persist only after an accepted deliberate Control Panel change, then pass the
selected step to Console OS, native games, or Doom when opening an audio
session.
