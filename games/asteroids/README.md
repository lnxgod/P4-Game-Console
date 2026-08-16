# Asteroids

Asteroids is a native P4 Game API v1 arcade shooter. Rotate and thrust with
the arrows, press A to fire, and survive the wraparound asteroid field. Large
asteroids split into smaller rocks; losing all three lives shows a restart
screen. The launcher discovers `game.json` automatically and places this game
under `GAMES/ARCADE`.

The playfield and actors use original ImageGen-generated art in
`assets/deep_space_v1.png` and `assets/sprites_v1.png`, converted to bounded
RGB565 includes for static firmware use.

Use only the `p4/` headers for display, controls, drawing, and sound. Keep
board drivers and raw ESP-IDF peripheral ownership in platform components.
Press the on-screen Exit control to return to the launcher. The game uses only
original code-rendered geometry and host-owned tone audio.
