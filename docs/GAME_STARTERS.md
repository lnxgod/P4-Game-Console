# Making a P4 game

Start with the player's idea. These are optional starting points and examples
of reusable techniques, not a menu of allowed games. A game can combine genres,
use its own art and rules, or start from the smallest scaffold.

Use the repository's existing APIs and examples to answer implementation
questions. Do not ask an author to choose a template, transport or board driver
before helping with a clear game idea. Ask only for a missing decision that
changes the experience, such as same-device versus linked-console multiplayer.

## Use the native game path

| Need | Starting path |
| --- | --- |
| Any new game or native remix | [Native C authoring skill](../.agents/skills/esp32-make-game/SKILL.md), Game API v1 and `.P4G` |
| Networking added to a native game | Add the [multiplayer skill](../.agents/skills/esp32-multiplayer/SKILL.md) automatically |
| Packaging or installing an existing game | [Game package skill](../.agents/skills/esp32-add-game/SKILL.md) |
| Installing the operating system | [esp32-setup](../.agents/skills/esp32-setup/SKILL.md) |

Native C is the supported game-creation route. Accept free-form mechanics and
custom engines, including software 3D or raycasting; the examples below do not
limit the design. Render within the stable Game API and keep hardware ownership
in Console OS. Shared drawing helpers are optional. C++ integration needs an
explicitly tested adapter; the current source/package tools support C.

## Presentation defaults

New native scaffolds request 768x480 RGB565 automatically, with a tested
320x200 fallback and canonical 320x200 touch input. Read [Game art](GAME_ART.md)
for direct rendering, antialiased typography, card readability, ImageGen source
textures, deterministic conversion and size budgets. Custom renderers use the
same negotiated surface, resource limits and [performance contract](GAME_PERFORMANCE.md).
Preserve simulation and multiplayer coordinates when upgrading graphics.

## Optional starters and examples

Create a new native identity with [new-game.py](../scripts/new-game.py), then
borrow only the mechanisms needed from an example. New scaffolds are unpublished
(`enabled: false`); use the local runner's `-DP4_ALLOW_DRAFT_GAME=ON` while
iterating, then enable the finished, validated game for catalog packaging.
Preserve all identities reserved by `games/retired.json`. Do not copy its manifest
ID, launcher ID, resource identity, branding or whole game unless the user
actually wants a remix. Keep licensing and provenance intact.

| Idea or technique | Closest existing reference | What it teaches / limit |
| --- | --- | --- |
| Free-form native game | `python3 scripts/new-game.py "My Game" --dry-run` | Minimal lifecycle, normalized controls, tones, manifest and safe ID selection; remove `--dry-run` to create |
| Fixed-screen shooter | [Space Invaders](../games/space_invaders/README.md), its `src/` and `tests/` | Movement, shooting, waves, pause and Back |
| Platform game with animated art | [Skyline Leap](../games/skyline_leap/README.md), its `src/`, `tools/` and `tests/` | Platform physics, stages and deterministic sprite conversion |
| Sprite motion and effects | [Shared visual helpers](../components/p4_game_api/include/p4/visual.h) and [API tests](../components/p4_game_api/tests/test_p4_game_api.c) | Atlas animation, fixed-point motion, particles and camera shake without borrowing a retired game |
| Board game / first linked-console game | [Checkers](../games/checkers/README.md), `checkers_logic.c`, `checkers_network.c` and its tests | Small host-authoritative turn protocol, D-pad/touch parity, malformed snapshot rejection and peer-loss fallback |
| Real-time linked-console game | [P4 Air Hockey](../games/p4_air_hockey/README.md), `p4_air_hockey_network.c` and its tests | Host simulation, client intents, snapshots and two-instance test harness; its current gameplay is touch-only, so add normalized button controls for a controller-friendly new game |
| Detailed cards / multiple source files | [P4 Rummy](../games/p4_rummy/README.md) | Split rules/render/network code, dual-resolution drawing and bounded multi-part snapshots; current links still host only two human consoles |
| Resource sidecars, progress and optional services | [Byte Buddy](../games/byte_buddy/README.md), especially its save tests | Validated `.P4R` art and versioned optional saves; use selected pieces rather than copying this large game |

These references were chosen for the demonstrated mechanisms and their existing
source/tests, not as claims of hardware acceptance on every board. Read the
current code and manifest before copying a pattern; the [Game SDK](GAME_SDK.md)
and public headers govern the API when an older game's README differs.

The native `--multiplayer` option creates the manifest profile and capability.
It does **not** implement shared game state or a playable network protocol.
Use the multiplayer skill and the selected example to finish that behavior.

## Inherit the operating system's services

| Feature | Game's responsibility | Console OS responsibility |
| --- | --- | --- |
| Controls | Map normalized Up/Down/Left/Right, A/B, Start/Back and touch to game actions | USB/BLE parsing, mapping, device lifecycle and input snapshots |
| Multiplayer | Declare the profile; implement bounded game intents/state and offline behavior | Host/Join UI, room discovery, exact-game matching, registration, links and start barrier |
| Drawing | Use a bounded custom software renderer or optional shared drawing/animation helpers on the negotiated RGB565 surface | Panel, rotation, framebuffers and presentation |
| Sound | Request optional tone/PCM services and tolerate unavailable/full queues | Mixer, codec, volume and hardware sessions |
| Saves | Validate a versioned snapshot and queue bounded commits through `save` | Durable storage where supported, sealing, recovery and filesystem ownership |
| Large assets | Declare a validated `.P4R` and consume the immutable resource view | Sidecar matching, loading, memory lifetime and storage location |
| Achievements | Submit bounded events with stable IDs | Current boot-session catalog and deduplication |
| Dice accessories | Use the optional dice API; retain ordinary controls | Accessory connection, settings and delivery of validated results |

Only request capabilities the game uses. Optional services need a playable
fallback; mark truly indispensable resources as required. Do not invent
capabilities from this table: use the actual manifest schema, matching public
header and [Game SDK](GAME_SDK.md). Specialized features such as dice, text
input, realm or module handoff need their existing contract and focused example
rather than a new game-owned service.

Games never choose an SD/flash mount, serial port, USB device, BLE address,
socket, GPIO, display driver or audio driver. They do not call ESP-IDF or
start their own driver tasks. Storage location is an OS/installer concern.

## Make controllers work by default

For new games, map the complete playable flow to the normalized Game API
buttons wherever the mechanics permit it: title/start, navigation, gameplay,
pause, retry and Back. Keep touch usable too. Use `held` for continuous
movement and `pressed`/`released` for transitions, and clear game-owned
latched actions when input is released or a session ends.

No per-game USB flag, HID parser, pairing UI or driver is needed. A supported
USB/BLE controller feeds the same logical controls as the OS touch mapper.
Do not advertise raw sticks, triggers, rumble or multiple local controller slots
unless the game-facing API and selected adapter actually expose them.
The native input callback is one local logical input stream; a four-player
manifest does not create four independent controllers.

If touch-only interaction is essential to the requested design, preserve it
and describe that choice rather than pretending a gamepad mapping exists.
Air Hockey is a useful networking reference, not a ready-made controller
template. Use the [controller skill](../.agents/skills/esp32-controllers/SKILL.md)
only for missing/broken platform support or a new physical controller profile.

## Multiplayer without a second platform

For a linked-console request, use the multiplayer skill alongside authoring.
Choose turn-based, real-time or deterministic lockstep according to the game.
Keep all room and link selection in Console OS; games enter an already-started
session. Implement the game's rules and synchronization with the public
`p4_game_multiplayer_*` functions.

The current transport adapters support two active human consoles, even though
manifest bounds allow future four-player support. Same-device pass-and-play
and CPU seats are separate game mechanics. Check the selected OS adapter and
exact-device evidence before claiming a supported human-console count.

## Finish against the selected target

Implement the player's core loop, then tune it in the appropriate local runner.
For native games use the [SDL3 play-test skill](../.agents/skills/esp32-test-game/SKILL.md)
and the game's focused tests. Multiplayer needs two game instances with
mocked OS sessions as well; one SDL window does not prove synchronization.

Validate the manifest and exact package, then install it through the board's
documented content path. A game update normally needs no OS reflash.
Record tested behavior and board-specific gaps separately.

The Tab5 USB-A controller candidate is host-tested and build-tested, with
named controller hardware acceptance pending. C6 Wi-Fi/Bluetooth remains
disabled; native USB-C supplies content/relay transport. Its current
storage route is SD-only; internal storage plus SD-on-restart is specified in
[esp32-setup](../.agents/skills/esp32-setup/references/tab5-storage.md), not yet
implemented. Check [the Tab5 board document](boards/M5STACK_TAB5.md) for newer
evidence before making a device claim.

Use the native creator and native tests for new work. No Lua authoring assets,
fixtures, tools or skill are retained as an alternate game-creation path.
