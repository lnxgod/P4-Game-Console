# Input Test

Input Test is a removable `INPUT.P4G` diagnostic under `SYSTEM/TESTS`. It
shows the normalized held, pressed, and released button masks, current touch
contacts, and an event counter. Start clears the counter and Back returns to
Program Manager.

The app sees only P4 Game API input snapshots. It owns no GT911, USB, GPIO, or
board handle, so the same package can test the Waveshare touch path and PC
keyboard/mouse mapping through Console OS.

All visuals are original code-rendered RGB565 primitives. No raster or
third-party assets are used. License: MIT.
