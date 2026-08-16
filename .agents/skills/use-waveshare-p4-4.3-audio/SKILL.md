---
name: use-waveshare-p4-4.3-audio
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 ES8311/ES7210 audio path.
---

# Use the Waveshare 4.3 in audio path

This board's official schematic identifies an ES8311 codec, an ES7210
echo-cancellation input chip, and an 8 ohm 2 W speaker header. Do not reuse the
Elecrow factory I2S1/GPIO30 contract or its in-memory codec shim.

Record the exact vendor initialization, I2C address, I2S pins, master-clock
requirements, amplifier enable polarity, and microphone path from the exact
board revision. Keep the implementation behind `components/` and require a
power-off-safe startup/shutdown diagnostic before native games request audio.
