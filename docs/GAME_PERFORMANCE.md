# ESP32-P4 game performance

Use this contract when creating, upgrading or qualifying games. It complements
[Game Art](GAME_ART.md) and the [Game SDK](GAME_SDK.md); it does not expand a
game task into hardware work or authorize an install. Local changes can be
complete with device qualification explicitly pending.

## Design for the actual runtime

Native games target **60 presented FPS**, with **30 FPS the release floor on
the actual ESP32-P4** during sustained representative play. Render native
768×480 RGB565 for every maintained Tab5 game. Require high resolution in
the native manifest and descriptor, then verify the selected runtime surface.
Canonical 320×200 touch coordinates are input units, not a framebuffer target.
Never use a 320×200 framebuffer, upscale a completed low-resolution frame or
enable a per-title low-resolution override to satisfy the frame-rate floor or
repair readability. Optimize the measured native update/render/presentation
cost instead. Keep an unmet native cadence or readability gate open until it
passes; a fallback benchmark cannot close it. Preserve existing fallback source
and bounded ABI tests for explicitly requested legacy maintenance.
Native C is the supported authoring route, including custom software 2D/3D
engines and raycasters. A language or engine choice does not guarantee cadence;
measure the real P4 candidate. Native games have no script-renderer ceiling.

Keep simulation and presentation separate. Use elapsed time and retain
fractional positions and leftover animation time. Tween grid movement or
interpolate received snapshots without changing collisions, deterministic
rules, network tick rates, saves or supported player counts. Respawn/effect
timers must advance while input is locked. Bound catch-up work after a long
frame so recovering cannot create another stall.

Bound per-tick AI, collision candidates, particles, message processing and
retries. Use fixed or reused storage. Budget custom-engine geometry, depth
buffers, texture working sets and scratch space explicitly. Keep package, state,
stack, atlas and resource-cache totals within the existing SDK/manifest limits.
A full-screen RGB565 buffer alone is 737,280 bytes at 768×480: do not add an
unbudgeted cache or raise limits to conceal a rendering cost. Convert art
offline and use the existing bounded resource service where needed.

## Require fluid action play

Action-game actors must move continuously between simulation positions. A grid
may define rules, but whole-cell snapping is not an acceptable presentation for
maze, crossing, platform or sports games. Card and board games may use deliberate
turn-based movement. Test motion at native size and on the tablet; a 60 FPS title
counter or smooth desktop run cannot excuse tile-sized jumps on the device.
Fix action titles that fail this requirement, or disable and remove them from
the playable library. Finish useful, complete games before adding more demos.

## Keep large pixel loops inexpensive

Shared `p4/draw.h`, `p4/presentation.h` and `p4/visual.h` primitives are optional
building blocks. Use them where they fit; a custom software renderer is a valid
native implementation. Put generally reusable improvements in the shared
component while keeping game-specific engine code in its cartridge.
For custom hot paths, clip/validate once, cache row addresses and write
contiguous spans. Reuse texture rows or bounded scale mappings when useful;
avoid repeated API validation, division/modulus, redundant full-frame passes
and unnecessary 64-bit arithmetic inside loops touching many pixels. Keep
wide arithmetic where clipping/input overflow requires it, then narrow only
proven bounded values. Choose optimizations from measurements and compiled
code, not blanket bans on arithmetic or visual detail.

For a costly native raster path, inspect the pinned **RV32 `-Os`** assembly
using the cartridge packager's actual compiler flags. Desktop `-O2` timings
can hide per-pixel address multiplies, division and 64-bit carry/comparison
work. Record the affected path and before/after evidence. Preserve exact
pixels for a pure performance rewrite, with meaningful clipping/overflow,
padded-stride and guard tests; use capture hashes for unchanged scenes.
Shared helpers are compiled into cartridges: rebuild affected `.P4G` files
and their evidence, since an OS update alone does not replace that code.

Console OS owns pacing, buffers, asynchronous display submission and safe
reuse fences. Games render the complete supplied surface and use bounded
non-blocking services. Do not add display waits, sleeps, DMA ownership or a
private frame loop to a game. Diagnose input/update/render/presentation phases
at the shared boundary when needed; a proposed OS buffering fix is not proof
that any installed unit has it or meets the frame budget.

## Measure active play before adding more work

### Use the P4's two cores through shared OS services

Keep native game simulation, rendering and cartridge callbacks on the Console
OS foreground core (core 0 on the Tab5 configuration). The Tab5 0.54 candidate
uses `components/p4_game_platform/src/audio_worker.c` to mix and feed I2S from
a bounded core-1 task. PCM/tone callbacks copy commands without waiting;
the worker owns its mixer and board audio session until joined shutdown.
The C6 radio is a separate communications processor, not another game-rendering
core. Do not add FreeRTOS tasks, I2S handles or board-specific affinity inside
cartridges. Other independent work belongs in an appropriate shared service.

SMP being enabled does not prove useful parallel execution. Record actual
`AUDIO_WORKER game_core=0 audio_core=1` evidence, queue rejections, underruns,
clipping, write failures and stack reserve alongside frame timings. Keep each
mutable resource single-owned, copy bounded messages across cores, and join
workers before closing peripherals, unloading cartridge code or freeing data.
Never hold a queue/telemetry lock during I2S, display, SD or radio waits.

Budget PCM buffering against measured producer jitter: 512 frames at 16 kHz
hold only 32 ms and necessarily overflow when a frame submits 39 ms of audio.
The shared mixer now holds 2048 frames (128 ms); the worker has eight bounded
commands and a 768-frame startup prefill. The output sample clock is independent
of display cadence. Validate concurrent burst delivery, stop/restart generations,
backpressure, failed writes and teardown with sanitizers, then listen and
measure on the exact device. Host concurrency tests are not hardware proof.

Use focused rule/render tests, the sanitizer SDL loop and native-size visual
review first. The optimized native CPU benchmark compiles real game sources:

```sh
cmake --build build-host/play-<slug> --target p4_game_benchmark
build-host/play-<slug>/p4_game_benchmark 2000 768 path/to/input-tape.txt
```

The benchmark's 320-size mode is retained for explicit legacy diagnostics;
it cannot qualify maintained Tab5 performance or presentation.

Tape rows are increasing frame indices, held button masks and canonical touch
x/y; `30 16 -1 -1` presses A, and a later mask-zero row releases it. Use -1/-1
for no touch. Deliberately include sustained movement/dragging, busy effects,
CPU turns and transitions as appropriate. Check changed-frame counts and the
trace itself: a few moves followed by idle is not a drag or motion stress run.
The benchmark includes update/render/audio and reports p50/p95/p99/max plus
frames over 33.333 ms; it excludes the panel, transport and device execution.
Keep interactive SDL presentation/audio checks separate from CPU throughput.

## Qualify the exact device candidate

Record source, input trace, executable and package/resource hashes, OS image
version/hash, physical unit binding, surface size, test duration and actions.
Keep raw serial frame/deadline/display evidence and, where available, phase
timings and presented-frame intervals: p95/p99, worst gap and missed 33.333 ms
budgets. Separate loading/first-frame latency from steady gameplay without
hiding either. Averages or capped timing counters cannot prove a minimum;
retain stalls and gaps rather than relabelling an update rate as presented FPS.

Exercise sustained active play and demanding states on the selected unit;
linked games also need both roles and their declared player/session load.
Check physical touch responsiveness, motion and audio continuity alongside
cadence. Diagnose and retest cases below 30 FPS or with visible stutter before
calling the game release-ready. If raw cadence, hardware or a needed scenario
is unavailable, record that limitation and leave device qualification pending.
Mac CPU results, simulator title FPS, successful builds and successful transfers
are separate evidence, never a substitute for ESP32-P4 performance acceptance.
Preserve successful installation history by exact package and unit even when a
new candidate is pending; do not silently carry old acceptance onto new hashes.

## Rejected candidates and artifact closure

Operator reports of stutter, severe lag or unusable motion are failed gameplay
acceptance, even when a host benchmark or package transfer passed. Preserve the
known installed hash and report; distinguish an observed failure from a missing
measurement. Diagnose frame phases and the visual design before adding effects.
Use the final packaging checkout for host tests and assembly review, including
all linked shared helpers and headers. Retest after moving work between checkouts.
A game-source hash alone cannot bind a benchmark to an ELF with a different
raster library. A device update for diagnosis remains a candidate until active
play, cadence and the operator's reported defect have been checked.
