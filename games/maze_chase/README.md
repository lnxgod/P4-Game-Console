# Maze Chase

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/ARCADE`.

**Package:** `MAZE.P4G`. **Players:** Solo.

**Local preview:** `make play-game GAME=maze_chase` from the repository root.

Version 1.1.1, device qualification pending. Guide the golden chomping explorer through an original 25×13 labyrinth, collect every pearl, and turn the tables on four pursuing spirits with power crystals. This overhaul keeps the stable game ID, title, launcher ID, and Game API v1 ownership boundaries.

The playable maze uses 2.49 times the previous screen area. Connected cyan wall runs, rounded corners, subtle original circuit etching, luminous pickups, distinct spirit silhouettes, directional chomp frames, and a composed title/pause/result presentation draw directly into **768×480 RGB565**. A **320×200 fallback** retains the same complete board and controls. Neither mode stretches a low-resolution framebuffer.

## Arcade loop

- **Ember**, the coral flame, pursues your position. **Glint**, the cyan jelly, aims ahead of you. **Hex**, the violet wisp, uses your heading and Ember's position to flank. **Puff**, the orange cloud, retreats toward its corner when you get close. They alternate between scatter and chase phases and leave their starting area at staggered intervals.
- Four amber power crystals reverse the spirits at their next tile center and make vulnerable spirits flee. Their pale flashing warns when power is ending. Eating four distinct spirits in one power period awards **200, 400, 800, then 1,600 points**. Returning eyes use a bounded shortest path home. Revived spirits regain their normal color and danger after a release delay; another crystal can make them vulnerable again.
- Side portals connect the left and right ends of the middle corridor. Red berries appear after collecting 50 and 120 pearls and award a round-scaled bonus. Every 10,000 points earns another life, up to five. Clearing the board advances to the next round, increases pursuit speed, and gradually shortens power duration.
- A hit leaves a 700 ms recovery period, waits for a fresh direction or A press, then grants a visible 1.5 second shield. Holding a direction through a hit cannot restart the round or drain successive lives. Score and lives carry across rounds; a completed loss starts a new run while retaining the session best score.

Positions, rendered motion, pellet arrival, and contact share one current-to-target timeline. Swept relative contact catches opposing movement, including portal receiving contact, without sweeping an actor across the whole board. Bounded millisecond slices keep frightened/protection expiry consistent across different frame durations. Perpendicular turns can be buffered before an intersection; reversing direction responds immediately partway along an edge. Tunnel sprites slide through the two clipped lips instead of waiting and jumping at arrival.

## Controls

Keyboard/controller: arrows or WASD move; Space/Z is A; Enter/P is Start; Escape/Backspace/Q is Back. A or a direction starts the run; Start pauses/resumes; A retries after a loss; Back exits.

Touch coordinates remain canonical **320×200** at both resolutions. The compact `< v ^ >` buttons occupy x4–26, 28–50, 52–74, and 76–98 at y173–198. GO/RETRY is x264–316 in the same bottom strip. EXIT and PAUSE/RESUME remain in the top corners. Swipe in the maze to choose a direction. A contact keeps its starting role: a board swipe cannot turn into Exit or Pause when it reaches a screen edge. The game consumes its direct-touch controls before the old platform virtual-pad synthesis; physical controls continue to work when no finger is down.

## Original artwork and budget

`assets/chase_characters_v3.png` is original built-in ImageGen artwork: twelve directional chomp poses, four distinct spirits, frightened and returning states, berries, and pickups. `assets/chase_provenance.json` records the exact prompt, source hash, measured row/cell bounds, alpha threshold, and conversion. The 48×48 RGB565 sprite cells and 128×128 title hero use **143,360 bytes**. Existing original wall art contributes **32,768 bytes**, for **176,128 bytes** of static game image data. There is no runtime image decoder or asset allocation.

`assets/hires_atlas_v2.png` and its provenance retain the original wall-material source. The refreshed cartridge-owned launcher icon has separate provenance in `assets/launcher-provenance.json`. These are original project assets, not ROM rips or imported commercial characters. Game code/art are MIT. The shared Arimo presentation font is OFL-1.1; its notice is in `third_party/arimo/OFL.txt` and its pinned source is `third_party/arimo/source.json`.

```sh
python3 games/maze_chase/tools/convert_hires.py
python3 games/maze_chase/tools/convert_chase.py
```

## Local verification

```sh
cmake -S games/maze_chase -B build-host/maze_chase -G Ninja
cmake --build build-host/maze_chase
ctest --test-dir build-host/maze_chase --output-on-failure
build-host/maze_chase/maze_chase_hires_preview build-host/maze_chase/overhaul
cmake -S tools/p4-game-host -B build-host/play-maze_chase -G Ninja -DP4_GAME=maze_chase
cmake --build build-host/play-maze_chase
ctest --test-dir build-host/play-maze_chase --output-on-failure
make play-game GAME=maze_chase
```

The three focused sanitizer checks cover the existing game rules and real touch
pipeline, immediate mid-edge reversal without collecting unreached pearls,
continuous tunnel coordinates and split sprites, per-frame sub-tile actor
movement, and exact backdrop pixels with padded-stride guards. Five generic
SDL host checks also pass.

Version 1.1.1 precomputes the unchanged scenery at both native resolutions.
Deduplicated color-run rows use **83,640 cartridge bytes**, no heap/cache and no
retained framebuffer. Every supplied frame is completely redrawn. Regenerate
scenery after changing its procedural authoring code:

```sh
cmake --build build-host/maze_chase --target maze_chase_backdrop_generator
build-host/maze_chase/tools/maze_chase_backdrop_generator build-host/maze_chase/scenery
python3 games/maze_chase/tools/pack_backdrop.py build-host/maze_chase/scenery games/maze_chase/src/generated/backdrop.inc
```

The generator checks normal and round-clear palette equivalence. The focused
scenery test compares every output pixel against the procedural reference in
both modes. All sixteen standard scene captures remain identical. RV32 `-Os`
inspection confirms the scenery pixel loop is an increment, halfword store and
branch, with no per-pixel division or validation call.

The same active 2,000-frame Mac trace changed 1,724 frames before and after.
Native mean total CPU time fell from **0.1350 to 0.0906 ms** (33%); final p95
**0.305 ms**, p99 **0.326 ms**, max **0.353 ms**. Fallback maximum was
**0.086 ms**. These exclude device execution, panel and transport. They do not
establish a tablet frame rate or prove the reported device jumping is resolved.

The agent exercised keyboard start, movement, quick reversals, collision
recovery and pause in the updated SDL runner. Mouse interaction attempts lost
the app target; their result is unverified, although the deterministic real
mouse/mapper/service pipeline passed. Live swipe, full-round completion and
speaker quality remain unqualified. A bounded read-only capture on both
physical tablets recorded launcher activity only, with no Maze gameplay samples.

The primary Tab5 build packages **370,576 bytes**, including the unchanged
9,744-byte cartridge-owned launcher icon. `LOCAL_TESTING.json` binds the source,
checks, benchmark, package and per-unit transfer evidence. This is a game-only
update for the existing OS 0.51; physical fluidity acceptance remains pending.
