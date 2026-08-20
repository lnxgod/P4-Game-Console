# P4 Lua API v1

Status: normative game-facing contract and shared Lua runtime implemented;
graphical PC preview and on-device hardware acceptance remain pending.

P4 Lua is the source language for open P4 Cart v1 games. Runtime v1 pins and
vendors Lua 5.4.8 under the repository source lock. Packages contain text
source rather than precompiled
Lua chunks so they remain inspectable, portable, and AI-remixable.

## Lifecycle

`main.lua` executes once in a fresh environment and returns one table:

```lua
local game = {}

function game.start()
end

function game.update()
end

function game.draw()
end

function game.stop()
end

return game
```

`start`, `update`, and `draw` are required; `stop` is optional. `update` runs
at an exact logical 60 Hz with no wall-clock argument. `draw` runs at most
30 Hz and may be skipped without changing simulation. The host calls `stop`
once during an orderly exit, then destroys the complete Lua state and all
cart-owned sound. Back/Home always remains an OS escape and cannot be captured
by a cart.

A syntax, memory, instruction-budget, stack, or API error stops the cart,
neutralizes its inputs and audio, and returns to an OS-owned error page. It
must never reboot Console OS.

## Available language

The environment contains Lua arithmetic, comparisons, tables, strings, and a
bounded subset of `math`, `string`, `table`, and `utf8`, plus one read-only
global named `p4`. `p4.arcade` is the additive host-provided shorthand library
defined by `p4-lua-arcade-v1.md`; it reduces source size without removing
direct access to the complete API below. The environment does not expose
`io`, `os`, `package`, `debug`,
`coroutine`, `require`, `dofile`, `loadfile`, `load`, `string.dump`, native C
modules, environment variables, or process functions.

The implementation loads the entry with text-only mode, uses a counting
allocator, installs an unexposed instruction hook, and bounds every host call.
Opening all standard libraries and deleting a few names afterward is not an
acceptable sandbox construction.

Default limits are:

| Resource | Limit |
|---|---:|
| Lua heap requested by manifest | 64..512 KiB |
| Lua source per file | 256 KiB |
| Callback instructions | 200,000 |
| Host calls per update | 256 |
| Draw commands per presented frame | 256 |
| Text bytes per call | 96 |
| Sprite dimension | 256x256 |
| Tone voices per cart | 4 |
| Save data | 0..4 KiB |

The first hardware runtime may lower the callback budget after profiling, but
must report the effective limit before launch and behave identically in the PC
preview. No cart can request a value above these v1 ceilings.

## Scalar conventions

- Screen coordinates are signed integers on the 768x480 Console OS landscape
  canvas. A cart never sees the panel's rotated 480x800 scanout geometry.
- Colors are unsigned RGB565 integers (`0x0000` through `0xffff`).
- Player slots are 1 through 4. An absent player returns neutral input.
- Durations are integer logical ticks unless a function explicitly says
  milliseconds.
- Invalid types raise a bounded API error. Out-of-range drawing is clipped;
  invalid dimensions are ignored and counted in diagnostics.
- All game-visible time and randomness are deterministic.

## API surface

The names below are reserved in v1. Implementations may add diagnostic return
values but may not change their game-visible effects.

```text
p4.time.tick() -> integer

p4.random.seed(integer)
p4.random.u32() -> integer
p4.random.range(min_integer, max_integer) -> integer

p4.input.connected(player) -> boolean
p4.input.down(player, button) -> boolean
p4.input.pressed(player, button) -> boolean
p4.input.released(player, button) -> boolean
p4.input.axis(player, axis) -> integer [-32768, 32767]
p4.input.touch_count() -> integer [0, 5]
p4.input.touch(index) -> x, y, pressed

p4.screen.clear(rgb565)
p4.screen.pixel(x, y, rgb565)
p4.screen.line(x0, y0, x1, y1, rgb565)
p4.screen.rect(x, y, width, height, rgb565, filled)
p4.screen.circle(x, y, radius, rgb565, filled)
p4.screen.text(text, x, y, rgb565)
p4.screen.sprite(asset_id, x, y, frame, flags)

p4.audio.tone(channel, frequency_hz, duration_ticks, volume, waveform) -> boolean
p4.audio.stop(channel)
p4.audio.stop_all()

p4.save.get(key) -> string|nil
p4.save.set(key, value) -> boolean
p4.save.remove(key)
```

Buttons are `"up"`, `"right"`, `"down"`, `"left"`, `"a"`, `"b"`, `"x"`,
`"y"`, `"start"`, and `"select"`. Axes are `"left-x"`, `"left-y"`,
`"right-x"`, `"right-y"`, `"left-trigger"`, and `"right-trigger"`.
Waveforms are `"square"`, `"triangle"`, `"saw"`, and `"noise"`.

Tone frequency is 40 through 4,000 Hz, duration is 1 through 600 ticks, and
volume is 0 through 255. The host owns mixing and master volume. Calls beyond
the four-voice/cart budget return false; they never wait.

Save keys are lowercase ASCII `[a-z][a-z0-9_.-]{0,31}` and values are UTF-8
strings of at most 256 bytes. A cart can access only its own namespace. The
current Console OS runner keys that namespace by exact cartridge SHA-256 and
retains it across relaunches during one boot. Durable, atomic storage across a
reboot is the next backend milestone and games must tolerate its absence.

## Input and multiplayer

The API supports local multiplayer without game networking code: Console OS
maps controllers or touch control sets into up to four player slots.
Disconnect is neutral in the same update and reconnect inserts one neutral
barrier update. The first integrated adapter currently populates player one;
additional controller-slot assignment remains to be wired.

LAN multiplayer will use the same player-slot input API. Console OS owns
discovery, sessions, packet parsing, timeouts, content-hash matching, and peer
identity. A cart never receives a socket or IP address. The first network mode
will be an OS lockstep service for carts that explicitly declare deterministic
multiplayer; until that service is implemented, carts remain local-only even
if `players.max` is greater than one.

Network-eligible carts must not use save reads during a match, must seed P4
randomness from the OS session seed, and must avoid iteration whose order is
unspecified. Console OS will compare periodic state hashes and end a desynced
match rather than continue with divergent state. Snapshot/rollback is reserved
for a future API and is not implied by v1.

## Runtime boundary

The interpreter adapter translates P4 Lua calls into generation-tagged Game
API commands. It copies all strings and payloads out of Lua memory, clips and
bounds commands, and discards work from an old generation after exit. Games
never receive pointers to framebuffers, input structs, audio buffers, saves,
network packets, or platform services.

The nonvisual host smoke tool already uses the same interpreter source,
limits, fixed tick schedule, RGB565 command validation, input rules, and error
behavior. The future graphical PC player must keep that runtime and add only
presentation and host input. Differences in panel rotation, physical scaling,
speaker wiring, and controller drivers stay below the Game API boundary.
