# Console OS multiplayer foundation

Multiplayer is an OS service, never a socket API exposed to a game. The
implemented slice is an allocation-free C packet and session core with strict
length checks, CRC32 corruption detection, route binding, replay rejection,
four-player limits, and immediate neutral input on leave, timeout, or transport
disconnect. A bounded byte-stream decoder now sits beneath it so chunked or
noisy UART and USB-device traffic cannot hand malformed lengths to the session
core. Console OS now owns an H1 UART endpoint, deterministic two-peer Doom
lobby, and lockstep Doom tic adapter. Both exact Waveshare consoles have passed
an H1 relay round trip; live two-player gameplay remains a separate acceptance.

The repository now includes `scripts/p4-multiplayer-relay.py`, a bounded
two-port H1 relay that discards boot/debug text and forwards only complete
P4MP v1 frames with valid lengths, identities, and CRC32. It opens both serial
ports exclusively with DTR/RTS inactive, so the relay cannot silently reset a
console. The firmware endpoint and relay are present. Each H1 must connect to
the host running the relay; H1-to-H1 USB-C cabling cannot connect two USB
devices.

## Implemented v1 envelope

All integers are little-endian. A datagram is at most 1,056 bytes:

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | ASCII `P4MP` |
| 4 | 1 | protocol version `1` |
| 5 | 1 | packet type |
| 6 | 2 | flags, currently zero |
| 8 | 4 | session ID |
| 12 | 4 | peer ID |
| 16 | 4 | sequence, never zero |
| 20 | 4 | acknowledgement |
| 24 | 2 | payload length, at most 1,024 |
| 26 | 2 | reserved, zero |
| 28 | variable | payload |
| end | 4 | CRC32 over header and payload |

Types are Discover, Offer, Join, Accept, Input, State Hash, Ping, Pong, Leave,
and Reject. Normalized input is a fixed 24-byte payload. A session has one host
and at most three remote peers. Routes and peer IDs cannot be silently rebound,
and packets must have a newer sequence number. The default peer timeout is
three seconds. CRC32 detects accidental corruption; it is not authentication or
encryption.

The transport adapter must copy a complete bounded datagram into this core and
translate its own connection identity to an opaque 64-bit route ID. It must
call the disconnect API before releasing a route. The core never opens Wi-Fi,
owns a driver, allocates memory, or invokes game callbacks.

## Wired topology on the Waveshare 4.3

The working first topology uses the CH343 bridges on H1 and puts a real host
between them, leaving H2 available for each console's controller fixture:

```text
Console A H1 (CH343 USB-UART) --+-- Mac/Linux relay
Console B H1 (CH343 USB-UART) --+
```

H2 remains controller-first and is not the inter-console link. H2 is
sink/device wired and is not authorized to source VBUS, so its controller-host
mode still requires the qualified externally powered fixture. A future H2
USB-device relay can be added, but it must remain exclusive with controller
host and USB Drive modes.

H1 exposes the ESP32-P4 UART through the on-board CH343. It carries the P4MP
stream through two serial ports and a host relay and is shared with programming,
diagnostics, and negotiated content upload. Direct board-to-board UART still
needs a separate, electrically authorized 3.3 V TX/RX/GND connection and must
not be inferred from the USB-UART sockets.

The byte-stream decoder supports both paths. It scans for `P4MP` magic, waits
for the complete bounded header, rejects payloads above 1,024 bytes before
buffering them, validates CRC32, and reports how many source bytes were
consumed so multiple frames in one USB/UART read are handled without loss.

## Product modes

1. Same-device multiplayer comes first. Console OS maps up to four local
   controllers/touch sets to stable player slots, with no networking.
2. Native arcade games use an OS-owned host-authoritative model: clients submit
   tick-stamped normalized input; the host advances the fixed simulation and
   sends bounded snapshots/state hashes. This favors smooth recovery over
   perfect peer lockstep.
3. P4 Carts may later opt into deterministic lockstep. The lobby requires an
   exact cart ID, API version, content SHA-256, player count, and session seed.
   The OS exchanges delayed input frames and periodic state hashes; a mismatch
   ends the match rather than allowing divergent state.
The intended first link is local and wired: no account, cloud relay, public
matchmaking, or arbitrary Internet listener. Radio transports remain optional;
the ESP32-C6/Wi-Fi 6 dependency must be pinned and independently qualified
before Console OS can advertise wireless networking as ready.

## Remaining implementation order

1. Complete live two-console Doom gameplay acceptance with identical WADs.
2. Record disconnect, timeout, desync, and malformed-packet behavior.
3. Add same-console player slots and a simple native two-player reference game.
4. Freeze a versioned multiplayer Game API above the OS-owned P4MP transport.
5. Consider an exclusive H2 USB-device multiplayer mode after controller-first
   behavior is preserved.
