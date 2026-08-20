# P4 Lua Arcade helpers v1

Status: additive API contract, reference implementation, and device-runtime
embedding complete; hardware gameplay acceptance remains pending.

`p4.arcade` is a convenience library layered on the complete P4 Lua API. It
does not replace or hide `p4.input`, `p4.screen`, `p4.audio`, `p4.save`, custom
assets, multiplayer declarations, or any future OS-owned service. A large cart
can ignore these helpers and use the full API directly. A small QR-traded game
can use them to express common arcade behavior in fewer source bytes.

The host provides the library without charging its implementation bytes to
each cartridge. Its version is available as `p4.arcade.version`, which is `1`
for this contract. The normative reference behavior lives in
`game-platform/runtime/p4_arcade.lua`.

Typical source begins with:

```lua
local q = p4.arcade
local s = p4.screen
```

## Colors

The library exports the classic 16-color names `BLACK`, `BLUE`, `GREEN`,
`CYAN`, `RED`, `MAGENTA`, `BROWN`, `LIGHT_GRAY`, `DARK_GRAY`, `LIGHT_BLUE`,
`LIGHT_GREEN`, `LIGHT_CYAN`, `LIGHT_RED`, `LIGHT_MAGENTA`, `YELLOW`, and
`WHITE` as RGB565 integers. Games may still use any RGB565 color directly.

## Actors and movement

```text
q.actor(x, y, width, height, color) -> actor
q.sprite_actor(asset_id, x, y, width, height) -> actor
q.move4(actor, speed, player=1) -> moved
q.step(actor)
q.step_all(actors) -> actors_considered
q.hit(first, second) -> boolean
q.touching(actor, touch_index=1) -> boolean
q.contain(actor, left, top, right, bottom) -> collided
q.bounce(actor, left, top, right, bottom) -> bounced
q.wrap(actor, left, top, right, bottom) -> wrapped
q.draw(actor) -> drawn
q.draw_all(actors) -> actors_drawn
q.compact(actors) -> live_actor_count
```

An actor is an ordinary cart-owned Lua table. Standard fields are `x`, `y`,
`vx`, `vy`, `w`, `h`, `color`, `sprite`, `frame`, `flags`, `visible`, and
`alive`. Games can add their own fields. Helpers consider at most 64 actors per
bulk call so an accidental large table cannot consume an unbounded frame.

Collision is deterministic integer axis-aligned overlap. Movement is measured
in logical pixels per fixed update. Advanced games remain free to implement
fixed-point motion, tile collision, circles, paths, or their own entity model.

## Timers and scenes

```text
q.clamp(value, minimum, maximum) -> integer
q.timer(period_ticks) -> timer
q.tick(timer) -> fired
q.scene(initial_name) -> scene
q.scene_set(scene, name)
q.scene_tick(scene) -> age_ticks
```

Timers are repeating and deterministic. A scene is an ordinary table with
`name` and `age`; the helper does not impose a particular title, play, pause,
or game-over structure.

## Compact sound

```text
q.sfx("click" | "jump" | "hit" | "score" | "fail") -> queued
q.song(note_frequencies, step_ticks, volume=120, waveform="triangle") -> song
q.song_tick(song) -> advanced_or_queued
```

The five effects are procedural tone recipes stored by the runtime, so a cart
spends only the effect name. Song notes are integer frequencies; zero is a
rest. A song uses audio channel four. Games needing richer sound retain direct
access to all bounded `p4.audio` functions and packaged original assets.

Audio helpers remain optional in practice: they return false when the OS audio
service cannot accept a tone. Game state must never depend on whether sound
played.

## Example

```lua
local q, s = p4.arcade, p4.screen
local game, ball = {}, q.actor(40, 60, 12, 12, q.YELLOW)

function game.start()
    ball.vx, ball.vy = 4, 3
end

function game.update()
    q.step(ball)
    if q.bounce(ball, 0, 0, 768, 480) then q.sfx("click") end
end

function game.draw()
    s.clear(q.BLACK)
    q.draw(ball)
end

return game
```

Back/Home remains an OS-owned escape even when a cart uses this library.
Instruction, heap, host-call, draw-command, text, sprite, sound, and save
limits remain exactly those of P4 Lua API v1.
