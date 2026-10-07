# ESP32-P4 app and game performance

Always apply this contract when creating, changing or qualifying Console OS,
apps, native games and integrated engines. It complements
[Game Art](GAME_ART.md) and the [Game SDK](GAME_SDK.md); it does not expand a
local task into hardware work or authorize an install. Local changes can be
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

Blast Circuit 0.2.9 has an explicit owner exception accepting approximately
19 FPS for the exact package, OS 0.77 and Tab5 unit recorded in
[its acceptance report](../games/blast_circuit/NATIVE_READABILITY_TESTING.json).
This exception permits keeping that measured candidate; it does not change
the general 30 FPS floor or qualify other packages, OS builds or units.
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

## Always preserve smooth scrolling

Every scrollable app/game surface must always provide smooth scrolling at the
panel's presentation rate, near 60 presented FPS on the maintained Tab5.
This includes launcher/catalog and file lists, menus, settings, in-game lists
and scrolling playfields. The 30 FPS game release floor does not qualify a
choppy scrolling surface. Static surfaces need no artificial scrolling or
continuous redraws; preserve each game's intended movement and camera rules.

Content must promptly follow contact from the first real drag through release.
Keep fractional displacement, use actual elapsed time and fresh samples for
velocity-aware momentum, and decelerate smoothly to a bounded stop. A slow drag
must not become a large coast. Touching moving content must stop it promptly
without launching the touched item; a drag must not become a tap on release.
Preserve hit regions, layout, tap/actions, focus/selection, file safeguards,
reduced-motion behavior, invalid/stale input cancellation and multiplayer rules.

Reuse bounded OS rendering, raster caches, damage tracking and DMA services
where they fit. Avoid whole-scene or whole-viewport reconstruction for an
otherwise provable scroll delta; retain an authoritative full-render fallback.
Caches have explicit capacity, revision and preparation epochs, and are
prepared/published in bounded idle slices rather than during a drag or glide.
An immutable source is DMA-clean only after its current written rows were
published; unchanged pointers or content signatures do not prove cleanliness.
Partial logical pixels may never enter an ordinary full submission without
reconstruction. Preserve exact stationary context, geometry, source/offset
bindings, dirty-region proof and conservative physical-buffer retirement.
Release caches and join owners before game handoff or freeing their storage.

For rendering optimizations, compare composed/cached pixels with the
authoritative renderer. Cover odd/even shifts, padded strides and guards,
endpoint/footer changes, focus/press/status updates, cold caches, invalidation,
recenter, publication failure/retry and handoff. Test fast/slow releases at
different sample/frame cadences, reversals, stop-fling touches, stale input and
no accidental activation. Use the cases relevant to the changed surface;
shared OS improvements must remain reusable across apps and games.

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
non-blocking services. The supplied `surface.pixels` and stride belong to the
current frame and may change on every frame. Read them from the supplied
surface each time; do not cache the pixel pointer across callbacks or retain
it after presentation. Render the complete current frame rather than relying
on the contents of a previously borrowed buffer.
Do not add display waits, sleeps, DMA ownership or a
private frame loop to a game. Diagnose input/update/render/presentation phases
at the shared boundary when needed; a proposed OS buffering fix is not proof
that any installed unit has it or meets the frame budget.

## Measure active play before adding more work

### Always use relevant resources and both P4 application cores

Always plan and use all relevant available P4 resources for responsive work:
both application cores, shared input/logic/render/audio workers, PPA and DMA,
bounded SRAM/PSRAM caches and buffers, queues and panel pacing. Identify the
available services and measured bottleneck before choosing the work split;
record why a resource is inapplicable or unsafe rather than ignoring it.
This requirement does not mean occupying idle cores, allocating unused caches
or enabling unrelated peripherals. Respect the selected hardware scope.

Use both cores for useful independent work through OS-owned, joined services.
The Tab5 shell keeps input sampling on core 0 independent of its core-1 joined
render/display owner. The maintained native-game candidate keeps
update/render callbacks on core 0, with PPA presentation on core 1 at priority 2
and audio output on core 1 at priority 4 through
`components/p4_game_platform/src/audio_worker.c`. PCM/tone callbacks copy
bounded commands without waiting; the audio worker owns its mixer and board
audio session until joined shutdown. Verify the actual service affinities.
The C6 is a separate communications processor, not another P4 application core.
Games use stable APIs and must not create FreeRTOS tasks, raw display/DMA/I2S
handles, board-specific affinity or a private presentation loop. Add reusable
parallel work at the owning platform service boundary when needed.

The native video worker owns exactly two PSRAM game framebuffers for direct
768x480 RGB565 rendering; these leases are separate from physical panel scanout
buffers. The foreground borrows one writable lease, fills the entire current
frame, and commits it without a framebuffer copy. A committed frame stays
immutable until the backend consumes its source; bounded admission and reuse
fences prevent an in-flight frame from being overwritten. The OS refreshes the
game surface pointer when it acquires the next lease. Drain and join the worker
before freeing OS-owned buffers/context or returning display ownership to the
launcher. The native backend retains only OS-owned callbacks and pixel buffers,
so cartridge unload does not invalidate an in-flight frame. A failed join
retains the worker and its resources until safe shutdown. If worker allocation
requires synchronous recovery, preserve the same direct 768x480 surface; no
low-resolution fallback is allowed.

Keep every mutable resource single-owned and copy bounded messages across
cores. Join workers before closing their peripherals, freeing their data or
unloading code they reference. Never hold a queue/telemetry lock during I2S,
display, SD or radio waits. Preserve immutable source publication, cache
synchronization, DMA joins and retirement fences; parallelism must not weaken
ownership or lifetime proof.

SMP configuration and worker creation do not prove useful parallel execution
or the device cadence gates. On the exact OS/package/unit candidate, record
actual input, game/update/render, video and audio core IDs, priorities and
accelerator paths, completed-backend frame intervals, and concurrent phase
timing showing game/update/render work overlapping backend presentation.
Retain `UI_WORKER_READY`, `VIDEO_WORKER_START` and `AUDIO_WORKER` evidence where
those services apply. Keep accepted submissions, completed backend work and
physical scanout evidence distinct. Include queue and backend timeouts, hard
errors, audio queue rejections, underruns, clipping, write failures and stack
reserve. Exercise busy gameplay, title/ready, pause and results transitions,
plus stop/restart and synchronous native recovery. Device 30 FPS and native
readability remain pending until measured; scrolling additionally requires the
near-60-FPS and input/glide/cold-cache evidence defined in this contract.

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

For scrolling, always measure separate input report age/contact-to-visible
latency, update/logic and render time, display copy/rotation/submission and
panel pacing/presented intervals. Report missed panel periods (about 16.7 ms
at 60 Hz), p95/p99 and worst gap; distinguish active drag from release/glide,
first-drag/cold-cache preparation from warm motion, and endpoint/reversal
transitions. Record actual core and accelerator use, cache readiness/epoch,
fallback counts and buffer reuse waits for the exact image. Include quick
flicks, slow drags and touch-to-stop; an idle FPS counter cannot close this
gate. Preserve owner reports of hopping, delayed jumps or choppy motion as
failed acceptance until the same surface is retested. If device timing or
physical feedback is unavailable, leave smooth-scroll qualification pending.

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
