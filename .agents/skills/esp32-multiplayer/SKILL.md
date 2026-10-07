---
name: esp32-multiplayer
description: Create or add multiplayer to P4 Console OS games using the existing OS-owned P4MP session. Use automatically for linked-console, co-op, versus, Host/Join or synchronized native-game requests alongside the game-authoring skill. Distinguish same-device play from networked play; games own rules and bounded messages, not controller drivers, lobbies or transports.
---

# ESP32 - Multiplayer

Preserve the user's game idea. Multiplayer is a feature of the game, not a
reason to replace it with a template or create a second networking platform.
Use this skill with `$esp32-make-game` when a request needs linked
consoles; the user does not need to name the skill. Same-device pass-and-play
can remain ordinary local game logic.

## Load the relevant contracts

Read `docs/GAME_SDK.md`'s multiplayer sections,
`components/p4_game_api/include/p4/game.h`, the game's manifest and current
source. Read `docs/MULTIPLAYER.md` when examining OS integration or an actual
link; use the selected board document for physical support.

Use `docs/GAME_STARTERS.md` to choose an optional reference:

- Checkers is the smallest turn-based host-authority example. Read
  `games/checkers/src/checkers_network.c` and
  `games/checkers/tests/test_checkers.c`.
- Air Hockey demonstrates real-time host simulation and client input. Read
  `games/p4_air_hockey/src/p4_air_hockey_network.c` and its two-instance tests.
  Its touch-only controls are not a default for a new controller-friendly game.
- P4 Rummy demonstrates bounded multi-part snapshots and CPU seats. Do not
  copy a large protocol when a single small snapshot will do.

Only ask whether players share one console or use linked consoles when context
does not settle it and the choice changes implementation. Infer cooperative
versus competitive rules from the idea; do not force a transport questionnaire.

## Keep presentation separate from synchronized rules

Use the native 768x480 presentation standard in `docs/GAME_ART.md` with the
320x200 fallback. Native touch and network coordinates stay canonical; do not
change packet fields, simulation rates, seat counts or saved state just to
upgrade art. Preserve each game's supported player count, including four-player
games. Run its existing multi-instance tests after a rendering upgrade, and
verify readable player colors plus non-color seat/turn cues at both sizes.

Apply the [ESP32-P4 performance contract](../../../docs/GAME_PERFORMANCE.md)
in both roles: budget per-update message work and interpolate presentation
without changing synchronized rules. Qualify active session load on the exact
packages, OS and units; offline host CPU timing does not qualify linked cadence.

## Declare the existing service

For a new game:
```sh
python3 scripts/new-game.py "Link Game" --multiplayer turn-based --dry-run
```
Choose `realtime` for host-authoritative action. Use `lockstep` only when the
simulation is deterministic and its tick/input-delay handling is implemented.
Remove `--dry-run` when the scaffold is ready to create. New drafts are
unpublished; use `-DP4_ALLOW_DRAFT_GAME=ON` for local play, and enable the
finished manifest only after gameplay/protocol checks pass.

The generator adds **metadata only**, not game synchronization. For new or
existing games, keep `game.json`, the descriptor's optional
`P4_GAME_CAP_MULTIPLAYER_SESSION` and the actual implementation aligned.
A minimal manifest extension is:
```json
{
  "optional_capabilities": ["audio-tone", "multiplayer-session"],
  "multiplayer": {
    "schema": 1,
    "style": "turn-based",
    "min_players": 2,
    "max_players": 2,
    "message_bytes": 64,
    "protocol": 1
  }
}
```
Merge this into the existing manifest, preserving unrelated capabilities.
Set `message_bytes` to the largest actual packet, at most 64. Increment
`protocol` when message meaning changes. Use the schema's validated timing
fields where needed; never add transport names or addresses.

The Tab5 0.53 candidate supports up to four active human players through the
OS Local Wi-Fi adapter; Bluetooth and the USB serial relay retain their
two-player limit. Shared-component and multi-instance game tests do not prove
four-console hardware acceptance. Check the selected OS artifact and current
device evidence before promising an available link. Never hide a private
transport inside the game to bypass an OS adapter limit.

## Let Console OS own connection setup

Installation/catalog scanning registers the validated native cartridge
automatically. Console OS owns Host/Join, exact package matching, room lists,
transport choice, compatibility, the start barrier and session teardown.
Do not add a central registration table, game-side room browser, BLE service,
UART parser or USB handle.

At `start`, read `p4_game_multiplayer_read_profile()` and
`p4_game_multiplayer_read_status()`. When connected, enter gameplay using the
existing role, local slot, player count, seed and generation. Do not ask the
player to choose Host/Join again. Ordinary launcher starts keep a complete
offline/same-device/CPU mode appropriate to the game.

## Implement the game protocol

Use only `p4_game_multiplayer_send()` and
`p4_game_multiplayer_receive()` for copied, bounded messages.

- Prefer host authority: clients send action intents, the host validates them
  against the current player/turn/state revision, applies the ordinary rules
  and publishes authoritative state.
- Encode bytes explicitly. Validate packet size, kind, version, player,
  revision, ranges and legal actions before changing state. OS framing checks
  do not validate game semantics; never cast arbitrary message bytes to a
  C structure or trust a client to choose the winner.
- Bound receive work per update and obey the negotiated tick/message budget.
  Calls do not block; handle false sends and queue exhaustion with bounded
  retry or a later full snapshot, never a busy wait.
- For multi-part snapshots, bind chunks to one revision, validate every count
  and apply only a complete consistent state. Avoid leaking hidden information
  such as an opponent's secret hand when the rules require it to stay private.
- Lockstep requires deterministic state, seed use, ordered inputs and a
  bounded policy for missing ticks; selecting the manifest style alone does
  not provide these.
- On peer loss, error, offline state or a changed session generation, clear
  stale input and pending messages before a suitable offline fallback or
  match-ended screen. Never let the previous peer keep moving a player.
- Keep normalized local controls usable in both roles. The OS handles USB/BLE
  controllers; game messages carry gameplay intent, not HID reports.

Transport services, lobby logic and reusable fixes belong in `components/`
and Console OS. Only use the platform/controller skills if that shared layer
actually needs a change. Do not expand ordinary multiplayer authoring into
radio or pin bring-up.

## Prove behavior at the right boundary

Adapt the Checkers/Air Hockey two-instance harness to run real host and client
game code through mocked Game API service callbacks. Exercise accepted and
illegal actions, synchronized state, malformed/stale messages, queue failure,
peer loss and offline launch. Keep the ordinary gameplay tests and native
SDL3 smoke/play loop.

Use `make game-registry-check` for manifest/registry changes. Run the changed
game's focused CMake/CTest target for game protocol changes; run
`make p4-multiplayer-host` or `make p4-multiplayer-registry-host` only when
those shared components change. Broaden checks for an actual shared API change.
No firmware build is needed for skill-only edits.

Use `$esp32-add-game` to validate/install the same compatible packages on
both consoles. For a requested hardware run, exercise both OS roles, entry
through the shared lobby, actual gameplay, Back and peer loss on the selected
board's supported link. The existing serial relay is
`scripts/p4-multiplayer-relay.py`; read its arguments and topology before use.
Two USB device ports do not form a link just because a cable fits.

New games use native C through the stable game contract. Tab5 0.45 adds a
core-OS candidate
for encrypted Bluetooth and standalone local Wi-Fi Host/Join alongside the USB relay. Wi-Fi hosts create
their own nearby game network; no router, account or internet is required.
Games use the same P4MP API on every transport. This does not automatically
make a single-player game multiplayer: it still needs its declared profile,
bounded rules/protocol and two-instance tests. Consult the current Tab5
hardware evidence before claiming either radio link is hardware-qualified.
Never inherit another board's claims; report host, build and device evidence
separately.

## Responsiveness is part of multiplayer acceptance

For real-time games, exercise steering in each role while rendering and effects
are busy. Track input-to-visible-motion delay separately from snapshot rate and
frame cadence. Interpolation can hide packet steps while adding a full snapshot
period of control delay; do not describe it as responsive without checking the
guest's own controls. If local prediction is needed, bound reconciliation, keep
collisions/rules authoritative, reset on respawn and timeout, and test loss and
reordering. Preserve protocol compatibility unless message meanings change.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
