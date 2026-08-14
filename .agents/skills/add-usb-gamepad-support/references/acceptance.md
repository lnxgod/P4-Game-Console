# Controller acceptance

## Host tests

- valid descriptor fixtures cover buttons, hat, signed axes, unsigned triggers, report IDs, non-byte-aligned fields, and global PUSH/POP;
- malformed descriptors and truncated/oversized reports are rejected safely;
- reconnect and disconnect always publish a complete neutral snapshot;
- game adapters consume only the canonical API.

## Hardware test

For each supported controller, record its product name, VID/PID, USB class/protocol, captured report descriptor hash, fixture revision, board revision, and firmware Git state. Then verify:

1. cold-plug and hot-plug enumeration;
2. every advertised button and direction;
3. full axis range, center, deadzone, and trigger independence;
4. simultaneous controls used by the game;
5. unplug while controls are held, with neutral state no later than the next game tick;
6. repeated reconnects without leaks, duplicate callbacks, or stuck input;
7. malformed or short reports without crashes or stale partial state;
8. at least 30 minutes of continuous gameplay.

State exactly which tier passed. Do not generalize one controller result to all devices sharing a console brand.
