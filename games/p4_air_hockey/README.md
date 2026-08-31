# P4 Air Hockey

P4 Air Hockey is a native Game API v1 table-sport game for two P4 consoles.
The Host console owns puck physics, collisions, scoring, and rematches. Each
console controls one paddle, and the Join console sends bounded input intents
while receiving complete 30 Hz snapshots through the OS-owned P4MP session.
The Join view mirrors the long axis so each player always defends the left
goal and sees the rival on the right. Player identity colors remain fixed
across both screens: the Host striker is cyan and the Join striker is magenta.

Use only the `p4/` headers for display, controls, drawing, and sound. Keep
board drivers and raw ESP-IDF peripheral ownership in platform components.
Touch or drag anywhere in your half to move the mallet. First to seven wins;
touch the result panel for a rematch and touch Exit to return to the launcher.
A normal launcher start plays against a CPU. There are no D-pad or action-button
gameplay controls.
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

## Art asset

`assets/source/p4_air_hockey_rink_imagegen_v6.png` is original ImageGen art,
generated for this game and polished into a smooth cyber arena for long-axis
landscape play with goals at the left and right. The committed converter uses
high-quality Lanczos downsampling and emits a fixed 320x200 RGB565 include:

```sh
python3 tools/png_to_rink.py \
  assets/source/p4_air_hockey_rink_imagegen_v6.png \
  src/generated/p4_air_hockey_rink.inc
```

`assets/source/p4_air_hockey_objects_imagegen_v1.png` is the reviewed
transparent ImageGen sheet for both strikers and the puck. Its deterministic
converter emits keyed RGB565 sprites:

```sh
python3 tools/png_to_objects.py \
  assets/source/p4_air_hockey_objects_imagegen_v1.png \
  src/generated/p4_air_hockey_objects.inc
```

## Focused host proof

```sh
cmake -S games/p4_air_hockey -B build-host/p4_air_hockey -G Ninja
cmake --build build-host/p4_air_hockey
ctest --test-dir build-host/p4_air_hockey --output-on-failure
```
