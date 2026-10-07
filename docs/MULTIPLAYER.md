# Console OS multiplayer foundation

Multiplayer is an OS service, never a socket API exposed to a game. The
implemented slice is an allocation-free C packet and session core with strict
length checks, CRC32 corruption detection, route binding, replay rejection,
four-player limits, and immediate neutral input on leave, timeout, or transport
disconnect. A bounded byte-stream decoder now sits beneath it so chunked or
noisy UART and USB-device traffic cannot hand malformed lengths to the session
core. Console OS now owns a direct-first dual-UART endpoint, a two-player
multi-game lobby, the lockstep Doom tic adapter, a bounded native-game message
bridge, and transport-neutral handoff between wired UART and BLE gaming. The
lobby enumerates Doom, Chex Quest, and installed cartridges that declare the
optional `multiplayer-session` capability. It advertises the selected game
identity and exact content hash. The first Multiplayer screen is an explicit
`HOST` or `JOIN` choice: Host selects a game and its settings, while Join scans
all P4 room beacons and lists each locally resolvable game without a separate
game filter. Selecting a row binds the local exact game identity before any
connection attempt. Entering the generic lobby never scans Doom or Chex data. A
Doom-family match exact-hashes only the selected title during terminal launch,
while the same bounded pass captures the immutable PSRAM snapshot used by the
engine. Native multiplayer cartridges use their already-validated catalog
identity and never trigger a WAD scan. Wired Auto remains the boot/default
transport. BLE is opt-in from the lobby and lazily starts the Waveshare's
on-board ESP32-C6 only after selection, so normal boot and wired play do not
pay the radio startup cost. A board-authorized direct UART can run without a
computer; H1 remains the automatic host-relay fallback and the
diagnostic/content-upload path. Both exact Waveshare consoles have passed a
sustained H1 relay game run. Console OS 0.4.83 also passed the explicit BLE
Host/Join path on those two units: the guest discovered and selected the Doom
room, both consoles crossed synchronized launch, and live gameplay ran. Direct
J3 UART remains a separate hardware acceptance. The exact 0.4.83 artifact and
operator result are recorded in
`hardware/test-runs/2026-08-25-waveshare-two-unit-console-os-0.4.83-multiplayer-role-ui.json`.

## Declarative native-game profiles

Native `.P4G` games describe their networking behavior in the same
`game.json` that already declares title, capabilities, and package identity.
The smallest profile is `{"schema":1,"style":"turn-based"}`. Console OS
normalizes it into player limits, simulation rate, lockstep delay, protocol,
and message budget; packages do not select a physical transport. The profile
is validated at build time, encoded in bounded package metadata, exposed to
the game through `p4_game_multiplayer_read_profile()`, and included in lobby
compatibility. See `docs/GAME_SDK.md` for the complete field bounds.

This keeps the extension point data-driven: a new game appears in the Host
game list, can be resolved from an advertised Join row, and receives the
existing sanitized session API without adding a game-specific BLE service,
UART parser, socket, lobby screen, or launcher table.
Console OS performs this registration itself when it scans an installed P4G;
cartridge code has no registration API and cannot mutate the registry. The OS
accepts only validated package metadata, rejects duplicate identities and
profiles the current transport cannot host, and rebuilds the bounded registry
after catalog changes. Thus a newly installed kid-created game is registered
before its first launch simply by declaring the profile in `game.json`.
The current OS can run two players. Profiles can already describe up to four,
so later multi-peer transports can expand capacity without changing Game API
v1 or the package manifest shape.

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
Reject, and Game Message. Normalized input is a fixed 24-byte payload. A Game
Message carries 1-64 bytes for a cartridge through the OS-owned capability; it
does not expose the route or transport. A session has one host
and at most three remote peers. Routes and peer IDs cannot be silently rebound,
and packets must have a newer sequence number. The default peer timeout is
three seconds. CRC32 detects accidental corruption; it is not authentication or
encryption. The BLE adapter additionally requires a link encrypted with LE
Secure Connections, but its Just Works pairing does not authenticate the
physical identity of the other badge.

The transport adapter must copy a complete bounded datagram into this core and
translate its own connection identity to an opaque 64-bit route ID. It must
call the disconnect API before releasing a route. The core never opens Wi-Fi,
owns a driver, allocates memory, or invokes game callbacks.

## Wired topology on the Waveshare 4.3

The preferred topology is a dedicated 3.3 V UART crossover on J3. The exact
Waveshare schematic maps J3 pin 16 to GPIO28, J3 pin 18 to GPIO29, and J3 pin
24 to GND:

```text
Console A J3-16 (GPIO28 TX) ----> Console B J3-18 (GPIO29 RX)
Console A J3-18 (GPIO29 RX) <---- Console B J3-16 (GPIO28 TX)
Console A J3-24 (GND)       ----- Console B J3-24 (GND)
```

Do not connect either board's 3V3 or 5V pin to the other board. Power each
console normally. A 100-220 ohm series resistor in each TX lead is recommended
for the first bench cable. The initial link is 115200 baud, 8-N-1 on UART1.

The proven fallback uses the CH343 bridges on H1 and a real host between them,
leaving H2 available for each console's powered controller fixture:

```text
Console A H1 (CH343 USB-UART) --+-- Mac/Linux relay
Console B H1 (CH343 USB-UART) --+
```

H2 remains controller-first and is not the inter-console link. H2 is
sink/device wired and is not authorized to source VBUS, so its controller-host
mode still requires the qualified externally powered fixture. A future H2
USB-device relay can be added, but it must remain exclusive with controller
host and USB Drive modes.

H1 exposes the ESP32-P4 console UART through the on-board CH343. It carries the
fallback P4MP stream through two host serial ports and is shared with
programming, diagnostics, and negotiated content upload. H1-to-H1 USB-C cannot
form a direct link because both CH343 ends are USB devices.

At discovery, firmware sends direct-only probes first. If a valid direct frame
arrives, route 2 locks and all lobby/game traffic remains on the wire. After
three unanswered direct probes, discovery is mirrored to H1; the first valid
relay frame locks route 1. Route binding is released only when the lobby is
reset or the peer times out, preventing a match from silently switching paths.

The byte-stream decoder supports both paths. It scans for `P4MP` magic, waits
for the complete bounded header, rejects payloads above 1,024 bytes before
buffering them, validates CRC32, and reports how many source bytes were
consumed so multiple frames in one USB/UART read are handled without loss.

## BLE gaming on the Waveshare 4.3

The ESP32-P4 has no radio of its own. Console OS runs NimBLE on the P4 and
carries HCI to the board's ESP32-C6 over the exact authorized SDIO map
(reset 54, clock 18, command 19, data 14/15/16/17). The dependency versions
remain pinned to ESP-IDF 5.5.3, ESP-Hosted 1.4.7, and Wi-Fi Remote 0.14.5.
The build refuses to continue if generated configuration falls back to a
different Hosted transport or pin map.

BLE gaming is deliberately narrow:

- one encrypted peer, for two-player games;
- multiplayer peers are not bonded; controller bonds are a separate Console
  OS service. There is no account, cloud relay, Wi-Fi transport, or public
  matchmaking;
- idle badges scan but do not advertise, so they cannot accidentally pair;
- the Join browser gets a 1.8-second scan settle window and accepts a wildcard
  game token only for discovery. Every result retains its advertised game
  token; an explicit row selection must resolve to a locally installed exact
  identity before `JOIN SELECTED` is enabled;
- `OPEN ROOM` allocates a fresh P4MP session and advertises one compact,
  game-specific room beacon. While no guest is connected, hosts alternate
  advertising with short collision scans. If two compatible rooms were
  created independently, the lower session ID remains host and the higher
  session ID automatically yields and joins it;
- browsers keep a bounded, expiring strongest-first room list and `JOIN`
  connects only to the selected host address and advertised session;
- only the host may start a match; a guest remains at the ready screen until
  the host sends the synchronized start barrier;
- independent host/session pairs can coexist in radio range without
  cross-pairing; each current room still has a two-player capacity;
- P4MP datagrams are split into bounded ATT fragments, reassembled without
  dynamic allocation, then length- and CRC-checked before entering the shared
  session core;
- a failed join or disconnect immediately neutralizes the multiplayer route
  and returns the guest to the room browser; a disconnected host reopens its
  same room until the user leaves the Multiplayer page.

Console OS 0.4.98 makes entry and exit own the shared radio explicitly. A
disconnected saved controller's queued or active reconnect is cancelled before
BLE room discovery starts, the UI waits for that bounded GAP cancellation
without blocking touch, and the saved reconnect resumes after Multiplayer is
left. An already-connected encrypted controller keeps its link and uses the
committed controller-plus-peer capacity.

Console OS 0.4.84 adds a backend-host compatibility case for macOS
CoreBluetooth. Apple's peripheral API can advertise the P4 service UUID but
not the ten-byte room service-data beacon. A Join browser therefore maps an
otherwise valid UUID-only P4 peripheral to reserved session `0x4c4f5244` and
the currently selected local game token. This only permits the encrypted GATT
connection attempt: the backend must then send a normal Offer whose exact game
ID, API, content identity, multiplayer profile, and compatibility SHA-256
match the installed cartridge before the console sends Join or launches it.
Ordinary P4 consoles continue using the full room beacon.

To use it, open Multiplayer on both consoles. On the first console choose
`HOST`, choose the game and settings, select the desired `LINK`, then press
`OPEN ROOM`. On the second choose `JOIN`, select the same link, wait for the
cross-game room list, select the row showing the intended game and room ID,
then press `JOIN SELECTED`. Join has no game picker and cannot create a room.
The host owns match settings and presses `START MATCH` only after the
guest is connected. If both consoles briefly say `ROOM OPEN` and `1/2`, they
are separate hosts; the collision resolver must converge them automatically
without discarding content identity or session validation. Both consoles cross
the same start barrier, then Console OS
launches the selected game: Doom receives its lockstep adapter, while a native
cartridge receives the bounded `multiplayer-session` capability. Selecting BLE
lazily starts the C6 radio stack unless a saved BLE controller already started
it after the launcher became usable. H1 content upload remains serviced while
BLE owns game traffic. Pairing a controller temporarily pauses room browsing;
an established controller link and one multiplayer peer fit the committed
two-link budget.

The encrypted BLE link, explicit Host/Join room discovery, selected Doom room,
host start, synchronized launch, and live two-board gameplay have hardware
acceptance on Console OS 0.4.83. The operator also reported visibly smoother
gameplay, but that is qualitative rather than a measured FPS claim. Concurrent
rooms advertising different games, room isolation under radio contention, and
clean recovery after either badge powers off remain separate acceptance work.

## Mac-hosted BBS realms

Turn-based BBS doors do not use a P4MP lobby as their world-size limit. The
LORD backend opens one logical two-slot session per console: the Mac owns slot
0, a console joins slot 1, and the Mac automatically initiates synchronized
start. Repeated H1 workers plus one BLE peripheral all share one authoritative
SQLite realm. LORD currently caps that realm at 100 stable player profiles and
pages the other 99 records eight at a time. Mail, friendship, team state,
ChompCoin transfer, PvP, feeds, snapshots, presence, and the hourly realm day
are server-owned cross-session data.

This is still a local transport deployment, not a cloud socket API. The game
continues to receive only the bounded `multiplayer-session` capability. See
`docs/LORD_REALM_HUB.md` for the operator topology and trust boundary.

## Product modes

1. Same-device multiplayer comes first. Console OS maps up to four local
   controllers/touch sets to stable player slots, with no networking.
2. Native games use the OS-owned `multiplayer-session` boundary plus the
   declarative `game.json` profile. Turn-based/realtime games normally use
   host authority; lockstep games exchange their bounded deterministic game
   messages. P4 Yahtzee is the turn-based reference.
3. P4 Carts may later consume the same profile vocabulary for deterministic
   lockstep. The lobby already
   requires an exact cart ID, API version, content SHA-256, player count, and
   session seed.
   The OS exchanges delayed input frames and periodic state hashes; a mismatch
   ends the match rather than allowing divergent state.
The intended links are local wired UART and opt-in BLE: no account, cloud
relay, public matchmaking, arbitrary Internet listener, or Wi-Fi game mode.

## Remaining implementation order

1. Test simultaneous BLE rooms advertising different games and prove selected
   room isolation under radio contention.
2. Complete direct-UART two-console Doom acceptance and record the J3 cable.
3. Record disconnect, timeout, desync, and malformed-packet behavior for both
   transports.
4. Hardware-accept one P4 Yahtzee match launched from the multi-game lobby over
   the selected two-console transport without weakening the existing Doom
   evidence boundary.


## Tab5 0.45 standalone radio candidate

Core OS provides USB relay, Bluetooth and **Local Wi-Fi** choices in the shared
Multiplayer screen. Host creates a room; Join selects a nearby room. Local
Wi-Fi creates a tablet-owned AP on demand, scans only versioned P4 game room
names, binds association to the selected BSSID, obtains DHCP automatically,
and carries the existing P4MP datagrams over UDP port 42424. It requires no
router or internet. Only one guest associates; the current game adapter
supports two human players. This local AP is open (no password/encryption),
has no uplink/forwarding and exposes no content-transfer or administration
service. Bluetooth retains the existing encrypted, bonded GATT path.

Scanning/connection/RPC work runs on an OS worker. Active games perform bounded
nonblocking sends and at most eight received datagrams per poll. Exact package
compatibility, lobby handshake, start synchronization and game messages remain
owned by the existing P4MP service. Invalid sizes/CRC/session/source endpoints
are rejected; link state expires after three seconds without received traffic.
Games do not implement radio drivers. A single-player cartridge still needs a
multiplayer implementation before it can participate.

Wacky Wheels' local prototype uses the same OS service for its two-player
sprint. Its game protocol has host tests; this statement is not hardware
acceptance. Radio and charging evidence must name the exact 0.45 image/unit.

## Tab5 0.53 four-racer candidate

The 0.53 native-game adapter accepts declared profiles up to four players.
Local Wi-Fi admits three guests, keeps a separate bounded route per guest,
and addresses JOIN acceptance to its recipient. The host may keep admitting
players after the first joins, until Start freezes the roster. A reusable
`p4_multiplayer/group.c` start exchange waits for every admitted player's ACK;
missing members time out before gameplay. Bluetooth, the USB serial relay,
and Doom retain their existing two-player limits.

Wacky Wheels 0.1.0 uses protocol 2 (64-byte maximum) for two to four racers.
The old track-one prototype is incompatible and must be updated on all peers.
Its game ID stays `org.p4console.wacky-probe`; launcher ID changes from 120 to
9001 to avoid the current Tide Maze identity. Three/four-instance tests pass on
all five race courses, and shared group-start plus real UDP route tests pass.
Physical four-console acceptance and device cadence remain pending. See
`test-runs/2026-10-06-wacky-wheels-full-port.json` for exact artifact/unit results.

## Game Changers AI arena candidate (Tab5 0.56)

The special **Multiplayer → Game Changers AI** mode uses the OS local Wi-Fi
room with one playing host and up to three guests. Its separate Doom adapter
relays bounded four-slot command batches with per-guest recovery; ordinary
Doom/Chex and Bluetooth/serial keep their existing two-player path. The host
stays the server even while its player takes a break. After 15 seconds without
movement/fire, that visit ends; Use returns the connected player at zero.
New/disconnected consoles join at the initial lobby, not during a running world.

During arena startup, the Doom adapter continues answering valid lobby READY
retries with COMMIT after taking over the session. This recovers a guest that
missed the original commit burst. Session/seed and roster checks remain in force,
replies are bounded, and only all-player engine readiness releases canonical
tics. The four-process lobby-to-Doom burst-loss regression covers this recovery;
the shared native-game handoff is a separate path.

See [mode behavior, storage, tests and limitations](GAME_CHANGERS_AI_DOOM.md).
This source/build candidate does not extend historical two-board radio evidence
to four physical consoles. The supplied Pure Hell two-map loop is the default;
the host can select either Pure Hell arena or DWANGO 5 MAP01–24. The in-game
menu proposes a map and takes a strict-majority vote through synchronized tic
commands, preserving visit scores. All consoles admit the same exact three-WAD
bundle with notices. Physical acceptance remains pending.
