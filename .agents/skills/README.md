# Repository skills

These skills ship with Game Changers AI OS. Open the repository root so your
coding agent can read `AGENTS.md` and this directory. No personal skill folder
is required. Each skill links to the source contracts and board-specific checks.
M5Stack Tab5 is the only actively maintained board target; shared game and
platform workflows below default to Tab5.

| Skill | Use |
| --- | --- |
| [installos](installos/SKILL.md) | First setup, storage choice, verified Doom download, guarded installation and initial configuration |
| [develop-p4-console-games](develop-p4-console-games/SKILL.md) | Native C gameplay and custom engines through the stable Game API |
| [create-p4-game-art](create-p4-game-art/SKILL.md) | Native-resolution art, launcher icons, provenance and packing |
| [develop-p4-games](develop-p4-games/SKILL.md) | Manifests, cartridges, resources, catalogs and game installation/removal |
| [test-p4-games-locally](test-p4-games-locally/SKILL.md) | Real SDL play, sanitizer checks and gameplay iteration |
| [develop-p4-multiplayer-games](develop-p4-multiplayer-games/SKILL.md) | Game synchronization using OS Host/Join and bounded protocols |
| [develop-esp32-p4-platform](develop-esp32-p4-platform/SKILL.md) | Pinned tools, shared services, Tab5 builds and guarded hardware work |
| [add-usb-gamepad-support](add-usb-gamepad-support/SKILL.md) | OS-owned USB/Bluetooth controllers and normalized input |

## Legacy board maintenance

These skills are retained for explicitly requested work on existing legacy
boards. They are not the Tab5 installation or acceptance workflow.

| Skill | Use |
| --- | --- |
| [develop-waveshare-p4-4-3](develop-waveshare-p4-4-3/SKILL.md) | Exact Waveshare 4.3-inch board builds and qualification |
| [use-waveshare-p4-4-3-display](use-waveshare-p4-4-3-display/SKILL.md) | Waveshare display path |
| [use-waveshare-p4-4-3-audio](use-waveshare-p4-4-3-audio/SKILL.md) | Waveshare audio path |
| [test-console-os-builds](test-console-os-builds/SKILL.md) | Exact Elecrow 10-inch Console OS qualification |
| [use-elecrow-p4-display](use-elecrow-p4-display/SKILL.md) | Exact Elecrow 10-inch display path |
| [use-elecrow-p4-audio](use-elecrow-p4-audio/SKILL.md) | Exact Elecrow 10-inch factory speaker path |

Board-specific skills never transfer flash
authorization to another board. Game work uses native 768×480 rendering with a
tested 320×200 fallback, a 60 FPS target and a measured 30 FPS device release
floor. The removed Lua authoring stack is not a supported creation route.
