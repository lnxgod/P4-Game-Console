---
name: add-usb-gamepad-support
description: Add, change, diagnose, or test console-level USB or Bluetooth controller services on this ESP32-P4 platform. Use for USB Host, BLE HID/HOGP pairing, HID parsing, controller hotplug, shared mappings, deadzones or Doom input adapters. Ordinary game action mappings use the native game-authoring skill and existing normalized controls.
---

# Add console gamepad support

Implement controller support once in the platform and expose normalized state
to every game. Do not put USB Host calls, NimBLE/GATT calls, pairing state,
descriptor parsing, device quirks, or pin configuration inside a game.

If a game only maps the existing normalized P4 buttons to game actions, use
`$develop-p4-console-games` and that
game's focused host tests. New games should consume the existing controls by
default; no per-game USB support layer is needed. For linked-console game logic,
add `$develop-p4-multiplayer-games` without changing controller ownership. Use this skill when the
canonical input contract, parser, profile, lifecycle, transport, or physical
controller support changes. Read `references/architecture.md` for either
transport, and `references/acceptance.md` before a named hardware claim.

## Pass the hardware gate first

Read `hardware/board-profile.json`, `docs/HARDWARE.md`, and `references/architecture.md`.

Resolve the exact target before touching USB code:

- `elecrow-crowpanel-advanced-10` is the Elecrow CrowPanel Advanced 10.1-inch.
  Its J16 connector is sink-wired and requires the reviewed powered,
  current-limited, backfeed-safe host fixture for controller testing. Console
  OS normally uses J16 as USB-device game storage instead of USB Host.
- `olimex-esp32-p4-pc` is the Olimex ESP32-P4-PC Rev.B development board. Its
  ESP32-P4 high-speed root feeds the onboard powered FE1.1s USB-A hub; use that
  path directly for gamepad, boot keyboard, and boot mouse. Its USB-C is only
  native Serial/JTAG and is not a controller or storage connector.
- `waveshare-esp32-p4-wifi6-touch-lcd-4.3` uses H2 as the runtime-switch USB
  connector. Controller-first Console OS runs the full-speed Host/HID path
  through an externally powered, current-limited, backfeed-safe direct cable or
  hub; firmware does not source VBUS. H1 CH343 remains programming, serial, and
  content transfer only. The USB Drive app is the explicit role switch from H2
  Host to H2 device/MSC and must unmount FAT before exposing it.
- `m5stack-tab5` uses its dedicated USB-A connector and the native HS PHY.
  The default candidate enables the shared USB/HID path and controls USB5V_EN
  through the board service on expander 0x44/P3. USB-C remains Serial/JTAG.
  Read `docs/boards/M5STACK_TAB5.md`; this path is build-tested, with exact-unit
  controller acceptance pending. The wired XUSB client also recognizes
  alternate-zero `ff/5d/01` interfaces and has host/build tests. Read
  `components/platform_gamepad_xusb/README.md`. Do not claim named-pad
  acceptance, GIP, wireless receivers, hubs or multiple simultaneous pads.
- Another target must have its own source-pinned board-port profile and USB
  adapter plan. Never inherit either identity from a connector name.

Elecrow's published CrowPanel Advanced reference circuit wires the USB-C connector as a sink/device port: its CC pins use `Rd`, and VBUS feeds the board rather than sourcing a controller. Until the exact unit and PCB revision are confirmed and their circuit is reviewed, treat this physical port the same way. Do not connect a passive USB-A adapter or enable host mode until a powered, current-limited, backfeed-safe host fixture has been reviewed for the exact board revision.

Firmware cannot fix missing USB VBUS source circuitry. A build may still be tested without claiming physical USB success.

Bluetooth input is currently a Waveshare 4.3 feature. The P4 has no radio;
`platform_ble_host` runs NimBLE on the P4 and carries HCI over the exact
board-authorized ESP32-C6 SDIO transport. Do not add a second NimBLE owner,
change Hosted pins, or start the radio from a game. The shared host must retain
both BLE HID and multiplayer registrations before it starts.

## Preserve the platform boundary

Use this dependency direction:

```text
USB Host or BLE HOGP lifecycle
  -> bounded transport adapter
  -> bounded HID descriptor parser + known-device profiles
  -> complete transport snapshot
  -> platform_gamepad broker (wired USB priority, BLE fallback)
  -> per-game action adapter
```

Reuse `components/gamepad_core` and `components/platform_gamepad`. Tab5's
`platform_gamepad_xusb` supplies the separate bounded wired-XUSB tier. Extend the
stable public state only when a control cannot be represented there. Keep Doom
key translation in the project-owned `components/doom_gamepad_input` adapter,
while connect, pairing, decode, normalization, hotplug, and device quirks
remain platform code. The adapter consumes only a complete canonical snapshot,
queues releases before presses, and neutralizes invalid or disconnected input.

## Implement the USB lifecycle

- Use the versions pinned in `toolchain.lock.json`; never float managed components.
- Use Espressif's native USB Host stack and managed HID host component.
- Run `usb_host_lib_handle_events()` from a dedicated daemon task.
- On connection, inspect the interface, open it, obtain and validate the report descriptor, prepare mappings, then start input.
- Accept HID interfaces with `HID_PROTOCOL_NONE`; generic gamepads are not boot-protocol devices.
- Copy callback report bytes into bounded storage quickly. Parse or dispatch outside timing-sensitive callbacks.
- On disconnect, atomically publish a neutral state, synthesize releases, close exactly once, and invalidate device-owned descriptor data.
- Shut down class drivers and host tasks in a defined order.

Do not treat wired Xbox/XInput/GIP, Xbox Wireless Adapter, or another
vendor-class device as generic USB HID. Add a separate transport/profile tier
when required.

For an Xbox-compatible controller, identify its actual USB mode before
choosing an adapter. Tab5 selects the existing HID or wired-XUSB adapter from
its descriptors; an XInput label alone does not distinguish wired XUSB from GIP. The user may have a third-party pad with a different
protocol from an official Microsoft controller. A Windows DirectInput label
does not prove a raw USB HID interface. Record unknown identity/mode and
hardware availability honestly; do not repeat a request for a controller the
user has already said is unavailable. Continue work that has sufficient
protocol evidence and keep model-specific acceptance pending.

## Implement the BLE HID lifecycle

- Use `platform_ble_host`; never call `nimble_port_init()` from a controller
  component after the shared host exists.
- Act as a central for the standard HID service `0x1812`. Bound candidates,
  services, characteristics, descriptor bytes, reports, subscriptions, and
  timeouts before parsing any peer data.
- Pair only after an explicit Controllers-panel action. Require encryption and
  persistent bonding, and accept input only after the encrypted peer identity
  matches the saved bond. A device name may help rediscover a privacy-rotated
  advertisement but is never authorization.
- Neutralize before terminating or forgetting a link. Forget only the selected
  controller bond; never erase multiplayer or unrelated BLE state.
- Yield BLE lobby discovery while pairing and restore it after the bounded
  operation. Preserve the committed capacity for one BLE pad plus one
  multiplayer peer.
- Treat modern Bluetooth-capable Xbox Wireless Controllers as the priority
  HOGP acceptance target, not as proof that older Xbox or proprietary GIP
  transports work. Add a quirk only from an exact report-map capture and hash.

## Parse untrusted descriptors defensively

Support report IDs, usage ranges, array and variable items, signed non-byte-aligned values, nested collections, and global PUSH/POP. Maintain separate bit offsets per report ID and report kind. Reject truncation, overflow, impossible ranges, excessive nesting, oversized descriptors, and unsupported layouts without corrupting the last good state.

Never retain pointers whose lifetime ends when a USB or BLE device closes.
Apply explicit caps to descriptors, fields, collections, and reports.

## Normalize before games see input

Publish a complete snapshot containing connection status, identity, sequence/timestamp, buttons, D-pad/hat, signed axes, and unsigned triggers. Apply mappings and calibration before publication. Readers should observe either the previous complete state or the next complete state, never a partially updated structure.

Assign support tiers honestly:

1. Standards-compliant wired USB HID or BLE HID/HOGP gamepad.
2. Explicit PlayStation or Switch HID profiles.
3. Xbox XInput/GIP, Xbox Wireless Adapter, or another vendor-class transport.
4. Output reports such as rumble and LEDs.

## Verify proportionally

- A game-only action mapping needs only that game's host tests.
- A parser, profile, lifecycle, transport, canonical-state, or shared adapter
  change needs `make gamepad-host` before the exact firmware build.
- A physical transport or newly supported controller needs the relevant cases
  from `references/acceptance.md` and one named run through the safe fixture.

Do not run repo-wide `make check`, unrelated peripheral suites, or a physical
fixture acceptance for an isolated game mapping. Do not repeat an unchanged
firmware build or hardware run. A controller is supported only after its
descriptor capture, input mapping, hotplug, disconnect neutralization,
malformed-report behavior, and named hardware run have passed.

For Console OS integration, the exact firmware builds are
`make console-os-elecrow-idf` for Elecrow and `make console-os-olimex-idf` for the
Olimex Rev.B development board. The standard BLE-controller Waveshare build is
`./scripts/build-waveshare-console-os.sh`, followed by
`python3 scripts/verify-console-os-waveshare.py`. The proven wired
controller-first build is
`./scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host`,
followed by `python3 scripts/verify-console-os-waveshare.py
apps/console_os/build-waveshare-usb-host`. A generic SNES-style USB HID pad has
named direct/hub evidence on that route, including D-pad/button mapping and
disconnect recovery; do not generalize that result to other descriptors.
For Tab5, run `make tab5-usb-host` and `make gamepad-host`, then
`make console-os-tab5-idf`. The guarded build verifier checks the linked host,
board power and Doom adapters. Use the exact-unit/artifact install route in the
Tab5 board document before any hardware test.
Build only the physically selected target unless shared input or board
selection code changed.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
