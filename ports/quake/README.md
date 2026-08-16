# Quake easter-egg port

> **Dormant compatibility port:** Current Console OS does not link this port,
> scan for its PAK, start its USB receiver, or show Quake in the launcher. The
> files remain for isolated host experiments and historical failure analysis.

This port keeps Quake separate from the open P4 Cart ecosystem. The engine is
GPL source pinned under `third_party/quakegeneric`; Quake's game-data PAK is a
local/SD input with separate shareware terms and is never committed or embedded
into firmware.

The PC adapter uses SDL3, renders the engine's 512x300 indexed framebuffer in
the Console OS 768x480 landscape window, supports keyboard/mouse/gamepad input,
queues Quake's software-mixed PCM through SDL3, and retains protocol-15 UDP.
The 512x300 image is aspect-fit at 768x450 with 15-pixel top/bottom margins.

```sh
make quake-provenance
make quake-smoke
make quake-play
```

`quake-smoke` verifies the ignored PAK identity and runs eight deterministic
headless frames. `quake-play` opens a local window. Neither target builds or
flashes ESP32 firmware.

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
