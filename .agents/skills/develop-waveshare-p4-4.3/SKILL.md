---
name: develop-waveshare-p4-4.3
description: Build, port, diagnose, and qualify firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 board.
---

# Develop Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3

Use this skill for the Waveshare 4.3 in board only. Keep it separate from the
Elecrow 10 in variant. Read `hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json`,
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md`, `toolchain.lock.json`,
`AGENTS.md`, and `docs/HARDWARE.md` before changing board-facing code.

The board is currently schematic-reviewed only. Its display, touch, audio,
and USB host paths are denied until exact board identity and bench evidence
are recorded. Do not enable an Elecrow pin map under a Waveshare build flag.

## Port boundary

- Reuse the stable Console OS shell, Game API, storage API, and framebuffer
  contracts where they remain board-neutral.
- Add Waveshare-specific services under `components/` or a dedicated board
  layer; games never own DSI, I2C, codec, USB, or raw GPIO.
- Start with a pin-independent bring-up image, then separate display, touch,
  audio, and USB diagnostics.
- Keep the active Elecrow `hardware/board-profile.json` unchanged until the
  Waveshare target has independent evidence.

## USB gate

H2 is the OTG Type-C connector. The official schematic shows P4 GPIO24/GPIO25
for USB data, H2 VBUS, and 5.1 kOhm pulldowns on CC1 and CC2. Treat that as a
sink/device port until measured otherwise. A VBUS pin is not a host-power
authorization. Host tests require a regulated, current-limited, backfeed-safe
fixture and must measure source/sink direction and fault behavior.

## Definition of done

Do not call the port hardware-tested until exact SKU/revision, complete flash
backup, display/touch/audio/USB evidence, and serial acceptance records exist.
Build success alone is only `build-tested`.
