# P4 Game Platform

This subtree is the first device-side slice of a kid-friendly game platform for
the Elecrow ESP32-P4 console. A game will use one small API in the browser, be
packaged as a `.p4cart`, and later be installed over a byte-clean Web Serial
connection. The browser preview and device runtime must agree on inputs, fixed
time, drawing commands, limits, and error behavior.

The current slice deliberately stops before display, controller, storage, and
USB-serial integration. It contains:

- the normative v1 game API and cartridge-transfer protocol;
- an allocation-free, ESP-independent runtime core;
- an allocation-free, transport-independent cartridge receiver;
- a thin FreeRTOS task adapter with a deterministic 60 Hz game loop and a
  separate best-effort render task;
- a peripheral-free ESP-IDF proof app using a fake game and render sink; and
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

This is a scheduling foundation, not yet a sandbox. The included native fake
game is trusted: a future WebAssembly backend must enforce instruction/fuel
limits and implement a prompt interrupt signal. Render sinks must also use a
bounded platform-service timeout. The runtime detects a returned over-budget
callback and exposes bounded stop waits, but it never forcibly deletes a task
that may own VM or renderer state.

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
- [`api/cartridge-transfer-v1.md`](api/cartridge-transfer-v1.md) defines the
  transport-independent binary protocol that a future Web Serial adapter will
  carry.
