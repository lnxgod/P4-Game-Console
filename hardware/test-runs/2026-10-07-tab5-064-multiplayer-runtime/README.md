# Tab5 0.64 Doom Arena runtime evidence

**Overall result: gameplay performance and presentation failed.** The operator
reported rubberbanding and stale black break/score boxes. Device-local successful
frame-submission counters averaged 14.999 FPS on A and 15.103 FPS on B before
their first departure/cleanup receipt. This fails both the operator's requested
25 FPS minimum (aim 30) and the repository's 30 FPS release floor. Counters include
idle and quit-menu phases and do not measure simulation tics or visibly displayed
frames.

The [runtime record](../2026-10-07-tab5-064-multiplayer-runtime.json) binds the
[installation receipt](../2026-10-07-tab5-064-multiplayer-debug-install.json),
hashed local captures, sanitized excerpts and offline analysis tools. The exact
4,374,400-byte application has SHA-256
`fa7fab63ba217e33f856bf4dc7e62e280d19e3b5d01ec320100073ab83551eeb`;
an ignored predecessor archive preserves it independently of later build output.

Session `0dabec02` used A as host/slot 0 and B as guest/slot 1, Arena map 1,
skill 5, two players and Local Wi-Fi. Matching lobby and engine READY receipts,
clean guest exit, the host's PLAYER_LEFT receipt, continued host frame submissions
and clean host exit passed the serial checks. Validation-to-engine-ready took
80.496 seconds on A and 82.678 seconds on B. No engine fatal/error receipt or
video timeout/failure increment was observed in the measured intervals. Existing
gamepad poll, hosted RPC and boot warnings remain in the evidence.

The bounded overlap of increasing local counter spans is **229.953 seconds**,
05:36:46.912766–05:40:36.865412 UTC. The parser uses ordered raw log/reply positions
and conservative host UTC bounds; it never interpolates device clocks. Full
local counter deltas can extend beyond those edges and are not attributed to the
exact overlapping slice. The stricter wholly-contained sample-bracket metric
has a zero positive-duration lower bound because timestamps are sparse. That
does not mean a zero-duration session. Neither metric proves continuous
simulation or physical gameplay.

Movement/fire, Start and score-touch commands were acknowledged. The operator's
reported visual failures remain authoritative for this result; no engine
screenshots or visible input-effect proof were captured. No reversed-host session,
active-match original-roster rejoin, or other multiplayer title was tested on
this image. Pre-terminal music counters were all zero. Nonzero/forwarded audio
backend counters do not establish audible sound, and music/sound acceptance
remains open.

B's intentional quit began at 05:40:56.929778 UTC; cleanup and restart were
confirmed, followed by Home status at 05:41:11.002061. A continued frame
submissions, acknowledged movement/fire commands after guest departure, and
averaged 16.260 FPS in the host-only post-departure sample interval. A's quit
began at 05:42:14.133011; its cleanup and Home return were also confirmed.

The cumulative phase reports estimate composition at about 13 ms and submission
at 26.6 ms per submitted frame, versus about 18 microseconds in the measured debug
phase. This supports further render/display investigation; phases can overlap,
and no isolated debug-on/off comparison was performed. Host A's final cumulative
report includes time after B's departure. The saved pre-close phase summary
exactly matches recalculation from the closed captures.

All ten command errors are retained: six lowercase `b` harness-key errors sent no
device action; two final quit-confirmation release replies were unavailable
across intentional restarts; two CLOSE calls used stale pre-restart sessions and
returned inactive, neutral state. Final STATUS independently confirms both units
at Home with inactive debug sessions and neutral inputs. Capture opened at
05:32:22.152892 UTC and closed at **05:42:37.155826 UTC**. An earlier standalone
ports-closed journal event is preserved without inferring its cause.

Both streams have zero bad debug frames and zero pending bytes. Offline
validation passed 18 paired-runtime/capture-binding tests and 6 ordered-log tests.
Published excerpts redact MAC addresses, physical ports and route/lobby numbers;
raw captures remain ignored local evidence. Historical 0.60–0.63 evidence remains
intact, including failed acceptance results.
