# Game Changers AI multiplayer arena

[Monorepo](../../../README.md) · [Console OS](../../../apps/console_os/README.md) · [Full arena guide](../../GAME_CHANGERS_AI_DOOM.md)

This is the **Doom-based arena game mode** in the P4 Game Console monorepo.
Console OS hosts it and provides shared services; the arena is one game within
that platform. It currently appears as **Multiplayer → Host → Game Changers AI**,
with no separate launcher tile or `.P4G` package.

## What you can play

Two to four consoles use **Local Wi-Fi**, with one playing host and up to three
guests. The host chooses from **26 arenas**: Pure Hell's Shotguns/Rockets pair,
or DWANGO 5 MAP01–MAP24. Pure Hell loops between its two maps. The DWANGO pack
loops through its 24 maps. During a match, **Menu/Start → Choose Arena** proposes
a map; active players vote Yes/No and a strict majority changes it.

Current-visit kills persist across map changes. Fifteen seconds without
movement/fire puts a connected player on a break; release and press **Use**
to return at zero score. The host continues serving during its own break.
Disconnected/new consoles must join at the initial lobby: live admission and
host migration are not implemented. Ordinary Doom/Chex and Bluetooth/USB
serial retain their separate two-player behavior.

## Build, content and launch

The game is integrated in the Tab5 0.56 OS source. Use
`make console-os-tab5-idf` and the [guarded Tab5 route](../../boards/M5STACK_TAB5.md)
for firmware. Each console also needs the identical verified **11-file bundle**
on microSD: Freedoom Phase 2, Pure Hell, DWANGO 5, MIDI and the required notices.
To prepare it locally from the repository root:

```sh
python3 scripts/doom/arena-content.py --fetch
python3 scripts/doom/arena-content.py --stage build-host/game-changers-ai/sd-card
```

The [full arena guide](../../GAME_CHANGERS_AI_DOOM.md) gives exact hashes,
content installation commands, menu controls, scoring/vote rules, limits and
host tests. [Pure Hell's README](../../../game-data/pure-hell/README.md) covers
the original pack and music credits. Freedoom/DWANGO remain ignored local
inputs; the Pure Hell exception does not permit republishing other WADs.

## Code and acceptance

The OS launch path is in [`apps/console_os`](../../../apps/console_os/);
shared engine/session support is in [`components/doom_multiplayer`](../../../components/doom_multiplayer/)
and the [engine integration](../../../apps/doom_audio_probe/components/doom_engine_audio/).
Host/content tooling is under [`scripts/doom`](../../../scripts/doom/).

Rules, voting, storage and four-instance lossy-network tests are recorded in
the detailed guide. The exact 0.56 OS and content were installed/read back on
the two recorded Tab5 units. **Physical multiplayer play, controls/audio and
sustained device cadence remain pending**; four real consoles have not been
qualified. Keep this distinction when describing the game publicly.
