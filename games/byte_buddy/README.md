# Byte Buddy

Byte Buddy is an original, code-rendered virtual pet for the native P4 Game
API v1. It has four care actions (Feed, Play, Clean, Rest), needs that decay
over time, three growth stages, a catch-the-star mini-game, coins, optional
tone feedback, and four OS achievement events.

Controls: Left/Right selects a care action, A performs it, B pets Buddy, Start
is available as the standard control, and Exit returns safely to Console OS.
Selecting Play starts the mini-game; steer the catcher Left/Right and press B
to leave it early.

The pet is clean-room code with code-drawn geometry only. No Tamagotchi ROM,
third-party art, or third-party source is included. Care/achievement state is
currently session-only because the Waveshare Console OS storage service is
read-only; durable save data is intentionally deferred to the reviewed save
service.
