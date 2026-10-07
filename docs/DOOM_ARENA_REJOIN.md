# Doom Arena deterministic replay fixture

Run `python3 scripts/doom/test-arena-rejoin.py --negative-consistency` after
promotion. The script compiles the real Doom engine under AddressSanitizer,
then launches separate host and cold-guest processes. It requires the exact
Freedoom 0.13.0 Phase 2, Pure Hades v0.6 and DWANGO5 WAD identities recorded in
`third_party/game-data.json`. External WADs must remain ignored local inputs.
It downloads no content and never accesses a device port.

The host records complete canonical commands, player masks and original
consistency bytes from tic 0. Slot 3 departs at tic 700. Its independent cold
guest replays under a frozen synthetic clock, catches up at tic 2000, and
enables local command generation at future tic 2048 while retaining buffered
canonical tics. Replay presentation suppression continues through that
activation edge. Both engines finish at tic 2704 after 656 resumed live tics.

The fixture compares all 2,705 tic boundaries across fixed-point actor state,
player state, both RNG indices, world geometry and flags, thinker and spatial
list order, item and corpse queues, and Arena votes, map, visits and scores.
Every snapshot must have known thinkers, resolved references and no malformed
state before equality can pass. Dormant special fields that vanilla Doom does
not initialize are excluded. Actor-free seats omit only the local camera
`viewz` sentinel set by `P_SetupLevel`; that field is compared again when the
actor respawns. Both engines use identical headless presentation; cosmetic
RNG and automap flags in rendered games can differ between host and replay.

Real movement, attack, use and vote chat commands exercise Arena idle break,
return, map votes, departure and a fresh rejoin visit. Explicit harness-only
events deal damage at tics 50, 1305 and 2100, apply nonfatal damage and sound
alert references immediately before departure at tic 699, and remove a map
item at tic 1300. Item respawn then runs through the actual engine queue.
These fixture events are keyed to canonical tic and applied in both processes;
production recovery requires its complete canonical input history. They do
not define production journal semantics.

The optional negative control clears the guest consistency ring after replay
finish and must fail its first live input at tic 2048. This verifies that
successful replay preserved the real consistency history.

Evidence is written under ignored `build-host/doom-arena-rejoin/`: canonical
journal, host and guest state traces, process logs, sanitizer binary, compiler
diagnostics and a manifest with source/data hashes and compiler identity.
`--no-build` reuses the existing binary; rebuild after source changes.
`P4_DOOM_ASAN=0` disables the sanitizer and is recorded in the manifest.

For a staged source overlay, set `P4_DOOM_SOURCE_OVERRIDE` to its absolute root.
`P4_DOOM_REPO_ROOT` overrides repository discovery only when testing the runner
before promotion. Both roots are recorded in its manifest. Default execution
uses live repository sources with no overlay.

This is host proof of deterministic gameplay reconstruction and resumption
under identical headless presentation. P4MP packet delivery, reconnect UI,
device cadence and physical two-console multiplayer remain separate acceptance
checks.
