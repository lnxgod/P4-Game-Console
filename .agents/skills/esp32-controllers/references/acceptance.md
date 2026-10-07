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
For lockstep gameplay, also record periodic completed-tic, retransmit,
transport-frame, transport-drop, and rendered-frame counters. A controller run
that feels responsive but advances below the stated frame-rate target is not a
performance pass.

For a BLE-controller-plus-BLE-multiplayer run, prove that the room browser
finishes its bounded scan gate, lists advertised rooms with resolved game name,
room ID, players, and signal, requires an explicit room selection, and joins it
without disconnecting the bonded controller. Prove the first screen offers only
the explicit Host and Join roles, Host owns game/settings/create, and Join has
no create action or separate game filter. If both units
show `ROOM OPEN` and `1/2`, record that as two independent hosts, not a link
failure. Prove the console-level host-collision resolver makes the higher
session ID yield and join the lower session ID without user recovery. Do not
accept a UI that lets an immediate confirm press bypass the initial room scan,
or a collision resolver that weakens game/content compatibility checks.

For every SD-backed Doom-engine title, start from a cold per-mount validation
state and exercise the controller continuously during the full exact SHA-256
plus same-pass PSRAM capture, engine initialization, and first gameplay
frames. Opening the generic Multiplayer page must not start a Doom or Chex
scan; only terminal launch of the selected title may do so. The run fails if
input disconnects, the launcher/game handoff stalls, storage reports an
allocation failure, a button report resets the console, or the controller does
not remain usable after the first frame. Also force one transient link loss
and prove immediate neutralization followed by only the documented bounded
reconnect attempts.

For Waveshare H2, also record the external-power/backfeed fixture or powered
hub, root-controller speed policy, downstream speed, hub depth/port, and USB
role before and after the run. Verify that entering USB Drive mode stops and
tears down Host/HID before MSC starts, and that leaving it remounts storage
before controller-host mode resumes. For two-player hub work, test both pads
simultaneously, unplug each independently while held, and prove the remaining
pad stays live.

State exactly which tier passed. Do not generalize one controller result to all devices sharing a console brand.
