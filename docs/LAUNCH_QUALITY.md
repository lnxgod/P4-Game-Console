# Game Changers AI OS launch and remix quality

The initial Game Changers AI OS launch is the software foundation for the
future STEM box. Apply these gates to game creation, remixes, library curation
and release preparation. They supplement [game art](GAME_ART.md),
[P4 performance](GAME_PERFORMANCE.md) and [library policy](GAME_LIBRARY.md).

## Preserve the game while improving its presentation

Native C cartridges are the supported path. Render genuine detail at 768×480,
with a tested 320×200 fallback. Keep fractional motion and bounded rendering;
target 60 presented FPS and require the measured 30 FPS floor on the P4.
Language choice, multicore configuration and a simulator counter do not prove
device responsiveness. A reported laggy candidate has failed gameplay
acceptance and must be fixed or excluded from the release-ready selection.

For a visual remix, preserve rules, progression, saves, public IDs, collision
geometry and multiplayer semantics unless the user requests a gameplay change.
Retain deterministic tests and exercise real play after changing the renderer.
Protected games such as Red Dragon must retain their save identity and the
exact OS/cartridge lineage checks when a newly linked payload is installed.

ANSI-style games can retain their terminal character while using crisp native
glyphs, readable spacing, richer palettes, deliberate borders, panels and
restrained animation. Separate semantic text/cell state from pixel rendering;
do not enlarge a completed low-resolution framebuffer and call it high-res.
Keep command keys, selections and readable information in their existing flow.
Compare representative town, combat, inventory and status screens, plus narrow
fallback layouts, without altering the underlying rules to fit the artwork.

## Ship an honest, curated library

Incomplete prototypes remain disabled and out of default content bundles.
Build them in the separate developer output and require an explicit
`make install-dev GAME=<slug> PORT=<port>` opt-in; never promote a WIP tile
as the standard home feature.
An explicit request to retain a WIP title is an exception for that title only;
mark its status clearly. Removal must cover registry/default bundles and any
authorized exact installed copy, while preserving saves and reserved IDs.

Every shipped title needs recognizable artwork and a coherent opening view.
Artwork must represent the actual game and era: classic Doom must not use
Doom Eternal branding. Native game covers travel inside the cartridge;
built-in Doom artwork belongs to Console OS. Keep required text exact and
asset provenance reproducible. Card/board games retain direct touch controls.

## Record acceptance and publish the source

Record operator feedback against the known game/package, OS and unit where
available; label an unknown target honestly. Positive feedback for one title
does not qualify other games. Preserve successful results without carrying
them onto changed packages, and keep visual, host, device-cadence and physical
input/audio acceptance separate. Fix input-to-motion latency as well as FPS,
especially for tilt-controlled games.

Before a public source release, review the staged files and reachable history
for credentials, raw device identifiers, personal data, recovery images and
non-redistributable game data. Keep WADs, ROMs, generated firmware/packages and
local device recordings outside Git. Preserve licenses and provenance for
included source/art. A launch label must state which games are ready, retained
as WIP, or still awaiting device qualification.
