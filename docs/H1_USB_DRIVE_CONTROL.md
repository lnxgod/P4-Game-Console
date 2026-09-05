# H1 USB Drive control

The Waveshare 4.3 controller-first image normally owns H2 as USB Host/HID.
Its H1 CH343 programming connector can expose a deliberately small local
control endpoint for an operator who needs the H2 USB Drive role without using
the touchscreen:

```sh
python3 scripts/p4-transfer.py usb-drive status --port /dev/cu.wchusbserial...
python3 scripts/p4-transfer.py usb-drive on --port /dev/cu.wchusbserial...
python3 scripts/p4-transfer.py usb-drive off --port /dev/cu.wchusbserial...
```

`off` is intentionally refused while a connected laptop has not cleanly
ejected the exported volume. Eject the volume in the operating system (or
disconnect H2), confirm `ejected=1` with `status`, and retry. A rejected `off`
leaves MSC active and does not remount the card locally.

## Boundary and wire format

This is a physical, trusted-local-admin channel; it is **not authentication**.
Anyone able to use the H1 serial connector can request the same bounded role
change that the on-device USB Drive app exposes. It is not a network control
plane and must not be bridged to multiplayer, BLE, or arbitrary remote input.

Each request is exactly 20 bytes, little-endian:

| Bytes | Field |
| --- | --- |
| 0..3 | `P4U1` |
| 4 | protocol version (`1`) |
| 5 | command: status (`1`) or set mode (`2`) |
| 6 | mode: `0` for status/off, `1` for on |
| 7 | reserved, zero |
| 8..11 | nonzero session nonce |
| 12..15 | sequence (`0` to open/query a session; then monotonic) |
| 16..19 | CRC-32 of bytes 0..15 |

The fixed 28-byte `P4V1` response repeats command/result/mode, a sanitized
storage snapshot, session/sequence, and a CRC-32. A session lasts ten seconds;
mode commands require the current session and exactly the expected sequence.
Malformed, oversized-by-construction, stale, or CRC-invalid frames never call
the role-transition callback.

## Firmware wiring contract

`p4_h1_usb_drive_control` is transport- and display-independent. Console OS
must populate its status callback from `platform_game_storage_get_status()` and
set `control_available` only when no H1 content/file transfer or catalog scan
owns the mounted FAT volume. The callback must not manufacture state.

The mode callback owns the only hardware transition:

1. For `on`, neutralize input, stop Host/HID, then call
   `platform_game_storage_set_usb_mode(true)`. If that fails before TinyUSB is
   running, recover Host/HID as the existing UI does.
2. For `off`, call `platform_game_storage_set_usb_mode(false)` first. It is the
   ejection gate and tears down MSC, remounts, and rescans. Restart Host/HID
   only when it succeeds.

No callback may start host/HID after a failed storage teardown, mount FAT while
MSC owns it, or treat an H1 frame as an authorization to override the host
eject gate. The service only returns a status response; it never accesses the
screen, starts USB roles, opens files, or changes storage itself.
