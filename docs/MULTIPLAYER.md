# Console OS multiplayer foundation

Multiplayer is an OS service, never a socket API exposed to a game. The first
implemented slice is an allocation-free C packet and session core with strict
length checks, CRC32 corruption detection, route binding, replay rejection,
four-player limits, and immediate neutral input on leave, timeout, or transport
disconnect. A bounded byte-stream decoder now sits beneath it so chunked or
noisy UART and USB-device traffic cannot hand malformed lengths to the session
core. It is host-tested and linked to the Console OS status page. A physical
UART/USB firmware endpoint, playable lobby, and Doom adapter are not yet
implemented, so the UI describes the core as ready without claiming a live
link.

The repository now includes `scripts/p4-multiplayer-relay.py`, a bounded
two-port H1 relay that discards boot/debug text and forwards only complete
P4MP v1 frames with valid lengths, identities, and CRC32. It opens both serial
ports exclusively with DTR/RTS inactive, so the relay cannot silently reset a
console. The firmware UART endpoint is the next boundary; until that endpoint
is present, the relay and core are host-testable infrastructure rather than a
playable physical link.

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

The safe first topology keeps both consoles as USB devices and puts a real USB
host between them:

```text
Console A H2 (USB 2.0 device) --+-- laptop / Raspberry Pi relay
Console B H2 (USB 2.0 device) --+
```

H2 is sink/device wired and is not authorized to source VBUS. Two consoles
therefore cannot be joined directly with a cable, and a powered hub by itself
does not route traffic between two USB devices. A laptop, Raspberry Pi, or
other USB host can run the relay; a powered hub is useful only when attached to
that host. A future composite or exclusive multiplayer USB-device mode can
carry P4MP frames over CDC or a vendor endpoint. USB 2.0 has ample bandwidth
for input frames and state snapshots; the host-relay and firmware endpoints
still need implementation and two-console latency testing.

H1 exposes the ESP32-P4 UART through the on-board CH343. It can prototype the
same P4MP stream through two serial ports and a host relay, but it is shared
with programming and diagnostics. Direct board-to-board UART needs a separate,
electrically authorized 3.3 V TX/RX/GND connection and must not be inferred
from the USB-UART sockets.

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

1. Add the Console OS H1 serial endpoint for the implemented two-port
   Mac/Linux relay, then add an exclusive H2 USB-device multiplayer mode.
2. Freeze and test Offer/Join/Accept lobby payload codecs, including exact game
   and content-hash matching.
3. Add a lobby state machine and UI, then two-console relay/packet-loss tests.
4. Add one simple native two-player game and a versioned Game API service
   before adapting Doom's tic networking for deathmatch.
5. Qualify on two physical consoles and record disconnect, timeout, desync, and
   malformed-packet evidence.
