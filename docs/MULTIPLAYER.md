# Console OS multiplayer foundation

Multiplayer is an OS service, never a socket API exposed to a game. The first
implemented slice is an allocation-free C packet and session core with strict
length checks, CRC32 corruption detection, route binding, replay rejection,
four-player limits, and immediate neutral input on leave, timeout, or transport
disconnect. It is host-tested and linked to the Console OS status page. The
Waveshare Wi-Fi transport and playable lobby are not implemented yet.

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

## Product modes

1. Same-device multiplayer comes first. Console OS maps up to four local
   controllers/touch sets to stable player slots, with no networking.
2. Native arcade games use a LAN host-authoritative model: clients submit
   tick-stamped normalized input; the host advances the fixed simulation and
   sends bounded snapshots/state hashes. This favors smooth recovery over
   perfect peer lockstep.
3. P4 Carts may later opt into deterministic lockstep. The lobby requires an
   exact cart ID, API version, content SHA-256, player count, and session seed.
   The OS exchanges delayed input frames and periodic state hashes; a mismatch
   ends the match rather than allowing divergent state.
The intended first network scope is local LAN only: no account, cloud relay,
public matchmaking, or arbitrary Internet listener. The ESP32-C6/Wi-Fi 6
transport dependency must be pinned and independently qualified before the
Console OS can advertise networking as ready.

## Remaining implementation order

1. Add the pinned P4-to-C6 transport beneath an OS-owned datagram interface.
2. Freeze and test Offer/Join/Accept lobby payload codecs, including exact game
   and content-hash matching.
3. Add a lobby state machine and UI, then two-PC loopback/packet-loss tests.
4. Add one simple native two-player game before exposing LAN to P4 Carts.
5. Qualify on two physical consoles and record disconnect, timeout, desync, and
   malformed-packet evidence.
