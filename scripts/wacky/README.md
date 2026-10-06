# Wacky Wheels on P4

This local development port uses Justin Marshall's recreation at revision
`7dd510096c58ee84c36970a11c4f57b4a9c2a4cf` from
<https://git.retrodamage.com/jmarshall/wacky-wheels>, with the separately pinned
v1.1 shareware archive. The initial prototype deliberately launched track one
and omitted the upstream menu/audio layer. Those omissions were in the port,
not an absence of menus or music in the upstream source.

The shareware contains **eight map asset sets**: `1–5`, `7`, `16`, `17`.
Five are available as ordinary races and one as Duck Shoot. The additional
16/17 maps remain unused: the pinned recreation's two-player race and shootout
menu entries have no gameplay implementation. Registered/bonus courses absent
from the shareware have not been substituted from the upstream repository.

## Included

- Original animated intro, eight drivers, five selectable race courses,
  five-race championship and original order-of-finish/podium screens.
- Three racing classes, 6/12 HP engines, selectable laps, time trial with saved
  course records, the upstream three-lap kid setting, and Duck Shoot.
- Pause/resume/restart/home, visible gas/brake/steering/fire touch controls,
  normalized controller controls, menu touch rows,
  clock/map/speedometer and separate music/effects/engine volume settings.
- Original instruction/shareware information slides and original launcher art.
- Actual SMF MIDI playback through `components/p4_midi`, using the procedural
  synthesizer derived from the existing Doom music backend. This is MIDI
  playback, not a recording; its instrument timbre differs from Freepats.
  All 16 MIDI assets parse through a complete loop and produce nonzero PCM.
- Original VOC effects and pitch-changing engine audio. All 36 VOC assets decode;
  gameplay hooks use the relevant start, firing, crash, water, last-lap and duck
  effects. Audio streams through the OS-owned 16 kHz stereo PCM API.
- Versioned, bounded automatic saves for settings, Duck Shoot best score and
  time-trial records, using the OS `AUTO` slot.

This remains an experimental port. In particular, the network mode below is a
new non-contact sprint, not the original DOS item-combat/split-screen mode.
No full commercial-game or original shootout parity is claimed.

## Reproduce

With the existing CMake/Ninja/SDL3 and pinned P4 toolchain:

```sh
python3 scripts/wacky/fetch.py
python3 scripts/wacky/prepare.py
cmake -S scripts/wacky -B build-host/wacky-p4 -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host/wacky-p4 -j 8
ctest --test-dir build-host/wacky-p4 -R '^wacky_' --output-on-failure
python3 scripts/wacky/cross_build.py
python3 scripts/wacky/package_probe.py
build-host/wacky-p4/wacky_play.app/Contents/MacOS/wacky_play
```

The simulator compiles the shared SDL3 host runner against the actual port.
Arrows steer/accelerate/brake, Space/Z is A, X/Shift is B/fire, Enter/P is Start,
and Escape/Q/Backspace exits. Menu arrows select; A chooses; B returns.
The original 320x200 presentation is preserved and scaled by the host/display;
a large simulator window does not imply native 768x480 detail.

Generated sources, upstream source/data, binaries and packages stay ignored.
`prepare.py` preserves the pinned upstream checkout and applies explicit
portable adaptations to its generated copy. `cross_build.py` uses the installed
Tab5 compiler, frozen cartridge import allowlist, actual Console OS ELF parser,
and 4 KiB individual stack-frame limit. `package_probe.py` checks the current
catalog for identity conflicts and validates Host/Join registration.

The local bundle is `build-host/wacky-riscv/bundle/GAMES/WACKYTRY.P4G` and
`WACKYTRY.P4R`. Game ID `org.p4console.wacky-probe` and filenames remain stable.
Version 0.1.1 uses launcher ID **9001**: the old prototype's 120 now belongs to
Tide Maze, so the new cartridge must not reuse it. Saves bind the unchanged game
ID. The resource archive is unchanged from the installed prototype.

The 0.1.1 touch overlay matches the OS's canonical touch regions and highlights
held controls. Menus accept direct row taps, and their bottom strip returns to
the previous screen. The matching Tab5 OS 0.54 candidate moves native PCM output
onto core 1 while game updates/rendering remain on core 0. Its bounded command
queue copies each PCM block, and its 128 ms mixer buffer absorbs uneven game
frame arrival after a 48 ms startup prefill. This fixes the known 0.53 mismatch
between a 32 ms FIFO and roughly 39 ms observed Wacky frames; physical listening
and cadence still need to be measured on the new image.

## Linked racing (protocol 2)

Use Console OS Multiplayer → Host/Join with identical packages on each console.
Normal launcher entry opens the offline game. Two racers use supported P4MP
transports; **three or four require the new OS local Wi-Fi adapter**. Bluetooth
and the existing USB serial relay retain their two-console limit. Local Wi-Fi
uses the host console's nearby network without a router or internet.

The OS creates and admits the room, matches content, assigns slots, waits for
all racers at the start barrier, and owns packet routing and teardown. The game
has no radio, socket, USB or lobby implementation. Wi-Fi permits three client
routes and rejects a fourth. Older OS adapters may reject this four-player
profile; install the matching OS candidate before testing it.

Each match is a three-lap sprint on one of the five courses, selected from the
shared session seed. Each slot gets its own driver/view. The host evaluates all
cars' physics, checkpoint order and finish places. Clients submit only steering,
accelerator and brake intentions. Items and CPU opponents remain offline-only.
Start does not pause linked play; Back ends the session.

Protocol 2 uses 25-byte input messages and one or two 64-byte snapshot parts.
Each part contains two cars, a shared session seed, revision and tick. Clients
apply only complete consistent snapshots with distinct valid ranks/finish places.
Receive work is capped at eight packets per update. Messages run at an 88 ms
cadence. Failed sends are replaced by later full states; stale controls clear
at 250 ms, the host freezes at 500 ms, and loss ends the match after three seconds.
Session generation changes and OS peer-left/error events also end the match.

## Validation and limits

ASan/UBSan tests cover all five courses with all eight drivers, menu callbacks,
Duck Shoot, championship results/podium transitions, time trial/kid settings,
intro, all original info slides, automatic-save round trips, MIDI/VOC parsing,
audio backpressure, Back, restart and leak-free cleanup. Championship transition
checks seed completed-race state; they are not a claim of five manually driven
wins. Checkpoint tests place cars on actual map checkpoints before running the
real physics/lap code, including reverse-crossing suppression.
Touch checks feed the actual OS mapper into race physics, compare movement with
equivalent normalized inputs, and cover simultaneous gas/steering, brake, fire,
release, pause, exit and menu return. The core-1 worker separately passes
ASan/UBSan and ThreadSanitizer tests for uneven PCM arrival, queue bounds,
stop/restart, write failures and joined teardown.

Two-, three- and four-instance game harnesses exercise real port instances,
different local generations, independent inputs/views, packet loss, full queues,
malformed/stale packets, atomic snapshots, finish order and disconnects. Three-
and four-player cases run on every available race course. Shared P4MP start and
Wi-Fi tests cover roster bounds, start retries, missing-player timeout and three
real UDP client routes. These results do not prove four physical consoles.

The renderer borrows the host's RGB565 surface as indexed scratch and expands
it backwards. Assets borrow immutable resource bytes. P4 aggregate heap is
bounded at 512 KiB; the multi-instance host harness allows 2 MiB. The original
136/12 Hz simulation is retained with interpolated camera/opponent motion at
host rendering cadence. Rendering frequency cannot advance game logic.

User feedback on the simulator confirmed that it looks good and sound effects
work. Exact cartridge/OS/unit bindings and hardware acceptance are recorded in
`test-runs/2026-10-06-wacky-wheels-full-port.json`. Host timing is not device FPS;
physical touch, speaker quality and sustained linked performance need device
observations. The target remains 60 FPS with a measured 30 FPS device floor.
The follow-up touch/core-1 audio candidate and its verified installation on
both Tab5 units are recorded in
`test-runs/2026-10-06-wacky-touch-dualcore.json`. The subsequent log captures did
not contain a Wacky Wheels launch, so this candidate's physical play acceptance
remains pending.

## Repository publication

The repository contains the port adapters, reproducible preparation/build
scripts, tests and shared MIDI/audio services. Downloaded upstream source and
shareware data remain under ignored `.tools/`; generated cartridges, firmware,
raw screenshots and device backups are local inputs or outputs. Run the
reproduction commands above to prepare a local game. Publishing this source
workflow does not make the experimental port a qualified default game or add
it to the shipping catalog.

## Source/data status

The pinned recreation has no explicit source license, and its original
repository also contains registered game data. This port uses only separately
hashed shareware, keeps upstream code/data and generated artifacts local, and
is absent from the shipping catalog. Resolve redistribution terms before
publishing it. The new MIDI component retains the Doom backend's GPL-2.0-or-later
notice. Original shareware notices accompany the local bundle.
