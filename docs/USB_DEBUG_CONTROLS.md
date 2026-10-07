# Tab5 USB debug controls

The Tab5 Console OS exposes bounded synthetic touch and button input through its
native USB serial port. This lets a developer drive the actual launcher and game
input paths while preserving both units' serial logs during a local Wi-Fi match.
The service reports textual state and can capture device-submitted launcher and
Doom pixels, including Arena's SCORE overlay.
A framebuffer capture does not establish that a person saw or heard a result.

The session is available only over the connected USB cable. Opening a debug
session permits input for 30 seconds; accepted new control commands refresh
that lease. Each input expires within one second, even if the host disappears.
`status` reads state without opening or extending a session. CRCs and session
numbers provide framing and stale-command protection, not authentication.

Only one process should own each serial port. Keep its connection open throughout
the test: native USB port opening can cause a warm reset on some host/device
states. Do not run a serial monitor, content upload, flash operation, USB relay,
or another debug client on the same port during a test. Use local Wi-Fi for the
two-console match. Arbitrary interleaving with USB content-transfer or serial
relay protocols is not supported.

## Persistent command line

Use the pinned ESP-IDF Python environment, which includes `pyserial`. The
separate host-test environment does not include the serial dependency. Select
the current live port for the intended unit; these examples do not identify a
unit. Another Python 3 environment also works when `pyserial` is installed.

```sh
.tools/espressif/python_env/idf5.5_py3.12_env/bin/python scripts/p4-debug-control.py \
  --port /dev/cu.usbmodemPORT \
  --log build-host/debug-unit-a.rx --show-log --interactive
```

Send one JSON object per stdin line. Stdout returns one JSON result for each
command, including the optional caller `id`. `--show-log` writes firmware text
to stderr; the required `--log` file always appends every received byte, including
binary responses, while the client is idle or waiting for input. The CLI creates
owner-only logs which can contain device identifiers; redact those before
publishing evidence.

```json
{"id":"state","command":"status"}
{"id":"menu-tap","command":"tap","x":640,"y":360,"hold_ms":120}
{"id":"move-fire","command":"buttons","buttons":"up+a","hold_ms":500}
{"id":"stop","command":"release"}
{"id":"menu-image","command":"screenshot","path":"build-host/menu.png"}
{"id":"menu-preview","command":"screenshot","path":"build-host/menu-small.png","scale":4}
{"id":"end-debug","command":"close"}
```

Coordinates address the physical 1280 × 720 landscape display: `x=0..1279`,
`y=0..719`. `tap` presses then explicitly releases and waits for the requested
hold plus a 200 ms neutral interval after release. That interval keeps chained
taps separate while the foreground input loop catches up with USB replies.
`hold` and `buttons` return after the device acknowledges the hold, which
then expires automatically. Every hold replaces the previous synthetic input;
it does not accumulate button presses. Durations must be 1..1000 milliseconds.

A successful command reply acknowledges the debug protocol state, before the
application necessarily consumes it. The neutral interval is pacing, not proof
that a menu action completed. Blocking loading work can exceed that interval;
check the expected status or firmware log before sending the next action.
Low-level `release()` returns immediately after its reply, so scripts chaining
their own `hold()`/`release()` calls must provide their own neutral interval.

`hold` permits simultaneous touch and buttons:

```json
{"command":"hold","touch":true,"x":400,"y":300,"buttons":"a","hold_ms":200}
```

Button names map to the normal P4 input bits:

| Name | Mask |
| --- | ---: |
| up | 1 |
| down | 2 |
| left | 4 |
| right | 8 |
| a | 16 |
| b | 32 |
| start | 64 |
| back | 128 |

Use names joined by `+` or commas, a JSON integer, or a numeric string such as
`"0x11"`. Game-specific actions follow each game's regular button mappings.
Input automatically opens a new session when needed. `open` may be sent
explicitly. `close` ends the debug session while leaving the serial reader
running, so later `status` commands and logs remain available. EOF or Ctrl-C
attempts release and close before disconnecting. A killed process or unplugged
cable falls back to device input/session expiry.

One-shot reads or taps are also available, but persistent connections are
preferred when preserving a running match:

```sh
.tools/espressif/python_env/idf5.5_py3.12_env/bin/python scripts/p4-debug-control.py \
  --port /dev/cu.usbmodemPORT --log build-host/debug.rx status
.tools/espressif/python_env/idf5.5_py3.12_env/bin/python scripts/p4-debug-control.py \
  --port /dev/cu.usbmodemPORT --log build-host/debug.rx tap --x 640 --y 360
```

## Python test harness

`screenshot` is available while the launcher, Doom, or a native Game API game
owns the foreground and no file/content transfer is active. Loading handoffs
reject capture. It neutralizes injected input, copies the
published native 720 × 1280 RGB565 scanout after the display's existing refresh
fence, then releases the display mutex before any USB transmission. The host
rotates those pixels into a 1280 × 720 PNG using only Python's standard library.
This captures the device's submitted buffer, including its renderer, scaling,
rotation and damage updates; it is not a photograph or optical panel test.
The copy and USB transfer can affect game timing: exclude the entire capture
interval from cadence and latency measurements. Capture adds no per-frame work
when no screenshot is requested.

One immutable 1,843,200-byte PSRAM copy is retained per capture. The host pulls
at most 2,048 bytes per CRC-protected response using the same reader and serial
connection. Retries read the same image. END, a foreground handoff, a content
transfer, 30 seconds without a read, or a 120-second absolute lifetime releases
the copy. No SD file is created. Allocation, refresh-fence and transfer failures
return an error; the client does not substitute an earlier image.

Full resolution is the default (`scale=1`). An optional `scale=4` selects one
native pixel from the center of each 4-by-4 block, producing a 180 × 320 native
RGB565 image and a 320 × 180 landscape PNG. Its 115,200-byte image payload is
one sixteenth of a full capture. It is a sampled preview: use full resolution
when small text or single-pixel details matter. Input coordinates still use the
full 1280 × 720 display. The actual transfer duration must be measured; exclude
the whole BEGIN request through successful END response (including retries),
or through failure cleanup, from cadence/latency measurements at either scale.

The same full-size immutable allocation is initially captured. Quarter mode
compacts that owned copy in place after the display mutex is released; it adds
no second image allocation, render changes, or work when no capture is requested.
Response dimensions describe the sampled image exactly, and rotation remains
90 degrees clockwise. Release, transfer conflicts and lifetime limits are
unchanged. Older firmware rejects the optional flag; the client reports that
failure instead of silently transferring a full image.

On an existing `DebugClient`, use `receipt = client.screenshot(path)` to save a
PNG and get its absolute path, dimensions, capture ID, raw RGB565 byte count and
SHA-256. `client.capture()` returns the same metadata plus a `data` bytes value
for callers that need the native pixels. Neither method opens another port.
Use `client.screenshot(path, scale=4)` or `client.capture(scale=4)` to request
the smaller image; only integer scales 1 and 4 are supported. Receipts retain
the selected scale along with the actual dimensions, byte count and pixel hash.
The response scanner removes valid image frames from textual log callbacks;
the raw log continues to preserve every byte. Screenshots contain displayed
information and should be reviewed before sharing.

A standalone capture is also available when no other process owns the port:

```sh
.tools/espressif/python_env/idf5.5_py3.12_env/bin/python scripts/p4-debug-control.py \
  --port /dev/cu.usbmodemPORT --log build-host/debug.rx \
  screenshot --output build-host/menu.png
```

Add `--scale 4` to that screenshot command for a sampled preview.

The snapshot wire protocol keeps the 32-byte `P4S1` version-1 request and
`P4T1` reply layout. BEGIN byte 6 is flags: bit 0 selects quarter width and
height; all other bits must be zero. READ and END require byte 6 to be zero.
All other reserved fields stay zero. Repeating BEGIN with the same active ID
and flags returns the same immutable capture without extending its deadlines;
changing flags under that ID returns INVALID. Use a new ID for a different
capture. The unchanged response width, height, RGB565 format and rotation fields
describe either resolution, and CRC covers the same complete header/payload.

During native play, `status` returns `mode=native id=<package game ID>` using
an owned copy of the running package's stable ID. The ID is cleared on exit or
handoff. This identifies the game, not its package bytes or version: bind the
`CARTRIDGE_START` filename and the installed package inventory separately.

The importable `DebugClient` accepts an already-open serial transport with a
bounded read timeout. It owns all reads through one background thread. The
caller must not read from that transport concurrently. `open_port` sets DTR and
RTS inactive before opening, requests exclusive access, and clears `HUPCL` on
POSIX systems. It never clears the receive buffer. A host/USB driver may still
reset the device when a port first opens.

```python
import importlib.util
import os
from pathlib import Path
import time

path = Path("scripts/p4-debug-control.py").resolve()
spec = importlib.util.spec_from_file_location("p4_debug_control", path)
debug = importlib.util.module_from_spec(spec)
spec.loader.exec_module(debug)

log_fd = os.open("build-host/unit-a.rx", os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
with os.fdopen(log_fd, "ab", buffering=0) as log:
    with debug.DebugClient(debug.open_port("/dev/cu.usbmodemPORT"),
                           raw_log=log, close_transport=True) as client:
        print(client.status())
        print(client.tap(640, 360))
        print(client.hold("up+a", hold_ms=500))
        time.sleep(0.6)  # The reader keeps capturing logs here.
        print(client.status())
        client.release()
```

Use one client per physical port for a two-unit test. `status()` returns a dict
with `text`, `active`, `touch`, `buttons`, `x`, `y`, `session_remaining_ms`,
`input_remaining_ms`, and the echoed command/session/sequence/result. A serial
timeout retries the exact request bytes. A device rejection raises
`RemoteError`, whose `response` retains the decoded reply. A repeated input
request does not extend its hold. The client does not automatically replay a
rejected action under a new identity.

## Wire contract, version 1

All integers are little-endian. CRC-32 is the standard IEEE/zlib CRC over all
preceding frame bytes. Fixed bounds and reserved fields are checked before
accepting input. Logs may occur before, after, or between separate response
frames; response frame bytes are sent together by the firmware.

| Request offset | Length | Meaning |
| --- | ---: | --- |
| 0 | 4 | `P4D1` |
| 4 | 1 | Version 1 |
| 5 | 1 | STATUS=1, OPEN=2, INPUT=3, RELEASE=4, CLOSE=5 |
| 6 | 1 | INPUT flag bit 0: touch down; other bits zero |
| 7 | 1 | Reserved, zero |
| 8 | 4 | Session ID; nonzero for control commands |
| 12 | 4 | Sequence; nonzero for control commands |
| 16 | 4 | P4 button mask, only bits 0..7 |
| 20 | 2 | Touch x |
| 22 | 2 | Touch y |
| 24 | 2 | Input duration, 1..1000 ms for INPUT |
| 26 | 2 | Reserved, zero |
| 28 | 4 | CRC over bytes 0..27 |

Commands other than INPUT carry zero flags, buttons, coordinates and duration.
STATUS accepts any session/sequence, does not change accepted sequence state,
and does not renew the lease. OPEN uses a new nonzero session. Later control
commands require that active session and increasing sequence numbers. An exact
duplicate is idempotent; it does not renew lease or input duration. A changed
packet with the same sequence is rejected. A different session cannot replace
an active owner. Expired sessions require a fresh OPEN.

| Response offset | Length | Meaning |
| --- | ---: | --- |
| 0 | 4 | `P4E1` |
| 4 | 1 | Version 1 |
| 5 | 1 | Echoed command |
| 6 | 1 | Result: OK=0, invalid=1, busy=2, session=3, sequence=4 |
| 7 | 1 | Bit 0 session active; bit 1 synthetic touch down |
| 8 | 4 | Echoed request session |
| 12 | 4 | Echoed request sequence |
| 16 | 4 | Current synthetic buttons |
| 20 | 2 | Current synthetic touch x |
| 22 | 2 | Current synthetic touch y |
| 24 | 4 | Session milliseconds remaining |
| 28 | 4 | Input milliseconds remaining |
| 32 | 92 | NUL-terminated state text |
| 124 | 4 | CRC over bytes 0..123 |

Host protocol tests run without a serial device:

```sh
.tools/espressif/python_env/idf5.5_py3.12_env/bin/python scripts/tests/test-p4-debug-control.py
```

These tests prove host parsing, validation and session behavior. Device hosting,
joining, input response and game cadence require separate exact-firmware serial
evidence from the physical Tab5s.
