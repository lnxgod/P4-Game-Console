---
name: esp32-start
description: Use when working on games, artwork, testing, installation, multiplayer, controllers or Console OS in the P4 Game Console monorepo, or choosing the right repository workflow.
---

# ESP32 - Start Here

Read the root `AGENTS.md`. Infer the workflow from the user's request and open
only the matching skills and references. Respect board choices and authorization
already established in the conversation; users do not need to name a skill.

## Choose the workflow

| Request | Skill |
| --- | --- |
| First-time Console OS setup and provisioning | [ESP32 - Set Up](../esp32-setup/SKILL.md) |
| Make or change a native game | [ESP32 - Make Game](../esp32-make-game/SKILL.md) |
| Draw sprites, textures or launcher art | [ESP32 - Game Art](../esp32-game-art/SKILL.md) |
| Package, install or update game content | [ESP32 - Add Game](../esp32-add-game/SKILL.md) |
| Play-test a native game on the computer | [ESP32 - Test Game](../esp32-test-game/SKILL.md) |
| Add linked-console play | [ESP32 - Multiplayer](../esp32-multiplayer/SKILL.md) |
| Build, diagnose or change the shared OS/hardware | [ESP32 - Fix Console](../esp32-fix-console/SKILL.md) |
| Change shared USB/Bluetooth controller support | [ESP32 - Controllers](../esp32-controllers/SKILL.md) |

Game changes use **Make Game**, then **Test Game**. Add **Game Art** for asset
work, **Multiplayer** for linked consoles and **Add Game** for packaging or
installation. Same-device pass-and-play and ordinary button mappings remain
with **Make Game**. Compatible cartridge updates normally need no OS reflash.
Shared audio, display, storage, build or device problems use **Fix Console**;
its Tab5 route owns the dual-core and guarded-flash contracts.
Documentation-only work needs reference/skill validation, not a firmware build.

## Always keep scrolling and resource use responsive

For every app/game or OS change, apply the shared
[smooth scrolling](../../../docs/GAME_PERFORMANCE.md#always-preserve-smooth-scrolling)
and [P4 resource/core requirements](../../../docs/GAME_PERFORMANCE.md#always-use-relevant-resources-and-both-p4-application-cores).
All scrollable surfaces must always follow contact promptly and remain smooth
at panel rate, near 60 FPS, with velocity-aware stopping and intact taps/actions.
Always plan use of all relevant resources and both application cores through
owned OS services; static surfaces need no invented motion or work.
Route reusable cache/DMA, scheduling or ownership changes through **Fix Console**;
keep game mechanics in **Make Game** and verify with **Test Game**. Record
actual input/render/submission/pacing, glide/cold-cache and core/accelerator
evidence before closing a device performance claim. Existing authorization
and peripheral scope still govern any hardware run.

## Require native game resolution

Every game on maintained Tab5 must render directly at 768×480 RGB565. Follow
[the presentation standard](../../../docs/GAME_ART.md): native cartridges
require `video-highres` in both manifest and C descriptor. Canonical 320×200
touch coordinates remain input units. Never lower the game framebuffer,
upscale a completed 320×200 frame, or enable a per-title low-resolution override
to fix readability or performance.

For a blurry, noisy or unreadable game, establish the actual runtime surface
before changing textures. If it is 320×200, use **Fix Console** to trace the
manifest, descriptor, OS resolution overrides and framebuffer selection, then
**Make Game → Game Art → Test Game** for native presentation. Require exact
package/OS/unit-bound `surface=768x480` evidence plus readable active play before
closing device acceptance. A high-resolution declaration or enlarged screenshot
alone is insufficient. Preserve legacy source/recovery contracts only for
explicitly requested legacy maintenance.

## Know the monorepo boundaries

**P4 Game Console** is the monorepo. **Console OS** is the shared platform in
`apps/console_os` and `components`, currently branded Game Changers AI OS.
**Doom Arena by Game Changers** is an integrated Doom mode; **Pure Hades** is
one map pack. Documentation naming never changes game/package/save IDs or
current OS branding.

Doom, Chex Quest and arena engine work use **Fix Console** with their owning
`docs/games/` README and engine guide. Wacky Wheels and Quake use their own
README/harness linked from the [engine inventory](../../../README.md#engine-games-and-ports).
The native game runner does not run these engines. New game creation uses C
`.P4G` cartridges through the stable Game API; custom 2D/3D renderers follow
the same surface, lifecycle and resource contracts.

## Legacy boards, only when requested

**M5Stack Tab5 is the only maintained default.** Initial installation uses
**Set Up**; OS work uses **Fix Console** and the Tab5 native USB workflow.
Select these older-board routes only for explicit legacy maintenance:

| Legacy task | Skill |
| --- | --- |
| Waveshare 4.3 console | [ESP32 - Waveshare](../esp32-waveshare/SKILL.md) |
| Waveshare screen | [ESP32 - Waveshare Screen](../esp32-waveshare-screen/SKILL.md) |
| Waveshare sound | [ESP32 - Waveshare Sound](../esp32-waveshare-sound/SKILL.md) |
| Elecrow 10-inch console | [ESP32 - Elecrow Test](../esp32-elecrow-test/SKILL.md) |
| Elecrow screen | [ESP32 - Elecrow Screen](../esp32-elecrow-screen/SKILL.md) |
| Elecrow speaker | [ESP32 - Elecrow Sound](../esp32-elecrow-sound/SKILL.md) |

Olimex Rev.B uses **Fix Console** with `docs/boards/OLIMEX_ESP32_P4_PC.md`.
Preserve legacy source and exact-unit recovery evidence. Flash authorization
never transfers between boards or units.

## Keep game status honest

Follow [library policy](../../../docs/GAME_LIBRARY.md),
[performance](../../../docs/GAME_PERFORMANCE.md),
[presentation](../../../docs/GAME_ART.md#complete-game-presentation) and
[launch quality](../../../docs/LAUNCH_QUALITY.md) for game/release work.
Keep each game's README current and link new titles from the root inventory
and `games/README.md`. Drafts stay disabled until qualified; retired IDs stay
reserved. Standard content excludes disabled games and `GAMES/WIP`; named
developer installs require explicit opt-in. Preserve Tide Maze's availability
and open device-lag acceptance, and Blast Circuit's featured status.

A build, enabled flag or upload does not prove physical play, speaker quality
or P4 frame rate. Preserve failed/pending acceptance and exact artifact/unit
bindings. The selected specialist owns the task's detailed checks; do not load
all skills or run every test by default.
