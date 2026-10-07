---
name: esp32-waveshare-sound
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 ES8311/ES7210 audio path.
---

# ESP32 - Waveshare Sound

This board's official schematic identifies an ES8311 codec, an ES7210
echo-cancellation input chip, and an 8 ohm 2 W speaker header. Do not reuse the
Elecrow factory I2S1/GPIO30 contract or its in-memory codec shim.

Read `hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json`,
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md` and
`components/platform_audio_es8311/README.md`. The implementation is
`components/platform_audio_es8311`, selected through the counted adapter in
`apps/doom_embedded_touch_audio/components/platform_audio`; games consume the
stable P4 audio API. Use `$esp32-waveshare` for build/install gates.

Preserve the profile's exact I2C/I2S pins, master clock, amplifier polarity,
muted startup and ordered shutdown. The ES7210's presence does not establish a
supported microphone service. Check the existing audio service's host tests
and build the consuming Waveshare target after a service change; ordinary
game audio uses focused game tests. Record delivery counters and acoustic
feedback separately for each named artifact/unit. Do not require a fresh board
bring-up for games using the already implemented audio API.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
