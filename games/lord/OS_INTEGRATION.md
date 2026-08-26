# LORD 1.4.0 OS integration contract

LORD is a complete standalone cartridge. This document describes optional OS
services that turn its persistent local realm into a shared BBS realm without
giving the game filesystem, network, clock, USB, display, or raw-input
ownership.

## Services used now

- Required: `video`, `controls`
- Optional: `audio-tone`, `save`, `multiplayer-session`

The save adapter is implemented in `src/lord.c`. At launch it accepts save
schema 3 or 4, validates the nonzero host sequence and copied launch snapshot,
and migrates schema 3 in memory. At safe update boundaries it encodes schema 4
into a bounded
4 KiB staging buffer, queues slot `AUTO`, polls the returned ticket, and clears
`save_dirty` only after `COMMITTED`. Conflicts and unavailable storage fail
closed while gameplay continues locally.

The game-defined `LDSV` payload is explicit little endian and CRC protected.
Schema 4 persists the complete player, all three skill trees, daily counters, eight
local warriors, twelve mail slots with text, twelve news records, adventure
teams, trust, youth mentoring, conversation, announcement, IGM usage, and realm
revision, plus opaque directory/team actor IDs and the last applied hub event
ID. It never
serializes pointers, raw enums, structure padding, or `lord_state_t` itself.

The host-level save container remains the OS's responsibility: namespacing,
SHA-256, optimistic sequence, atomic replacement, backup, journal recovery,
storage ownership, quota, and Save Manager UI all live outside the cartridge.

## Controller-only text fallback

Names, mail, announcements, public sayings, and conversation already work
with the cartridge's 44-key ANSI character picker. A future `text-input`
service should be an optional acceleration path for touch/USB keyboards, not a
requirement. Copy at most 47 printable characters into `editor_text`, reject
controls or malformed UTF-8, and preserve the in-cartridge picker when the
modal is unavailable or cancelled.

## Mac-hub realm service used now

LORD declares a two-player turn-based `multiplayer-session` profile with
protocol `0x4c53`. Console OS already owns lobby selection, P4MP framing,
H1/BLE routing, replay checks, route binding, timeout, and disconnect. The
cartridge receives only the existing 64-byte Game Message service plus the
immutable session seed and status.

`src/lord_realm_net_impl.h` layers bounded `P4RM` v2 messages on that service.
It synchronizes full `LRSY` snapshots with the Mac hub, publishes the local
profile, consumes an eight-entry remote directory, and applies one trusted
hourly refresh. It also submits bounded actions and applies/acknowledges durable
events for mail, transfers, friendship, teams, mentoring, PvP, tavern, and
news. It remains inactive when the capability or connected room is
absent. No new Game API fields, raw P4MP packets, routes, USB/BLE handles, or
clock callbacks are exposed to LORD.

The exact deployment and current limitations are in
[the Mac hub guide](../../docs/LORD_REALM_HUB.md).

## Local typed realm adapter implemented

The offline/full-head snapshot consists of:

```text
lord_realm_player_t realm[8]
lord_mail_t         mail[12]
lord_log_entry_t    log[12]
realm_revision, partner_index, npc_friend
```

The Mac compatibility path synchronizes this complete snapshot for one actor
and maps cross-actor flows to nonce-idempotent actions plus numbered durable
events:

| Existing game flow | Optional realm operation |
|---|---|
| Other Warriors / rankings | paged directory snapshot |
| Player challenge / inn attack | lease immutable opponent snapshot |
| PvP finish | idempotent outcome commit |
| Inbox / sent mail | list, read, mark-read, send by opaque ID |
| Friendship / team invitation / parting | consent-based team transaction |
| Bank transfer | idempotent bounded transfer |
| Daily News / conversation | bounded sanitized feed |
| Sleep / daily reset | trusted realm-day transition |

Opaque remote IDs and leases remain inside validated P4RM messages and schema-4
state; local array indexes are never used as hub identity. A conflict may not
award ChompCoin, defeat an opponent, deliver duplicate mail, or form an
adventure team. Remote team changes require both players' consent. Offline
state must never overwrite a newer server revision.

A future authenticated/public `realm` Game API tail should preserve these
semantics while moving authentication, TLS, quotas, moderation, and hostile
client validation into Console OS. The local P4MP peer does not provide those
public-service boundaries.

## Backend sync record implemented now

`src/lord_sync_impl.h` defines the bounded `LRSY` version-1 record copied over
the current P4RM compatibility path and reusable by a future `realm` callback.
It contains a 52-byte explicit
little-endian header followed by the complete CRC-protected schema-3 or
schema-4 save:

```text
magic="LRSY", format=1, total bytes, record CRC
realm revision, save sequence, one-use 64-bit operation nonce
16-byte opaque actor ID, save length, LDSV save payload
```

The maximum record is 4,148 bytes. Encoding rejects zero actor IDs, zero
nonces, invalid realm revisions, undersized output, and invalid save payloads.
Decoding verifies every length, the record CRC, expected actor ID, minimum
revision, embedded save CRC, and matching save/realm revisions before success.
It contains no username, email, password, device address, route, token, or
server URL. Unit tests cover round trip, stale revision, wrong actor, corrupt
payload, and zero-nonce rejection.

The codec itself remains transport-free. Version 1.4 submits it through a
reviewed P4RM state machine over `multiplayer-session` when the local Mac hub
occupies the peer slot. The complete record, server, conflict, and future
typed-action rules are in [BACKEND_SYNC.md](BACKEND_SYNC.md).

The P4RM use is deliberately a local backward-compatible deployment. A future
authenticated/public backend should expose the same classic mail, transfer,
PvP, friendship, and team operations behind `realm` callbacks.
Optional live human duels or tournaments may continue to use a separate
`multiplayer-session` profile/mode later.

## External IGM handoff

Seven source-pinned add-ons are built in and work offline. `module-handoff`
is only for separately installed packages. The OS must:

1. accept a copied, versioned, bounded stat context and nonce;
2. queue LORD's save before normal exit;
3. launch only a compatible installed module;
4. validate a bounded one-use result and declared delta limits;
5. relaunch LORD and consume the result once.

An IGM never opens LORD saves or runs inside LORD's address space. Prefer the
planned sandbox for untrusted modules. Native modules remain fully trusted
packages but still exchange only typed data.

## Vector scenes

The title and twelve built-in ANSI/RIP-style scenes use clipped RGB565 Game
API drawing plus the shared pinned CP437 glyph primitive and need no new
capability. They deliberately do not depend on the shell's 80×30 terminal or
accept ANSI escape sequences. If `vector-scenes` is implemented for shared
assets, it must remain a bounded drawing-data format. Never execute RIPscrip
terminal, file, callback, or download commands.

## Acceptance needed for optional shared services

- Save: launch an empty slot, migrate schema 3, observe a copied schema-4 commit,
  relaunch with the committed snapshot, recover after interrupted replacement,
  and prove conflict/read-only/unavailable behavior.
- P4RM Mac realm: cover upload/download retry, stale revisions, duplicate
  nonce, disconnect, offline edits, reconnect, directory bounds, hostile
  frames, and hourly rollover exactly once.
- Typed realm: host tests cover duplicate action requests, changed-body nonce
  rejection, mail delivery/acknowledgement, ChompCoin transfer, PvP outcome,
  team consent, and tavern fan-out. Hardware acceptance must repeat these over
  exact H1 and encrypted BLE routes and retain serial evidence.
- IGM handoff: cover wrong schema/game, expired/replayed nonce, excessive
  deltas, cancellation, missing module, and save-before-exit recovery.

The standalone cartridge must keep working when all optional services are
absent.
