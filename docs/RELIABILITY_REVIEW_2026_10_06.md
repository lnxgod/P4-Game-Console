# Reliability and security review — 2026-10-06

The review found and fixed four issues in the upload and BLE-controller
boundaries. The changes are local on `codex/reliability-performance-review`,
based on cleanup commit `a89342dc543c4ce05c6d2503b9fea6556064cade`.
Implementation commits are `08966f1fbc2fee06f5a7cc7c847b08ed911babe5`
and `0dfc4b0cff06003f28a1d9c2c5f813be3704e0f3`.
The source checkout and canonical cleanup branch were not changed.

## Fixed findings

| Priority | Finding and trigger | Change and evidence |
| --- | --- | --- |
| P1 | Uploading an exchange file such as `NOTE.TXT` could delete or rename another valid user file named `NOTE.TXT.P4T` or `NOTE.TXT.P4B`. The transaction suffixes occupied the same namespace as user files. | Exchange staging and backups now live under `TRANSFER/.P4FT/`. A public wire-protocol regression uploads both suffix filenames, creates/replaces the base file, and checks all three contents. It failed on the baseline and passes after the fix. |
| P2 | A media `fsync` failure lost ownership of the upload descriptor without closing it. Repeated failures could exhaust file handles. Both general file upload and fixed-content upload had this defect. | A shared helper always attempts close after sync, including the error path. Fault injection through the general upload protocol proves the descriptor closes, subsequent upload works, and failed replacement preserves the old file. The same helper is used by fixed-content upload; that complete receiver was compile-tested, not separately fault-injected. |
| P2 | Waveshare BLE HID checked encryption but did not require a persistent bond. It also published a connected input model before the final security check. | Both security gates require encryption and bonding. Final peer-identity authorization now precedes model publication. Confirmed against the pinned NimBLE security-state implementation and compiled in the Waveshare image. Physical pairing acceptance remains pending. |
| P2 | A BLE repeat-pairing event automatically deleted the existing peer bond and retried. Failure also left the last input snapshot active until asynchronous disconnect. | Repeat pairing is rejected; the user must select **Forget**, then **Pair**. Failed links neutralize input before requesting termination. The existing selected-controller Forget path remains in use. Compiled in the Waveshare image; repeat-pairing and rejection timing need controller testing. |

These BLE findings are violations of the repository's bonding and input
lifecycle contracts. No over-the-air exploitation claim is made.
Tab5 does not currently compile the BLE-controller component; those changes
apply to Waveshare. General upload fixes are included in both images.

Old firmware may have left ambiguous `.P4T`/`.P4B` files in `TRANSFER/`.
The new receiver preserves those files rather than guessing whether they are
user data or interrupted transactions. See [content behavior](CONTENT_LIBRARY.md)
and [controller recovery](CONTROLLERS.md).

## Scope and verification

Manual review covered upload parsing/activation, native package and resource
validation, verified storage reads, OS update staging, USB/HID descriptor bounds
and disconnect snapshots, BLE controller authorization, audio worker ownership
and teardown, and Tab5 display buffer reuse/cache ownership. This was a focused
review of higher-risk boundaries, not a line-by-line audit of all applications,
third-party dependencies, network services, or game rules.

The final host results include **31 distinct CTest cases**, **40 Python tests**,
and a separate **ThreadSanitizer audio concurrency run**:

| Area | Passed cases | Evidence under ignored `build-host/reliability-review/` |
| --- | --- | --- |
| Transfer/control protocol | 5 CTest + 28 Python | `focused-after.log` |
| Audio session/worker/concurrency | 3 CTest | `focused-after.log` |
| Storage and verified readers | 6 CTest | `core-host.log` |
| Game package validation | 1 CTest | `core-host.log` |
| OS update package validation | 2 CTest | `core-host.log` |
| Controller parser, XUSB, USB lifecycle, snapshots, radio handoff, Doom adapter | 10 CTest + 12 Python | `gamepad-host.log` |
| Display layout and frame queue | 4 CTest | `display-host.log` |
| Audio concurrent stats, bounded submissions, stop generations, write failure, teardown/reopen | 1 ThreadSanitizer program | `audio-concurrency-tsan.log` |

The new audio test exercises twelve worker lifecycles with a concurrent stats
reader. Readers and producers stop before teardown, as required by the ownership
contract. No ThreadSanitizer race was reported in that run. Existing sanitizer
suites retain AddressSanitizer/UndefinedBehaviorSanitizer coverage as configured.
This does not establish race freedom on FreeRTOS, DMA, interrupts, or the radio.

The two upload regressions failed on the original production implementation
(`transfer-before.log`): the failed-sync descriptor was still open, and the
suffix-file collision prevented an unrelated target upload. Both pass after the
fix, including failure of a replacement upload without changing the old target.

The initial default-Python invocation lacked `pyserial`; all Python cases passed
using the existing pinned IDF Python environment. The original audio throughput
test also failed a queue-admission timing assertion under ThreadSanitizer
instrumentation, with no race diagnostic. It passes under its normal sanitizers;
the new concurrency test avoids real-time throughput assertions and passes under
ThreadSanitizer. Neither initial failure is represented as a passing run.

## Firmware results and remaining acceptance

- **Tab5:** build and full candidate verifier pass; 17 native cartridges verified.
  The image was built at `08966f1fbc2f`. The subsequent commit changes only the
  Waveshare BLE-controller source, excluded from Tab5's compilation database.
- **Waveshare controller-first:** image compiles and links at `0dfc4b0cff06`,
  including the changed BLE-controller translation unit. The complete verifier
  **fails** at its hard-coded `PROJECT_VER "0.42"` condition while the app is
  version `0.56`. The verifier, app CMake file, and main CMake file are byte-for-byte
  unchanged from the cleanup baseline; runtime statistics are still disabled by
  default. Later verifier checks are not claimed as passed. Existing factory-audio
  and vendored Doom compiler warnings remain outside these changed files.
- No device was flashed or tested. SD power-loss recovery, BLE first pair/reconnect,
  refusal of unbonded links/repeat pairing, immediate neutral input on rejection,
  and named-controller compatibility remain hardware acceptance work.
- No toolchain/component locks, board authorizations, factory recovery evidence,
  native gameplay, game identities, save formats, or release version were changed.

[Machine-readable evidence](../test-runs/2026-10-06-reliability-review.json) records
artifact sizes/digests, source revisions, command results, and local log hashes.
The Waveshare verifier failure is retained; it was not bypassed or weakened.

## Performance candidates, not measured improvements

1. `validate_native` in `components/p4_usb_content_transfer/src/file_transfer.c`
   allocates the entire package/resource and rereads it after a full-file hash.
   A resource can be 8 MiB. A bounded streaming resource validator could lower peak
   PSRAM use and validation latency, provided payload, table, and block-digest checks
   stay intact. First capture allocation high-water marks and activation timings
   for representative large `.P4R` files on SD.
2. Tab5 partial UI redraws still invalidate/write back full 720×1280 RGB565
   destination buffers. Cache operations over the actual aligned destination
   regions might help scrolling, but must preserve DMA coherence, damage replay,
   and the retired-buffer fence. Measure transform/handoff times and frame cadence
   before changing this conservative path.

No FPS or device-latency gain is claimed. Preserve the 60 FPS target and actual-device
30 FPS release floor; host test speed does not qualify either. Audio queue,
underrun, stack-reserve, and display timing counters already provide suitable
instrumentation for a later exact-device acceptance run.
