# Changelog

All notable changes to the cumulative feature branch are documented here.

**Selected production baseline: Console OS 0.5.12.** Versions 0.5.13 through
0.5.15 are retained below as rejected/not-accepted experiment history.

Device-specific install wrappers, exact-unit authorizations, and raw UART
transcripts are intentionally omitted from the public branch. The sanitized
hardware and software validation outcome is recorded in
`hardware/evidence/waveshare-console-os-0.5.12-public-validation-20260905.json`.

## [0.5.15] - 2026-09-04

### Changed

- Staged a full-performance scrolling candidate that uses a bounded CPU row
  copy for zero-rotation native Home damage, avoiding the per-frame cache
  maintenance cost observed in the 0.5.13/0.5.14 compositor path.
- Preserved native 768x480 presentation, the optional BBS surface, and all
  SD-loaded game paths. The candidate does not change GT911 configuration.

### Validation

- The diagnostic-enabled image passed the exact unit-3 app-only install and
  readback, followed by the complete retained-UART startup gate.
- Two interaction captures produced no complete gesture marker. They remain
  only in the private device work record and must not be used as latency
  measurements.
- A diagnostic-free 0.5.15 image was built and verified, but was not flashed
  after the operator reported that 0.5.12 was better.
- The exact rollback chain 0.5.15 -> 0.5.14 -> 0.5.13 -> 0.5.12 passed, and
  the restored 0.5.12 startup capture passed.
- 0.5.15 is rejected and not accepted. The restored 0.5.12 image remains the
  operator-confirmed baseline.

## [0.5.14] - 2026-09-04

### Added

- Added a diagnostic-only, per-gesture latency record for native Windows Home
  scrolling. It accumulates corrected GT911 report cadence and age, late-latch
  use, compositor/fallback selection, driver GDMA/DPI frame-boundary latency,
  and bounded render/submit phases in RAM.
- Deferred its single compact serial record until at least 250 ms after the
  gesture ends without another contact and a later driver frame-boundary
  callback. There is no logging in the active drag or display ISR, and ordinary
  builds keep the diagnostic disabled.
- Added an explicit build input so ordinary builds force the diagnostic off and
  investigation builds opt in with
  `P4_CONSOLE_SCROLL_GESTURE_LATENCY_DIAGNOSTIC_BUILD=ON`.

### Validation

- The diagnostic-enabled Waveshare object build and Console Shell host tests
  pass. Exact USB-host build, static verification, guarded install/readback,
  retained-UART startup, and the interactive trace remain pending.
- This candidate intentionally makes no latency-improvement claim. It retains
  0.5.13's full-resolution UI, BBS/game paths, and controller configuration.

## [0.5.13] - 2026-09-04

### Changed

- Retained the native 768x480 shell and unchanged BBS and game presentation
  contracts. Home drag anchoring now rebases at the hard scroll limits, so a
  held contact does not accumulate an unreachable displacement.
- Added meaningful touch-state notification wake/coalescing while preserving
  the absolute 60 Hz cadence. A post-fence late latch can consume the newest
  active Home-drag sample before presentation.
- Added native active-to-inactive integer-only PPA scroll-region composition,
  with exact overlay redraw and a conservative full-frame fallback whenever
  composition is not provably safe.
- Kept periodic runtime statistics disabled by default. The 0.5.12 operator
  check found that the reported periodic 3--4 second blip is gone, but
  continuous scrolling lag remains.

### Validation

- Console shell, platform touch, platform display, Console OS host, environment,
  and exact Waveshare USB-host build/verifier checks pass. Broad `make check`
  reaches only the historical dormant Doom E5 metadata assertion after the
  relevant suites pass.
- The guarded unit-3 route preserved exact 0.5.12, installed and independently
  read back all 1,884,160 authorized bytes, then passed a 60-second retained-
  UART startup gate with the already-original GT911 filter-8 baseline, one
  boot, zero periodic statistics bursts, and no rejected runtime markers.
  The operator reports that the requested touch-follow latency is not fixed;
  0.5.13 is therefore rejected as a latency fix. Scroll smoothness, visual,
  BBS, and representative-game acceptance remain pending.

## [0.5.12] - 2026-09-04

### Changed

- Rejected GT911 normal-filter 4 as an operator tuning result: scrolling was
  less smooth and touch-follow latency was unchanged.
- Restored the exact unit-3 sealed filter-8/checksum-`0x79` baseline. The
  guarded path accepts only exact filter-4/checksum-`0x7d` or already-original
  state, performs one full-block write with `Config_Fresh=1`, reads back the
  complete block, and fails closed without retrying on any mismatch or error.
  The controller update is not power-loss atomic, and rolling firmware back
  does not itself roll back GT911 configuration NVM.
- Disabled the synchronous three-second runtime-statistics burst after captures
  showed 132--134 ms main-loop stalls (about eight frames). Diagnostic builds
  may opt in with `-DP4_CONSOLE_RUNTIME_STATS_BUILD=ON`.
- Added 50 mV hysteresis to the battery shell display. Raw millivolt changes no
  longer dirty Home, avoiding five-second noise redraws; Power detail still
  exposes the raw millivolt reading.
- Preserved full-resolution 768x480 presentation and all game surfaces.

### Validation

- Host tests and the exact USB-host build verifier pass. A first guarded route
  rejected a smaller-image tail-sector mismatch and automatically restored and
  reverified 0.5.11. The replacement full-span route then installed 0.5.12 and
  read back all 1,884,160 authorized bytes exactly.
- Retained-UART startup proved the single filter-4/checksum-`0x7d` to original
  filter-8/checksum-`0x79` restoration. A second boot proved the restored block
  persisted and took the no-write path. Both 60-second captures contained zero
  periodic `STATS` records, resets, panics, or rejected runtime markers.
- The operator confirmed that the periodic 3--4 second stutter was gone, while
  continuous touch-follow and reversal lag remained.

## [0.5.11] - 2026-09-04

### Changed

- Added a unit-bound, fail-closed GT911 filter experiment based on the sealed
  0.5.10 controller snapshot. It changes only register `0x8050`'s low six
  normal-filter bits from 8 to the first staged value 4; first-filter bits,
  report period, movement thresholds, debounce, geometry, and every unknown or
  reserved byte remain exact.
- Guarded the controller write with the captured product, firmware, resolution,
  configuration version, known identity bytes plus the vendor byte bound by an
  immediate readback, full 186-byte baseline, and checksum. The complete block
  is written once with a recalculated checksum and `Config_Fresh=1`, then reread
  and compared byte-for-byte after application.
- Added immediate original-block restoration and verified readback after any
  write or candidate-verification failure. An identity or baseline mismatch
  refuses the experiment before the first write and leaves touch fail-closed.
- Documented that an equal-version changed GT911 configuration is saved by the
  controller, so successful tuning can persist across reset and the
  candidate/restore sequence is not atomic against power loss. The complete
  original block remains sealed in the repository for an explicit recovery
  build; a successful experiment is not silently reverted by older app images.

### Validation

- Platform-touch host tests, the exact Waveshare build, static verifier, guarded
  unit-3 install/readback, retained-UART tuning gate, and reset-persistence
  check pass. The operator latency/jitter test remains pending; no user-visible
  improvement is claimed.

## [0.5.10] - 2026-09-04

### Changed

- Corrected GT911 sample timestamps so repeated cached coordinates retain the
  controller report time instead of appearing newly acquired on every 120 Hz
  mailbox poll. Neutral frames remain valid indefinitely and do not trip the
  contact-age guard after an idle period.
- Added a read-only startup snapshot of the GT911 identity and complete
  186-byte configuration block, including checksum validation and decoded
  report-rate, debounce, filter, and coordinate-threshold fields. No touch
  configuration register is written by this candidate.
- Added unique controller-report cadence to the existing bounded mailbox
  telemetry so controller filtering can be separated from application polling
  and display presentation time.

### Validation

- Platform-touch host coverage proves that a data-ready report receives a new
  timestamp, a repeated driver snapshot preserves it, a report arriving across
  the status/read race is detected by changed contents, and status-read errors
  fail closed.
- The exact Waveshare build and static verifier pass. The guarded unit-3
  app-only install and complete mutation-span readback pass, followed by one
  retained-UART start with every required marker and no panic, reboot, display
  timeout, or accelerator-failure marker.
- The checksum-valid exact-unit capture reports GT911 firmware `0x1060`,
  configuration version 65, a 10 ms report period, zero X/Y movement
  thresholds, and normal filter 8. The complete original 186-byte block is
  sealed as recovery evidence before any tuning experiment. An operator-visible
  latency result is not expected from this read-only diagnostic revision.

## [0.5.9] - 2026-09-04

### Changed

- Kept the accepted full-resolution 768x480 Windows shell and every game
  presentation contract while moving launcher drag math to raw physical touch
  coordinates. A drag now starts after four physical pixels and every
  one-pixel reversal updates the fractional scroll position without waiting
  for another 320x200 logical-coordinate step.
- Added a Waveshare-only 120 Hz latest-sample GT911 worker. It continuously
  acknowledges controller reports into a one-frame mailbox while the launcher
  is busy drawing or presenting, so the next UI iteration consumes the newest
  sample instead of a queued history.
- Preserved game input behavior by stopping and joining the launcher sampler
  before native, script, or Doom touch ownership begins, then restarting it
  when returning to the launcher. A bounded stop timeout fails closed rather
  than permitting two touch readers.
- Added passive touch-to-refresh telemetry that distinguishes partial and full
  shell presents and reports sample age, transform, handoff, replay, and
  confirmed-refresh timing without logging in the per-frame hot path.

### Validation

- The 0.5.8 live reversal trace established the baseline: sustained native
  scrolling presented at roughly 26--29 ms per changed frame, including about
  14--16 ms of cached rendering and 10 ms of partial display transfer, with no
  display timeout or accelerator failure. That leaves touch report freshness
  and coordinate quantization as the bounded targets for this revision.
- Console Shell and platform-touch host suites pass, including explicit
  four-physical-pixel activation and one-physical-pixel reversal coverage.
- The exact Waveshare build and static verifier pass. The guarded unit-3
  app-only install and complete mutation-span readback also pass, followed by
  one retained-UART 0.5.9 start with all required markers and no display
  timeout, accelerator failure, panic, or reboot marker.
- During the interactive capture the 120 Hz mailbox had zero read failures,
  no stale samples, and a worst observed sample age of 8.849 ms. Sustained
  changed samples reached a confirmed refresh in about 39.8--39.9 ms on
  average; handoff-to-refresh averaged about 8.6 ms and never exceeded one
  16.7 ms panel interval. The final cumulative sample contained 333 partial
  and 11 full interactive presents.
- Operator testing found the same perceptible finger-follow and direction-
  reversal delay as 0.5.8. The fresh-mailbox hypothesis is therefore rejected
  as the primary cause, and 0.5.9 is not the accepted latency fix. Controller-
  reported coordinate timing and the final scanout phase remain to be
  isolated; representative game acceptance was not repeated for this
  diagnostic candidate.

## [0.5.8] - 2026-09-04

### Changed

- Reduced launcher drag recognition from five to two logical pixels and applied
  the complete finger displacement on the first drag frame, removing the
  apparent second dead zone at gesture start.
- Deferred the transient tile press redraw while touch input is still deciding
  between a tap and a scroll, and retained cached row shifting while a held
  drag crosses an integer row boundary.
- Added conservative dirty-region metadata to the native 768x480 Windows Home
  renderer. The Waveshare display backend now rotates and presents only that
  changed source band after each DSI framebuffer has an authoritative base.
- Coalesced overlapping dirty history when alternating between the two panel
  framebuffers; the backend falls back to one full transform whenever replay
  would cost at least as much as the complete 768x480 source.
- Kept BBS pages, structural shell frames, and every 320x200 or negotiated
  768x480 game on their existing full-frame presentation contracts.

### Validation

- Host tests compare consecutive native Home frames and prove that reported
  regions cover every changed pixel during fractional motion, row/status
  transitions, scrollbar press/release, and thumb dragging.
- Host Console Shell, Console OS, and Waveshare display-layout suites pass;
  the exact Waveshare USB-host build and static verifier pass.
- The exact unit-3 app-only install and complete mutation-span readback pass,
  followed by a retained-UART startup gate with one 0.5.8 start, the native
  content and partial-present markers present, and no display timeout, display
  failure, accelerator failure, panic, or reboot marker.
- Idle startup reports an authoritative full shell render at about 40.6 ms and
  transform at about 15.0--15.5 ms. The partial-submit counters correctly
  remain zero without an interactive scroll, so sustained dirty-region timing
  and operator-visible scrolling/game acceptance remain pending.

## [0.5.7] - 2026-09-04

### Changed

- Returned the Waveshare Windows shell to a native 768x480 RGB565 source and
  persistent framebuffer while preserving each game's existing 320x200 or
  negotiated 768x480 geometry.
- Added cached scroll presentation: tile rows shift in place, the exposed band
  is redrawn, and scrollbar/footer updates are clipped. Settled endpoints
  redraw the complete tile viewport; invalidated or structural frames use an
  authoritative full-frame redraw.
- Restored refresh-synchronous handoff for both default 320x200 and negotiated
  768x480 native games while retaining the pipelined shell handoff.

### Validation

- Added host coverage for cache shifts, reverse scrolling, exposed-band
  redraw, static-pixel preservation, cache invalidation, and convergence to a
  full native render.
- The exact unit-3 app-only install and complete mutation-span readback pass,
  followed by a retained-UART startup gate with the native 768x480 content and
  refresh-synchronous game-handoff markers present.
- Startup telemetry reports zero display timeouts, display failures, or
  accelerator failures. An authoritative full shell frame remains about
  40.3-40.6 ms to render and 15.1-15.5 ms to transform; the cache counters are
  intentionally idle until interactive scrolling begins.
- Operator-visible sustained-scroll, font/folder, tearing, and representative
  320x200/768x480 gameplay acceptance remain pending.

## [0.5.6] - 2026-09-04

### Added

- Expanded the Console OS launcher and native cartridge catalog for the full
  removable-storage library.
- Added independent persistent Boot Sounds and Game Audio controls from 0–10,
  with safe zero defaults and one-time migration handling.
- Added persistent battery status and a bounded voltage-derived percentage
  estimate to the Waveshare launcher and Power panel; it is not fuel-gauge or
  charge-state telemetry.
- Added physical-local H1 control of the eject-gated, no-format H2 USB Drive
  mode while preserving filesystem ownership and clean-eject requirements.
- Added bounded frame, transform, handoff, refresh, and storage telemetry for
  on-device acceptance of shell cadence and interaction behavior.

### Changed

- Moved the fractional 60 Hz UI and audio scheduler to the 1 kHz FreeRTOS
  clock, with 16/17 ms frame phases and matching 266/267-frame audio cadence.
- Ordinary Waveshare Windows-style shell pages now render into a compact
  384x240 source and scale exactly 2x to the established 768x480 viewport;
  the native 80x30 BBS launcher remains pixel-exact at 768x480.
- Improved launcher motion with responsive quadratic easing, bounded fling,
  missed-deadline anchor recovery, and persistent scrollbar-thumb ownership
  during touch drags.
- Pipelined the existing DSI framebuffers while retaining refresh-confirmed
  buffer ownership and the fixed shell/touch geometry.
- Corrected the compact-raster presentation path so crisp 2x shell pixels are
  preserved through the Waveshare display transform.
- Avoided redundant compact-glyph overdraw so crisp strokes do not consume
  the frame-time margin recovered by the compact shell path.

### Fixed

- Removed uneven 10/20/20 ms scheduler quantization that caused visible
  launcher judder.
- Prevented stale touch velocity and overdue wake anchors from producing
  scroll jumps or catch-up bursts.
- Fixed scrollbar-thumb capture so a held drag retains ownership; the
  operator confirmed the interaction in 0.5.4.
- Preserved catalog availability without consuming the fixed internal/DMA
  reserve by keeping bounded catalog snapshots in external RAM.

### Validation

- Host Console Shell tests and platform display layout tests cover logical
  384x240 rendering, exact 2x nearest-neighbor expansion, native 768x480
  rendering, bounds, and framebuffer ownership behavior.
- The exact 0.5.6 Waveshare build, guarded unit-3 app-only install/readback,
  and retained-UART startup capture pass with no display timeout, display
  failure, accelerator failure, panic, or reboot marker.
- The 0.5.6 startup capture measured about 10.2–10.9 ms for compact shell
  rendering and 5.8–6.0 ms for the display transform. Sustained interaction
  timing remains part of final operator acceptance.
- The operator accepted 0.5.4 scrolling and scrollbar-thumb dragging and
  reported that 0.5.5 improved the font while leaving some compact-raster
  artifacts. Final 0.5.6 visual and sustained-interaction acceptance remains
  pending.

### Notes

- This entry summarizes the cumulative feature branch; intermediate 0.5.x
  candidate labels are intentionally omitted.
- The version remains a feature-branch release note and does not by itself
  claim a completed production hardware release.
