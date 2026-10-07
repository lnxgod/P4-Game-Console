# Cartridge preparation

Read this only when preparing a new game manifest/package. Gameplay and source
authoring belong to Make Game; existing compatible installs do not need a new
scaffold.

For a new game, inspect the plan before creating files:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
python3 scripts/new-game.py "Card Table" --folder GAMES/CARDS --high-res
```

The generator chooses a free non-retired launcher ID, never overwrites a game,
and starts with `enabled: false`. Preview drafts with `make play-game GAME=<slug>`.
Publish only after the quality checks in `docs/GAME_LIBRARY.md` pass. It creates
the manifest and CMake entry at the game root, runtime code under `src/`, and a
README. Add deterministic host tests under `tests/` and an
optional preview tool under `tools/` as the game grows; follow an existing game
for their CMake wiring.

For an existing game, preserve its public ID unless the task intentionally
creates a different title. Prefer small, deterministic game-state transitions
that can be exercised without display, audio, USB, or filesystem hardware.
