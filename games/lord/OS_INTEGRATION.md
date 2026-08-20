# LORD 1.1.0 OS integration contract

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
local warriors, twelve mail slots with text, twelve news records, marriages,
children, conversation, announcement, IGM usage, and realm revision. It never
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
realm_revision, spouse_index, npc_spouse
```

Map the existing flows to asynchronous, revisioned operations:

| Existing game flow | Optional realm operation |
|---|---|
| Other Warriors / rankings | paged directory snapshot |
| Player challenge / inn attack | lease immutable opponent snapshot |
| PvP finish | idempotent outcome commit |
| Inbox / sent mail | list, read, mark-read, send by opaque ID |
| Courtship / proposal / divorce | consent-based relationship transaction |
| Bank transfer | idempotent bounded transfer |
| Daily News / conversation | bounded sanitized feed |
| Sleep / daily reset | trusted realm-day transition |

Opaque remote IDs, revisions, leases, and tickets belong in a future adapter
tail. Never treat local array indexes as remote identity. A conflict may not
award gold, kill an opponent, deliver duplicate mail, or create a marriage.
Remote relationship changes require both players' consent. Offline state must
never overwrite a newer server revision.

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
- Realm: cover stale revisions, duplicate outcomes, declines, consent,
  disconnect, offline edits, reconnect, and sanitized hostile text.
- IGM handoff: cover wrong schema/game, expired/replayed nonce, excessive
  deltas, cancellation, missing module, and save-before-exit recovery.

The standalone cartridge must keep working when all optional services are
absent.
