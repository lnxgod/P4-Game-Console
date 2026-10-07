# Development and companion tools

[Monorepo](../README.md) · [Game workspace](../games/README.md)

| Tool | Purpose | How to start |
| --- | --- | --- |
| [`p4-game-host/`](p4-game-host/) | SDL3 runner compiling the selected native game's actual sources; smoke, input and performance tools | `make play-game GAME=frog_hop`; [local testing skill](../.agents/skills/esp32-test-game/SKILL.md) |
| [`p4-console-host/`](p4-console-host/) | Desktop preview and test harness for the console shell | `make play-console-os`; [OS guide](../apps/console_os/README.md) |
| [`p4_realm_hub/`](p4_realm_hub/) | Red Dragon's companion realm service, protocol/storage helpers and multi-client tests | [Realm Hub guide](../docs/LORD_REALM_HUB.md), [Red Dragon README](../games/lord/README.md) |

The realm hub is a separate companion service, not the OS's ordinary P4MP
Host/Join room or the four-player Doom arena server. Its larger multi-client
host tests do not establish that many physical Tab5 connections.

Legacy engine runners use their own paths: [Doom](../docs/games/doom/README.md),
[arena](../docs/games/arena/README.md), [Wacky Wheels](../scripts/wacky/README.md)
and [Quake](../ports/quake/README.md). They are not accepted as native game
slugs by the generic runner. Host rendering and tests do not qualify device
touch, audio, storage, radio or frame cadence.
