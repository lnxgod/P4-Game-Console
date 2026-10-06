# Tide Maze

A native C marble labyrinth: tilt a flooded, raised ceramic maze, collect its
pearls and roll into the gold dock. Three original mazes share a conservative
water simulation. Currents push the marble; braking steadies it. Whirlpools
return it to the start and cost three seconds.

Version 0.2.0 replaces the rejected flat 0.1.1 presentation with a perspective
scene. Raised wall faces, bevels, the tray, sphere lighting, shadows, floating
pearls and water geometry are drawn at the negotiated native resolution.
The water mesh follows the simulated pressure field, with continuously animated
refraction from a bounded material. Local marble presentation interpolates the
50 Hz physics step; touch steering inverts the actual perspective projection.
The game remains native C, with no Lua or private hardware/transport ownership.

In linked co-op each player steers one marble, shares collected pearls and stirs
the water. Both must reach the dock. Start through Console OS Multiplayer / Host
and Join; both consoles need the same cartridge. A normal launcher start is solo.

## Controls

| Input | Action |
|---|---|
| Tilt | Roll; all three accelerometer and gyro axes contribute |
| Twist / gentle vertical movement | Stir currents and wave impulses |
| A / Space / Brake | Brake |
| B / X / Center | Set the current comfortable pose as neutral |
| B on title or pause | Rotate steering orientation by 90 degrees |
| Arrows / WASD | Steer; title arrows choose a maze |
| Drag the visible maze | Steer toward the projected finger position |
| Start / Enter / Pause | Pause/resume; host owns linked pause |
| A / tap the message | Start, resume, retry or continue |
| Back / Escape / Exit | Return to the launcher |

Hold the tablet comfortably when starting; its first valid IMU sample establishes
neutral. Physical orientation, feel and device cadence require an operator run.
The Mac runner has keyboard/touch fallback, with synthetic six-axis host tests.

## Build and test

```sh
cmake -S games/tide_maze -B build-host/tide-fluid -G Ninja
cmake --build build-host/tide-fluid
ctest --test-dir build-host/tide-fluid --output-on-failure
cmake -S tools/p4-game-host -B build-host/tide-fluid-play -G Ninja \
  -DP4_GAME=tide_maze -DP4_ALLOW_DRAFT_GAME=ON
cmake --build build-host/tide-fluid-play
ctest --test-dir build-host/tide-fluid-play --output-on-failure
make play-game GAME=tide_maze
```

`PERFORMANCE_REWORK.json` records the 0.2.0 candidate, source closure and checks.
`LOCAL_TESTING.json`, `STRESS_TESTING.json` and `PUSH_TESTING.json` retain historical
0.1.x evidence. The operator rejected 0.1.1 for lag and its flat visual direction;
its successful transfers and host tests never qualified physical gameplay.

The focused tests cover all three solo mazes and three full co-op voyages with
loss, delay, duplicates, reordering and temporary outages; water conservation for
60 simulated minutes; 120,000 six-axis samples; malformed packets; lifecycle;
fractional local motion and respawn; perspective touch inversion; both render
sizes and padded-stride guards. Shared mesh tests independently check geometry,
UV wrapping and clipping. The faster existing raster helpers are tested against
original scalar pixel oracles and inspected with the pinned RV32 `-Os` compiler.

Host timing excludes the ESP32-P4, display and transport. A device transfer or
catalog registration is installation evidence only. The lag complaint remains
open until active device cadence and physical responsiveness support acceptance.

## Budgets and rendering

State remains 4,000 bytes. Simulation uses 20 ms fixed steps with bounded catch-up
and retained fractional time. The 30x18 face-flux water grid conserves volume,
reflects walls and supports pressure, damping, wakes and impulses. Water rendering
adds cosmetic travelling waves; authoritative physics stays in the solver.

The shared `p4/mesh.h` renderer uses clipped, bounded convex faces and scanline
texture sampling. It has no full-frame cache, z-buffer, allocator or per-pixel
division. The game controls painter order, camera and scene. Surfaces remain
768x480 RGB565 with 320x200 fallback; input stays canonical 320x200.

The host owns both marbles and synchronized rules. Clients send 20-byte intents;
the host publishes 64-byte snapshots at 20 Hz using protocol 1. Guest marble
positions interpolate snapshots; guest water is cosmetic. Input expires after
250 ms and a silent link ends after three seconds. Actual radio responsiveness
in both roles is a separate device check.

The cartridge requires a motion-aware OS validator (installed Tab5 0.51 and
later recognize the optional motion bit). Missing sensor data falls back to
normal controls. Package, loader, stack and capability bounds are unchanged.
Game updates use the existing native USB content transfer, without an OS flash.

## Original artwork

`assets/water-source.png` is original ImageGen material. `tools/convert_water.py`
Lanczos-resamples it to 128x128 and produces three RGB565 lighting palettes:
98,304 bytes total. Water is sampled on moving geometry; no PNG is decoded at
runtime. `assets/launcher-3d-source.png` supplies the updated 128x72 indexed icon
via `scripts/pack-game-icon.py` (9,744 bytes). Exact prompts, hashes and conversion
parameters are in `assets/perspective-provenance.json`. The older cover and its
provenance remain historical source art; that cover is no longer linked.

Code is MIT-licensed. The shared Arimo font retains its OFL attribution. The
launcher artwork is an illustration; gameplay captures come from the C renderer.
