# Tab5 0.65 Doom Arena runtime evidence

**Overall result: frame rate failed; original-roster return and clean exits passed
serial checks.** In screenshot-free resumed intervals, successful frame submissions
averaged **17.518 FPS on A** and **17.516 FPS on B**, below the operator's 25 FPS
minimum and the repository's 30 FPS release floor. Initial selected intervals
ranged from 17.045 to 19.506 FPS across the two units. These counters measure
submissions, not visible scanout or simulation tics. Physical gameplay and heard
music/sound acceptance remain open.

The [runtime record](../2026-10-07-tab5-065-multiplayer-runtime.json) binds the
[guarded installation](../2026-10-07-tab5-065-multiplayer-debug-install.json),
both hashed physical units, local raw logs, four screenshots, sanitized text and
offline tools. The exact application is 4,395,472 bytes, SHA-256
`a369cfc77e85427641c7eaa60db62655d9375649422992b901c15588635bfc96`;
its ignored predecessor archive preserves it independently of later builds.
Both saved streams contain version 0.65 boot and shell-ready receipts.

Session `5d623b3d` used A as host/slot 0 and B as initial guest/slot 1 over
Local Wi-Fi. Both reached the initial two-player engine barrier. B then quit
cleanly; A recorded `PLAYER_LEFT` and continued. B returned to the same match:
A accepted frontier 11844 and armed tic **16514**, matched by B's
`REJOIN_READY slot=1 tic=16514`. One original-roster return was exercised;
fresh first late join, additional return cycles, reversed hosting and other
multiplayer games were not tested on this image.

B's return-ready to second cleanup spanned **305.005 seconds of B uptime**.
This includes a screenshot transfer and quit navigation, so it is not a
five-minute continuous cadence result. After B's second clean exit, A recorded
another `PLAYER_LEFT`; four subsequent STATS samples advanced frames
16950 to 17400 before A's own clean exit. No fatal/engine error was detected.
The root's reviewed guest screenshot shows the resumed view; physical input
response and motion smoothness remain separate from these receipts.

All four saved screenshots passed complete BEGIN/900 READ/END checks,
1,843,200-byte RGB565 payload hashes, and byte-for-byte PNG reconstruction.
The root inspected `normal-before-score-a.png`, `score-open-a.png`,
`score-break-closed-a.png`, and `rejoined-guest-active-b.png`. The score panel
clears when closed; the remaining large black score/break button still needs
revision. Each complete screenshot command took about 51 seconds. Every such
interval is excluded in full for **both peers** from selected cadence and phase
metrics; no adjusted FPS is invented by subtracting capture time.

Music events, notes, loops and generated frames are nonzero, and the mixed audio
backend produced nonzero PCM. Observed audio/backend write failures and available
music-parse failure counters stayed zero. MUS and MIDI share telemetry, while
music and SFX share the output backend. These counters do not identify each
sound or prove that a person heard either music or SFX; listening remains pending.

The record preserves 113 A and 86 B `GAMEPAD_POLL_FAIL` warning lines, all
`ESP_ERR_INVALID_RESPONSE state=neutral`. These are emitted warning counts,
not total failed polls. Current unchanged model/provider code initializes a
never-connected controller with transport `NONE`, while the broker requires the
registered transport even for a disconnected snapshot. That source inference
is consistent with no-device warning noise, but no live provider probe was made.
Doom neutralizes an invalid physical snapshot before merging synthetic debug
buttons; acknowledged remote input is separate from physical HID acceptance.
An isolated performance effect from these warnings was not measured. Other
boot/display/RPC warnings also remain in the sanitized logs.

All six command errors are retained: an initial A status timeout recovered on
retry; three final release replies were unavailable during intentional restarts;
and the two closing commands used stale debug sessions after restart, returning
inactive neutral state. Final STATUS at **06:32:17.825030 UTC** independently
confirms both units at Home with inactive sessions and neutral inputs. Capture
opened at 06:12:09.991568 UTC and closed at **06:32:35.047266 UTC** on
2026-10-07; the root confirmed harness exit 0 and closed physical ports.

Both streams have zero bad binary frames and zero pending bytes. Offline
validation passed 25 capture/interval regressions and 6 ordered-log regressions.
A separate read-only audit confirmed the lifecycle, image integrity, warning
counts and final states. Text excerpts redact MAC addresses, physical port paths
and numeric route/lobby fields; raw captures and images remain ignored local
files with exact hashes. Historical failed acceptance records are preserved.
