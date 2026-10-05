---
name: develop-p4-script-games
description: Create, remix, validate, package, measure, or install readable Lua games for the P4 Console OS source-cartridge runtime. Use for game-platform/*, p4-lua-5.4-v1, p4.arcade shorthand, .P4CART files, compact QR-tradable games, one- or multi-part QR payloads, cartridge manifests, sandbox limits, or open kid-remixable game templates.
---

# Develop P4 script games

Build original, readable Lua games for the bounded P4 Cart runtime. Keep the
source inspectable and remixable, then package it as a deterministic
`.P4CART`. This path is separate from native `.P4G` cartridges and legacy
engines; never rename one format to another.

## Load the source-game contract

Read these files before changing a cart or its runtime:

1. `AGENTS.md`
2. `game-platform/README.md`
3. `game-platform/api/p4-lua-api-v1.md`
4. `game-platform/api/p4-lua-arcade-v1.md`
5. `game-platform/api/cartridge-container-v1.md`
6. `game-platform/api/qr-cartridge-transfer-v1.md` when QR size or transport
   is in scope
7. The closest source-included template under `game-platform/templates/`

Use `$develop-p4-console-games` and `$develop-p4-games` instead for native C
source and `.P4G` packaging. Use `$develop-esp32-p4-platform` only when the
request changes Console OS, the Lua runtime adapter, or a board service.

## Create or remix a cart

Start from `bounce-lab` for a straightforward full-API example or `qr-dodge`
for a compact `p4.arcade` example. A source project contains an explicit
`p4.json` manifest, text-only `main.lua`, `README.md`, and license notice.

For a new game or remix:

- assign a new stable lowercase UUID and record the parent UUID for a remix;
- keep runtime identity exactly `p4-lua-5.4-v1` and API version 1;
- keep the logical contract at 768x480, 60 fixed updates, and at most 30
  presented frames per second;
- list every packaged file explicitly instead of recursively including a
  working directory;
- include human-readable source and accurate original/licensed asset notices;
  and
- request only the bounded heap, save, player, and capability values the game
  uses.

Implement `start`, `update`, and `draw`; add `stop` when cleanup is needed.
Use the complete `p4` API for input, drawing, tones, private saves, and
deterministic time/randomness. Use `p4.arcade` actors, movement, collision,
timers, scenes, and procedural effects as optional shorthand when reducing
source bytes helps. The shorthand never removes access to the full API.

Back/Home remains OS-owned. A cart never receives filesystem paths, sockets,
USB handles, display memory, codec access, GPIO, ESP-IDF calls, native modules,
dynamic loaders, or Lua bytecode execution. Keep state and loops within the
documented instruction, heap, host-call, draw-command, text, sprite, audio,
and save limits.

## Validate and run the focused host proof

For a cart-only change, validate, pack, and inspect the exact source tree:

```sh
python3 game-platform/scripts/p4cart.py validate path/to/source-game
python3 game-platform/scripts/p4cart.py pack \
  path/to/source-game /tmp/GAME.P4CART
python3 game-platform/scripts/p4cart.py inspect /tmp/GAME.P4CART
```

Run `make -C game-platform check` once when game behavior, the Lua API, the
sandbox, packer, or shared helpers changed. Then run the exact-runtime smoke
tool produced by that build:

```sh
game-platform/build-host/firmware/components/p4_lua_runtime/p4_lua_smoke \
  /tmp/GAME.P4CART
```

Do not use the native SDL3 `$test-p4-games-locally` runner for `.P4CART` files.
The graphical script-game PC player remains pending; report host runtime smoke
honestly rather than claiming interactive play.

## Measure one or many QR payloads

QR transport carries the exact validated cart and does not reduce game
features. Estimate first, then split only when requested:

```sh
python3 game-platform/scripts/p4qr.py estimate /tmp/GAME.P4CART
python3 game-platform/scripts/p4qr.py split \
  /tmp/GAME.P4CART /tmp/game-qr
python3 game-platform/scripts/p4qr.py inspect /tmp/game-qr
python3 game-platform/scripts/p4qr.py join \
  /tmp/game-qr /tmp/GAME-ROUNDTRIP.P4CART
```

Report the easy, balanced, and dense symbol counts instead of promising that a
game fits one QR. Compare the original and round-trip SHA-256 before calling a
split valid. The repository currently emits checked `.p4qr` text frames, not
QR bitmap images, and Console OS does not yet provide an on-device scan/import
UI. Never present either pending layer as complete.

## Install without flashing

Install a validated cart under `P4/GAMES` on the mounted game volume through
the guarded content tool:

```sh
python3 scripts/p4-content.py cart /tmp/GAME.P4CART \
  --sd-root /Volumes/P4GAMES
```

For Tab5 with the native USB transfer firmware, leave the card in the tablet
and use the connected cable instead:

```sh
python scripts/p4-transfer.py push /tmp/GAME.P4CART --class p4cart --port /dev/cu.usbmodem1101
```

Select the actual unit's port. The host and device validate the cartridge before
atomic activation under `P4/GAMES`; the Lua catalog then refreshes. Use
`--no-replace` to reject an existing destination. The transport remains Console
OS-owned; cartridges receive no USB or raw storage access.

Use `--replace` only when replacement is intentional. The tool validates,
stages, syncs, reads back, and atomically activates the complete cart without
formatting or deleting unrelated files. Never let Console OS and the laptop
mount the writable card concurrently, and never flash firmware for a
cart-only update.

## Keep capability claims honest

Script saves currently survive relaunch during the same Console OS boot;
durable save persistence across reboot remains pending. The API describes up
to four players, but the first integrated device adapter currently populates
only player one. Networked Host/Join sessions are currently exposed only to
native `.P4G` games through `multiplayer-session`; `.P4CART` games remain local
and must not claim BLE/UART room support. Use `$develop-p4-console-games` when
a requested networked game needs that service. QR bitmap rendering, camera
scanning, and device import UI remain pending. State these limits in the game
README when its design depends on them.

A script-game change is complete when its source and license remain readable,
the manifest validates, deterministic pack/inspect and runtime smoke pass, any
requested QR round trip matches exactly, and any claimed device behavior has a
separate exact-device acceptance record.
