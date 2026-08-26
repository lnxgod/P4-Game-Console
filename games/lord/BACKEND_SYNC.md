# LORD backend synchronization

LORD 1.5.0 has a working local Mac-hosted BBS realm and remains a
complete offline cartridge. The cartridge never opens a socket, file, serial
port, or BLE connection. Console OS owns the physical route and supplies the
existing bounded `multiplayer-session` service.

## Implemented compatibility path

No Game API or P4MP version upgrade was needed. `game.json` declares a
two-player turn-based profile with protocol `0x4c53`.
The Mac hosts one logical room per console, every console joins slot 1, and
the backend automatically starts the synchronized launch. All logical rooms
share one SQLite realm with a 100-profile cap. LORD and the backend exchange
`P4RM` v2 records through ordinary P4MP Game Message packets.

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

- strict P4MP v1 and P4RM v2 framing;
- the existing noisy-stream H1 and P4B BLE adapters;
- SQLite actor heads, CRC validation, compare-and-swap commits, nonce
  idempotency, durable cross-actor events, profile presence, private vault
  balance, friendship/team state, PvP leases, shared feeds, and a trusted
  realm clock;
- stop-and-wait transfer with retry; and
- one database shared by several H1 workers and one optional BLE worker;
- an exact 100-profile realm cap with reconnect allowed at capacity; and
- eight-record Previous/Next roster pages covering the other 99 actors.

See [the operator guide](../../docs/LORD_REALM_HUB.md) for setup, commands,
security boundaries, and verification.

## Snapshot and clock rules

The server head revision is independent from the game-local `realm_revision`.
A full head is replaced only when the submitted expected server revision
matches. It is never field-merged: merging can duplicate ChompCoin, mail, PvP
rewards, team state, or daily limits. Repeating the same actor/nonce/body is
idempotent; reusing the nonce with a different body is invalid.

The hub computes `floor(unix_time / 3600) + 1`. When the stored head's last
realm day is older, LORD applies exactly one hourly refresh after a validated
download. Missing several hours never grants several refreshes. The online
refresh restores daily actions but deliberately pays no bank interest. The new
state must commit under the current day before it becomes the server head.

Disconnect leaves the ordinary local save path intact. A later stale upload
returns `SYNC CONFLICT` and does not overwrite the hub. Version 1.5.0 does not
offer an in-game conflict chooser; the safe recovery is to exit and relaunch
from the current server head or use a different hub profile for the divergent
character.

## Shared-directory boundary

The game publishes printable ASCII name, hero style/class, level, alive and
inn flags, health, strength, defense, experience, carried ChompCoin, and PvP
record. The hub binds these values to the session actor, validates all ranges,
and returns one requested page of at most eight other profiles from the
bounded 100-player roster. Directory entries use opaque actor IDs internally;
the cartridge never treats a profile label or transport address as identity.
Page statistics carry server-owned directional trust and team state so
switching pages does not discard relationships.

Presence means a validated profile was seen within 90 seconds. It is a game UI
hint, not proof of account identity. The current local hub has no public signup,
password, TLS, Internet listener, or remote administration surface.

## Typed cross-player actions implemented

P4RM v2 separates actor snapshots from cross-actor mutations. An action uses a
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
  the target's carried-ChompCoin prize at the hub, and queue the target outcome;
- tavern/news: store bounded printable feed rows and fan out durable events to
  the registered profiles.

Events carry a monotonically increasing 64-bit ID and remain unacknowledged in
SQLite until the target cartridge applies them. LORD save schema 4 persists
the last applied event ID plus opaque directory/team actor IDs; schema-3 saves
migrate with zeroed event state. A repeated old event is acknowledged without
reapplying its mail, ChompCoin, PvP record, trust, team, or feed effect.

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
and disconnect. The host suite now covers action codecs, changed-body nonce
rejection, two-node mail delivery/acknowledgement, two-sided ChompCoin
transfer, team consent, PvP lease resolution, and tavern fan-out. Power-loss
fault injection, PvP lease expiry, hostile-client fuzzing, and exact H1/BLE
device evidence remain hardware/release gates. A build is not H1 or BLE
hardware acceptance.
