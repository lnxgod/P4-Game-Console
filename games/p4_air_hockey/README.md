# Air Hockey

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included. **Folder:** `GAMES/SPORTS`.

**Package:** `P4_AIR_HOCKEY.P4G`. **Players:** Solo vs CPU; 2 linked.

**Local preview:** `make play-game GAME=p4_air_hockey` from the repository root.

Linked counts describe the game profile; see [transport and hardware limits](../README.md#test-status-and-multiplayer).

Air Hockey is a native Game API v1 table-sport game for two P4 consoles.
The Host console owns puck physics, collisions, scoring, and rematches. Each
console controls one paddle, and the Join console sends bounded input intents
while receiving complete 30 Hz snapshots through the OS-owned P4MP session.
The Join view mirrors the long axis so each player always defends the left
goal and sees the rival on the right. Player identity colors remain fixed
across both screens: the Host striker is cyan and the Join striker is magenta.

Use only the `p4/` headers for display, controls, drawing, and sound. Keep
board drivers and raw ESP-IDF peripheral ownership in platform components.
Touch or drag anywhere in your half to move the mallet. First to seven wins;
tap Rematch on the result panel and tap Exit to return to the launcher.
A normal launcher start opens the Air Hockey title screen; tap Play or press
A/Start to face the CPU. During an offline match, tap Pause or press Start;
tap Resume or press A/Start to continue exactly where you stopped. Back exits
from every screen. Linked matches enter through the OS start barrier, show
LINKED in the header and keep running when Start is pressed; there is no
unsynchronized local pause. Menu actions require a fresh tap, so dragging a
mallet into the header cannot activate Exit or Pause. There is no permanent
virtual gamepad, and paddle movement remains direct touch.
If a multiplayer peer leaves, the cartridge neutralizes network input and
starts a fresh CPU match.


Cyber strikers, puck sprites, and layered event tones replace the original
placeholder circles and beeps. Sound-event bits travel in the authoritative
snapshot so Host and Join consoles hear the same serve, hit, wall, goal, and
win cues.

The `realtime` multiplayer profile in `game.json` uses protocol 4, a 30 Hz
session tick, and a 32-byte maximum message. After installation, Console OS
validates the P4G and automatically
registers it in the Multiplayer selector before its first launch. There is no
game-side registration call and no central OS table to edit. Game code uses
only `p4_game_multiplayer_read_profile()`,
`p4_game_multiplayer_read_status()`, `p4_game_multiplayer_send()`, and
`p4_game_multiplayer_receive()`; it never chooses BLE, UART, or USB directly.
Keep a complete offline mode and increment `multiplayer.protocol` whenever the
meaning of your game messages changes.

## Focused host proof

```sh
cmake -S games/p4_air_hockey -B build-host/p4_air_hockey -G Ninja
cmake --build build-host/p4_air_hockey
ctest --test-dir build-host/p4_air_hockey --output-on-failure
```

## Native high-resolution presentation

Version 1.1.0 negotiates **768x480** through optional `video-highres`,
with a complete **320x200** fallback. Touch coordinates remain canonical
320x200 in both modes. Game geometry is rasterized directly into the supplied
surface, with native 24/38px antialiased typography in high resolution and
legible compact bitmap text in fallback; no small framebuffer is enlarged.

The original ImageGen atlas `assets/presentation_imagegen_v2.png` provides
66px glossy cyan/magenta strikers and puck, with 64px rink and rail materials.
The exact built-in generation prompt, source SHA-256, reviewed crops and
license are recorded in `assets/presentation_provenance.json`. These are
original project assets distributed under MIT, with no commercial game art.
The deterministic `tools/convert_presentation.py` converter uses reviewed
source rectangles, nearest-neighbor sampling, bounded RGB565 arrays and an
explicit transparent key. Its compiled image budget is **75,288 bytes**;
font data and game code are additional. Original earlier art remains as
historical source material and is no longer included by the game renderer.

Run the converter with Pillow installed:

```sh
python3 tools/convert_presentation.py
```

Focused tests cover padded framebuffer guards and full-surface rendering at
both resolutions, canonical touch targets after rendering, and representative
menus, play and result states. Set `P4_CAPTURE_DIR` to an existing absolute
directory to retain native PPM captures from the presentation test. The
existing rules and mocked multiplayer assertions remain covered, including linked
start and continued simulation when local pause controls are pressed. See
`LOCAL_TESTING.json` for exact automated evidence and pending interactive/
physical-device acceptance; host tests are not hardware acceptance.

## Frame-time budget

The minimum target is 30 presented frames per second (33.333ms per frame).
The renderer copies mirrored material row spans and paints visible regions
without repeatedly painting covered full-screen layers. State, touch targets,
rule timing and multiplayer packet formats remain unchanged.
Moving objects keep their fixed-point coordinates until native pixel mapping,
so subpixel motion remains visible on the high-resolution surface.

The shared optimized CPU benchmark uses real updates, rendering and audio:

```sh
cmake --build build-host/play-p4_air_hockey --target p4_game_benchmark
build-host/play-p4_air_hockey/p4_game_benchmark 2000 768 games/p4_air_hockey/tests/performance-input.txt
```

The input tape explicitly starts the game before exercising real play. A focused
replay checks that more than 1,500 of its 2,000 frames are active puck play and
more than 1,700 contain moving presentation poses. Timings in `LOCAL_TESTING.json` describe
this Mac CPU run, exclude display/transport/device costs, and **do not certify
30 FPS on the ESP32-P4**. The simulator targets 60Hz; physical Tab5 frame-time
and presentation measurements remain required for device acceptance.


## ESP32-P4 raster work reduction

The material converter now stores each 64-pixel row followed by its mirror.
This adds 24,576 read-only asset bytes and removes the per-region-row stack
reconstruction: 1,855 row rebuilds per native frame. Rink circles use exact
horizontal annulus spans instead of scanning the full bounding squares.
No full-screen cache, new allocation, lower detail, physics change, or protocol
change is involved. The artwork, source PNG and rendered pixels are unchanged.

The focused presentation suite compares the annulus to the original geometric
definition at both resolutions, including clipped edges and padded strides;
material rows are checked against independently mirrored texels. Before/after
native and fallback scene captures are byte-identical. The pinned RV32 `-Os`
assembly shows the material helper stack reduction from 592 to 80 bytes and
removal of the per-row mirror loop. These are architecture-specific work
reductions, not a claim of measured device FPS. Final shared-helper and cartridge
hashes are recorded separately for the subsequent physical test.

Host/offline presentation now interpolates the previous and current fixed-step
poses using the 16 ms remainder. Collision, score and network state still use
only the authoritative pose. Join presentation interpolates from the currently
displayed pose toward a new snapshot over at most 33 ms, then holds if packets
are late; it never extrapolates. Serve, goal, match reset and first-snapshot
transitions snap to their new state. This adds at most one simulation step of
visual delay offline/Host and one snapshot interval on Join while avoiding
alternating one/two-step jumps. Tests exercise alternating 16/17 ms frames,
early/late packets, phase transitions, unchanged authoritative positions and
render purity. The protocol remains version 4 with the same 32-byte packet.

The offline title, pause and match-result panels reuse the original rink art
and striker sprites with native crisp typography and visible Play, Resume,
Rematch and Exit buttons. The top bar keeps the game name, local/rival score
and current mode visible; rink geometry and canonical touch coordinates are
unchanged. No simulation or serve timer advances in offline menus. Pause
stops queued tones and clears the local movement target without changing the
puck, score or accumulated sub-step. Peer loss still transitions directly to
a fresh CPU match rather than a title screen. These UI fields are local only
and never appear in the existing protocol-4 messages.
