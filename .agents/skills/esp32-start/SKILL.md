---
name: esp32-start
description: Find the right workflow in the P4 Game Console monorepo. Use for requests to make, draw, play-test, install or fix its games, multiplayer, controllers or Console OS, and when the user is unsure where to start. Select only the matching specialist skills; M5Stack Tab5 is the default board.
---

# ESP32 - Start Here

Read the root `AGENTS.md` and use the user's plain-language request to choose
from the table below. Open only the skills needed for the task. The user does
not need to remember skill names or say “use a skill.” Respect board choices
and authorizations already established in the conversation.

## Know what belongs where

This is the **P4 Game Console monorepo**. Console OS is the launcher and shared
platform in `apps/console_os` and `components`. Native games live in `games`;
engine ports and experimental work have their own entries in the root README.
The **Game Changers AI multiplayer arena** is a Doom-based game mode inside
the OS, documented in `docs/GAME_CHANGERS_AI_DOOM.md`. It is not the whole
platform. Pure Hades is one of its map packs. Do not rename game IDs, packages,
saves or the OS's current branding merely to clarify these documentation names.

## Choose the workflow

| What the user wants | Skill to read |
| --- | --- |
| Set up a Tab5 with Console OS and games | [ESP32 - Set Up](../esp32-setup/SKILL.md) |
| Make or change a native console game | [ESP32 - Make Game](../esp32-make-game/SKILL.md) |
| Draw game art, sprites and launcher pictures | [ESP32 - Game Art](../esp32-game-art/SKILL.md) |
| Package, install or update console games | [ESP32 - Add Game](../esp32-add-game/SKILL.md) |
| Play and check games on your computer | [ESP32 - Test Game](../esp32-test-game/SKILL.md) |
| Let friends play on connected consoles | [ESP32 - Multiplayer](../esp32-multiplayer/SKILL.md) |
| Build, fix and test the Tab5 console OS | [ESP32 - Fix Console](../esp32-fix-console/SKILL.md) |
| Connect and fix USB or Bluetooth controllers | [ESP32 - Controllers](../esp32-controllers/SKILL.md) |

A game request normally starts with **Make Game**. Add **Game Art** when assets
change, **Test Game** for the required native local play loop, **Multiplayer**
for linked consoles, and **Add Game** when packaging/installing. Same-device
pass-and-play alone does not require a network protocol. Ordinary button
mappings use the game API; use **Controllers** when the shared HID service or
physical controller support changes.

For Doom, Chex or arena engine work, read the owning `docs/games/` README
and use **Fix Console** with the engine-specific guide; the native game runner
does not run those engines. Wacky and Quake use their own documented harnesses.

For initial OS installation use **Set Up**, which coordinates **Fix Console**
and **Add Game**. A compatible game update uses **Add Game** and needs no OS
reflash. For shared audio stutter, display, storage, build or device problems,
use **Fix Console**. Its Tab5 workflow owns both P4 cores and the guarded
hardware route. Do not substitute an Elecrow or Waveshare procedure for Tab5.
Documentation-only edits need reference/skill validation, not a firmware build.

## Older boards, only when requested

M5Stack Tab5 is the maintained default. Preserve existing ports and exact-unit
recovery evidence. For explicitly requested legacy maintenance, select:

| Older-board task | Skill to read |
| --- | --- |
| Maintain the older Waveshare 4.3 console | [ESP32 - Waveshare](../esp32-waveshare/SKILL.md) |
| Fix the older Waveshare console screen | [ESP32 - Waveshare Screen](../esp32-waveshare-screen/SKILL.md) |
| Fix the older Waveshare console sound | [ESP32 - Waveshare Sound](../esp32-waveshare-sound/SKILL.md) |
| Check the older Elecrow 10-inch console | [ESP32 - Elecrow Test](../esp32-elecrow-test/SKILL.md) |
| Fix the older Elecrow 10-inch screen | [ESP32 - Elecrow Screen](../esp32-elecrow-screen/SKILL.md) |
| Fix the older Elecrow 10-inch speaker | [ESP32 - Elecrow Sound](../esp32-elecrow-sound/SKILL.md) |

Olimex Rev.B uses **Fix Console** with `docs/boards/OLIMEX_ESP32_P4_PC.md`;
there is no separate Olimex skill. Never transfer flash authorization between
boards or units.

## Keep the library understandable

For a new or changed game, keep its own README current: what it is, controls,
local versus linked players, data requirements, build/play/install commands,
asset notices and honest test status. Link new entries from the root README
and `games/README.md` (or the engine-port entry) and follow `docs/GAME_LIBRARY.md`.
A manifest's enabled flag or a successful upload is not proof of complete play,
speaker quality or device frame rate. Preserve recorded failed/pending acceptance.

New games use native C `.P4G` cartridges and stable OS services. Keep toolchain
locks, game/save identities, data licensing and factory-backup gates intact.
Read the selected specialist's contracts for the task instead of loading every
skill or running every test by default.

## Standard and development content

Standard bundles exclude disabled manifests and every `GAMES/WIP` title.
Byte Buddy, Red Dragon and Skyline Leap require an explicit
`make install-dev GAME=<slug> PORT=<port>`; see the
[developer install guide](../../../games/README.md#developer-installs).
Tide Maze remains in the normal bundle by the owner's request; preserve its
open device-lag acceptance. Feature Blast Circuit, with Wacky Wheels as an
installed fallback. Preserve hidden games' source, package IDs and saves.
