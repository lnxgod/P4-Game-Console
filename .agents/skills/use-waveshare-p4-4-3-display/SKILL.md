---
name: use-waveshare-p4-4-3-display
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 MIPI-DSI display path.
---

# Use the Waveshare 4.3 in display

Read `hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json`,
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md` and
`components/platform_display/src/platform_display.c`. Use
`$develop-waveshare-p4-4-3` for the exact-board build and hardware gates.
The existing ST7701 path uses a two-lane MIPI-DSI panel with 480x800 physical
geometry and 800x480 landscape output. Console OS renders a 768x480 logical
surface; game input remains canonical 320x200. Preserve the selected profile's
timings, orientation and ownership. Do not copy Elecrow EK79007 timing.

Keep DSI, panel reset, power rails, backlight, framebuffer ownership, and
cache synchronization inside the shared service's board-specific path. Retain
zero-backlight startup, refresh-confirmed buffer handoff, bounded PPA/CPU
scaling and stage-specific failures. Existing device evidence applies only to
its recorded images and units; a new panel/revision still needs exact matching.

For a display-service change, run its host CMake/CTest target and the selected
Waveshare build/verifier. For game-only pixels use `$test-p4-games-locally`.
Measure moving scanout, frame completions, timeouts and tearing separately on
hardware before claiming a display improvement.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
