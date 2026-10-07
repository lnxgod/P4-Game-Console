# Console OS multiplayer foundation

Multiplayer is an OS service, never a socket API exposed to a game. The
implemented slice is an allocation-free C packet and session core with strict
length checks, CRC32 corruption detection, route binding, replay rejection,
four-player limits, and immediate neutral input on leave, timeout, or transport
disconnect. A bounded byte-stream decoder now sits beneath it so chunked or
noisy UART and USB-device traffic cannot hand malformed lengths to the session
core. Console OS owns a direct-first dual-UART endpoint, a multi-game lobby,
the lockstep Doom tic adapter, a bounded native-game message bridge, and
transport handoff between wired UART, BLE and local Wi-Fi gaming. Tab5 supports
up to four players over local Wi-Fi; BLE and wired relay support two. The
lobby enumerates Doom Arena by Game Changers, Doom, Chex Quest, and installed cartridges that declare the
optional `multiplayer-session` capability. It advertises the selected game
identity and exact content hash. Tab5 uses the game-first workflow below.

The legacy Waveshare first Multiplayer screen is an explicit
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

## Tab5 game-first workflow

On each Tab5, open Multiplayer and follow **Game → Connection → Host or Join**.
Choose the same installed game and connection on both consoles. The game
chooser uses installed-package metadata and the Arena content-presence check;
Doom/Chex exact data validation stays deferred until launch. The game chooser shows
up to twelve games at once, with paging for additional installed titles. Local
Wi-Fi shows its startup state, keeps Back available, and enables Host/Join only
when ready; a failed start gives explicit retry guidance. The connection
chooser enables only the routes supported by that game. Doom Arena by Game Changers uses
**Local Wi-Fi**. Its host settings expose the map selection; Arena's mode and
other gameplay rules remain fixed. Other games retain their applicable match
settings and transport choices.

The host chooses **Host game**, adjusts match settings, and creates a room.
The guest chooses **Join game**, selects a room, and joins it. Tab5's room list
filters by the selected game's discovery token and excludes rooms advertised
as full. The handshake still checks the complete compatibility hash; Wi-Fi
beacons do not expose a live player count, so admission can still reject a
full room. Game filtering happens before filling the Wi-Fi scan cache and the
bounded visible list. Changing the connection rebuilds the local offer, including
player capacity and its compatibility token, so a four-player native game
selected while using Bluetooth has the correct identity after switching to
Wi-Fi.

Room selection stays bound to the advertised session and radio route when scan
results reorder. A delayed row tap resolves its original advertised session;
if that session has disappeared or occurs on multiple routes, the previous
selection is cleared and the guest must choose again. The UI retains the game
and connection context while showing discovery, joining, connected and launch
progress. Failures remain visible with retry guidance instead of returning
silently through the setup choices. The host starts the match only when its
guests are ready; guests wait in the room for that synchronized start.

A retried Join from an already admitted peer must still match the exact route,
peer identity, compatibility hash and assigned slot. A valid retry resends
Accept for the existing admission, including after group start has begun. It
does not admit a second player or reset the start barrier. This recovers a
lost Accept without sending the guest repeatedly back through discovery.

During Doom-family content validation, the calling task polls the prepared
network adapter between bounded SD hash blocks and Arena structural-validation
reads. Arena exchanges a distinct
loading heartbeat that preserves the Wi-Fi route without declaring the engine
ready. This prevents a validation pass longer than the three-second route lease
from leaving both peers unable to send their startup messages. The readiness
barrier still requires each engine to configure successfully.

Arena also services its prepared session between bounded VFS reads while the
engine loads sprites and textures. This hook is active only before engine
configuration begins; ordinary gameplay reads do not poll through it.

Host verification for this change:

- `make console-multiplayer-flow-host` executes the production firmware's
  selection, offer and room-filter C functions with controlled hardware
  boundaries. Its eight cases cover connection-dependent identity, Wi-Fi and
  BLE filtering, stale and ambiguous selections, Arena transport restrictions,
  and missing game data. Five regression cases fail against the original
  source; all eight pass with the changes.
- `make p4-multiplayer-host` passes the four C suites, including Join retry
  coverage, and the serial relay test. The Wi-Fi component's host suite also
  passes.
- `make doom-multiplayer-host` passes 26 tests under ASan/UBSan. The new startup
  tests combine the production Wi-Fi UDP transport and Arena adapter with a
  controlled clock: a five-second loading pass keeps its route, an unserviced
  expired route is rejected, and loading heartbeats cannot satisfy readiness.
  The production VFS bridge is also exercised with slow aligned and unaligned
  reads, failed reads with cursor preservation, and no loading-hook network
  polling after configuration begins.
  Eight additional virtual-clock cases execute the production Arena adapter:
  host/client readiness delayed 45 seconds succeeds; continuous loading stops
  at five minutes; silence, replay, malformed/lobby-only traffic, and a missing
  peer among otherwise active peers fail without releasing gameplay tics.
  The previous adapter fails both delayed-ready cases at 30 seconds.
- `make platform-game-storage-host` passes seven model/reader tests under
  ASan/UBSan. The six existing suites include
  progress through uncached WAD directory reads and callback restoration after
  successful and failed validation. The new block-reader regression requires
  one descriptor read for a complete 4 KiB block and checks the returned bytes
  and advanced cursor. It also covers partial reads, bounded EINTR retries,
  EOF, errors after partial progress, invalid seek results, null buffers and
  offset/length overflow. It failed against the previous stdio reader before
  passing with the descriptor reader. The callback path in the ESP storage
  backend is covered by the pinned Tab5 build; actual SD timing remains part
  of device acceptance.
- The Console Shell host suite passes seven tests, including Tab5 navigation
  and legacy views. Full-frame captures and the ImageGen concept are in
  [the design record](../design/tab5-nextgen/multiplayer-flow/README.md).
- `P4_TAB5_FIRMWARE_ONLY=1 make console-os-tab5-idf` passes with the pinned
  toolchain. The ELF resolves the Arena loading hook to its strong
  implementation. An earlier, uninstalled firmware candidate built on
  base `467bf9261b4f` is 4,324,160 bytes, SHA-256
  `19aa5c4665ffb7847c0ce21272d024c81d7f1946e66ea0006e9eae9015b460a4`.
  This verifies firmware and update packaging, not the content bundle or a
  device install.

The 0.60 candidate carries these fixes onto upstream `51aefe100804`, preserving
the installed 0.59 library, Pure Hades content and USB content batches. Its
firmware-only build is 4,329,312 bytes, SHA-256
`036692bcc1a2e792cfd3ce510069ce41d1797351f2a95c724fa4e28ad10b5ff7`.
It adds [USB debug controls](USB_DEBUG_CONTROLS.md) for bounded remote touch and
buttons through the ordinary launcher/native/Doom input paths. Use a persistent
exclusive USB connection per unit and local Wi-Fi for a two-device test.

The 0.61 candidate additionally fixes Arena's SD block reader. Inspection of
the pinned Newlib implementation in the 0.60 ELF confirmed that `_IONBF`
sets a one-byte stdio buffer and `fread` repeatedly refills it. Thus each
requested 4 KiB block caused 4,096 one-byte descriptor reads; Arena's initial
33,308,283-byte content pass alone required over 33 million such reads.
The replacement uses direct `lseek`/`read` calls bounded to 4 KiB under the
existing storage mutex, with no stdio read-ahead. It retains the advanced
descriptor position so sequential reads can reuse FatFS's current cluster;
the pinned SDK's `pread` would restore the prior cursor after each block.
`FILE` remains the lifetime owner through `fclose`. Exact whole-file SHA-256,
per-block hashes, structural validation and loading callbacks remain in place.

The firmware-only 0.61 build passes with app SHA-256
`0733e6f5955da30587b3052020dc46014cbc73c495e1b494a2c2fce3f66f6107`.
The [0.61 two-unit runtime record](../hardware/test-runs/2026-10-07-tab5-061-multiplayer-runtime.json)
shows both consoles joined the same lobby and completed exact content validation
in 37.490 seconds (A) and 26.969 seconds (B). Both handed off to Doom. During
renderer initialization A exhausted DMA-capable memory and rebooted; B reached
configuration first, exceeded the fixed 30-second startup deadline, and rebooted.
Neither reached engine READY. The earlier
[0.60 loading failure](../hardware/test-runs/2026-10-07-tab5-060-multiplayer-runtime.json)
remains preserved separately.

Candidate 0.62 enables Tab5 FatFS fast seeks with bounded 64-word maps and
moves the CPU-only Doom `visplanes` and `openings` arrays into PSRAM. The linked
ELF verifier checks both placements. Configuration now allows up to five minutes
while each required peer supplies valid startup messages within 30 seconds;
loading heartbeats never satisfy engine readiness or release gameplay ticks.
USB debug status is serviced during loading/configuration, with periodic DMA
memory telemetry. The multiplayer chooser shows twelve titles at once, preserves
the full Doom Arena by Game Changers label, and exposes Local Wi-Fi startup
and retryable failure states.

The separate [0.61 menu investigation](../hardware/test-runs/2026-10-07-tab5-061-menu-investigation.json)
found nine installed native multiplayer entries on each console and measured
Local Wi-Fi initialization at 2.460 seconds (A) and 2.461 seconds (B). Remote
touch selections, status and Back remained responsive in that run; it did not
reproduce a persistent hang or capture the rendered frames.

The [0.62 two-unit follow-up](../hardware/test-runs/2026-10-07-tab5-062-multiplayer-runtime.json)
captured the twelve-title menu and full Arena name
from both device-submitted framebuffers. Local Wi-Fi initialized in 2.460 seconds
on each unit, and Back/reselect remained responsive. Both joined one room and
eventually reached engine READY, but the match failed: A dropped B while B was
still loading its level, and B aborted and restarted. READY did not establish
two-player gameplay. B also reported FatFS fast-seek reason 17: the allocated
64-word cluster map was too small for its fragmented WAD.

Candidate 0.63 increases the bounded Tab5 fast-seek map to cover every possible
fragment of the largest pinned Arena WAD, including 512-byte clusters. The
[Tab5 storage notes](boards/M5STACK_TAB5.md) record its PSRAM bound. Loading-time
transport service avoids engine reentry during WAD reads, preserves the ordinary
stall window after loading, and bounds both initial and later loading waits.
The 41-case multiplayer host suite and loading-presentation test pass;
two-console gameplay acceptance remains pending. The operator authorized SD
backup, formatting and verified restoration if needed; no card has been formatted
as part of the firmware change.
Historical Waveshare results apply only to their recorded boards and builds.

## Joining a running Arena match

An Arena host can start alone over Local Wi-Fi. A first-time guest chooses
Multiplayer → Game Changers AI Arena → Local Wi-Fi → Join, selects the host's
room, then chooses Join selected. The host keeps playing its current match;
no second Start or pregame Ready action is required. Other Doom modes and
native games keep their existing start barriers. This path uses Arena protocol
7 and GCR2 control messages, so older Arena peers cannot mix into the room.
After admission, all packets for that attempt carry its GCE1 nonce envelope.
The adapter checks it before changing session state; delayed input, progress
or leave packets from an earlier attempt cannot affect the new connection.

Arena reserves four engine slots and freezes the actual starting roster in an
immutable initial mask. A solo match begins with only the host in that mask;
unused slots have no actor or input. A first guest receives a never-owned slot,
while a returning guest receives its original reserved slot. Running room
occupancy counts permanently reserved and pending seats, rather than active
players. A room with four reserved seats appears only to a console retaining
a matching return record. The complete offer must still match the saved host,
session, seed and compatibility before the console sends its ticket.

A guest that leaves Arena through Quit can return after its console's software
restart using the same room-selection sequence. Its retained ticket cannot
claim another player's slot or another match. A cold boot or crash discards
the retained ticket. The version-2 retained record also binds the original
initial mask and capacity. Local Wi-Fi remains an open local transport: the
bearer ticket binds continuity to the saved match and current route; it does
not encrypt network traffic.

Arena protocol 8 uses a bounded exact checkpoint and canonical suffix after
content loading; see [Doom Arena checkpoint admission](DOOM_ARENA_CHECKPOINT.md)
for ownership, memory, deadlines and verification limits. Its rolling history
removes the match-age admission cap. Physical checkpoint acceptance is pending.
The following replay-from-start description and 4 MiB limit record the protocol
7 behavior and its historical proof; they are not protocol 8 resource limits.

In protocol 7, every joining guest starts from the original map and settings, then replays
the host's canonical tic journal to reconstruct the current game. The session
seed binds the protocol identity; gameplay RNG uses the engine's ordinary
vanilla reset. Live input, audio and presentation stay gated during catch-up.
The guest becomes active only at the host's chosen future tic, after
synchronization and its first real input. The host preserves its world, score,
map and timeline. Discovery, slot admission, history transfer, catch-up and
activation are separate gates. Physical two-Tab5 acceptance against the exact
installed artifact and units remains pending for this path.

The host reserves a bounded 4 MiB history journal, about 49.9 minutes at 35
canonical tics per second. Allocation failure, missing history or exhausted
capacity closes admission while the existing match continues. A returning
ticket holder receives an explicit unavailable result. A host shutdown or new
match ends the old reservations. The match retains at most 64 fresh admission
records and each guest slot permits at most 64 admitted attempt nonces,
including fresh admission. Old nonces remain retained until the match ends,
so delayed requests cannot reopen old attempts. Attempts can expire after
30 seconds of silence or five minutes total without stopping the host.

A first guest's ticket is provisional until a canonical frame activates its
slot. Expiry before activation releases that unused seat and retains the old
attempt identity. If this guest restarts before activation, its saved ticket
cannot take over the still-live lease. After the old lease expires, an exact
host reply clears that provisional ticket and asks the user to select the room
again for a fresh attempt. Mismatched replies cannot clear it. Tickets for
previously active guests remain reserved through departures and failed return
attempts.

Host verification separates the OS admission, adapter and engine boundaries:

- `make console-multiplayer-flow-host` runs 12 production-C admission cases,
  six solo/group start and configuration cases, nine room-flow cases, and
  22 loading, handoff and snapshot cases alongside transport checks. These cover stable
  retry nonces, saved identity matching, persistence before launch, exact
  host/route admission, provisional expiry, full-room return visibility and
  failed-load recovery, reopened admission after pre-start departures, and
  unchanged ordinary Doom/native start gates.
- `make doom-multiplayer-host` runs the adapter, codecs and OS checks under
  ASan/UBSan. The first-guest fixture exercises real journal exhaustion, all
  64 fresh admissions, exact retries, lease revocation, retained tombstones,
  canonical activation, ACK ordering and forged identity rejection. Mocked
  adapter delivery remains separate from real engine reconstruction.
- `python3 scripts/doom/test-arena-rejoin.py --negative-consistency` compares
  2,705 strict state snapshots from independent host and cold-guest engine
  processes under ASan. The [original-roster engine fixture](DOOM_ARENA_REJOIN.md)
  records its coverage, exclusions, local data inputs and reproduction.
- `python3 scripts/doom/test-arena-late-join.py --negative-consistency` compares
  a solo host with a true one-player engine for 1,024 snapshots, a first cold
  guest for 1,702 snapshots and its cold return for 2,705 snapshots. It requires
  identical world, actor references, RNG and Arena rules; negative controls
  detect premature virgin actors and a reset consistency history.

These host checks do not establish physical first-join/exit/rejoin, rendered
cross-device determinism or on-device cadence. Engine reconstruction proof is
separate from P4MP delivery and two-console gameplay acceptance.

Network profiling uses `P4_DoomNetGetStats` on the foreground network owner
task. Its cumulative counters reset at adapter preparation. Poll calls, total
microseconds and maximum microseconds include every public `P4_DoomNetPoll`
call, including direct engine calls and early returns. The maximum is since
preparation, not the last reporting interval. Snapshot reads neither poll nor
clear the counters; the counters saturate instead of wrapping.

Send attempts include encoding failures; send failures count encoding or
transport callback errors. Received packets count frame callbacks, while
rejections count route/session-envelope rejection rather than every game
payload refusal. Departures count the first bound-route departure per peer
lifecycle. Arena host blocked-peer samples count each peer whose ACK is behind
or whose required tic is missing in an ordinary runtime poll. Loading and
resuming peers are excluded. This sample count is not time stalled or measured
packet loss. There is no per-poll logging or change to retry/timeout decisions.
`python3 scripts/tests/test-doom-net-stats.py` checks the production polling,
snapshot and reset paths with a deterministic clock; the adapter fixture also
checks failed sends, rejected packets, missing guest input and departures.

## Declarative native-game profiles

Native `.P4G` games describe their networking behavior in the same
`game.json` that already declares title, capabilities, and package identity.
The smallest profile is `{"schema":1,"style":"turn-based"}`. Console OS
normalizes it into player limits, simulation rate, lockstep delay, protocol,
and message budget; packages do not select a physical transport. The profile
is validated at build time, encoded in bounded package metadata, exposed to
the game through `p4_game_multiplayer_read_profile()`, and included in lobby
compatibility. See `docs/GAME_SDK.md` for the complete field bounds.

This keeps the extension point data-driven: a new game appears in Tab5's game
chooser (the Host game list on the legacy UI), can be resolved from an
advertised Join row, and receives the
existing sanitized session API without adding a game-specific BLE service,
UART parser, socket, lobby screen, or launcher table.
Console OS performs this registration itself when it scans an installed P4G;
cartridge code has no registration API and cannot mutate the registry. The OS
accepts only validated package metadata, rejects duplicate identities and
profiles the current transport cannot host, and rebuilds the bounded registry
after catalog changes. Thus a newly installed kid-created game is registered
before its first launch simply by declaring the profile in `game.json`.
Profiles describe up to four players. Tab5's local Wi-Fi transport supports
that capacity; the two-player BLE and wired paths retain the same Game API v1
and package manifest shape.

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
to four physical consoles. The Pure Hades five-map loop is the default;
the host can select any Pure Hades arena or DWANGO 5 MAP01–24. The in-game
menu proposes a map and takes a strict-majority vote through synchronized tic
commands, preserving visit scores. All consoles admit the same exact three-WAD
bundle with notices. Physical acceptance remains pending.
