# Arena 0.80 checkpoint — 2026-10-07

The owner requested a source checkpoint and a stop after the current flash check,
so other merges can proceed. This checkpoint is not a multiplayer release.

Both Tab5 cards contain the compact 17-file Pure Hades/DWANGO5 bundle, with
device SHA-256 verification. The Arena base is 12,253,462 bytes. Firmware 0.79
still passed the original 28,787,748-byte size into startup VFS registration;
0.80 now uses the generated compact size, identity and digest. The engine's
`freedoom2.wad` virtual filename remains intentional.

Tab5 A (ST7121) was flashed with 0.80 using the guarded app-only installer.
Device checksum, launcher boot and OTA health checks passed. A Local Wi-Fi
host room then reached Arena gameplay. Runtime logs confirm an actual
768×480 engine and video-worker surface. The brief stationary capture measured
approximately **13.4 rendered FPS**, below the 30 FPS release floor. Codec and
music telemetry were active; nobody has confirmed audible music and effects
for this artifact. The old startup failure remains in the historical evidence.

Tab5 B (ST7123) remains on 0.79. Its compact card content is verified, but it
needs 0.80 or a successor before matching two-device testing. Guest join,
leave/rejoin during a running match, sustained performance, physical input
latency, and score/break dismissal still require final device acceptance.
Full SD reformat was not performed; no external card reader was available.

The pinned firmware build and verifier passed. The startup regression runs
actual production declarations against the production VFS under ASan/UBSan.
Content, USB transfer wire, Doom source/patch provenance and metadata checks
passed. Seven nonfunctional vendor trailing spaces remain in the verified
Doom patch; no source rewrite was made after the tested build.

Exact local artifacts, unit binding, flash receipt, captured counters and
remaining acceptance are recorded in
[`2026-10-07-tab5-080-a-startup-checkpoint.json`](../hardware/test-runs/2026-10-07-tab5-080-a-startup-checkpoint.json).
WADs, firmware binaries, backup images and raw device logs remain local.
Both USB readers are closed and automated work is stopped at this checkpoint.
