# ESP32 skills

Open the repository root and describe what you want. [AGENTS.md](../../AGENTS.md)
loads [ESP32 - Start Here](esp32-start/SKILL.md), which selects the relevant
skills automatically. No personal installation or remembered command is needed.
Human-facing names use **ESP32 - …**; optional `$esp32-…` commands use short,
space-free identifiers.

## Everyday work

| Name | Optional command | What it helps with |
| --- | --- | --- |
| [ESP32 - Start Here](esp32-start/SKILL.md) | `$esp32-start` | Pick the right workflow from your request |
| [ESP32 - Set Up](esp32-setup/SKILL.md) | `$esp32-setup` | Set up a Tab5 with Console OS and games |
| [ESP32 - Make Game](esp32-make-game/SKILL.md) | `$esp32-make-game` | Make or change a native console game |
| [ESP32 - Game Art](esp32-game-art/SKILL.md) | `$esp32-game-art` | Draw game art, sprites and launcher pictures |
| [ESP32 - Add Game](esp32-add-game/SKILL.md) | `$esp32-add-game` | Package, install or update console games |
| [ESP32 - Test Game](esp32-test-game/SKILL.md) | `$esp32-test-game` | Play-test native games on your computer |
| [ESP32 - Multiplayer](esp32-multiplayer/SKILL.md) | `$esp32-multiplayer` | Let friends play on connected consoles |
| [ESP32 - Fix Console](esp32-fix-console/SKILL.md) | `$esp32-fix-console` | Build, fix and test the Tab5 console OS |
| [ESP32 - Controllers](esp32-controllers/SKILL.md) | `$esp32-controllers` | Connect and fix USB or Bluetooth controllers |

Say “make a racing game,” “draw its cover,” “let four friends play,” or
“put it on my Tab5.” The agent follows the matching workflow and combines
skills when needed. Multiplayer, art and testing supplement the game idea.

## Legacy board maintenance

These workflows are only for explicitly requested work on existing older
boards. Tab5 setup and testing use **Set Up** and **Fix Console**.

| Name | Optional command | What it helps with |
| --- | --- | --- |
| [ESP32 - Waveshare](esp32-waveshare/SKILL.md) | `$esp32-waveshare` | Maintain the older Waveshare 4.3 console |
| [ESP32 - Waveshare Screen](esp32-waveshare-screen/SKILL.md) | `$esp32-waveshare-screen` | Fix the older Waveshare console screen |
| [ESP32 - Waveshare Sound](esp32-waveshare-sound/SKILL.md) | `$esp32-waveshare-sound` | Fix the older Waveshare console sound |
| [ESP32 - Elecrow Test](esp32-elecrow-test/SKILL.md) | `$esp32-elecrow-test` | Check the older Elecrow 10-inch console |
| [ESP32 - Elecrow Screen](esp32-elecrow-screen/SKILL.md) | `$esp32-elecrow-screen` | Fix the older Elecrow 10-inch screen |
| [ESP32 - Elecrow Sound](esp32-elecrow-sound/SKILL.md) | `$esp32-elecrow-sound` | Fix the older Elecrow 10-inch speaker |

## Previous names

The folders, skill IDs, UI names and maintained links have all been renamed.
Old acceptance records retain their original paths and hashes as historical
evidence. Frozen recovery installers retain their original inventory keys and
must be reproduced with their exact historical source revision; these renames
do not issue a new hardware authorization. When an older conversation uses
one of these names, select its replacement; do not recreate a second copy of the skill.

| Previous name | Current skill |
| --- | --- |
| `installos` | [ESP32 - Set Up](esp32-setup/SKILL.md) |
| `develop-p4-console-games` | [ESP32 - Make Game](esp32-make-game/SKILL.md) |
| `create-p4-game-art` | [ESP32 - Game Art](esp32-game-art/SKILL.md) |
| `develop-p4-games` | [ESP32 - Add Game](esp32-add-game/SKILL.md) |
| `test-p4-games-locally` | [ESP32 - Test Game](esp32-test-game/SKILL.md) |
| `develop-p4-multiplayer-games` | [ESP32 - Multiplayer](esp32-multiplayer/SKILL.md) |
| `develop-esp32-p4-platform` | [ESP32 - Fix Console](esp32-fix-console/SKILL.md) |
| `add-usb-gamepad-support` | [ESP32 - Controllers](esp32-controllers/SKILL.md) |
| `develop-waveshare-p4-4-3` | [ESP32 - Waveshare](esp32-waveshare/SKILL.md) |
| `use-waveshare-p4-4-3-display` | [ESP32 - Waveshare Screen](esp32-waveshare-screen/SKILL.md) |
| `use-waveshare-p4-4-3-audio` | [ESP32 - Waveshare Sound](esp32-waveshare-sound/SKILL.md) |
| `test-console-os-builds` | [ESP32 - Elecrow Test](esp32-elecrow-test/SKILL.md) |
| `use-elecrow-p4-display` | [ESP32 - Elecrow Screen](esp32-elecrow-screen/SKILL.md) |
| `use-elecrow-p4-audio` | [ESP32 - Elecrow Sound](esp32-elecrow-sound/SKILL.md) |

The hardware safety, native API, dual-core ownership and measured-performance
contracts remain in the specialist skills. Friendly names do not change them.
