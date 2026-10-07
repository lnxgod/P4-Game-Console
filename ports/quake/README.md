# Quake port

[Monorepo](../../README.md) · [Console OS](../../apps/console_os/README.md)

This directory contains the SDL3 host adapter and build definition for the
pinned Quake engine. The retained device adapter is in
[`components/p4_quake`](../../components/p4_quake/). Quake is **hidden from the
current Console OS launcher**: the OS does not link the adapter, scan its PAK
or start its receiver. It is not a default installed game. Retained
source and upload tooling do not mean the Tab5 game is release-ready.

The PC adapter renders a 512×300 indexed image aspect-fit into a 768×480
window, with keyboard/mouse/gamepad input and software-mixed SDL3 audio. Its
protocol-15 UDP networking remains separate from the Console OS P4MP service.

## Data and local preview

The engine source is the pinned GPL-covered tree under
[`third_party/quakegeneric`](../../third_party/quakegeneric/). The shareware
`id1/pak0.pak` is a separate, ignored local input; its exact identity and source
are in [`third_party/game-data.json`](../../third_party/game-data.json).
Do not commit PAKs or commercial game data.

With CMake, Ninja, SDL3 and the verified local PAK prepared, run from the root:

```sh
python3 scripts/quake/verify-quakegeneric.py
cmake -S ports/quake -B build-host/quake -G Ninja -DP4_QUAKE_DATA_DIR="$PWD/local-data/quake"
cmake --build build-host/quake
ctest --test-dir build-host/quake --output-on-failure
build-host/quake/p4_quake_host --basedir "$PWD/local-data/quake"
```

The host runner offers `--headless`, `--no-audio`, `--frames COUNT` and trailing
engine arguments after `--`. Use Quake's in-game controls menu for bindings.
A data-backed smoke test is registered only when a data directory is configured.
This is a separate engine runner, not `make play-game GAME=quake`.

## Scope

No Console OS Quake multiplayer or current physical Tab5 acceptance is claimed
here. Device integration must follow the [Tab5 guide](../../docs/boards/M5STACK_TAB5.md)
and [content policy](../../docs/CONTENT_LIBRARY.md), including the reviewed
engine/storage lifecycle. Do not add an arbitrary-data launcher entry or use a
historical install script as authorization to flash a device.

## Historical embedded adapter

The retired Waveshare adapter loaded only the exact
hash-gated `/sdcard/GAMES/QUAKE/ID1/PAK0.PAK`, denies engine writes, renders
512x300 into a 768x450 view centered on the OS-owned 768x480 canvas, and sends
software-mixed audio through the OS-owned 16 kHz stereo session. Touch overlays
provide movement, fire, jump, Escape, and Enter. It targeted near 30 FPS and
restarted Console OS after exit because the engine's global state is not
reentrant.

The engine ran on a dedicated 96 KiB PSRAM task stack. This was a platform
boundary, not a workaround for one call site: the pinned legacy engine has a
16 KiB console-resize frame and a later 32 KiB save-game frame, either of which
is too large for Console OS's 24 KiB main task. The retired adapter uses the
external-stack launch path, and the first successfully submitted frame logs
the task's stack low-water mark. WinQuake's roughly 128 KiB software-renderer
edge/surface scratch is separately moved into the component's PSRAM-backed BSS;
it is too large even for the isolated engine stack. A single display refresh
timeout drops a frame and recovers on the next successful submit; three
consecutive timeouts still stop the easter egg fail-closed.

The dormant embedded adapter uses loopback-only networking. Protocol-15 UDP on
PC remains intact. Building the standalone port does not flash or constitute
hardware acceptance.
