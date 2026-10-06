---
name: use-waveshare-p4-4-3-display
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 MIPI-DSI display path.
---

# Use the Waveshare 4.3 in display

Use the existing Waveshare branch of `components/platform_display`, with the
source-pinned ST7701 panel configuration and exact-unit evidence in
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md`. Read that document, the board
profile and `../develop-waveshare-p4-4-3/SKILL.md` before changing the path.
The physical panel is 480x800; the Console OS landscape shell is 768x480.
Do not substitute the Elecrow EK79007 driver or 1024x600 timing.

Keep DSI, reset, rails, backlight, framebuffer ownership, scaling and cache
synchronization in the platform component. Preserve the bounded PPA/CPU paths
and dark-on-failure behavior. Run the component's focused host checks and build
the selected Waveshare configuration once when implementation changes. A new
panel, timing or buffer-lifecycle change needs named scanout, tearing and
completion/timeout evidence; an existing unit's pass does not qualify another
module or grant a new flash authorization.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
