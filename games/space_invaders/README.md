# Space Invaders

An original clean-room fixed-screen shooter for P4 Game API v1. It uses
code-rendered geometric ships, enemies, barriers, stars, and project-owned
tone cues; it includes no arcade ROM, sprite, font, sound, map, or other
third-party game asset.

Use Left/Right to move, A or B to fire, Start to pause, and Exit to return to
Console OS. Clear all four enemy rows to advance to a faster wave.

From the repository root:

```sh
make play-game GAME=space_invaders
make space-invaders-host
make game-sdk-host
make console-os-idf
```

The local SDL3 runner executes this same game source. Use arrows or WASD to
move, Space/Z or X to fire, Enter/P to pause, and Escape/Q to exit.

The game owns only gameplay state. Console OS continues to own display, touch,
timing, audio hardware, and lifecycle services.
