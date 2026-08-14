# P4 game API v1

Status: device runtime foundation. The browser shim and executable cartridge
engine are follow-on work, but they must implement this contract without
changing its observable behavior.

## Execution model

A game has `load`, `tick`, and `unload` lifecycle callbacks. `tick` runs at an
exact logical rate of 60 updates per second. Every call receives a monotonically
increasing integer tick and a fixed delta of `1/60`; wall-clock delays never
change that delta. A slow renderer may drop intermediate frames, but it may not
slow or alter game simulation.

The native structs in `p4/game_api.h` are an embedding boundary inside the
device firmware, not a WebAssembly memory ABI. A future WebAssembly engine and
the browser preview expose equivalent scalar operations:

```text
time.tick() -> u64
input.down(button) -> bool
input.pressed(button) -> bool
input.released(button) -> bool
input.axis(axis) -> i16
input.trigger(trigger) -> u16
input.connected() -> bool
input.dpad(direction) -> bool
screen.clear(rgb565)
screen.rect(x, y, width, height, rgb565)
screen.sprite(asset_id, x, y, frame, flags)
```

This avoids copying compiler-dependent C structure layouts into a cartridge.

## Canvas and color

The logical canvas is 320 by 200 pixels. Coordinates are signed integers.
Rectangles with a non-positive width or height are rejected. The renderer clips
valid rectangles and sprites to the canvas. Colors are RGB565 integer values;
the browser shim must quantize to RGB565 before drawing so preview colors match
the device.

Each tick may emit at most 256 drawing commands. Additional commands are
ignored for presentation and reported to the host runtime, but the simulation
continues. Drawing backpressure is never visible to game logic.

## Input snapshots

One complete input snapshot is frozen for each tick. `pressed` and `released`
are edges derived from consecutive snapshots. A disconnect becomes neutral in
the same tick and advances `input_epoch`. A new game or controller-source change
also advances the epoch. A reconnect retains the disconnect epoch and inserts
one neutral barrier tick so held controls cannot leak between games.

Button bit numbers and D-pad bits are defined in `p4/game_api.h`. Unknown bits
are cleared before a cartridge sees them.

Scalar browser/WebAssembly identifiers are stable integers. Buttons use `A=0`,
`B=1`, `X=2`, `Y=3`, `LEFT_BUMPER=4`, `RIGHT_BUMPER=5`, `LEFT_STICK=6`,
`RIGHT_STICK=7`, `START=8`, `SELECT=9`, `HOME=10`, `TOUCH_PRIMARY=11`, and
`TOUCH_SECONDARY=12`. Axes use `LEFT_X=0`, `LEFT_Y=1`, `RIGHT_X=2`, and
`RIGHT_Y=3`; triggers use `LEFT=0` and `RIGHT=1`. D-pad directions use bit
values `UP=1`, `RIGHT=2`, `DOWN=4`, and `LEFT=8`.

## Cartridge generations

Every running cartridge receives a nonzero generation. Render packets carry
that generation and never contain pointers into cartridge memory. Packets from
an old generation are discarded before old assets are unloaded.

## Determinism boundary

Game-visible time is only the logical tick. Randomness will be supplied by a
seeded platform service in a later API revision. Filesystem paths, network,
device handles, FreeRTOS primitives, ESP-IDF errors, and wall-clock time are not
part of the game API.
