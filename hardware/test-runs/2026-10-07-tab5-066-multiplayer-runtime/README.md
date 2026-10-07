# Tab5 0.66 Doom Arena runtime evidence

**Cadence failed. Fresh late admission, one owned-seat return, and clean exits
passed the serial checks.** The completed 328.709-second scripted workload
measured **20.125 FPS on A** and **20.683 FPS on B**, below the requested 25 FPS
minimum and repository 30 FPS release floor. These are successful frame
submission rates, not visible scanout or simulation tics. Heard music/SFX and
human physical gameplay acceptance remain pending.

The [runtime record](../2026-10-07-tab5-066-multiplayer-runtime.json) binds the
[guarded installation](../2026-10-07-tab5-066-multiplayer-debug-install.json),
both hashed physical Tab5 units, closed raw logs, thirteen screenshots,
sanitized text and offline analysis tools. Application 0.66 is 4,406,000 bytes,
SHA-256 `a5d2fd131fbe5bb838c8dddce761f0563cdd20b232438d76af07bdfef83760bd`.
Its ignored predecessor image preserves exact installed bytes independently of
later builds. No later firmware performance or admission result is implied.

A started alone as host/slot 0 in Local Wi-Fi session `50c9f90c`. B joined the
running match as a fresh slot 1: both sides logged `kind=fresh`, and host
`REJOIN_ARMED` matched guest `REJOIN_READY` at tic **6106**. After B intentionally
quit and restarted to Home, A continued the same match. B returned to that
activated owned seat: both sides logged `kind=return`, with matching activation
tic **44896**. A's same Doom visit and session span both admissions. B was not
in the initial roster; this run does not claim a tic-zero roster return.
`READY players=4` is configured capacity, not four connected players.

| B visit | Admission to replay | Replay to ready | Admission to ready |
| --- | ---: | ---: | ---: |
| First late join | 62.551 s | 16.325 s | 78.876 s |
| Returning owned seat | 62.514 s | 93.795 s | 156.309 s |

Durations use same-boot B timestamps. The first segment includes WAD validation
and engine initialization; the second includes replay catch-up and activation,
not isolated CPU time. A began on-demand WAD validation at uptime 96.737 s and
reached its solo engine-ready marker at 178.340 s. Guest return-ready to final
cleanup spans 267.509 seconds, including two screenshot transfers and exit
navigation; it is not uninterrupted gameplay cadence. Reverse hosting,
additional return cycles and a simultaneous multi-guest join were not exercised.

The completed workload ran from **07:04:35.418328 to 07:10:04.127654 UTC**:
60/60 paired pulse requests, each applied sequentially to A and B. Each unit
received 50 one-second movement-plus-fire attempts and 10 one-second Use
attempts. All command receipts succeeded; this was intermittent synthetic input,
not simultaneous continuous human play. An earlier attempt stopped after 5/60
pulses because its initial releases used expired debug sessions; that failed
attempt remains recorded separately.

| Unit | Eligible local interval coverage | Submitted frames | Weighted FPS |
| --- | ---: | ---: | ---: |
| A | 320.499 s / 43 intervals | 6,450 | 20.124868 |
| B | 326.358 s / 45 intervals | 6,750 | 20.682808 |

The local counter spans differ from the shared 328.709-second journal envelope.
No screenshots or lifecycle boundaries overlap these eligible intervals.
Within this completed workload, measured audio write/parse failures, network
send failures, rejected session packets, departures and worker failures stayed
zero. Music events, notes, loops and generated frames advanced; mixed backend
PCM was nonzero. These counters do not isolate SFX or prove speaker audibility.
Worker completion means blocking platform-submit success, not a unique scanned
frame. Worker and STATS endpoints differ, so their frame totals need not match.

All thirteen images passed complete BEGIN/900 READ/END checks, 1,843,200-byte
RGB565 payload hashes, and byte-for-byte PNG reconstruction. Root inspected the
saved score-open, score-closed-break, break-return, initial late-join and returned
guest captures. Score closure and ending break are separate actions. Complete
screenshot commands took **50.892–53.129 seconds** and are excluded in full for
both peers from eligible cadence intervals; no capture time is subtracted to
manufacture an adjusted FPS.

The pair named **`066-return1-fire` is mislabeled**. Numeric masks 2 and 1 were
Down and Up; those images still show the break overlay. The explicit
**07:25:41.891529 UTC** correction remains in the journal. Root then sent symbolic
`b` (Use) and `a` (fire). The separate **`066-return1-use-and-fire`** images show
the break text/shade gone, both guns visible, ammunition **47** on both units
(previously A48/B50), and only the tiny white `SCORE` label. This supports return
to the active view and ammunition expenditure. Frags remain zero; there is no
kill, continuous-motion, optical-panel, or heard-SFX acceptance claim.

B's two intentional exits each completed cleanup with no retained resources;
A recorded two `PLAYER_LEFT` markers, continued, and then completed its own
intentional cleanup/restart. All eight command errors are retained: initial A
status timeout, two expired-session workload releases, first B exit release
timeout, stale-session B tap and close, and final B/A exit release timeouts.
The missing release replies overlap planned restarts; later statuses show
successful recovery. Final STATUS at **07:28:10.877887 UTC** shows both units at
Home with inactive debug sessions and neutral inputs. The journal ends with
**ports closed at 07:28:20.491264 UTC**; root confirmed harness exit 0.

No fatal/engine error, malformed debug frame, or pending binary frame was detected.
Warnings remain preserved, including 198 A and 124 B emitted
`GAMEPAD_POLL_FAIL` records. No-controller transport identity mismatch is a prior
source explanation; no live provider probe or isolated timing effect was measured.
These warnings do not establish that synthetic debug input failed, and physical
USB/Bluetooth HID acceptance remains untested.

**Native game hardware tests were not executed.** Tide Maze 0.2.2 and Yahtzee
1.4.2 were installed and fully read back before this OS test; those receipts are
referenced separately. Package installation does not establish native gameplay,
multiplayer, input, cadence, or sound acceptance.

Offline validation passed **41 runtime, 21 worker, and 15 workload regressions**.
An independent read-only review reproduced the A/B cadence from decoded raw
STATS, verified the completed workload's prefix bindings against the final
closed captures, and checked the screenshot correction and final state. The
original active-window review remains unchanged at SHA-256
`82e3872617a7d9423f32fb3ea25cfe18874aff42c0006db32d49aa71ffb9bd0e`.
Sanitized outputs redact MACs, physical port paths and numeric route/lobby
fields. Raw captures, image payloads and firmware remain ignored local evidence.
The 0.65 runtime record and earlier failed acceptance history remain unchanged.
