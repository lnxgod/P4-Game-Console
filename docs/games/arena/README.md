# Doom Arena by Game Changers

[Monorepo](../../../README.md) · [Console OS](../../../apps/console_os/README.md) · [Full arena guide](../../GAME_CHANGERS_AI_DOOM.md)

This is the **Doom-based arena game mode** in the P4 Game Console monorepo.
Console OS hosts it and provides shared services; the arena is one game within
that platform. It currently appears as **Multiplayer → Doom Arena by Game Changers → Local Wi-Fi → Host or Join**,
with no separate launcher tile or `.P4G` package.

## What you can play

Two to four consoles use **Local Wi-Fi**, with one playing host and up to three
guests. The host chooses from **29 arenas**: the five-map Pure Hades pack,
or DWANGO 5 MAP01–MAP24. Pure Hades loops through its five maps. The DWANGO pack
loops through its 24 maps. During a match, **Menu/Back → Choose Arena** proposes
a map; active players vote Yes/No and a strict majority changes it.

Scores stay hidden until you tap the small **SCORE** button or press controller
**Start**. Tap **CLOSE** or press Start again to dismiss them. The touch **P**
shortcut also toggles scores in Arena. This local panel does not pause play;
vote and break prompts remain visible, and opening the menu or changing maps
closes the scores.

Current-visit kills persist across map changes. Fifteen seconds without
movement/fire puts a connected player on a break; release and press **Use**
to return at zero score. The host continues serving during its own break.
Disconnected/new consoles must join at the initial lobby: live admission and
host migration are not implemented. Ordinary Doom/Chex and Bluetooth/USB
serial retain their separate two-player behavior.

## Build, content and launch

The compact-content game is staged for the Tab5 0.79 candidate OS source. Use
`make console-os-tab5-idf` and the [guarded Tab5 route](../../boards/M5STACK_TAB5.md)
for firmware. Each console also needs the identical verified **17-file bundle**
on microSD: the separate compact Freedoom-derived Arena base, Pure Hades,
DWANGO 5, MIDI and the required notices. All 29 Arena maps are retained.
The compact base uses `/GCADOOM/ARENA2.WAD`; ordinary campaign Freedoom
remains `/FREEDOOM2.WAD`. Every peer needs the new matching content identity.
To prepare it locally from the repository root:

```sh
python3 scripts/doom/arena-content.py --fetch
python3 scripts/doom/arena-content.py --stage build-host/game-changers-ai/sd-card
```

The [full arena guide](../../GAME_CHANGERS_AI_DOOM.md) gives exact hashes,
content installation commands, menu controls, scoring/vote rules, limits and
host tests. [Pure Hades's README](../../../game-data/pure-hades/README.md) covers
the original pack and music credits. Freedoom/DWANGO remain ignored local
inputs. The generated compact WAD also stays outside Git; its recipe and
derivative notice are tracked. The Pure Hades exception does not permit republishing other WADs.

## Code and acceptance

The OS launch path is in [`apps/console_os`](../../../apps/console_os/);
shared engine/session support is in [`components/doom_multiplayer`](../../../components/doom_multiplayer/)
and the [engine integration](../../../apps/doom_audio_probe/components/doom_engine_audio/).
Host/content tooling is under [`scripts/doom`](../../../scripts/doom/).

Rules, voting, storage and four-instance lossy-network tests are recorded in
the detailed guide. The [0.62 installation record](../../../hardware/test-runs/2026-10-07-tab5-062-multiplayer-debug-install.json)
binds the installed firmware for the two recorded Tab5 units. Its follow-up
reached READY on both, then failed when the slower guest was disconnected during
level loading. The 0.63 candidate addresses this failure; it still needs device
acceptance. **Physical multiplayer play, controls/audio and
sustained device cadence remain pending**; four real consoles have not been
qualified. Keep this distinction when describing the game publicly.
