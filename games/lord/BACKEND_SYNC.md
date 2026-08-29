# LORD backend synchronization

LORD 1.6.1 has a working local Mac-hosted BBS realm. Unbound standalone
characters remain complete offline games; realm-bound characters keep all
solo play offline but reconnect for shared-player mutations. The cartridge
never opens a socket, file, serial
port, or BLE connection. Console OS owns the physical route and supplies the
existing bounded `multiplayer-session` service.

## Implemented compatibility path

No Game API or P4MP version upgrade was needed. `game.json` declares a
two-player turn-based profile with protocol `0x4c53`.
The Mac hosts one logical room per console, every console joins slot 1, and
the backend automatically starts the synchronized launch. All logical rooms
share one SQLite realm with a 100-profile cap. LORD and the backend exchange
`P4RM` v3 records through ordinary P4MP Game Message packets.

`src/lord_sync_impl.h` owns the deterministic `LRSY` version-1 record. It wraps
the complete existing `LDSV` save with:

- a 16-byte opaque actor ID assigned by the hub profile;
- a nonzero one-use 64-bit operation nonce;
- the game-local realm and save revisions;
- explicit little-endian lengths and CRCs; and
- a maximum total size of 4,148 bytes.

`src/lord_realm_net_impl.h` owns only the game-side state machine. It performs
hello/welcome, complete snapshot download, optimistic upload, conflict/error
status, profile publication, directory updates, and trusted hourly rollover.
It never sees the route, database, device address, account credential, or
server path.

The Mac implementation lives in `tools/p4_realm_hub/` and is launched by
`scripts/p4-realm-hub.py`. It provides:

- strict P4MP v1 and P4RM v3 framing;
- the existing noisy-stream H1 and P4B BLE adapters;
- full schema-3/4/5 `LDSV` parsing, exact embedded-base checks, cumulative
  per-realm-day transition anchors, and conservative stat/economy validation;
- SQLite actor heads, CRC validation, compare-and-swap commits, nonce
  idempotency, durable cross-actor events, profile presence, private vault
  balance, friendship/team state, PvP leases, shared feeds, and a trusted
  realm clock;
- stop-and-wait transfer with retry; and
- one database shared by several H1 workers and one optional BLE worker;
- an exact 100-profile realm cap with reconnect allowed at capacity; and
- eight-record Previous/Next roster pages covering the other 99 actors.

On startup, the hub rebuilds protected profiles from fully decoded accepted
heads plus admitted, unapplied economy reservations. Client-authored legacy
profile rows, orphan actors, and stale presence cannot publish stats or
balances. Legacy shared artifacts are migrated only during an explicit
operator-approved `--migrate-legacy-artifacts` startup after backup. Raw rows
are preserved with permanent admission/quarantine state; a schema-4/5 event
cursor can prove consumption, while an old wire acknowledgement proves receipt
only. Ambiguous pending legacy PvP, v3 heads with events, malformed heads, and
inconsistent ledgers fail closed. The operator procedure is in the hub guide.

See [the operator guide](../../docs/LORD_REALM_HUB.md) for setup, commands,
security boundaries, and verification.

## Snapshot and clock rules

The server head revision is independent from the game-local `realm_revision`.
A full head is replaced only when the submitted expected server revision
matches and the embedded actor, server revision, and committed generation are
the exact prior accepted lease. It is never field-merged: merging can duplicate
ChompCoin, mail, PvP rewards, team state, or daily limits. Repeating the same
actor/nonce/exact SHA-256-bound body is idempotent; reusing the nonce with a
different body is invalid even if its CRC32 were to collide. Legacy operations
without a stored SHA-256 can be backfilled only when their revision and exact
snapshot still equal the current head.

The hub computes `floor(unix_time / 3600) + 1`. When the stored head's last
realm day is older, LORD applies exactly one hourly refresh after a validated
download. Missing several hours never grants several refreshes. The online
refresh restores daily actions but deliberately pays no bank interest. The new
state must commit under the current day before it becomes the server head.
WELCOME keeps its 36-byte P4RM-v3 size and uses bytes 33–34 for the validated
head snapshot's `player.day` (byte 35 remains zero). That durable before-state
lets a cartridge reboot after saving the refresh but before committing it: a
local day equal to the head day is refreshed once, while head day plus one is
recognized as already refreshed. Any other relationship fails closed, as does
the saturated `UINT16_MAX` day where those states cannot be distinguished.
Firmware that expected all three old reserved bytes to be zero must be updated
with this hub. The exact cartridge compatibility hash keeps consoles on the
same LORD cartridge, but it does not attest the Python hub implementation, so
operators must deploy the cartridge and hub from the same release.

An upload retains its original realm day along with its exact nonce and record.
If the hour changes before acceptance, the hub returns retryable
`COMMIT_STALE_DAY` (status 4) without taking the realm offline; LORD discards
that old transaction, reconciles the head once, and creates a new current-day
record. If acceptance wins the race but COMMIT_RESULT observes the next hour,
the accepted upload becomes the new anchor and LORD immediately applies one
new pending refresh. A repeated accepted nonce remains idempotently successful
across the boundary.
Once a character has a nonzero actor, including the authorized revision-zero
adoption transition, the cartridge blocks the inn's classic local sleep reset
while connected or offline. The Mac realm alone grants that character's next
day/hour. An unbound local-only character retains classic sleep and its local
bank-interest rule.

Realm-bound characters may still play forest battles, training, IGMs, NPC
friendship, Dragon Dice, shops, healing, and their own bank while disconnected.
Cached roster and mail remain readable. Transfers, player duels and inn
sparring, mail compose/send, friendship/team/mentor/saying mutations, and
shared tavern/news posts stop at a friendly `Connect to the Mac realm` message
until online actions are ready. An unbound standalone character retains the
classic local realm simulation. If a leased duel loses its connection, the
next activation aborts it without a reward; its spent fight/entry and received
HP damage may remain.

LORD schema 5 stores the last accepted hub actor ID, server revision, and game
save generation inside the OS-owned local save. P4RM v3 includes that base and
the current generation in `HELLO`. If the local character changed offline and
the stored base still equals the hub head, the hub accepts the local copy and
the ordinary compare-and-swap upload advances it once. A clean stale local
copy downloads the current head.

If a dirty local copy and the server head both advanced, or the persisted actor
does not match the selected hub profile, the welcome returns `SYNC CONFLICT`.
Neither copy is overwritten and no ChompCoin, mail, PvP reward, team state, or
daily action is field-merged. Version 1.6.1 does not offer an in-game conflict
chooser; preserve the local save, then relaunch under the intended stable
profile or have a parent explicitly start the hub once with
`--adopt-local PROFILE`. Normal
offline-first play therefore assumes one active console save per stable
profile between successful synchronizations.

If the persisted base still matches and the local branch is merely dirty, the
hub gives that branch ordering priority. It withholds directory projections,
action results, and new durable events until the exact frozen upload commits.
LORD reports `SYNCING`, keeps polling the network and servicing the OS save
queue, and pauses gameplay until `COMMIT_OK`. It also pauses input between a
bounded event's begin and body packets. A newer event is left unacknowledged
and replayable whenever a local generation still needs to commit.

For an explicit parent-authorized recovery, P4RM v3 reserves welcome flag bit
4 as `ADOPT_LOCAL`. Its welcome remains 36 bytes. The flag is
mutually exclusive with snapshot, ordinary local acceptance, and conflict.
It binds the welcome actor and server revision as the upload's expected
compare-and-swap base, including revision zero when that actor has no head yet,
while leaving the local committed generation at zero; the current local save
therefore uploads normally. Schema 5 permits this bounded actor-bound,
revision-zero pre-commit state, and the ordinary commit result records the
accepted generation. `LOCAL_CONFLICT` may also report revision zero so the
cartridge shows `SYNC CONFLICT`; snapshot and ordinary local acceptance still
require a nonzero head. The hub must never send `ADOPT_LOCAL` without that
external approval. The hub archives the replaced head, binds the grant to the
exact local record, updates the accepted head/profile/day anchor, and consumes
the one-time grant in one SQLite transaction. A lost commit reply may retry the
same nonce and record; it does not create a second adoption.

## Shared-directory boundary

Legacy profile packets may update only ephemeral presence and the at-inn UI
hint. Printable name, hero style/class, level, alive state, health, strength,
defense, experience, carried ChompCoin, and PvP record are projected from the
last snapshot the hub accepted; a cartridge cannot publish authoritative stats
through the directory side channel. The hub returns one requested page of at
most eight other profiles from the bounded 100-player roster. Directory entries use opaque actor IDs internally;
the cartridge never treats a profile label or transport address as identity.
Page statistics carry server-owned directional trust and team state so
switching pages does not discard relationships.

Presence means a validated profile was seen within 90 seconds. It is a game UI
hint, not proof of account identity. The current local hub has no public signup,
password, TLS, Internet listener, or remote administration surface.

## Typed cross-player actions implemented

P4RM v3 separates actor snapshots from cross-actor mutations. An action uses a
nonzero per-actor nonce, bounded target actor ID, kind/code/value, optional
48-byte body, and body CRC. SQLite records the request hash and result in the
same transaction as its state change. A retry with the same request returns
the stored result; changing a request under a used nonce is invalid.

The implemented actions are:

- mail: one durable target event containing the bounded printable body;
- ChompCoin transfer: validate 100 vaulted ChompCoin, atomically update the
  hub's private source-vault and target-carried balances, and queue one debit
  event and one credit event;
- friendship: directional trust from encouragement or shared supplies;
- adventure team: invite first, form only after the other actor reciprocates,
  and notify both sides of formation or friendly parting;
- mentoring: notify the confirmed teammate and update each character once;
- asynchronous PvP: acquire one current-day lease, resolve it once, calculate
  the target's carried-ChompCoin prize at the hub, and queue durable outcomes
  for both source and target. The cartridge removes provisional local prize,
  XP, PvP counters, cached-target changes, mail, and logs before saving. A
  source win event grants exactly one win plus its prize (including a
  zero-prize win); a source loss event grants exactly one loss/knockout;
- tavern/news: store bounded printable feed rows and fan out durable events to
  the registered profiles.

Events carry a monotonically increasing 64-bit ID. A wire acknowledgement is
only a receipt: an event remains replayable until an accepted target snapshot
advances its persisted event cursor, which marks the event committed in the
same transaction. LORD save schema 5 persists
the last applied event ID, opaque directory/team actor IDs, and the safe sync
base; schema-3 and schema-4 saves migrate with zeroed sync-base state. A repeated old event is acknowledged without
reapplying its mail, ChompCoin, PvP record, trust, team, or feed effect.
The hub serializes delivery: it does not begin event N+1 until an accepted
snapshot proves event N through that cursor. If the cursor commit races ahead
of a lost or duplicated wire acknowledgement, the late acknowledgement reads
the authoritative head and does not re-arm the already-satisfied barrier.

An economy debit is acknowledged only after the stock cartridge can subtract
the complete amount from the account named by the event: a transfer debit uses
the vault, while shared-supplies and PvP-victim debits use carried ChompCoin.
If that account is short, LORD does not advance the event cursor or send an
acknowledgement. It leaves the realm session so local solo play and banking
remain available, then replays the still-pending debit after the player restores
the amount and reconnects. The hub also checks the resulting account movement
when it accepts the next head. These checks close ordinary unplug/retry paths;
they do not prove honest behavior from modified firmware or a modified client.

An unresolved admitted PvP knockout is included in the hub's effective-alive
projection before the target's cursor snapshot arrives. Profiles and new PvP
leases therefore cannot treat the target as alive during the receipt-to-save
window or after a hub restart. The pending projection clears when an accepted
head reflects the knockout; later revival still follows the normal realm-day
transition rules.

This is authoritative for the trusted local Mac deployment, not a hostile
Internet economy. The hub has no account authentication, TLS listener,
moderation, rate-limit policy, or server-side combat transcript validation.
Built-in IGMs remain part of the actor snapshot; arbitrary external IGM
packages still need typed OS handoff.

## Future Internet/backend adapter

A future authenticated service should retain the same `LRSY` record and action
semantics behind an OS-owned `realm` capability:

```text
read_head() -> status, actor_id[16], server_revision, copied snapshot
queue_commit(expected_revision, nonce, copied LRSY record) -> ticket
read_commit(ticket) -> queued | committed(new_revision) | conflict | error
queue_action(kind, target_actor, expected revisions, nonce, copied payload)
read_action(ticket) -> queued | committed(result) | conflict | error
```

The OS must terminate authentication/TLS, journal copied requests, expose no
token or URL to the cartridge, enforce quotas and timeouts, sanitize display
text, and make all callbacks optional. The Mac P4MP peer is a backward-
compatible local deployment, not a replacement for those trust boundaries.

## Acceptance gate

Automated and two-device tests must cover clean first upload, relaunch
download, offline play/reconnect, retry, power loss, duplicate nonce, changed
body under a nonce, stale revision, simultaneous devices, malformed and
oversized frames, hostile text, SQLite recovery, hourly rollover exactly once,
and disconnect. The host suite now covers clean/matching offline acceptance,
stale-dirty conflict without overwrite, clean-stale download, action codecs, changed-body nonce
rejection, two-node mail delivery/acknowledgement, two-sided ChompCoin
transfer, team consent, PvP lease resolution/drop/zero-prize/loss replay,
realm-bound offline shared-action blocking with solo-play preservation,
tavern fan-out, retryable
boundary-stale upload, accepted-commit boundary crossing, and reboot after a
locally saved rollover without a second refresh. A separate two-client E2E
uses independent P4MP/P4RM client state against one temporary SQLite realm and
proves both offline uploads, mail serialization into the reopened LDSV, an
exactly-once 100-ChompCoin debit, dirty-branch/event ordering, and replay after
simulated power loss. Its expected terminal heads are Pink revision 3 and Green
revision 4 with all three events and both economy rows committed. Device-level
mid-write fault injection, PvP lease expiry, hostile-client fuzzing, and exact
H1/BLE evidence remain hardware/release gates. The SDL runner exercises the
real C cartridge separately but currently has no multiplayer-session callback,
so a host pass is not H1 or BLE hardware acceptance.
