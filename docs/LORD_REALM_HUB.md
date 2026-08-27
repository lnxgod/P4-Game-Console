# LORD Mac BBS realm server

LORD 1.6.0 uses the Mac as the authoritative BBS-style realm server. Every
console chooses **Join**. A console never hosts the shared world.

```text
P4 console 1 -- H1 USB -------+
P4 console 2 -- encrypted BLE +--> Mac P4MP host --> one SQLite LORD realm
P4 console N -- H1 USB -------+          |
                                           +-- up to 100 player accounts
```

Each physical link is an independent two-slot P4MP session: the backend owns
slot 0 and one console joins slot 1. These point-to-point sessions are only
transport tunnels. They all share the same SQLite realm, so P4MP's current
two-player Console OS runtime is not the LORD player limit.

The realm accepts 100 stable player profiles. LORD displays an eight-player
page at a time and provides Previous/Next realm-page rows for the complete
99-other-player roster. The backend owns player snapshots, presence, mail,
friendship, teams, mentoring, PvP leases/results, tavern/news feeds, carried
and vaulted ChompCoin, and the hourly realm-day clock. Offline players receive
numbered durable events when they next join.

## Requirements

- The Mac and every console must use the exact same `LORD.P4G`. The server
  derives the content and compatibility hashes from the supplied cartridge
  and rejects a mismatched Join.
- H1 USB needs Python 3 and `pyserial`.
- BLE needs PyObjC CoreBluetooth; installing `bleak` on macOS installs that
  framework binding.
- BLE backend hosting requires Console OS 0.4.84 or later. CoreBluetooth can
  advertise a service UUID but not P4 room service data, so 0.4.84 recognizes
  the reserved UUID-only Mac room. The encrypted GATT connection must still
  pass the normal exact P4MP Offer/Join compatibility check.
- Persistent offline LORD progress on the authorized Waveshare profile
  requires Console OS 0.4.85 or later. Older firmware can join the realm but
  advertises session-only save storage.
- Use one stable `PROFILE` label for each player. A profile label is the local
  account binding and maps deterministically to one opaque 16-byte actor ID.

Install dependencies once:

```sh
python3 -m pip install pyserial bleak
```

The default database is ignored at
`local-data/realm/lord.sqlite3`. Stop the server before copying it for backup
so SQLite can close its WAL cleanly.

## H1 USB server

Connect every console's H1 CH343 port to the Mac. H2 remains available for the
qualified controller fixture. Start one server process and repeat `--usb` for
each attached console:

```sh
python3 scripts/p4-realm-hub.py \
  --cartridge /absolute/path/to/LORD.P4G \
  --usb "alice=/dev/cu.wchusbserial110" \
  --usb "bob=/dev/cu.wchusbserial120"
```

On each console:

1. Open **Multiplayer** and choose **Join**.
2. Choose **Wired Auto**.
3. Select the advertised LORD server row and confirm **Join Selected**.
4. Wait while the backend accepts slot 1 and automatically sends the existing
   synchronized-start barrier.

There is no backend Start button and no console Host step. Console OS launches
LORD after the barrier; LORD changes from `SYNCING` to `MAC REALM` after its
welcome and snapshot exchange.

Do not run a firmware monitor, content uploader, or two-console relay on an H1
port while the server owns it. The server opens H1 exclusively and keeps DTR
and RTS inactive so attaching it does not intentionally reset the console.

## BLE server

Run one BLE profile in the same process as any H1 workers:

```sh
python3 scripts/p4-realm-hub.py \
  --cartridge /absolute/path/to/LORD.P4G \
  --usb "alice=/dev/cu.wchusbserial110" \
  --ble-profile bob
```

On Console OS 0.4.84 or later choose **Multiplayer → Join → BLE**, wait for
the LORD server row, and join it. The console is the BLE central; the Mac is
the encrypted GATT peripheral and P4MP host. The Mac supports one BLE console
through this process, while repeated H1 workers can serve more consoles.

The BLE characteristic requires encrypted writes. Pairing is still local
Just Works and does not prove which physical console is present, so the
operator must keep profile-to-device assignments trustworthy.

## Realm and scaling rules

- `MAX_REALM_PLAYERS` is 100. Existing profiles can reconnect after the realm
  reaches capacity; creation of profile 101 fails closed.
- One shared `RealmStore` uses a process lock, separate bounded SQLite
  connections, WAL mode, foreign keys, and immediate transactions for
  cross-player mutations.
- The roster is ordered stably by name and opaque actor ID, eight records per page.
  A page includes opaque actor ID, display profile, PvP stats, combat stats,
  ChompCoin, directional trust, and team state.
- Presence is a 90-second UI hint. It is not authentication.
- The trusted realm day advances every hour. Reconnect grants at most one
  missed refresh and never loops catch-up interest.
- Full snapshots use compare-and-swap revisions. Repeating the same
  actor/nonce/body is idempotent; stale or changed reuse fails closed.
- A schema-5 local save remembers the last accepted actor and server revision.
  Offline edits upload only if that base still matches the server head. If both
  copies advanced, LORD shows `SYNC CONFLICT` and preserves both rather than
  choosing or field-merging them.
- Events remain pending until the target cartridge applies and acknowledges
  their monotonically increasing event ID.

The service is a trusted local BBS deployment, not an Internet-facing game
server. It has no password signup, TLS listener, remote administration,
moderation, public rate limiting, or hostile-client combat attestation.

## Protocol

P4MP remains version 1. LORD uses protocol `0x4c53` and bounded 1–64 byte Game
Messages. `P4RM` v3 provides persisted-base hello/welcome reconciliation,
complete snapshot transfer,
profile publication, paged roster records, hourly clock, typed actions, and
durable events. Records are capped at 4,148 bytes and use explicit lengths,
little-endian fields, CRCs, stop-and-wait acknowledgement, and one-second
retry.

The UUID-only BLE discovery sentinel is `0x4c4f5244`. It is used only to make
the CoreBluetooth peripheral selectable before GATT connects. The subsequent
Offer carries the exact game ID, API, multiplayer profile, content SHA-256,
compatibility SHA-256, and nonzero session seed; a mismatch cannot launch.

## Verification

```sh
PYTHONPATH=. python3 -m unittest tools.p4_realm_hub.tests.test_protocol -v
cmake -S games/lord -B build-host/lord -G Ninja
cmake --build build-host/lord
ctest --test-dir build-host/lord --output-on-failure
make p4-multiplayer-host
./scripts/build-waveshare-console-os.sh
python3 scripts/verify-console-os-waveshare.py \
  apps/console_os/build-waveshare-landscape
```

A successful build is not hardware acceptance. Release evidence must name the
exact console, firmware/P4G hashes, route, retained serial output, and observed
Join, automatic start, realm online, reconnect, rollover, and conflict results.
