# LORD backend synchronization plan

LORD 1.2.0 is ready for a future Console OS-owned backend adapter while
remaining a complete offline cartridge. The cartridge never opens a socket,
stores credentials, chooses a route, or trusts wall-clock time. Console OS
will own authentication, TLS, retries, queueing, timeouts, and account UI; the
server will own authoritative revision and realm-day decisions.

## Implemented game-side boundary

`src/lord_sync_impl.h` implements the deterministic `LRSY` version-1 record.
It wraps the complete existing `LDSV` save with:

- a 16-byte opaque actor ID supplied by Console OS;
- a nonzero one-use 64-bit operation nonce;
- the expected realm revision and local save sequence;
- explicit little-endian lengths and a CRC over metadata plus payload; and
- a maximum total size of 4,148 bytes.

No PII or transport data enters the record. The record codec does not make the
current cartridge online: Game API v1 has the `realm` capability bit but no
reviewed callbacks through which the cartridge can submit or receive records.

## Console OS adapter required

Add one optional, non-blocking `realm` service tail after the currently frozen
Game API v1 fields. Its minimum operations should be:

```text
read_head() -> status, actor_id[16], realm_revision, copied snapshot
queue_commit(expected_revision, nonce, copied LRSY record) -> ticket
read_commit(ticket) -> queued | committed(new_revision) | conflict | error
```

The host must copy every cartridge buffer before returning, cap records at
4,148 bytes, expose no account/session token to the game, and make callbacks
absent when signed-in sync is unavailable. A false callback result must never
block or disable offline play. Only after these callbacks exist should
`game.json` request optional `realm` and the descriptor add
`P4_GAME_CAP_REALM`.

Launch and commit flow:

1. Console OS signs in outside the cartridge and obtains an opaque actor ID.
2. It fetches the server head before launch and validates game ID, schema,
   actor, sizes, CRCs, and monotonic revision.
3. It supplies the accepted snapshot through the normal immutable save launch
   view; LORD never receives a file or token.
4. LORD plays offline-first and continues queueing ordinary local `AUTO`
   saves.
5. At a safe boundary, LORD creates one `LRSY` record using the OS-supplied
   actor ID and nonce, then queues it with the expected server revision.
6. Console OS journals the copied record locally, uploads asynchronously, and
   reports a ticket result. A disconnect leaves the journal retryable.
7. LORD accepts only a committed higher revision. A conflict never overwrites
   the remote head; the adapter fetches the new head and offers an explicit
   keep-local/keep-server choice outside active combat.

## Suggested backend API

The transport can evolve without changing the cartridge record:

```text
POST /v1/sessions                 -> authenticated OS session
GET  /v1/games/lord/head          -> actor ID, revision, LRSY record
PUT  /v1/games/lord/head          -> If-Match revision + idempotency nonce
POST /v1/games/lord/actions       -> later mail, transfer, duel, team intents
GET  /v1/games/lord/directory     -> later bounded sanitized player page
```

The server must bind actor ID to the authenticated account, reject reused
nonces with a different body, retain a bounded idempotency window, rate-limit
every operation, validate both CRC layers, reject revision skips, sanitize all
display text, and store an append-only audit event before publishing a new
head. TLS and bearer/session credentials terminate in Console OS, never in the
cartridge.

## Merge and multiplayer rules

Full snapshots use optimistic single-writer replacement. They are not merged
field-by-field: that could duplicate ChompCoin, mail, PvP rewards, team state,
or daily resets. Later shared-realm actions are separate idempotent intents:

- ChompCoin transfer: debit and credit atomically under one nonce;
- mail: deliver once, then acknowledge read state separately;
- asynchronous duel: lease an immutable opponent revision, validate the
  outcome server-side, and commit rewards once;
- adventure team: invitation and acceptance are two-party consent events;
- realm day: server-authoritative rollover, never device-clock driven.

`multiplayer-session` remains for optional live local duels and tournaments;
it is not the account/save synchronization transport.

## Acceptance gate

Before claiming backend synchronization works, automated and two-device tests
must cover clean first upload, relaunch download, offline play and reconnect,
power loss during queue/upload, duplicate nonce, stale revision, simultaneous
devices, hostile text, malformed/oversized records, auth expiry, server
timeout, local journal recovery, ChompCoin non-duplication, mail exactly-once
delivery, team consent, and trusted realm-day rollover. Until then the honest
status is: record codec complete; OS adapter and server pending.
