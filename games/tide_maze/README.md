# Tide Maze

Roll a glass marble through three flooded stone labyrinths. Collect every pearl,
then reach the gold **E** dock before the tide timer expires. Water carries momentum,
banks against walls, pushes the marble, and ripples in its wake. Whirlpools return
you to the start and cost three seconds; collected pearls stay collected.

In two-console co-op, each player steers their own numbered marble. Both players
stir the shared currents. Pearls are shared, and **both marbles must reach the dock**.
Start the game through Console OS Multiplayer / Host and Join; the cartridge uses
the OS's existing room and transport services. An ordinary launcher start is solo.

## Controls

| Input | Action |
|---|---|
| Tilt the tablet | Roll; accelerometer gravity and gyro prediction combine to reduce lag/drift |
| Twist or gently move it vertically | Stir currents / create a wave impulse |
| A / Space / Z, or hold Brake | Brake against momentum and the current |
| B / X, or Center during play | Set your current comfortable pose as neutral |
| B on title or pause | Rotate steering orientation by 90 degrees |
| Arrows / WASD | Steer without sensors; title arrows select a starting maze |
| Drag within the maze | Steer toward the finger |
| Start / Enter / P, or Pause | Pause/resume; the host owns a linked game's pause |
| A / tap the message | Start, continue, retry, or start a fresh solo game after peer loss |
| Back / Escape / Backspace / Q, or Exit | Return to Console OS |

Hold the tablet comfortably when starting; the first valid sample establishes
neutral. The game uses all three accelerometer and all three gyroscope axes.
Orientation and comfortable tilt gain still require a physical Tab5 play-test.
The Mac runner has no real IMU and exercises keyboard/touch fallback; tests inject
six-axis samples through the same public API. Small natural movements are enough.

## Local play and verification

```sh
make play-game GAME=tide_maze
cmake -S games/tide_maze -B build-host/tide_maze -G Ninja
cmake --build build-host/tide_maze
ctest --test-dir build-host/tide_maze --output-on-failure
```

`STRESS_TESTING.json` records the current 0.1.1 candidate's exact hashes and
results; `LOCAL_TESTING.json` preserves the original 0.1.0 evidence. The sanitizer
harness finishes all three mazes solo and completes three full co-op voyages with
15% random packet loss, 0–120 ms delay, duplicates, reordering and 600 ms outages.
Both players use normal controls; the guest steers from delayed snapshots. It
also checks 180,000 water steps (60 simulated minutes across all three mazes),
120,000 full-range six-axis samples, 12 neutral poses, four orientations, variable
frame times, touch mapping, malformed packets, pause, retry and link loss.
Both render sizes have padded-stride canaries and eight lifecycle captures.

The additional tests found and fixed guest currents surviving a same-maze retry,
send-rate drift at 50 Hz update cadence, and position jumps when snapshots arrived
early. Protocol 1 and the 4,000-byte state budget remain unchanged.
The 10,000-frame keyboard/drag benchmark runs at both native and fallback sizes.

The latest visible SDL check confirmed keyboard movement, pause/resume and Back.
CUA mouse automation supplied incorrect raw SDL positions in this run (a sidebar
tap arrived at 0,0), so its sidebar/drag check is inconclusive. The real physical
viewport-to-game mapper and touch targets pass automated tests. Temporary input
logging was removed; physical touch and manual mouse acceptance remain pending.

This is a local-play-tested development cartridge, not hardware acceptance.
No board was flashed or installed by this task. Speaker acoustics, physical tilt,
launcher tile refresh, sustained tablet FPS, and two-tablet radio/relay gameplay
remain to be tested on named units with exact artifact hashes.

## Runtime and compatibility

The game state is 4,000 bytes. Simulation advances at 50 Hz with a bounded fixed
step and retained fractional time. Water is a 30 by 18 conservative face-flux
grid: equal-volume transfers, wall boundaries, pressure gradients, damping,
wakes and impulses. This is an intentionally small shallow-water approximation,
not a full 3D fluid solver. Rendering draws native 768 by 480 geometry, with a
320 by 200 fallback; touch coordinates always remain 320 by 200.

The host simulates both marbles, water forces, pickups, timer and win conditions.
Clients send 20-byte bounded tilt intents; the host sends complete 64-byte
snapshots at 20 Hz. Client marble positions interpolate between authoritative
snapshots. Water animation on the client is locally reconstructed and cosmetic;
only host water affects authoritative physics. Invalid/late input never chooses
a winner. Input expires after 250 ms; a stalled link ends after three seconds.
There is no private socket, radio, room browser, or hardware ownership in the game.

This cartridge needs an OS that recognizes the new optional `motion` capability
(the Tab5 0.50 candidate includes it). Older package validators reject unknown
capability bits even when optional. On a motion-aware OS without sensors, the
same cartridge uses its touch/controller fallback. The appended API v1 host
callback is size-guarded; existing cartridges retain their original table layout.

Build with `make console-os-tab5-idf`. The game is staged at
`apps/console_os/build-tab5/sd-card/GAMES/TIDE_MAZE.P4G`. After the exact OS candidate
passes the separate guarded installation workflow, game updates use the existing
native USB content transfer; both linked players need the identical cartridge.

## Art and provenance

Gameplay geometry, water shading, marbles, icons and text are original code.
The title illustration and cartridge launcher art were generated with the built-in
ImageGen tool; the original PNG, exact prompt and conversion details are in
[`assets/provenance.json`](assets/provenance.json). The source is
[`assets/launcher-source.png`](assets/launcher-source.png).
`tools/convert_art.py` converts it with a recorded Lanczos fit into the 288 by 162
RGB565 title image (93,312 bytes). `scripts/pack-game-icon.py` makes the cartridge's
128 by 72 indexed icon (9,744 bytes). Runtime code never decodes a PNG.
Code is MIT-licensed; original generated artwork accompanies the game. The shared
Arimo presentation font retains the repository's OFL attribution.

## Isolated source branch

The `codex/tide-maze` branch contains the game, motion API/Tab5 sampling adapter,
and the presentation/font/icon packaging dependencies needed to build it on
`codex/m5stack-tab5`. It excludes the shared workspace's unrelated UI, controller,
charger and radio changes. The 0.50/0.51 firmware hashes in the earlier records
are historical combined-workspace builds, not firmware built from this branch.
The launcher icon is embedded for icon-aware launchers; older launchers keep their
existing tile presentation. Linked play still requires an OS-supported transport
on both consoles. `PUSH_TESTING.json` records checks on this isolated source.
