---
name: add-usb-gamepad-support
description: Add, change, diagnose, or test USB controller input for any game on this ESP32-P4 platform. Use for USB Host, HID report descriptors, gamepads, keyboards used as game input, controller hotplug, mappings, deadzones, Doom controls, or new game input adapters.
---

# Add USB gamepad support

Implement controller support once in the platform and expose normalized state to every game. Do not put USB Host calls, descriptor parsing, VID/PID quirks, or pin configuration inside a game.

## Pass the hardware gate first

Read `hardware/board-profile.json`, `docs/HARDWARE.md`, and `references/architecture.md`.

Elecrow's published CrowPanel Advanced reference circuit wires the USB-C connector as a sink/device port: its CC pins use `Rd`, and VBUS feeds the board rather than sourcing a controller. Until the exact unit and PCB revision are confirmed and their circuit is reviewed, treat this physical port the same way. Do not connect a passive USB-A adapter or enable host mode until a powered, current-limited, backfeed-safe host fixture has been reviewed for the exact board revision.

Firmware cannot fix missing USB VBUS source circuitry. A build may still be tested without claiming physical USB success.

## Preserve the platform boundary

Use this dependency direction:

```text
USB Host lifecycle
  -> HID transport
  -> bounded HID descriptor parser + known-device profiles
  -> canonical gamepad state
  -> per-game action adapter
```

Reuse `components/gamepad_core`. Extend its stable public state only when a control cannot be represented there. Keep Doom key translation in the project-owned `components/doom_gamepad_input` adapter, while connect, decode, normalization, hotplug, and device quirks remain platform code. The adapter consumes only a complete canonical snapshot, queues releases before presses, and neutralizes invalid or disconnected input.

## Implement the host lifecycle

- Use the versions pinned in `toolchain.lock.json`; never float managed components.
- Use Espressif's native USB Host stack and managed HID host component.
- Run `usb_host_lib_handle_events()` from a dedicated daemon task.
- On connection, inspect the interface, open it, obtain and validate the report descriptor, prepare mappings, then start input.
- Accept HID interfaces with `HID_PROTOCOL_NONE`; generic gamepads are not boot-protocol devices.
- Copy callback report bytes into bounded storage quickly. Parse or dispatch outside timing-sensitive callbacks.
- On disconnect, atomically publish a neutral state, synthesize releases, close exactly once, and invalidate device-owned descriptor data.
- Shut down class drivers and host tasks in a defined order.

Do not treat Xbox/XInput or other vendor-class devices as generic HID. Add a separate transport/profile tier when required.

## Parse untrusted descriptors defensively

Support report IDs, usage ranges, array and variable items, signed non-byte-aligned values, nested collections, and global PUSH/POP. Maintain separate bit offsets per report ID and report kind. Reject truncation, overflow, impossible ranges, excessive nesting, oversized descriptors, and unsupported layouts without corrupting the last good state.

Never retain pointers whose lifetime ends when a USB device closes. Apply explicit caps to descriptors, fields, collections, and reports.

## Normalize before games see input

Publish a complete snapshot containing connection status, identity, sequence/timestamp, buttons, D-pad/hat, signed axes, and unsigned triggers. Apply mappings and calibration before publication. Readers should observe either the previous complete state or the next complete state, never a partially updated structure.

Assign support tiers honestly:

1. Standards-compliant wired USB HID gamepad.
2. Explicit PlayStation or Switch HID profiles.
3. Xbox/XInput/GIP or another vendor-class transport.
4. Output reports such as rumble and LEDs.

## Verify

Run `make gamepad-host` before the firmware build. That command covers the parser/state core, USB lifecycle model, transport publication model, and Doom action adapter under ASan/UBSan. Then follow `references/acceptance.md`. A controller is supported only after descriptor capture, input mapping, hotplug, disconnect neutralization, malformed-report behavior, and a named hardware run through the safe fixture have passed.
