# Doom embedded touch + factory-audio composite

This directory now contains two deliberately separated artifacts. The exact
E5 touch-only image remains installed and its immutable evidence is preserved.
The working source is an E6 sound-enabled successor; it does not modify the
frozen E1-E4 applications or the pinned Doom engine. E6 is not flashable while
its exact artifact, exact-unit audio release, capture, and rollback route are
under independent review.

`architecture-inventory.json` binds the exact E1-E4 main/CMake inputs reviewed
for this design. The app-local source-contract test re-hashes those inputs,
proves the narrowly scoped display/touch baseline and rejects USB components.
E6's source-bound runtime bytes are `1/1/1`; a successful build does not grant
execution or flash authority.

The preserved baseline is E1: exact local shareware WAD validation, the
read-only single-file VFS, `doom_video`'s `0x00RRGGBB` to RGB565 conversion,
the proven 320x200-to-1024x600 display path, and the unmodified Doom engine.
USB is excluded from the component and managed-dependency graph.

## Runtime architecture

### E6 successor under review

E6 retains E5's display, WAD, video, multi-touch, overlay, and no-USB paths.
Every mutating/control/data audio call goes through the app-local counted and atomically serialized
`platform_audio` adapter before reaching `platform_audio_factory`. The adapter
fails a concurrent recovery call closed instead of racing the worker, exposes
monotonic post-policy nonzero-frame/sample/maximum-absolute-magnitude
witnesses, and publishes a start-time GPIO30-low proof without main-task reads
of the backend's non-atomic state.
Thread-safe coherent telemetry snapshots are read-only, hardware-free, and tracked
separately from the mutating/control/data invocation counter.

The selected hardware shape reproduces the complete pinned vendor audio
initializer in order: I2S0 PDM RX at 16 kHz (GPIO24 clock, approximately
1.024 MHz, GPIO26 input) is created and enabled first, then I2S1 speaker TX at
16 kHz signed PCM16 stereo uses LRCLK 21, BCLK 22, and DOUT 23. I2S1 TX MCLK
is unused; that statement does not mean GPIO24 is quiet, and board strapping
may carry the PDM clock onto a codec-MCLK net. No external codec I2C
transaction is made. Doom never consumes the PDM microphone samples.

GPIO30 is active-low. The first audio hardware call latches it high and proves
the pad readback. Backend start then re-primes all 1,536 TX DMA frames with
zero, makes exactly one initial low request/readback, measures at least 350 ms
of zero clocks, and performs the second low readback before RUNNING. Backend
volume step 10/10 is exact bit-for-bit PCM16 unity with no amplification; the
range is `[-32768,32767]` and maximum absolute magnitude is 32768. Music stays
disabled.

Normal cleanup stops and joins the Doom audio worker before stop/safe/destroy.
A sound-init failure that returned the runtime to BOUND is accepted only after
direct unbind proves it is stopped. Timeouts and cleanup failures retain the
single owner. Silent Doom is permitted only when READY_MUTED, complete zero
DMA, and GPIO30-high safety are positively proven; otherwise
`AUDIO_SAFETY_FAULT` dark-halts and retries safe recovery.

Before any E6 build replaced the build output, the exact installed E5 binary
and its padded write span were sealed in the private 0700 rollback bundle
`test-runs/doom-e6-rollback-2026-08-13`. The 4,898,400-byte artifact has SHA-256
`68e97df1e89c28428d6a66c2ca4ab26c4eade76ff9357479f80d01ebc08609c8`;
the 4,898,816-byte padded span has SHA-256
`9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9`.
Any future authorized E6 installer must additionally preserve the live full
E6 mutation span on its retained UART handle and restore/read it back after
any post-mutation failure.

### Installed E5 record

The reviewed persistent-demo artifact is source-bound to exact runtime bytes
`1/1/0`: composite and touch are enabled while audio remains locked. This mode
makes no audio API calls, leaves GPIO30 untouched so the board's R71 10K
hardware pull-up retains shutdown, and runs Doom with `-nosound -nomusic`.
The frozen factory backend remains linked and build-tested so a future,
separately reviewed audio artifact has a stable seam; it cannot run from this
image because the independent audio byte is zero.

The active touch-only owner order is:

1. Make no audio call and leave GPIO30 untouched.
2. Initialize the proven display path, which owns LDO3 and LDO4.
3. Create exactly one I2C1 master bus on GPIO45/GPIO46.
4. Lend that bus to `platform_touch`.
5. Initialize the read-only WAD, video adapter, touch-input model, and Doom
   engine; enable the backlight only after the first black frame succeeds.
6. On exit or failure, attempt audio/touch/shared-bus release, darken the
   display, then deinitialize video, display, and the read-only blob VFS in
   that order; retain any owner whose release cannot be proven.

Touch samples use the component's bounded 1024x600 multi-contact snapshot.
The pure `doom_touch_input` component owns contact-to-key mapping, event queue
semantics, and overlay primitives. The app owns only the runtime polling,
frame composition, and service lifecycle.

The 1024x600 overlay mapping is fixed. The lower-left neutral-gray D-pad is
centered at `(180,445)`. On the lower-right, yellow Strafe is at `(640,430)`,
cyan Use at `(742,486)`, yellow Run at `(770,350)`, and red Fire at
`(900,455)`. The neutral-gray top row, left to right, is Map `(80,68)`, Back
`(178,68)`, Previous weapon `(625,68)`, Next weapon `(723,68)`, Accept
`(840,68)`, and Pause `(945,68)`. Active controls brighten in place.

Display, immutable-WAD, and video failures are the only dark-halt failures.
Shared-I2C and GT911 create/poll failures degrade without stopping Doom: touch
input is neutralized immediately, the overlay remains visible, and the image
always runs `-nosound -nomusic`. Touch reinitialization is permitted only at a
bounded interval after both prior-client cleanup and bus ownership are
positively proven.

## Current classification

Hardware-tested for exact app-partition installation and sustained touch-only
steady state on the bound 10 in variant. Full-project flash, USB, audio
runtime, and every unrelated peripheral remain denied. The guarded app-only
route rebuilt and verified the exact artifact, skipped all standalone live
probes, then retained one exclusive UART
descriptor for its authoritative identity/revision/security/16 MiB checks,
factory-partition validation, one pinned RAM-stub write, and ordered full-span
readback. The write covers 4,898,816 bytes (the 4,898,400-byte artifact plus an
exact 416-byte erased tail). One stub `flash_finish(false)` synchronizes the
write without launching. Only after the aggregate readback matches does a
direct hash-bound RTS-only hard reset launch the app once, with DTR held false.
Every physical flash-ID read is bound to exact JEDEC low 24 bits `0x1840c8`.
The pinned stub can return raw `0x001840c8` or the equivalent
high-byte-padded `0xff1840c8`; both canonicalize to `0x1840c8`, while every
other raw value fails closed.
The transaction never reopens the UART and never invokes esptool `run` or a
soft reset. This
artifact repeats `composite_gate=1 touch_gate=1 audio_gate=0 audio_calls=0
gpio30=untouched` in every periodic `P4_DOOM_E5 STATS` record, so a late
receive-only serial attachment can still prove the persistent mode and GPIO30
safety state. The first authorized hardware attempt reached the pinned RAM
stub and then failed closed on the high-byte-padded RDID before `flash_begin`:
zero application bytes were written, the partition was unmodified, and the
application was not launched. The corrected guarded attempt then installed the
4,898,400-byte artifact, verified all 4,898,816 padded bytes in ten bounded
chunks (SHA-256 `9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9`),
and launched it exactly once.

A receive-only late attachment captured frames 300 through 600, matching
submission/completion counts, zero video failures/timeouts, and successful
GT911 poll progress from 288 to 588 with zero poll failures. Every periodic
record reported gates `1/1/0`, zero audio calls/frames/failures, and GPIO30
untouched as derived from the sole counted audio API path; this was not a
direct electrical pad measurement. No USB-runtime or fault marker appeared,
and USB remains disabled/absent from the firmware graph rather than physically
tested. Because the attachment missed startup
and no person-contact observation was recorded, this does not yet qualify
touch coordinates, multi-touch behavior, control mapping, or visible gameplay
response. Sound was disabled and remains untested by this image. See
`hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json`.
