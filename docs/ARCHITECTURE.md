# Platform architecture

The platform separates hardware ownership from games so every acceptance game exercises the same production-shaped interfaces.

```text
selected board profile
  Elecrow: DSI panel | touch | speaker | flash FAT | USB device MSC
  Olimex:  HDMI      | no touch | ES8311 | microSD  | USB-A host hub
  Waveshare: DSI     | touch | ES8311 | microSD | H2 USB + C6 BLE
                              |
                   reusable platform services
       video | audio | storage | keyboard/mouse/gamepad input
                              |
                     stable game-facing API
                              |
                    cartridge / Doom adapter
```

## Ownership rules

- `platform_board` selects exactly one source-reviewed board profile. The BSP
  owns pin maps, rails, clocks, and electrical policy for that recorded board
  revision; the default remains the Elecrow 10 in variant.
- A singleton USB-host service owns host installation and its daemon task. Class drivers register with it; games never initialize USB.
- `platform_usb_host` selects P4 USB peripheral 0 (the dedicated HS controller), installs with the root port unpowered, validates build-bound fixture evidence, and only then enables the root port. It never controls CrowPanel VBUS circuitry.
- Class drivers hold generation-bound exclusive leases. Teardown is two-stage: host quiesce blocks new leases and disables the P4 root port (the external fixture still owns physical VBUS); existing class owners drain disconnect callbacks, uninstall, and release; final host stop then frees devices and the daemon. Uncertain cleanup enters a terminal fault state and retains resources instead of freeing synchronization objects beneath a live task.
- The gamepad USB adapter copies at most 1026 callback bytes into one of eight static slots and hands descriptor/report work to a manager task. Queue exhaustion, oversize data, transfer faults, malformed reports, and disconnect all neutralize the active session.
- The HID parser is pure C with no ESP-IDF dependency so hostile descriptors can be tested and fuzzed on a desktop.
- `platform_gamepad` brokers complete USB and BLE transport snapshots. A
  connected wired pad has deterministic priority; a connected BLE pad is the
  fallback. Games cannot see or select the transport.
- The gamepad services publish complete lock-protected snapshots containing session, transport identity, VID/PID when available, interface, report-descriptor SHA-256, capabilities, sequence/timestamp, buttons, D-pad, sticks, and triggers. A stale report or disconnect from a prior session cannot mutate a reconnected controller.
- Disconnect neutralization occurs in the HID callback before close finalization. The transport explicitly completes `usb_host_hid` 1.2.0's two-phase local-close handshake, copies descriptor storage before use, and invalidates it only after confirmed close.
- `platform_ble_host` is the one NimBLE/ESP-Hosted owner on Waveshare. BLE HID
  and BLE multiplayer register before the host starts, then share the pinned
  P4-to-C6 SDIO transport. The HID central requires persistent bonding,
  encryption, bounded GATT discovery and report maps, and saved-peer identity
  verification. Pairing yields lobby scanning and restores it afterward.
- Doom consumes one controller snapshot per game tic and remains ignorant of USB/BLE addresses and handles.
- On Elecrow, Console OS uses the P4 high-speed peripheral in USB **device**
  mode to expose a wear-levelled FAT `game_data` partition. Its device-storage
  and controller-host stacks remain separate firmware configurations.
- On Olimex Rev.B, the P4 high-speed root feeds the onboard powered FE1.1s
  four-port USB-A hub. One transport owns HID class lifecycle and can publish a
  generic gamepad, boot keyboard, and boot mouse concurrently. Reports are
  bounded and copied before parsing; a short/malformed report or disconnect
  immediately neutralizes only the affected session. The USB-C connector stays
  native Serial/JTAG and is not a game-storage MSC path.
- `platform_game_storage` serializes every mount transition. On Elecrow, the FAT
  volume has exactly one owner: the app, laptop MSC initiator, or a terminal
  running-game lease. On Olimex, persistent content is a locally mounted
  microSD volume; laptop mutation requires powering off and removing the card.
  The Olimex runtime never formats the card and does not claim hot-removal.
- The pinned TinyUSB MSC helper's deferred single buffer is bypassed by a link-time platform wrapper. WRITE(10) ranges are bounded and sector-aligned, then synchronously erased, written, and read back before success reaches the laptop; a failed commit is surfaced as SCSI failure.
- File Manager consumes bounded root-list and regular-file-delete operations from `platform_game_storage`; it never mounts FAT, handles raw blocks, deletes directories, or accepts path-like names. A second touch confirmation is required before deletion, and an ownership change invalidates the visible snapshot.
- Doom launch first obtains an exclusive game-storage lease and revalidates the
  entire WAD. Elecrow uninstalls its USB-device stack and restores app
  ownership before releasing display/touch. Olimex keeps the USB host/HID
  owner alive for gamepad, keyboard, and mouse, while the microSD lease blocks
  manager mutation. Both paths prevent storage ownership from changing beneath
  an open game file.
- The Olimex ES8311 backend borrows the display-created I2C1 controller, owns
  I2S1 only while an audio client is open, bounds writes to 128 stereo frames,
  and holds GPIO53 inactive during create, stop, failure, and teardown. Games
  consume the shared audio API and never see codec or I2S handles.
- Display output is an RGB565 surface contract. Board-specific scanout and scaling live below it.
- The hardware-tested display owner is `platform_display`; its M1 pattern proof does not yet qualify framebuffer submission or Doom scaling.
- Game data and saves use the storage service. WADs are never committed; only the exact local shareware development input may seed the build-only Console OS image. Commercial WAD data must never be compiled into, seeded into, or committed with firmware.

## Input compatibility tiers

1. Standards-compliant generic USB HID pads and BLE HID-over-GATT pads through the shared descriptor parser.
2. Profiled HID devices such as DualShock/DualSense and Switch Pro.
3. Vendor-class protocols such as wired Xbox XInput/GIP or the Xbox Wireless Adapter.
4. Optional output features such as LEDs and rumble.

Modern Bluetooth-capable Xbox Wireless Controllers are a priority tier-1 BLE
acceptance target only when they expose HOGP. Older/non-Bluetooth Xbox pads and
proprietary Xbox transports remain tier 3. Every named model still requires a
descriptor capture and hardware acceptance; see `docs/CONTROLLERS.md`.
