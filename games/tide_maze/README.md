# Tide Maze

A native C marble labyrinth: tilt a flooded, raised ceramic maze, collect its
pearls and roll into the gold dock. Three original mazes share a conservative
water simulation. Currents push the marble; braking steadies it. Whirlpools
return it to the start and cost three seconds.

Version 0.3.0 replaces both the renderer and the water solver. It is a native C
software 3D scene: a rotating tray/camera transform, inverse perspective touch
picking, reciprocal-depth triangles, beveled walls, spherical marble meshes,
underwater shadows, a translucent free surface and normal-driven refraction of
the tiled bed. Depth tests handle wall/ball/water overlap. Camera, water height
and marble positions interpolate between simulation steps.

The 60x36 staggered grid retains floating-point water volume and face velocity.
Two 10 ms substeps advect momentum, apply gravity/pressure and exchange volume
across faces from the same prior state. Closed faces reflect flow; the bounded
outgoing flux preserves nonnegative water and mass to floating-point tolerance.
A submerged sphere contributes solid column volume to the free surface; motion
of that volume generates waves. Buoyancy, submerged drag and pressure gradients
act back on the marble. Circle/wall and sphere/sphere contacts replace the old
square collision box. Wider mazes make currents and wakes visible.

This is a **coupled shallow-water approximation**, not a volumetric particle
solver: it has one surface height per grid cell, cannot overturn or produce
free-flying liquid sheets, and its material uses an analytic tiled-bed refraction
approximation. There are no time-scrolling water textures or unforced decorative
waves. The current surface on a guest is reconstructed locally, while the host
owns both marble bodies, pearls, timers and results.

The installed 0.2.1 was rejected by the operator for appearance and fluid feel.
That failure remains recorded. Earlier 0.2.0 device timing also failed: one
Tab5 B / OS 0.54 capture averaged 78.6 ms per cycle (52.3 ms game work, 23.5 ms
presentation). Host tests and transfers do not close either defect. Version
0.3.0 is a new candidate requiring actual-device cadence and operator acceptance.
The native C language has been used throughout; this was never a Lua conversion.

The existing 30 ms complementary tilt filter and sensor-free controls remain.
All three acceleration and gyro axes contribute through the calibrated basis;
spinning affects water momentum and vertical jolts affect the submerged body.

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
cmake -S games/tide_maze -B build-host/tide-3d -G Ninja
cmake --build build-host/tide-3d
ctest --test-dir build-host/tide-3d --output-on-failure
cmake -S tools/p4-game-host -B build-host/tide-3d-play -G Ninja \
  -DP4_GAME=tide_maze -DP4_ALLOW_DRAFT_GAME=ON
cmake --build build-host/tide-3d-play
ctest --test-dir build-host/tide-3d-play --output-on-failure
make play-game GAME=tide_maze
```

`PERFORMANCE_REWORK.json` preserves the historical 0.2.0 candidate, source closure and checks.
`LOCAL_TESTING.json` and `STRESS_TESTING.json` retain historical
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

State and scratch are statically bounded below the unchanged 128 KiB API limit.
See `ENGINE_REWORK.json` for the exact measured size and package/source closure.
The 12-row reciprocal-depth band occupies 18,432 bytes; material tags add 9,216.
There is no full-screen depth/color cache, heap allocation or private worker.
Projected water/sphere vertices and all solver scratch belong to each instance.
RGB565 drawing is native 768x480 with the 320x200 fallback; input stays canonical.

`p4/scene3d.h` and `p4/shallow_water.h` are optional header-only shared helpers;
no OS ABI or firmware update is needed. Single-precision instructions use the
pinned P4 F extension. The RV32 `-Os` span loop has no per-pixel division or
floating point; constant opaque faces take a depth-only path. A 192-scene baseline
checks exact pixels after optimization, both sizes, clipping/stride guards,
overlays and unchanged physical state. The old render hash file remains historical.

Protocol **2** retains 20-byte intents and 64-byte snapshots at 20 Hz. The host
also sends authoritative height and vertical speed. Guests interpolate the body
snapshots and simulate their water view locally; the full fluid field is not
transmitted and local prediction is not implemented. Both units require the
same new cartridge. Input expires after 250 ms; link loss ends the run after
three seconds. Physical linked latency and cadence remain separate acceptance.

The cartridge requires a motion-aware OS validator (installed Tab5 0.51 and
later recognize the optional motion bit). Missing sensor data falls back to
normal controls. Package, loader, stack and capability bounds are unchanged.
Game updates use the existing native USB content transfer, without an OS flash.

## Original artwork

The original ImageGen launcher illustration still ships in the cartridge as a
9,744-byte indexed icon; provenance is in `assets/perspective-provenance.json`.
It depicts the same marble/water-maze identity. Previous generated water and
sphere assets remain historical inputs and are no longer linked into the game.
The current bed is a 2,048-byte deterministic procedural tile pattern generated
by `tools/compile_floor.py`. All current scene animation is code-native geometry.

Reference reviewed: Matthias Müller's [interactive height-field water demo](https://matthias-research.github.io/pages/tenMinutePhysics/20-heightFieldWater.html)
for two-way solid/water interaction, and Chentanez/Müller's
[height-field fluid paper](https://matthias-research.github.io/pages/publications/hfFluid.pdf)
for velocity advection and mass transport. The implementation here is original
C, not copied JavaScript or a claim to reproduce that paper's GPU solver.

Code is MIT-licensed. The shared Arimo font retains its OFL attribution. The
launcher artwork is an illustration; gameplay captures come from the C renderer.

The earlier marble-lighting lookup and its generator are retained as historical
0.2.x assets; the 0.3.0 spherical meshes no longer use them.
