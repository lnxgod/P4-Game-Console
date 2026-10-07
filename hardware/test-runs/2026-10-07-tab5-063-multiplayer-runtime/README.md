# Tab5 0.63 Doom Arena runtime evidence

**Overall result: gameplay performance failed.** The operator reported lag;
device-local successful frame-submission counters averaged 14.6–15.3 FPS,
below the 30 FPS release floor. These rates include idle and quit-menu phases
and do not measure simulation tics or frames visibly displayed.

The [runtime record](../2026-10-07-tab5-063-multiplayer-runtime.json) binds the
[installation receipt](../2026-10-07-tab5-063-multiplayer-debug-install.json),
hashed local raw captures, sanitized excerpts and offline analysis tools.
The exact 4,370,048-byte application has SHA-256
`7ccb7cb2b5ba920da41e361111447cdb78afeaf8dcfa10ea9525739466664ca2`.
An ignored predecessor archive preserves that image while later builds replace
the original build output.

| Session | Host / guest | A submitted FPS | B submitted FPS | Bounded paired counter-span overlap |
| --- | --- | ---: | ---: | ---: |
| `8f67dce6` | A / B | 14.685 | 14.567 | 68.563 s |
| `a2701660` | B / A | 15.290 | 14.821 | 227.809 s |

Both sessions used Arena map 1, skill 5, two players and Local Wi-Fi. Matching
lobby and engine READY receipts, clean guest exits, host PLAYER_LEFT receipts,
continued host frame submissions and clean host exits passed the serial checks.
Validation-to-engine-ready intervals were 79.349/81.655 seconds for A/B in the
first session and 82.808/80.952 seconds in the reverse session. No engine error,
panic or video timeout/failure increment was observed in these intervals.

The paired spans use ordered raw log/reply positions and host UTC bounds;
device clocks are not interpolated or subtracted across units. Increasing local
counter series bracket the overlapping spans, but their full deltas can extend
beyond the overlap edges and are not attributed to those exact UTC slices.
The stricter requirement that every included sample's UTC bracket fit wholly
inside the shared window proves 35.906 seconds in the first session and no
positive reverse interval. Sparse command timestamps explain that narrower
result. Neither metric proves continuous simulation or physical gameplay.

Movement/fire/use debug commands were acknowledged. Visible input effects,
audio, touch and returning from a break remain unverified. No engine screenshots
were captured, no disconnected guest rejoined an active match, and the requested
scoreboard button was not part of the tested image. A fresh reversed-role session
does not establish mid-match rejoin.

The eight command errors remain in the record, including an initial boot-time
status timeout, unavailable release replies across intentional restarts, and
stale debug leases. Cleanup/EXIT/Home receipts corroborate each intentional
quit. Both units ended at Home with inactive sessions and neutral inputs. The
capture's final `ports-closed` event is 2026-10-07 05:14:36.896923 UTC; both raw
streams have zero bad debug frames and zero pending frame bytes.

Offline parser validation passed 14 paired-runtime and 6 ordered-log tests.
The saved raw captures are ignored local evidence. Published excerpts redact
MAC addresses, physical ports and route/lobby identifiers; historical 0.60–0.62
failures remain intact.
