# LORD Mac BBS realm server

LORD 1.6.1 uses the Mac as the authoritative BBS-style realm server. Every
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
  requires Console OS 0.4.85 or later. Console OS 0.4.88 adds authenticated
  device-local `P4SAVE2` objects and permanent legacy-downgrade closure; older
  firmware may save but does not provide this casual SD-edit resistance.
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

The launcher rejects duplicate profile labels, duplicate USB ports, and an
`--adopt-local` label that is not exactly one configured link.

## One-time upgrade of legacy multiplayer artifacts

Ordinary startup never guesses how an older database's shared events should be
classified. Stop the hub and make a complete backup first. If startup reports
that legacy multiplayer artifacts need authorization, run the same command
exactly once with `--migrate-legacy-artifacts`:

```sh
cp local-data/realm/lord.sqlite3 \
  local-data/realm/lord-before-artifact-migration.sqlite3
python3 scripts/p4-realm-hub.py \
  --cartridge /absolute/path/to/LORD.P4G \
  --database local-data/realm/lord.sqlite3 \
  --usb "alice=/dev/cu.wchusbserial110" \
  --migrate-legacy-artifacts
```

The transaction preserves every raw row, permanently quarantines artifacts
whose actors did not both have accepted heads, reconstructs only exact typed
economy rows, and uses a schema-4/5 head's persisted event cursor as the sole
proof of consumption. An old wire acknowledgement becomes receipt history; it
does not prove application. Ambiguous pending legacy PvP or inconsistent
ledger/head evidence aborts without a partial migration and needs operator
review. After one successful startup, stop the hub and remove the flag from
normal launches. Repeating a completed migration is harmless, but leaving the
authorization enabled weakens the intended operator gate.

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

## Resolve one preserved local conflict

The default conflict path never chooses a winner. Back up the stopped SQLite
database first. To select one fully validated local character exactly once,
start the hub with an explicit parent/operator grant for that configured link:

```sh
python3 scripts/p4-realm-hub.py \
  --cartridge /absolute/path/to/LORD.P4G \
  --database local-data/realm/lord.sqlite3 \
  --usb "pink=/dev/cu.wchusbserial5C371865781" \
  --adopt-local pink
```

The welcome uses the additive P4RM v3 `ADOPT_LOCAL` flag. The transaction
validates the complete local LDSV, archives the prior head and SHA-256,
installs the selected snapshot at the next revision, derives its public
profile and realm-day anchor, and consumes the grant atomically. It never
field-merges ChompCoin, XP, mail, PvP, teams, or daily state. Remove
`--adopt-local pink` from later starts; a consumed grant cannot be reopened by
accident. A failed/interrupted upload leaves the grant pending, and an exact
retry is idempotent.

## Realm and scaling rules

- `MAX_REALM_PLAYERS` is 100. Existing profiles can reconnect after the realm
  reaches capacity; creation of profile 101 fails closed.
- One shared `RealmStore` uses a process lock, separate bounded SQLite
  connections, WAL mode, foreign keys, and immediate transactions for
  cross-player mutations.
- Protected profiles are rebuilt at every startup from validated accepted
  heads plus admitted, unapplied server economy reservations. Legacy
  client-published balances and orphan profile rows are not authoritative;
  presence resets offline. A malformed head or impossible pending balance
  stops startup instead of publishing guessed state.
- The roster is ordered stably by name and opaque actor ID, eight records per page.
  A page includes opaque actor ID, display profile, PvP stats, combat stats,
  ChompCoin, directional trust, and team state.
- Presence is a 90-second UI hint. It is not authentication.
- The trusted realm day advances every hour. Reconnect grants at most one
  missed refresh and never loops catch-up interest. WELCOME carries the
  validated head's local player-day, so a reboot after the cartridge saved a
  rollover but before its realm commit recognizes that rollover instead of
  granting it again.
- Full snapshots use compare-and-swap revisions. Repeating the same
  actor/nonce/exact SHA-256-bound body is idempotent; stale or changed reuse
  fails closed even if two bodies had the same CRC32. A legacy operation with
  no SHA-256 is backfilled only while its revision and exact snapshot are the
  current head.
- An upload that reaches the hub after its declared hour receives retryable
  `COMMIT_STALE_DAY`, keeps the session online, and is regenerated for the
  current hour. A commit accepted just before a boundary remains accepted; its
  result anchors that snapshot and advertises the one newly pending rollover.
- Every normal commit must carry the exact inner actor, server revision, and
  committed save generation of the current head. Structural and progression
  limits are also checked cumulatively against a durable per-player realm-day
  anchor, so splitting a forged increase across repeated uploads does not
  multiply the hourly allowance.
- A schema-5 local save remembers the last accepted actor and server revision.
  Offline edits upload only if that base still matches the server head. If both
  copies advanced, LORD shows `SYNC CONFLICT` and preserves both rather than
  choosing or field-merging them.
- When a matching local branch is dirty, the hub admits that exact snapshot
  before it sends directory changes, action results, or queued realm events.
  The cartridge shows `SYNCING`, continues servicing its OS-owned save queue,
  and briefly blocks gameplay and other serialized-state mutations until
  `COMMIT_OK`. An event that has started but is waiting for its body also blocks
  input until the bounded body is applied. This prevents offline progress from
  being accidentally combined with a debit or other cross-player mutation in
  one unreviewed upload.
- A wire event acknowledgement records receipt but is not terminal. Events
  remain replayable until the target cartridge applies them and an accepted
  snapshot commits the monotonically increasing event cursor atomically. The
  hub sends only one durable event at a time and waits for that cursor commit
  before sending the next. A late or duplicated acknowledgement checks the
  authoritative cursor and cannot recreate a stale session barrier.
- A transfer-source event requires its full debit in the vault; initiated
  shared supplies and PvP-victim events require their full debit in carried
  ChompCoin. An underfunded stock client sends no cursor or acknowledgement,
  leaves realm mode without blocking solo play, and retries after the player
  restores the designated balance and reconnects. Hub-side head validation
  independently rejects an inconsistent debit, but cannot attest a modified
  client or firmware.
- An admitted but not yet cursor-committed PvP knockout immediately projects
  the target as not alive and prevents another challenge. That projection
  survives a hub restart and clears only when an accepted target snapshot
  reflects the result or a later legitimate revival becomes authoritative.

Realm-bound characters retain solo offline forest/training/IGM/NPC play,
shops, healing, Dragon Dice, local banking, and cached roster/mail viewing.
While the Mac action path is unavailable, transfers, player duels/inn
sparring, mail sending, friendship/team/mentor/saying mutations, and shared
tavern/news posts show `Connect to the Mac realm` without changing shared
state. Unbound standalone characters retain the classic local realm. A cable
drop during a leased duel aborts that duel on the next activation without a
reward; the spent fight/entry and HP damage received may remain. Winning and
losing counters plus any prize are granted only by durable source events, so a
lost action-result packet cannot mint or erase the settlement.

The service is a trusted local BBS deployment, not an Internet-facing game
server. Console OS 0.4.88 stops ordinary SD edits, and the hub rejects obvious
or cumulatively repeated state inflation, but neither is proof of play from a
modified cartridge or firmware. The service has no password signup, TLS
listener, remote administration, moderation, public rate limiting, secure-boot
attestation, or server-side combat transcript replay.

## Protocol

P4MP remains version 1. LORD uses protocol `0x4c53` and bounded 1–64 byte Game
Messages. `P4RM` v3 provides persisted-base hello/welcome reconciliation,
complete snapshot transfer,
profile publication, paged roster records, hourly clock, typed actions, and
durable events. Records are capped at 4,148 bytes and use explicit lengths,
little-endian fields, CRCs, stop-and-wait acknowledgement, and one-second
retry.

WELCOME remains 36 bytes: bytes 33–34 are the little-endian `player.day` from
the authoritative head snapshot and byte 35 is zero. `COMMIT_RESULT` status 4
means `COMMIT_STALE_DAY`; unlike revision conflict or invalid state, it is a
retryable clock race. This is an additive P4RM-v3 wire change, but firmware
that required the former reserved bytes to be zero must be upgraded together
with the hub. The normal exact cartridge compatibility hash keeps consoles on
the supplied LORD cartridge, but it does not version the Python process;
deploy the cartridge and hub from the same release.

The UUID-only BLE discovery sentinel is `0x4c4f5244`. It is used only to make
the CoreBluetooth peripheral selectable before GATT connects. The subsequent
Offer carries the exact game ID, API, multiplayer profile, content SHA-256,
compatibility SHA-256, and nonzero session seed; a mismatch cannot launch.

## Verification

```sh
PYTHONPATH=. python3 -m unittest tools.p4_realm_hub.tests.test_protocol -v
PYTHONPATH=. python3 -m unittest \
  tools.p4_realm_hub.tests.test_two_client_e2e -v
cmake -S games/lord -B build-host/lord -G Ninja
cmake --build build-host/lord
ctest --test-dir build-host/lord --output-on-failure
make p4-multiplayer-host
./scripts/build-waveshare-console-os.sh
python3 scripts/verify-console-os-waveshare.py \
  apps/console_os/build-waveshare-landscape
```

The two-client host test creates two independent encoded P4MP/P4RM console
clients with separate identities, nonces, revisions, transactions, and local
save state against one temporary SQLite realm. It covers enrollment, offline
progress on both clients, reconnect upload, directory discovery, mail, a
100-ChompCoin transfer, simulated power loss after each receipt, clean replay,
and reopened authoritative heads. The current expected summary is
`events=3/3 economy=2/2 heads=pink:r3,green:r4`; Pink ends with exactly one
500-to-400 vault debit and Green's reopened LDSV contains `MEET AT THE INN`.
The SDL cartridge runner separately executes the real LORD C code under
sanitizers, but its in-memory host does not implement multiplayer-session
callbacks. Neither host proof is a substitute for the recorded Pink/Green H1
and BLE acceptance run.

A successful build is not hardware acceptance. Release evidence must name the
exact console, firmware/P4G hashes, route, retained serial output, and observed
Join, automatic start, realm online, reconnect, rollover, and conflict results.
