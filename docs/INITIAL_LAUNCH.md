# Game Changers AI OS — initial launch

This is the native ESP32-P4 software foundation for the future STEM box.
M5Stack Tab5 is the primary target. The repository includes the complete
[skill index](../.agents/skills/README.md), native C game SDK, local SDL runner,
asset sources/converters, focused tests and exact-board installation tools.

Console OS 0.55 and all 17 native cartridges (14 games and 3 utilities), plus
Byte Buddy's required animation resource, are installed on Tab5 A and B.
Both app readbacks, startup checks and all 18 content-file hashes passed;
the protected Red Dragon cartridge registered against its paired OS. See the
[exact installation and test record](../test-runs/2026-10-05-initial-launch.json).
These checks establish installation, not universal gameplay or frame-rate
acceptance.

Native game creation supports custom 2D,
software 3D and raycasting within the Game API's surface, lifecycle and resource
contracts. The Lua creation/runtime/tooling stack is removed. The required
[performance contract](GAME_PERFORMANCE.md) targets 60 FPS and requires actual
device evidence for the 30 FPS release floor; it cannot guarantee that every
future generated game will meet the floor without testing.

## Initial configuration

Start with [esp32-setup](../.agents/skills/esp32-setup/SKILL.md). It checks the
pinned environment, asks about microSD, downloads and verifies Doom v1.9
shareware when missing, builds the basic native bundle and uses the exact-unit
guarded installation route. It then checks storage, clock, saved preferences,
touch, native gameplay and Doom startup/exit. Chex Quest requires explicit
selection and SD storage. Current Tab5 game storage is SD-only; internal starter
storage and combined internal/SD catalogs are not implemented.

`make prepare-game-data` acquires local data; `make game-data-host` checks the
bounded downloader. Neither command installs content. WADs, recovery backups,
generated packages and firmware stay outside Git.

## Game status

The [library](GAME_LIBRARY.md) distinguishes games, tools and retained WIP.
Maze Chase has positive operator feedback for its movement and presentation;
that feedback does not qualify unrelated games. Byte Buddy 5.0.0 and Red Dragon
1.9.0 retain their WIP category while receiving native presentation remixes
that preserve gameplay/save identities. Red Dragon requires its paired OS
allowlist when installing a newly linked cartridge.

Tide Maze 0.2.1 reduces rendering work and input filtering delay. Earlier
installed versions failed the operator's smoothness acceptance. Keep that
failure open until exact-package device cadence and physical play establish
the replacement's result. Host timing is recorded separately.

The Tab5 build, native SDK and focused game/setup checks pass. Repository-wide
`make check` still stops in the legacy Elecrow `display_diag` build, whose broad
component discovery includes conflicting pre-existing touch dependency pins
(1.1.2 and 1.2.1). No dependency versions or lockfiles were changed to bypass it.

Both Star Sprout prototypes are removed from Tab5 A and B, with reserved
identities and [exact receipts](../test-runs/2026-10-05-star-sprout-removal.json).
Skyline Leap remains disabled. The built-in Doom tile uses an original classic
gold-and-steel wordmark instead of newer-sequel character artwork.

## Public source publication status

The current-source review found no high-confidence credentials or game-data
blobs in the reviewed content, and current raw device identifiers were replaced
with hashes. Existing Git history still includes earlier device identifiers and
author metadata; no history rewrite was performed.

Before changing repository visibility, settle the top-level license map and
confirm public redistribution scope for Red Dragon. Its
[provenance](../games/lord/UPSTREAM.md) records permission to make the port, but
does not supply the text of a public redistribution grant. Preserve the existing
per-file and third-party licenses; do not infer a blanket license for the tree.
The [Wacky Wheels experiment](../scripts/wacky/README.md) documents its separate
upstream licensing limitation. Its fetched upstream/data and generated playable
artifacts remain local and outside the default source/content distribution.
