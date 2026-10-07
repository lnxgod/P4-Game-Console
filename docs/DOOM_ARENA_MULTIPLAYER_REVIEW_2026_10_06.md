# Doom arena multiplayer review — 2026-10-06

This review covers the Game Changers AI Doom arena mode, its four-seat P4MP
adapter, lockstep queues, arena rules, lobby start barrier and local Wi-Fi route
lifecycle. It found three issues corrected on
`codex/doom-arena-multiplayer-review`; a follow-up fixes the fourth issue, lost
start messages across the Doom handoff. All four identified arena issues now
have source fixes, with physical acceptance still pending.
These are local changes based on main at
`20aeac7008a81c3c3afa7c2b9d670f5ab6af860d`. They are not a hardware acceptance
or a release. Ordinary Doom/Chex retain their separate two-player adapter.

## Corrected findings

| Priority | Trigger and effect | Correction and proof |
| --- | --- | --- |
| P1 | A guest keeps heartbeating but stops submitting required tics. Canonical progress stops for everyone, so the host's ten-second progress timer removes healthy guests too. Clients also abandon a heartbeating host while it waits. | Only charge a host-side timeout to a guest missing a canonical ACK or the next required command after the host has its own command. Recognize session-validated host heartbeats on clients. Production-adapter regressions failed before the fix and pass afterward. Only the stalled guest leaves and simulation resumes. A paused host does not expel guests; an actually silent host still times out. |
| P2 | Wi-Fi marks an old route disconnected after three seconds but never frees its entry. After three distinct guest addresses, a replacement cannot join without restarting the link. | Reclaim expired routes under the existing transport lock during poll/send. The socket-backed regression failed before the fix and passes afterward: two refreshed routes survive, the replacement gets the expired slot, and sending to the expired address fails. |
| P2 | LEAVE from an earlier guest removes that session peer immediately. The lobby ignores the event and watches only the last accepted route. Slots can become `0,2,3` while the start message says three players; slot 3 rejects it. | Handle LEAVE through the existing host-reopen/client-reset path. Remaining guests must rejoin the fresh lobby. A production session/group probe confirms the sparse-roster failure. The Console OS callback change is build-tested, with full UI/device acceptance pending. |

The fixes preserve packet formats, negotiated game protocol 4, map rules,
save identities, WAD identities and board/component/toolchain pins. Reopening a
lobby invalidates its old seat assignments; it is not seamless live admission
or host migration.

## Corrected P1: start commit recovery across handoff

`components/p4_multiplayer/src/group.c` marks the host committed after all READY
messages, sends COMMIT for one second and then enters DUE. READY does not say
whether a guest received COMMIT. Before the follow-up, Doom replaced the lobby
handler after launch and did not recover a late group-start exchange.

A production-code probe dropped only COMMIT packets to guest 1 for the first
1,400 ms. All ten retries were lost. With the link otherwise working, host and
guests 2/3 reached DUE while guest 1 reached FAILED at the eight-second start
deadline. This is a **partial launch**, not demonstrated game-state
desynchronization: the arena's separate all-player configuration gate should
prevent a complete match starting, but leaves players loading or timing out.
The follow-up keeps COMMIT recovery active in the arena adapter while engines
configure. It answers only an admitted, session-validated guest's correctly
formed READY matching the original session/seed token and roster. The existing
group parser validates version, kind, count, sender, token and reserved bytes.
Replies are capped at ten per second per guest and stop once configured. Token
derivation is shared with the lobby, preserving the existing wire format and
game protocol 4.

Lobby READY never sets engine readiness. Each guest must still send `GCAREADY`
from the Doom configuration stage before the host can produce canonical tics.
This existing confirmation provides the all-engine gate without a new wire
protocol. A guest that never reaches that stage causes bounded failure at the
existing 30-second configuration deadline. Recovery requires the guest still
to be within its eight-second lobby deadline; a permanent partition cannot
start a complete match. Native-game handoff behavior is unchanged.

The new four-process regression runs real P4MP sessions and the production
group barrier, then switches each peer to the production Doom adapter. Dropping
all guest-1 COMMIT messages for 1,400 ms fails on the old adapter at the lobby
timeout and passes with recovery: all four configure and exchange canonical
commands, then handle guest departure and host shutdown under additional loss
and duplicates. A separate virtual-time test floods valid lobby READY while
withholding engine READY; it proves no tics are delivered, replies are bounded
and configuration times out. Group tests reject malformed recovery requests
and ensure duplicate COMMIT cannot postpone an already scheduled launch.

Follow-up checks: `make doom-multiplayer-host` passes 12/12 cases under its
configured ASan/UBSan checks. `make p4-multiplayer-host` passes the three shared
cases (already included in that twelve) and the Python relay checks. Logs are
under ignored `build-host/doom-arena-handoff-fix/`. Follow-up build identities
are recorded in `test-runs/2026-10-06-doom-arena-handoff-fix.json`.
The Tab5 build and complete verifier pass at `34fffb440be1`: the image is
4,318,512 bytes and the bundle contains 17 verified native cartridges. Nothing
was flashed; on-device acceptance remains pending.

## Initial review validation and continuing limits

- `make doom-multiplayer-host`: 10/10 CTest cases pass under the configured
  AddressSanitizer/UndefinedBehaviorSanitizer instrumentation. Coverage includes
  arena rules, lockstep validation, group start, the real four-process adapter
  loss/duplicate/peer-departure test and three new virtual-time stall tests.
- Local Wi-Fi socket/mocked-platform suite: 1/1 passes, including expired-route
  replacement. This does not test a physical C6 radio or FreeRTOS concurrency.
- Tab5 firmware builds and passes the complete candidate verifier at source
  `73b0bda17a4847cdcd5fdcbfcfe991a00cca58d3`: 4,318,272-byte image and 17 verified
  native cartridges. Existing vendored Doom warnings remain; no warning was
  reported in the three changed production translation units.
- Baseline suites passed before the new fault cases. Host/client stall cases
  and route replacement failed on the original production implementations.
  The original standalone handoff probe reproduces the sparse roster and
  partial launch without the follow-up adapter recovery. The new four-process
  lobby-to-adapter test is the regression check for that recovery.
- Source review did not identify another actionable defect in the bounded
  tic/history queues, vote generation/partial-message handling or deterministic
  idle/return rules. This is not proof that the engine is bug-free.
- No device was flashed. Two-, three- and four-Tab5 gameplay, radio recovery,
  Back/leave during launch, map votes and score preservation still need
  exact-image, named-unit acceptance. No performance gain was measured.

Local logs and the standalone diagnostic are under ignored
`build-host/doom-arena-review/`. The accompanying machine-readable record in
`test-runs/2026-10-06-doom-arena-review.json` records the source revision,
firmware result, artifact identity and log digests.
