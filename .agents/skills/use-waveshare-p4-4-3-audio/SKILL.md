---
name: use-waveshare-p4-4-3-audio
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 ES8311/ES7210 audio path.
---

# Use the Waveshare 4.3 in audio path

This board's official schematic identifies an ES8311 codec, an ES7210
echo-cancellation input chip, and an 8 ohm 2 W speaker header. Do not reuse the
Elecrow factory I2S1/GPIO30 contract or its in-memory codec shim.

Read `docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md`, the Waveshare board
profile and `../develop-waveshare-p4-4-3/SKILL.md` for the implemented path and
named evidence. Trace the selected `components/platform_audio` board branch
and its dependencies before editing; the similarly named
`components/platform_audio_es8311` is an Elecrow build-only investigation,
not the Waveshare implementation.

Preserve the exact vendor initialization, I2C address, I2S pins, master clock,
amplifier polarity and bounded startup/shutdown behavior. Keep hardware in
`components/`; games use the stable tone/PCM API. Run focused host checks and
one matching Waveshare build for implementation changes, then record acoustic
startup, playback and shutdown acceptance on the named unit. Existing speaker
results do not establish microphone support or authorize a different board.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
