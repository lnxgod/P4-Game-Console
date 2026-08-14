# GT911 touch diagnostic

This build-only diagnostic initializes the proven display path, borrows the
reusable shared I2C1 service, and mirrors Elecrow Lesson05 GT911 setup: 400 kHz,
active-low GPIO40 reset/GPIO42 address latch, primary 0x5d and a properly
cleaned 0x14 fallback. Runtime remains polling-only and registers no ISR.

The screen shows a grid plus a distinct crosshair and X/Y coordinate line for
each of up to five simultaneous contacts. Serial emits `P4_TOUCH T1 START`,
`READY`, `CONTACT`, `READ_FAIL`, and `HEARTBEAT` markers. Audio, storage, and
USB host are absent.

`flash_authorized` and both repository flash modes remain false until an exact
artifact verifier and hardware acceptance plan are reviewed.
