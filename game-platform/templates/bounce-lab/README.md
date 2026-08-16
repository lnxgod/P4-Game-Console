# Bounce Lab

Bounce Lab is a clean-room starter game for P4 Arcade Maker. It uses only
code-drawn shapes and generated tones, so there are no copied arcade graphics,
ROMs, maps, samples, or music.

Good first remixes:

- change the ball and paddle colors;
- add blocks near the top of the 768x480 screen;
- make the paddle shrink as the score rises;
- add a second paddle and declare two-player local multiplayer;
- change the bounce tones into a short melody.

Validate and pack it from the repository root:

```sh
python3 game-platform/scripts/p4cart.py validate game-platform/templates/bounce-lab
python3 game-platform/scripts/p4cart.py pack game-platform/templates/bounce-lab /tmp/bounce-lab.p4cart
python3 game-platform/scripts/p4cart.py inspect /tmp/bounce-lab.p4cart
```

The host preview and device Lua runtime are follow-on work. The current tool
proves the source manifest and deterministic cartridge bytes; it does not yet
make the cart launchable in Console OS.
