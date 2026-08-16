# BREAKOUT

Break the five rows of colored bricks with the paddle. The game is a native
P4 Game API v1 component under `GAMES/ARCADE`.

Touch anywhere in the playfield to move the paddle directly. Keyboard arrows
and A/D provide the same movement in the SDL3 host runner. Press A to restart
after a clear or game over, Start to pause, and Exit/Back to return to the
launcher.

From the repository root:

```sh
cmake -S tools/p4-game-host -B build-host/play-breakout -G Ninja \
  -DP4_GAME=breakout
cmake --build build-host/play-breakout
ctest --test-dir build-host/play-breakout --output-on-failure
make play-game GAME=breakout
```

The game uses only stable `p4/` APIs; display, touch, audio, and lifecycle
ownership remain with Console OS.
