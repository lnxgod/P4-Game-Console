# LORD Mac BBS realm server

LORD 1.8.0 uses the Mac as the authoritative BBS-style realm server. Every
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
and vaulted ChompCoin, Adventure Clubs, and the hourly realm-day clock. Offline players receive
numbered durable events when they next join. Neither the server rollover nor
standalone local sleep pays bank interest.

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
  ChompCoin, directional trust, and team state. The hub orders the
  authoritative directory by dragon deeds, level, XP, PvP wins, fewer PvP
  losses, then stable identity ties. LORD re-sorts the local hero plus the
  current up-to-eight-entry page by those same gameplay fields.
- Adventure Clubs are a separate server-owned social layer: at most eight
  members, one of sixteen unique curated names, one-day join eligibility and
  rejoin delays, deterministic leader succession, one rally per eligible
  member/day, and paged club standings. A 12-point cooperative quest earns a
  Banner Star and bonus club points. Season points reset every 24 realm days;
  lifetime prestige, stars, and W/L/D do not.
- Friendly Banner Clashes are calculated by the hub from accepted member heads,
  normalized same-day participation, rally-route tactics, and a deterministic
  server roll. Each club has at most one outgoing and one incoming clash per
  realm day and each pair has a three-day cooldown. Cheers are bounded daily.
  None of these operations changes personal ChompCoin, XP, deeds, PvP counters,
  or combat stats, and ordinary PvP between members of the same club is denied.
  The exact score is the floor average of each eligible member's
  `4*level + 10*dragon_deeds + min(PvP_wins, 50)`, plus rounded participation
  from 0--12, a +4 majority-route advantage, and a deterministic -3--3 SHA-256
  roll. Ward beats Charge, Sneak beats Ward, and Charge beats Sneak. A win,
  loss, or draw gives the two clubs 6/3, 3/6, or 4/4 points respectively.
- Presence is a 90-second UI hint. It is not authentication.
- The trusted realm day advances every hour. Reconnect grants at most one
  missed refresh and never pays or loops catch-up interest. Standalone Inn
  sleep uses the same no-interest reset. WELCOME carries the
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
- LORD 1.8.0 progression validation permits at most three gained levels from the
  anchored base and requires the XP threshold for the destination trainer.
  Cumulative XP gain is capped at 2,500,000 per realm day. Wealth may rise by
  at most 25,000,000 plus 20% of anchored wealth; max HP, strength, and defense
  may each rise by 5,000; and separate bounds cover PvP records, forest fights,
  skills, badges, charm, gems, and mentoring. These deliberately wide
  anti-editor bounds do not prove that a modified cartridge played each fight.
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
- The dirty-first barrier is pinned to the local save generation advertised by
  HELLO. An exact replay of an older successful operation still receives its
  idempotent original result, but cannot unlock a newer dirty branch. The only
  accepted generation step is the single trusted hourly rollover advertised by
  WELCOME or authorized by a retryable `COMMIT_STALE_DAY` response.
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
  the target as not alive and prevents another challenge. That pending
  projection survives a hub restart and clears only when an accepted target
  snapshot reflects the result. This is not a day-long server KO latch: after
  consuming the zero-HP loss, the stock dead-screen recovery may later commit
  restored HP in the same realm day, at which point the authoritative profile
  is alive again.
- Advancing the durable event cursor over a PvP result also has semantic
  checks: lifetime wins/losses must equal the prior counters plus every crossed
  result, so unreceipted movement in either direction is rejected, and a
  same-day loss must commit zero HP. An
  old-day loss may arrive in the same snapshot as its one hourly revival; a
  current-day knockout cannot use that exception.

Realm-bound characters retain solo offline forest/training/IGM/NPC play,
shops, healing, tactical intent battles, real Aragorn Math, target-18
Roll/Hold/Leave Dragon Dice, local banking, and cached roster/mail viewing.
While the Mac action path is unavailable, transfers, player duels/inn
sparring, mail sending, friendship/team/mentor/saying mutations, and shared
tavern/news posts show `Connect to the Mac realm` without changing shared
state. Adventure Club state is queried from the hub rather than serialized in
LDSV5, so a cold offline Club Hall asks the player to reconnect; solo progress
and local saves continue normally. Unbound standalone characters retain the
classic local realm. A cable
drop during a leased duel aborts that duel on the next activation without a
reward; the spent fight/entry and HP damage received may remain. Winning and
losing counters plus any prize are granted only by durable source events;
shared realm duels award no XP. A lost action-result packet therefore cannot
mint or erase the settlement, and a cached-duel XP/ChompCoin/prestige bundle is
rejected without blocking same-counter solo offline progression.

The service is a trusted local BBS deployment, not an Internet-facing game
server. Console OS 0.4.88 stops ordinary SD edits, and the hub rejects obvious
or cumulatively repeated state inflation, but neither is proof of play from a
modified cartridge or firmware. The service has no password signup, TLS
listener, remote administration, moderation, public rate limiting, secure-boot
attestation, or server-side combat transcript replay. In particular, the duel
outcome submitted for a valid three-per-day PvP lease is still client-claimed;
the chaos proof validates settlement and recovery, not a hostile client's
combat honesty. Unmetered mail/feed flooding is likewise outside this trusted
LAN deployment's current isolation guarantees.

## Protocol

P4MP remains version 1. LORD uses protocol `0x4c53` and bounded 1–64 byte Game
Messages. `P4RM` v3 provides persisted-base hello/welcome reconciliation,
complete snapshot transfer,
profile publication, paged roster and club records, hourly clock, typed
actions, and durable events. Additive kinds 23--26 carry actor-bound club
status, player club annotations, club-page metadata, and fixed-size standings
summaries; action kind 10 carries create/join/leave/rally/clash/cheer requests.
A new cartridge enables those actions only after a valid club-status packet,
so it remains safe with an older hub. Records are capped at 4,148 bytes and use explicit lengths,
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
make lord-realm-e2e-host
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

The four-client campaign test runs four independent encoded consoles through
four hourly realm days. Every player attacks twice and is targeted twice
across eight duels, with 16 offline-progress uploads, 12 rollover/revival
uploads, eight denied attacks against already knocked-out players, one
receipt replay across a complete hub restart, and one authoritative head
download after locally losing a committed sync. It reopens SQLite and verifies
all 16 outcome events and 14 economy rows are committed exactly once, all
eight leases are resolved on the intended day, final head revisions and event
cursors match, wins equal losses, and the original 3,200 carried ChompCoin is
conserved.

The ten-client chaos test runs a deterministic 12-hour campaign with all 45
unordered player pairings in its first nine days and reversed rematches for
three more. It proves two-page 8+1 roster discovery, 60 resolved duels, 120
durable PvP events, 91 economy rows, exact per-actor revision histories, and
8,000 conserved ChompCoin. Recoverable faults include a lost commit result,
current and ancient nonce replays, a pinned dirty-branch action attempt, a
corrupt upload chunk, a stale revision conflict, full store/session restarts
with pending knockouts, receipt-before-save power loss, authoritative download
after local rollback, and a forged cursor-only knockout reflection. The final
SQLite quick check, foreign keys, heads, profiles, anchors, leases, events, and
ledger are compared with an independently generated oracle.

The Adventure Club campaign drives fourteen accepted actors through thirty
realm days in unequal three-, four-, and five-member clubs plus a temporary
solo club. It proves name reuse, capacity, delayed eligibility, quest carry and
wrap, deterministic leader succession, route and size normalization, daily and
pair clash limits, cheers, nonce replay across restart, day-25 season rollover,
same-club PvP denial, malformed requests, and byte-for-byte isolation of every
personal head, ChompCoin, XP, combat stat, deed, and PvP counter.

The SDL cartridge runner separately executes the real LORD C code under
sanitizers, but its in-memory host does not implement multiplayer-session
callbacks. Neither host proof is a substitute for the recorded Pink/Green H1
and BLE acceptance run.

A successful build is not hardware acceptance. Release evidence must name the
exact console, firmware/P4G hashes, route, retained serial output, and observed
Join, automatic start, realm online, reconnect, rollover, and conflict results.
