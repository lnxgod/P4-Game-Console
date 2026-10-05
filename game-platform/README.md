# P4 Game Platform

This subtree defines the open, kid-friendly game platform for P4 Console OS on
PC and the Waveshare ESP32-P4 4.3. A game is readable Lua source, is packaged
as a deterministic `.p4cart`, and can be copied by SD card, split across one
or more QR codes, or sent by a future byte-clean serial/Wi-Fi adapter. Host
tests and device execution agree on the 768x480 canvas, inputs, fixed time,
drawing, limits, and error behavior.

The current implementation contains:

- the normative v1 game API, Lua API, container, and transfer protocol;
- a strict host validator/packer/unpacker, QR estimator/splitter/reassembler,
  and minimal automated-test fixtures;
- additive `p4.arcade` helpers for compact games without limiting the complete
  Lua API;
- an exact source-locked Lua 5.4.8 text-only sandbox with a counting allocator,
  positive API allowlist, callback instruction budget, and prompt interrupt;
- bounded RGB565 command rendering and a four-channel procedural tone synth;
- an allocation-free, ESP-independent runtime core;
- an allocation-free, transport-independent cartridge receiver;
- a thin FreeRTOS task adapter with a deterministic 60 Hz game loop and a
  separate best-effort render task;
- a peripheral-free ESP-IDF proof app using a fake game and render sink; and
- a Console OS SD catalog and launcher that revalidates source immediately
  before execution, plus a validate-stage-sync-activate host installer for
  complete `.p4cart` files;
- boot-session save namespaces keyed by the exact cartridge hash; and
- native tests built with strict warnings plus AddressSanitizer and
  UndefinedBehaviorSanitizer where supported.

The separation is intentional:

```text
PC source + packer                     ESP32-P4 Console OS
------------------                    -------------------
readable Lua source                    SD P4/GAMES scan
        |                                    |
        +---- exact .p4cart ---------------->full hash validation
                                             |
                                      p4_lua_runtime
                                        |          |
                                  60 Hz update  30 Hz draw
                                             |
                                   OS display/audio/input
```

The game task never owns display, USB, audio, touch, or storage drivers. Those
remain reusable operating-system services. The renderer may present fewer than
60 frames per second without changing simulation time.

The sandbox accepts UTF-8 text source only, opens only reviewed safe libraries,
and removes dynamic loaders, filesystem, process, package, debug, and bytecode
entry points. Render sinks use a bounded platform-service timeout. A script
failure tears down its VM and audio and returns to the launcher instead of
rebooting Console OS.

## P4Cart readiness

P4 Cart v1 now has a frozen source manifest, deterministic uncompressed
container, validator, packer, inspector, unpacker, source-included starter
games, exact-runtime host smoke tool, sandboxed device backend, and bounded
Console OS launcher. A valid cart copied into `P4/GAMES` is launchable without
an OS reflash. No native C/RISC-V ELF or BIN may be renamed to that extension.

Durable saves across reboot, runtime sprite-asset decoding, a graphical PC
player, and serial/Wi-Fi transfer adapters remain pending. The integrated save
service currently survives game relaunches during one Console OS boot only.
The API exposes four player slots, while the first device adapter currently
populates player one and leaves the others neutral.

The QR transport can already report a game's easy, balanced, and dense symbol
counts, emit independently checked Base45 frame payloads, accept them in any
order, and reconstruct the exact validated P4 Cart. QR image rendering and the
Console OS receive UI remain pending; no camera hardware is assumed by the
transport contract.

See `docs/CONTENT_LIBRARY.md` for the current SD copy workflow and explicit
runtime boundary.

Console OS keeps its native games and adds P4 Cart as a separate source-game
path; neither format weakens the other. Script games receive only logical
768x480 landscape rendering, normalized input, bounded tone audio, and their
private save namespace. Display, touch, audio, storage, USB, timing, and
lifecycle remain OS owned. Device-side audio is implemented; real-hardware
gameplay and acoustic acceptance are still required before calling the new
path hardware-qualified.

## Local verification

Run the ESP-independent tests:

```sh
make -C game-platform check
```

Measure or split a packed cart without changing it:

```sh
python3 game-platform/scripts/p4qr.py estimate /tmp/GAME.P4CART
python3 game-platform/scripts/p4qr.py split \
  /tmp/GAME.P4CART /tmp/game-qr-frames
game-platform/build-host/firmware/components/p4_lua_runtime/p4_lua_smoke \
  /tmp/GAME.P4CART
```

Build the proof firmware with the repository's pinned ESP-IDF checkout:

```sh
make -C game-platform firmware
```

`firmware` is build-only. This subtree intentionally has no flash or monitor
command, and a successful build is not hardware verification.

## Contracts

- [`api/game-api-v1.md`](api/game-api-v1.md) defines the game-visible API shared
  by browser preview and device execution.
- [`api/p4-lua-api-v1.md`](api/p4-lua-api-v1.md) defines source lifecycle,
  sandbox limits, graphics, input, tone, save, and multiplayer boundaries.
- [`api/p4-lua-arcade-v1.md`](api/p4-lua-arcade-v1.md) defines the optional
  compact actor, collision, timer, scene, and procedural-audio helpers.
- [`api/cartridge-container-v1.md`](api/cartridge-container-v1.md) defines the
  source manifest, deterministic bytes, SD layout, and remix workflow.
- [`api/qr-cartridge-transfer-v1.md`](api/qr-cartridge-transfer-v1.md) defines
  one- or multi-symbol QR transport without reducing cartridge features.
- [`api/cartridge-transfer-v1.md`](api/cartridge-transfer-v1.md) defines the
  transport-independent binary protocol that a future Web Serial adapter will
  carry.
