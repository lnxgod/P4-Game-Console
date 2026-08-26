# LORD backend synchronization

LORD 1.3.0 has a working local Mac-hub synchronization path and remains a
complete offline cartridge. The cartridge never opens a socket, file, serial
port, or BLE connection. Console OS owns the physical route and supplies the
existing bounded `multiplayer-session` service.

## Implemented compatibility path

No Game API or P4MP version upgrade was needed for the first realm slice.
`game.json` declares a two-player turn-based profile with protocol `0x4c52`.
The console hosts the room and the Mac hub joins slot 1. LORD and the hub then
exchange `P4RM` v1 records through ordinary P4MP Game Message packets.

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

- strict P4MP v1 and P4RM v1 framing;
- the existing noisy-stream H1 and P4B BLE adapters;
- SQLite actor heads, CRC validation, compare-and-swap commits, nonce
  idempotency, event audit rows, profile presence, and a trusted realm clock;
- stop-and-wait transfer with retry; and
- one database shared by several H1 workers and one optional BLE worker.

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
returns `SYNC CONFLICT` and does not overwrite the hub. Version 1.3.0 does not
offer an in-game conflict chooser; the safe recovery is to exit and relaunch
from the current server head or use a different hub profile for the divergent
character.

## Shared-directory boundary

The game publishes printable ASCII name, hero style/class, level, alive and
inn flags, health, strength, defense, experience, carried ChompCoin, and PvP
record. The hub binds these values to the session actor, validates all ranges,
and returns at most eight other profiles. Directory entries use opaque actor
IDs internally; the cartridge never treats a profile label or transport
address as identity.

Presence means a validated profile was seen within 90 seconds. It is a game UI
hint, not proof of account identity. The current local hub has no public signup,
password, TLS, Internet listener, or remote administration surface.

## Typed cross-player actions still required

Full snapshot sync makes one actor portable, but it cannot safely mutate a
second actor. These classic BBS interactions need a separate server-owned,
idempotent action layer:

- mail: enqueue once to a target actor, list/read/acknowledge separately;
- ChompCoin transfer: atomically debit and credit two current heads;
- asynchronous PvP: lease an immutable opponent revision and commit a bounded
  outcome exactly once;
- friendship/adventure team: invite and accept as two-party consent events;
- tavern/news: append bounded sanitized text to a paged shared feed.

Until that layer exists, LORD's existing mail replies, transfers, PvP results,
friendship actions, teams, conversation, and news operate inside the current
actor's saved local realm copy. They are playable and synchronized with that
actor's full snapshot, but they are not delivered to or authoritative for the
other hub profile. Built-in IGMs are part of the snapshot; arbitrary external
IGM packages still need typed OS handoff.

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
and disconnect. The typed-action phase additionally needs ChompCoin
non-duplication, mail exactly-once delivery, PvP lease replay/expiry, and team
consent tests. A build is not H1 or BLE hardware acceptance.
