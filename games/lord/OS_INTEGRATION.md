# LORD 1.2.0 OS integration contract

LORD is a complete standalone cartridge. This document describes optional OS
services that turn its persistent local realm into a shared BBS realm without
giving the game filesystem, network, clock, USB, display, or raw-input
ownership.

## Services used now

- Required: `video`, `controls`
- Optional: `audio-tone`, `save`

The save adapter is implemented in `src/lord.c`. At launch it validates
`save_schema_version == 3`, the nonzero host sequence, and the copied launch
snapshot before decoding. At safe update boundaries it encodes into a bounded
4 KiB staging buffer, queues slot `AUTO`, polls the returned ticket, and clears
`save_dirty` only after `COMMITTED`. Conflicts and unavailable storage fail
closed while gameplay continues locally.

The game-defined `LDSV` payload is explicit little endian and CRC protected.
It persists the complete player, all three skill trees, daily counters, eight
local warriors, twelve mail slots with text, twelve news records, adventure
teams, trust, youth mentoring, conversation, announcement, IGM usage, and realm
revision. It never
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

## Realm adapter

The offline snapshot consists of:

```text
lord_realm_player_t realm[8]
lord_mail_t         mail[12]
lord_log_entry_t    log[12]
realm_revision, partner_index, npc_friend
```

Map the existing flows to asynchronous, revisioned operations:

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

Opaque remote IDs, revisions, leases, and tickets belong in a future adapter
tail. Never treat local array indexes as remote identity. A conflict may not
award ChompCoin, defeat an opponent, deliver duplicate mail, or form an
adventure team. Remote team changes require both players' consent. Offline
state must never overwrite a newer server revision.

## Backend sync record implemented now

`src/lord_sync_impl.h` defines the bounded `LRSY` version-1 record that a
future `realm` callback will copy to Console OS. It contains a 52-byte explicit
little-endian header followed by the complete CRC-protected schema-3 save:

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

This codec is intentionally transport-free. The current Game API still lacks
the asynchronous `realm` callbacks needed to submit it, so version 1.2 remains
offline/save playable and never pretends a local warrior is a server player.
The complete server and adapter plan is in [BACKEND_SYNC.md](BACKEND_SYNC.md).

Asynchronous classic LORD PvP uses `realm`; `multiplayer-session` is reserved
for an optional future live duel/tournament mode.

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

- Save: launch an empty slot, mutate state, observe a copied schema-3 commit,
  relaunch with the committed snapshot, recover after interrupted replacement,
  and prove conflict/read-only/unavailable behavior.
- Realm: cover stale revisions, duplicate outcomes, declines, team consent,
  disconnect, offline edits, reconnect, and sanitized hostile text.
- IGM handoff: cover wrong schema/game, expired/replayed nonce, excessive
  deltas, cancellation, missing module, and save-before-exit recovery.

The standalone cartridge must keep working when all optional services are
absent.
