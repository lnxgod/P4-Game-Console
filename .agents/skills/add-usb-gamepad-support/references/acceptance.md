# Controller acceptance

## Host tests

- valid descriptor fixtures cover buttons, hat, signed axes, unsigned triggers, report IDs, non-byte-aligned fields, and global PUSH/POP;
- malformed descriptors and truncated/oversized reports are rejected safely;
- reconnect and disconnect always publish a complete neutral snapshot;
- game adapters consume only the canonical API.

## Hardware test

For each supported controller, record its product name/model, transport,
VID/PID when available, USB class/protocol or BLE services, captured report
descriptor/map hash, fixture revision when wired, board revision, controller
firmware, and firmware Git state. Then verify:

1. cold-plug and hot-plug enumeration;
2. every advertised button and direction;
3. full axis range, center, deadzone, and trigger independence;
4. simultaneous controls used by the game;
5. unplug while controls are held, with neutral state no later than the next game tick;
6. repeated reconnects without leaks, duplicate callbacks, or stuck input;
7. malformed or short reports without crashes or stale partial state;
8. at least 30 minutes of continuous gameplay.

For BLE, additionally verify explicit pairing, encrypted/bonded state, reboot
reconnect without blocking launcher readiness, privacy-address reconnect,
disconnect versus forget behavior, rejection of a same-name wrong identity,
and coexistence with one active BLE multiplayer peer. Record RSSI and dropped
report counters. Do not generalize a passing Xbox Wireless Controller model to
Xbox 360, Xbox Wireless Adapter, wired XInput/GIP, or a different firmware.

For Waveshare H2, also record the external-power/backfeed fixture or powered
hub, root-controller speed policy, downstream speed, hub depth/port, and USB
role before and after the run. Verify that entering USB Drive mode stops and
tears down Host/HID before MSC starts, and that leaving it remounts storage
before controller-host mode resumes. For two-player hub work, test both pads
simultaneously, unplug each independently while held, and prove the remaining
pad stays live.

State exactly which tier passed. Do not generalize one controller result to all devices sharing a console brand.
