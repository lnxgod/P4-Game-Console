---
name: use-waveshare-p4-4.3-display
description: Qualify and extend the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 MIPI-DSI display path.
---

# Use the Waveshare 4.3 in display

Do not copy the Elecrow EK79007 driver or its 1024x600 timing. The Waveshare
board is 480x800 portrait by default and exposes a two-lane MIPI-DSI LCD
connector, but the exact panel controller/timing must be confirmed from the
LCD module and vendor example before authorization.

Keep DSI, panel reset, power rails, backlight, framebuffer ownership, and
cache synchronization inside a board-specific platform display component.
Begin with zero backlight, a known test frame, and stage-specific markers.
Qualify scanout before connecting the shell or games.
