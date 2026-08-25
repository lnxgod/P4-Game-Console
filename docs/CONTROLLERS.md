# Console controllers

Controller input is an operating-system service. Games and Doom consume one
canonical `platform_gamepad_snapshot_t`; they never open USB Host, scan BLE,
parse a report map, retain a controller address, or manage a bond.

```text
USB HID --------> platform_gamepad_usb --+
                                          +--> platform_gamepad --> games
BLE HID/HOGP ---> platform_gamepad_ble ---+                    `--> Doom
                         |
                         `--> platform_ble_host <--> ESP32-C6 over SDIO
                                      `-------> BLE multiplayer
```

The broker gives a connected wired USB controller deterministic priority. If
USB disconnects, an already-connected Bluetooth controller becomes active on
the next complete snapshot. Every transport publishes the same buttons,
D-pad, sticks, triggers, sequence, timestamp, capabilities, and descriptor
SHA-256. A persistent console-wide mapping is then applied before Doom,
native cartridges, or script games see the snapshot. Disconnect and fatal
decode paths publish neutral input immediately.

## Bluetooth support

The Waveshare 4.3 build is a BLE central for the standard HID-over-GATT
service (`0x1812`). It discovers the bounded HID report map, reads report
references, subscribes only to validated input reports, and reuses the pure-C
`gamepad_core` parser. Pairing requires encryption and persistent bonding.
Saved-name discovery is only a way to find a privacy-rotated advertisement;
the encrypted peer identity must still match the saved bond before input is
accepted.

Bluetooth-capable Microsoft Xbox Wireless Controllers are the priority named
target. Microsoft distinguishes Bluetooth-capable controllers from older
Xbox controllers and proprietary Xbox Wireless/USB interfaces in its
[hardware interface guide](https://learn.microsoft.com/en-us/xbox/gdk/docs/features/common/input/hardware/input-hardware-interfaces?view=gdk-2604).
This implementation covers a controller only when it exposes BLE HID/HOGP.
It does not implement Xbox 360 wireless, the Xbox Wireless Adapter, or wired
XInput/GIP; Microsoft's [DirectInput/XUSB mapping notes](https://learn.microsoft.com/en-us/windows/win32/xinput/directinput-and-xusb-devices)
explain why those vendor-class paths must not be mislabeled generic HID.

The architecture is ready for the modern Xbox BLE path, but named Xbox
hardware acceptance is still required before claiming a specific controller
model. A descriptor-specific correction must be keyed by exact identity and
report-map SHA-256, never by a broad `Xbox` name match.

## Pair and manage a controller

1. Open **Controllers** on the main screen and leave **BLE Pad Mode** enabled.
2. Select **Pair** before creating or joining a multiplayer lobby.
3. Put the controller into Bluetooth pairing mode. For an Xbox controller,
   turn it on and hold its Pair button until the Xbox light flashes rapidly.
4. Keep the panel open until it reports `CONNECTED` and
   `BONDED + ENCRYPTED`.
5. Use **Disconnect** to retain the bond or **Forget** to remove only that
   controller's saved identity and keys.

A saved controller reconnects after the launcher is usable when BLE Pad Mode
is enabled; it does not block the boot screen or SD mount. Turning BLE Pad
Mode off disconnects and suppresses only the controller client. It does not
disable BLE multiplayer or erase the controller bond.

Select **Map** to bind one physical button each to A, B, X, Y, Start, and Back.
Release between steps. The wizard rejects multi-button presses and duplicate
sources, suppresses navigation while capturing, and commits the complete map
atomically. **Reset** restores the canonical face-button layout. The mapping
is transport-neutral, so the same saved layout applies to wired USB, BLE HID,
Doom, and all Game API tiers.

The same NimBLE host serves controllers and Console OS multiplayer. The
committed configuration allows one BLE controller plus one BLE multiplayer
peer. Pairing temporarily pauses lobby discovery and restores it after the
controller operation completes. Pairing is rejected during an active lobby or
match, while an established encrypted controller link remains connected when
the player opens Multiplayer, creates or joins a lobby, and launches Doom.
The Controllers panel reports `PAD + 1 PEER READY` when this state is ready.

Current limits are one BLE controller, no rumble/LED/battery UI, and no local
two-player assignment from two Bluetooth pads. These are explicit future
extensions; they do not change the canonical game input API.

## Verification

Run the bounded parser/broker tests and exact Waveshare build:

```sh
make gamepad-host
make console-shell-host
./scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host
python3 scripts/verify-console-os-waveshare.py \
  --firmware-only apps/console_os/build-waveshare-usb-host
```

The BLE host follows Espressif's [NimBLE central lifecycle](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/blecent/README.md)
while keeping ESP-IDF and ESP-Hosted pinned by this repository. A build proves
compilation and image contracts only. Named hardware acceptance must record
the controller model, firmware Git state, report-map hash, every control,
held-input disconnect neutralization, reconnects, malformed reports, and a
sustained gameplay run.
