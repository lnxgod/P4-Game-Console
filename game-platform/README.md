# P4 Game Platform

This subtree defines the open, kid-friendly game platform for P4 Console OS on
PC and the Waveshare ESP32-P4 4.3. A game is readable Lua source, is packaged
as a deterministic `.p4cart`, and can be copied by SD card or a future
byte-clean serial/Wi-Fi adapter. PC preview and device execution must agree on
the 768x480 canvas, inputs, fixed time, drawing, limits, and error behavior.

The current slice deliberately stops before the executable Lua sandbox and
USB-serial integration. It contains:

- the normative v1 game API, Lua API, container, and transfer protocol;
- a strict host validator/packer/unpacker and open Bounce Lab source template;
- an allocation-free, ESP-independent runtime core;
- an allocation-free, transport-independent cartridge receiver;
- a thin FreeRTOS task adapter with a deterministic 60 Hz game loop and a
  separate best-effort render task;
- a peripheral-free ESP-IDF proof app using a fake game and render sink; and
- a read-only Console OS SD catalog plus a validate-stage-sync-activate host
  installer for complete `.p4cart` files; and
- native tests built with strict warnings plus AddressSanitizer and
  UndefinedBehaviorSanitizer where supported.

The separation is intentional:

```text
browser editor/preview                 ESP32-P4
----------------------                 ---------
same game API semantics                p4_game_api
        |                                    |
        +---- .p4cart over Web Serial ------>p4_cartridge_transfer
                                             |
                                      verified inactive slot
                                             |
                                      p4_game_runtime_freertos
                                        |              |
                                  60 Hz game task   render task
```

The game task never owns display, USB, audio, touch, or storage drivers. Those
remain reusable operating-system services. The renderer may present fewer than
60 frames per second without changing simulation time.

This is a scheduling and packaging foundation, not yet a complete sandbox. The
included native fake game is trusted: the planned pinned Lua 5.4 adapter must
use a counting allocator, accept text source only, expose a positive API
allowlist, enforce instruction limits, and implement a prompt interrupt. Render sinks must also use a
bounded platform-service timeout. The runtime detects a returned over-budget
callback and exposes bounded stop waits, but it never forcibly deletes a task
that may own VM or renderer state.

## P4Cart readiness

P4 Cart v1 now has a frozen source manifest, deterministic uncompressed
container, validator, packer, inspector, unpacker, source-included starter
game, atomic SD installer, and bounded Console OS Library catalog. The
executable Lua backend, PC preview, durable save store, and serial/Wi-Fi
transfer adapters are still pending. Therefore a validated file can be copied
and listed, but it is not yet runnable on Console OS.
No native C/RISC-V ELF or BIN may be renamed to that extension.

See `docs/CONTENT_LIBRARY.md` for the current SD copy workflow and explicit
runtime boundary.

Until this changes, Console OS demo games use `p4-native-static-v1` and are
statically linked into `p4_console_os.bin`. The P4 Cart implementation must use
the reviewed sandboxed backend, validate every asset and resource limit,
provide only logical 768x480 landscape rendering plus normalized input, and keep
all display, touch, audio, storage, USB, timing, and lifecycle ownership in
the OS. The P4 Lua API defines bounded host-owned tone audio; implementation
and acoustic acceptance are still pending.

## Local verification

Run the ESP-independent tests:

```sh
make -C game-platform check
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
- [`api/cartridge-container-v1.md`](api/cartridge-container-v1.md) defines the
  source manifest, deterministic bytes, SD layout, and remix workflow.
- [`api/cartridge-transfer-v1.md`](api/cartridge-transfer-v1.md) defines the
  transport-independent binary protocol that a future Web Serial adapter will
  carry.
