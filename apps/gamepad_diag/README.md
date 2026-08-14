# ESP32-P4 USB gamepad diagnostic

This app builds the production-shaped input stack but is deliberately
electrically inert. Its committed configuration prints `GAMEPAD_D1_BLOCKED` and
returns without installing USB Host or enabling the root port.

The inactive artifact retains the complete post-gate implementation. An
app-local volatile flash byte is zero in the committed build, the fallback
fixture record is invalid by construction, and `platform_usb_host` has its own
independent volatile false build-authorization byte. These runtime reads stop
O3/section GC from turning the diagnostic into a misleading preflight-only
image while all electrical gates remain fail-closed.

An active artifact has a second, independent one-shot gate. It compiles only
the SHA-256 digest of a per-artifact random 256-bit arm token. The token itself
must exist only in the host's private mode-`0600` authorization state and is
never committed, compiled, printed, or retained in capture evidence. The
controlled build input is `P4_GAMEPAD_DIAG_ARM_TOKEN_SHA256`; active builds
require exactly 64 lowercase hexadecimal digest characters and reject an
all-zero digest. Inactive builds reject that environment variable and retain
an impossible all-zero digest.

The reusable path is:

```text
platform_usb_host (P4 HS singleton + class leases)
  -> platform_gamepad_usb (HID lifecycle + bounded callback queues)
  -> gamepad_core (bounded descriptor parser + normalization)
  -> mutex-protected platform_gamepad_snapshot_t
```

The managed dependencies are exact: `espressif/usb` 1.5.0 and
`espressif/usb_host_hid` 1.2.0. Generic HID `HID_PROTOCOL_NONE` interfaces are
accepted and identified by VID, PID, interface number, and SHA-256 of the copied
report descriptor. Xbox/XInput/GIP devices are not claimed as generic HID.

The host library starts with its root data port disabled. The HID class first
registers and acquires its host lease; only then does the diagnostic explicitly
enable the root port. This ordering prevents a cold-plugged controller from
enumerating before the HID client exists. No additional class can acquire a
lease after root-port enablement.

## Bounded diagnostic session

An authorized build first stays silent for 2.5 seconds so a reset-and-monitor
runner can attach to the same UART without losing the
`GAMEPAD_D1_SERIAL_ATTACH` and `GAMEPAD_D1_BOOT` capture boundaries. It then
drains any ROM-loader/SLIP residue and emits `GAMEPAD_D1_WAIT_ARM`. At that
point USB Host, HID, HCD, and the root data port are still entirely
uninitialized.

The host may transmit exactly one frame:

```text
P4_GAMEPAD_D1_ARM gamepad-diag-d1-one-shot-authorization-2026-08-13 <token>\n
```

`<token>` is exactly 64 lowercase hexadecimal characters and the whole frame
is exactly 133 ASCII bytes including one LF and no CR. Firmware hashes the
64 token characters, compares the result to the compiled digest with a
constant-time comparison, zeroizes the received line and both digest buffers,
and rejects any byte during a 100 ms duplicate guard. Control characters,
uppercase or non-hex token characters, wrong length or authorization, excess
bytes, digest mismatch, duplicate data, and the 15-second timeout all produce
`GAMEPAD_D1_HALT stage=host-arm reason=arm-* root_data_port_enabled=0` and a
permanent no-retry suspension. No USB platform call is made on those paths.
`GAMEPAD_D1_ARM_ACCEPTED` prints only the public token digest, never the token.

Only after arm acceptance does the app start the host daemon, register HID,
and enable the P4 root **data port**. It then runs one bounded 120-second
session; it is not a perpetual USB loop. A controller may already be attached
when the image boots. The external fixture owns controller VBUS; the ESP-IDF
root-port call does not make the panel source 5 V.

The accepted controller identity is deliberately exact:

- VID:PID `0079:0011`;
- report-descriptor SHA-256
  `05a151c932362fee13503905962880ce75212bbf8c43c95701527b8290833162`;
- profile `usb-gamepad-0079-0011`;
- canonical capabilities `BUTTONS | DPAD`.

The shared transport prints `GAMEPAD_CONNECTED` with the actual interface,
protocol, descriptor length, descriptor hash, selected profile, and
capabilities. The app independently compares the published snapshot identity
and prints `GAMEPAD_D1_CONNECTED` only for the exact expected identity and
profile result. Any other accepted HID interface terminates the session with
`GAMEPAD_D1_FAIL stage=identity-profile-mismatch`.

State polling is 20 ms, but changed-state output is rate-limited to four lines
per second. A five-second `GAMEPAD_D1_PROGRESS` heartbeat reports bounded
transport counts without flooding UART. `GAMEPAD_D1_STATE` exposes the complete
canonical buttons, D-pad, axes, and triggers. The first snapshot with a pressed
button or non-centered D-pad bypasses the rate limit and is followed by
`GAMEPAD_D1_ACTIVE_INPUT`. At least one committed input report and one such
active control are required for `PASS`; neutral reports or enumeration alone
cannot pass.

Unplugging during the window produces `GAMEPAD_D1_DISCONNECTED` followed by
`GAMEPAD_D1_NEUTRAL`. Reconnecting and receiving a report completes the window
early. The result distinguishes initial enumeration, a physical disconnect,
neutralization after held input, and reconnect. Firmware cannot determine
whether the first enumeration was physically cold-plugged or attached just
after the data port was enabled, so cold-plug evidence must also record the
operator's cable timing. A reconnect bit is set only after an observed
disconnect and a new session.

On completion, failure, or timeout, cleanup is always attempted in this order:

```text
root data port disable/quiesce
  -> HID stop, disconnect neutralization, uninstall, lease release
  -> final disconnected-neutral snapshot proof
  -> USB Host daemon stop and uninstall
```

`GAMEPAD_D1_RESULT result=PASS` requires the exact identity/profile, one or more
committed reports, `active_input_seen=1`, zero dropped/malformed/callback-fault
reports, final neutral state, and complete cleanup. A 120-second timeout is
therefore not itself a pass. Cleanup failure prints `GAMEPAD_D1_HALT` and
suspends the app instead of automatically retrying uncertain retained
resources. Successful cleanup and the result are followed by
`GAMEPAD_D1_TERMINAL state=halted root_data_port_enabled=0
resources_retained=0 automatic_retry=0`, after which firmware permanently
suspends with no reboot, retry, or USB activity. The app contains no display or
audio dependency.

The firmware result is only the runtime half of acceptance. Before reset the
authorized capture route must consume and durably remove the private token
receipt, then send the one frame only after the exact `WAIT_ARM` line. It
records the frame byte count and SHA-256, not the token, zeroizes its in-memory
copy, and performs no later UART transmit during the runtime. After the
terminal marker the same controlled route must restore and readback-verify the
exact 16 KiB install-transport-block-rounded diagnostic mutation span derived
from the active artifact and the live factory partition table. Restoration uses
exact 4 KiB packets, so no write crosses the sealed bound. Every byte the
app-only write transport could erase or pad is included. The durable ledger must not publish `COMPLETE`
or a final pass until restoration succeeds. A crash or power loss leaves the
resident diagnostic inert at `WAIT_ARM`, while successful restoration removes
even a UART-observed token's replay target.

## Hardware gate

Do not connect a controller through a passive OTG adapter. Before changing the
fixture gate, commit a reviewed evidence record that proves all of the following
for the exact physical board and fixture revision:

- exact PCB USB D+/D- and VBUS path;
- externally switched 5 V source role at the controller receptacle;
- measured current limit between 100 and 500 mA;
- backfeed blocking toward the CrowPanel and programmer;
- common ground and controlled direct D+/D- routing;
- an observable overcurrent/fault path;
- direct-controller operation without relying on an unqualified HS hub.

The evidence ID, its file SHA-256, Kconfig values, and app metadata must agree.
Until then both flash modes remain unauthorized.

Controlled shutdown order is `platform_usb_host_quiesce()`, then every class
owner such as `platform_gamepad_usb_stop()`, then
`platform_usb_host_stop()`. Quiesce disables the P4 data-path root port before
the HID driver is uninstalled; it does not switch the fixture's external VBUS.
