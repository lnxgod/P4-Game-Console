# LORD Mac realm hub

LORD 1.4.0 can use a Mac as a local BBS-style realm hub without adding a new
Game API ABI or changing P4MP v1. The console hosts the normal two-player LORD
room. The Mac joins the second slot and carries the bounded `P4RM` realm
protocol inside existing 1–64 byte P4MP Game Message packets.

```text
LORD cartridge -> Game API multiplayer-session -> Console OS P4MP v1
                                                       |
                                             H1 USB or encrypted BLE
                                                       |
                                      scripts/p4-realm-hub.py
                                                       |
                                local-data/realm/lord.sqlite3
```

This implementation is host-tested. The H1 and BLE paths still need retained
on-device evidence before either may be called hardware-qualified.

## What works

- A stable operator-assigned Mac profile maps to one opaque 16-byte actor ID.
- The complete CRC-protected `LRSY`/`LDSV` character snapshot uploads and
  downloads in 48-byte stop-and-wait chunks.
- SQLite commits use compare-and-swap revisions and one-use idempotency
  nonces. A stale device gets `SYNC CONFLICT`; it cannot silently overwrite a
  newer head.
- Other connected characters appear in LORD's warrior directory with bounded
  name, class, level, health, combat stats, experience, ChompCoin, PvP record,
  inn status, and online presence.
- The Mac supplies one trusted realm day per hour. A character that missed one
  or many hours receives exactly one refresh on reconnect, with no catch-up
  loop and no bank interest. Offline inn sleep keeps the classic local rules.
- The same database can serve several H1 consoles at once. One optional BLE
  console can run in the same hub process.
- Letters, 100-ChompCoin bank transfers, encouragement/shared supplies,
  adventure-team invitations and reciprocal acceptance, mentoring, leased
  asynchronous PvP outcomes, tavern conversation, and town announcements are
  committed as nonce-idempotent hub actions. Their target events remain in
  SQLite until the target cartridge applies and acknowledges the numbered
  event.
- Transfers publish both carried and vaulted ChompCoin privately to the hub.
  The hub validates the source vault, commits its debit and the recipient
  credit together, then queues one durable event to each console. Vault
  balances are never exposed in the player directory.
- If no multiplayer session is supplied, LORD remains the complete offline
  game and continues using ordinary local saves when available.

The complete snapshot includes the character's local mail, news, PvP,
friendship, team, built-in IGM, and ChompCoin state, so those fields follow the
same actor between consoles. Version 1.4.0 adds the local-hub cross-actor layer
listed above. It is still a trusted LAN/USB deployment: it has no public
signup, password, TLS listener, moderation console, or hostile-client economy
validation beyond the bounded installed cartridge protocol. It must not be
described as a public Internet BBS.

## Mac setup

H1 USB requires Python 3 and `pyserial`. BLE additionally requires `bleak`:

```sh
python3 -m pip install pyserial
python3 -m pip install bleak
```

The database defaults to `local-data/realm/lord.sqlite3`, which is ignored by
Git. Stop the hub before copying the database for a backup so the SQLite WAL is
fully closed.

Use one stable profile label per character, and do not connect the same label
from two consoles at once. If that happens, compare-and-swap protection keeps
the newer head and the stale console shows `SYNC CONFLICT` rather than merging
or duplicating state.

### H1 USB

1. Connect each console's H1 CH343 port to the Mac. H2 remains available for
   the controller-first powered host fixture.
2. On the console open **Multiplayer**, choose **HOST**, select **LORD**, leave
   **LINK** at **WIRED AUTO**, and press **OPEN ROOM**.
3. Find the exact H1 port on the Mac, normally `/dev/cu.wchusbserial...`.
4. Start the hub with a permanent profile label for that player:

```sh
python3 scripts/p4-realm-hub.py \
  --usb "alice=/dev/cu.wchusbserial110"
```

Several H1 consoles can share the Mac process:

```sh
python3 scripts/p4-realm-hub.py \
  --usb "alice=/dev/cu.wchusbserial110" \
  --usb "bob=/dev/cu.wchusbserial120"
```

When the console shows the guest connected, the console host presses **START
MATCH**. The Mac answers the existing synchronized start barrier, Console OS
launches LORD, and the town status changes from `SYNCING` to `MAC REALM`.

Do not run the content uploader, firmware monitor, or two-console relay on the
same H1 port while the realm hub owns it. The hub opens H1 exclusively and
keeps DTR and RTS inactive so attaching it does not intentionally reset the
console.

### BLE

1. In **Multiplayer**, select **LORD**, change **LINK** to **BLE**, then create
   a lobby.
2. Run the hub using the displayed room/session ID. Decimal and `0x` hex values
   are accepted:

```sh
python3 scripts/p4-realm-hub.py \
  --ble-profile alice \
  --ble-room 0x1234abcd
```

3. When the Mac has joined, press **START MATCH** on the console.

The BLE adapter validates the complete P4MP room beacon, connects only to a
console-hosted two-player room, uses the existing P4B fragment format, and
requires the Console OS protected GATT path. BLE LE Secure Connections protects
the link, but the existing Just Works pairing does not authenticate which
physical console is present. This local hub therefore relies on the operator
to choose the intended room and profile.

## Realm protocol

`P4RM` v2 has a 16-byte little-endian header and at most 48 payload bytes. Its
profile number in the LORD multiplayer manifest is `0x4c53`. It uses only
P4MP packet type 11, Game Message; old P4MP decoders, the relay, BLE framing,
CRC, replay checks, route binding, and timeouts remain unchanged.

The message set is: hello/welcome, download begin/chunk, upload begin/chunk,
acknowledgement, commit result, clock, error, profile/profile stats, directory
summary/stats, action begin/body/result, and event begin/body/acknowledgement.
Full records are capped at 4,148 bytes and 87 chunks. Snapshots, action bodies,
and event bodies use bounded stop-and-wait delivery with one-second retry,
which stays below Console OS's eight-message receive queue. Reusing an action
nonce with the same request returns the stored result; changing the request
under that nonce fails closed. The cartridge persists the last applied event
ID in save schema 4 and acknowledges old retries without applying them again.

## Verification

Run the focused host checks from the repository root:

```sh
PYTHONPATH=. python3 -m unittest tools.p4_realm_hub.tests.test_protocol -v

cmake -S games/lord -B build-host/lord -G Ninja
cmake --build build-host/lord
ctest --test-dir build-host/lord --output-on-failure

make game-registry-check
make game-sdk-host
make console-os-waveshare-idf
```

A successful build proves source integration only. Hardware acceptance must
name the exact console, firmware artifact SHA-256, connection route, retained
serial evidence, and the observed upload/relaunch/rollover/conflict behavior.
