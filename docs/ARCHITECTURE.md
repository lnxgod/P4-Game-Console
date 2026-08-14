# Platform architecture

The platform separates hardware ownership from games so every acceptance game exercises the same production-shaped interfaces.

```text
Elecrow revision-aware BSP
  display | touch | audio | SD | USB VBUS policy
       |       |       |      |          |
       +-------+-------+------+----------+
                         |
              reusable platform services
     video surface | audio mixer | storage | gamepad service
                         |
                  stable game-facing API
                         |
                    Doom adapter
```

## Ownership rules

- The BSP owns pin maps, rails, clocks, and electrical policy for one recorded board revision.
- A singleton USB-host service owns host installation and its daemon task. Class drivers register with it; games never initialize USB.
- `platform_usb_host` selects P4 USB peripheral 0 (the dedicated HS controller), installs with the root port unpowered, validates build-bound fixture evidence, and only then enables the root port. It never controls CrowPanel VBUS circuitry.
- Class drivers hold generation-bound exclusive leases. Teardown is two-stage: host quiesce blocks new leases and disables the P4 root port (the external fixture still owns physical VBUS); existing class owners drain disconnect callbacks, uninstall, and release; final host stop then frees devices and the daemon. Uncertain cleanup enters a terminal fault state and retains resources instead of freeing synchronization objects beneath a live task.
- The gamepad USB adapter copies at most 1026 callback bytes into one of eight static slots and hands descriptor/report work to a manager task. Queue exhaustion, oversize data, transfer faults, malformed reports, and disconnect all neutralize the active session.
- The HID parser is pure C with no ESP-IDF dependency so hostile descriptors can be tested and fuzzed on a desktop.
- The gamepad service publishes complete lock-protected snapshots containing session, VID/PID, interface, report-descriptor SHA-256, capabilities, sequence/timestamp, buttons, D-pad, sticks, and triggers. A stale report or disconnect from a prior session cannot mutate a reconnected controller.
- Disconnect neutralization occurs in the HID callback before close finalization. The transport explicitly completes `usb_host_hid` 1.2.0's two-phase local-close handshake, copies descriptor storage before use, and invalidates it only after confirmed close.
- Doom consumes one controller snapshot per game tic and remains ignorant of USB addresses and handles.
- Display output is an RGB565 surface contract. Board-specific scanout and scaling live below it.
- The hardware-tested display owner is `platform_display`; its M1 pattern proof does not yet qualify framebuffer submission or Doom scaling.
- Game data and saves use the storage service. Commercial WAD data is never compiled into or committed with firmware.

## Input compatibility tiers

1. Standards-compliant generic USB HID/DirectInput pads through descriptor parsing.
2. Profiled HID devices such as DualShock/DualSense and Switch Pro.
3. Vendor-class protocols such as Xbox XInput/GIP.
4. Optional output features such as LEDs and rumble.

The first milestone promises tier 1 input only. Higher tiers require explicit profiles, fixtures, and hardware evidence.
