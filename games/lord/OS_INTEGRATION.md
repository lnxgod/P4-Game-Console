# LORD 1.8.0 OS integration contract

LORD is a complete standalone cartridge. This document describes optional OS
services that turn its persistent local realm into a shared BBS realm without
giving the game filesystem, network, clock, USB, display, or raw-input
ownership.

Release boundary: Console OS 0.4.88 / LORD 1.6.1 is the historical accepted
host/build security baseline and was not installed on Pink or Green. The
Console OS 0.4.90 / LORD 1.8.0 source is an unsealed, unflashed successor
candidate and has no exact-device acceptance.

## Services used now

- Required: `video`, `controls`
- Optional: `audio-tone`, `save`, `multiplayer-session`

The save adapter is implemented in `src/lord.c`. At launch it accepts save
schema 3, 4, or 5, validates the nonzero host sequence and copied launch
snapshot, and migrates older schemas in memory. At safe update boundaries it encodes schema 5
into a bounded
4 KiB staging buffer, queues slot `AUTO`, polls the returned ticket, and clears
`save_dirty` only after `COMMITTED`. Conflicts and unavailable storage fail
closed while gameplay continues locally.

The game-defined `LDSV` payload is explicit little endian and CRC protected.
Schema 5 persists the complete player, all three skill trees, daily counters, eight
local warriors, twelve mail slots with text, twelve news records, adventure
teams, trust, youth mentoring, conversation, announcement, IGM usage, and realm
revision, plus opaque directory/team actor IDs, the last applied hub event ID,
and the last accepted hub actor/server revision/committed game generation. It never
serializes pointers, raw enums, structure padding, or `lord_state_t` itself.

Enemy intent and the current Strike/Guard/Technique/Feint exchange are
transient because a decoded save resumes safely in town. Dragon Dice totals and
Aragorn quiz operands, choices, and answers are transient for the same reason.
When `save` is available, starting either paid game debits the stake, and
Aragorn also sets its daily IGM-use bit, before an async save barrier blocks all
input and withholds every random total, operand, choice, and answer until the
host reports `COMMITTED`. A queue or terminal status failure cancels and
refunds the still-hidden round; an interruption after commit resumes safely in
town with the committed debit instead of granting a free preview. These
mechanics do not change save schema 5. When `save` is absent, the games remain
session-playable but make no durable anti-preview claim.

Dragon Dice stakes 5 ChompCoin and returns 8 on a win, a deliberately
negative-expected-value diversion. Only the first win in a daily cycle grants
one charm and high spirits; that reward lock is a persisted daily bit.

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
immutable session seed and status. The backend hosts each logical session,
the console joins slot 1, and the backend initiates synchronized start.

`src/lord_realm_net_impl.h` layers bounded `P4RM` v3 messages on that service.
It synchronizes full `LRSY` snapshots with the Mac hub, publishes the local
profile, consumes eight-entry pages of a 100-player roster, and applies one trusted
hourly refresh. It also submits bounded actions and applies durable events for
mail, transfers, friendship, teams, mentoring, PvP, tavern, news, and
hub-authoritative Adventure Clubs. The wire
acknowledgement records receipt; only a later accepted snapshot advancing the
persisted event cursor commits delivery. It remains inactive when the capability or connected room is
absent. Its hello carries only the persisted actor/revision/generation base.
The hub accepts an offline upload only while that base still equals the server
head; a divergent dirty copy becomes `SYNC CONFLICT` without replacing either
side. No new Game API fields, raw P4MP packets, routes, USB/BLE handles, or
clock callbacks are exposed to LORD.

The hub projects `dragon_kills` only from accepted snapshots and orders all
accepted profiles before pagination by dragon deeds, level, experience, PvP
wins, PvP losses, and stable identity tie breaks. P4RM v3 kind 22
`DIRECTORY_DEEDS` adds an 18-byte actor/deed sidecar; the existing summary and
stats packets are unchanged, so
older cartridges can ignore the extension. The Hall itself ranks only the
local hero plus the current up-to-eight-entry directory page. It is not a
single in-cartridge view of every profile in the realm.

The hub validates LORD 1.8.0 progression against the accepted head and a durable
realm-day anchor. A normal branch may gain at most three levels and must meet
the destination trainer's XP threshold; cumulative experience gain is capped
at 2,500,000 per anchored realm day, with separate economy, combat-stat,
skill, badge, and small-counter caps. These are conservative save-editor
checks, not proof of every client-side fight.

A direct matching-base offline Red Dragon deed is accepted only when the
authoritative hub head was already level 12. If that head is below level 12,
the player must sync once after reaching level 12 and before finishing the
Dragon. Once a level-12 head is accepted, the whole encounter may happen
offline and no separate pre-fight `seen_dragon` upload is required; the deed
must still advance exactly once and every rebirth field must be canonical.

P4RM v3 welcome bit 4 is the optional, explicitly parent-authorized
`ADOPT_LOCAL` recovery hook. It keeps the existing 36-byte welcome, is mutually
exclusive with snapshot/accept/conflict, and uses the returned actor/revision
as the upload compare-and-swap base while the local committed generation stays
zero. The actor-bound pre-commit save may carry revision zero; snapshot and
ordinary accept still require a nonzero head, while revision-zero conflict is
shown as `SYNC CONFLICT`. This adds no Game API field or save-schema version.
Realm-bound characters also cannot invoke the local inn reset: connected and
disconnected play both wait for the Mac realm's trusted hourly rollover, while
local-only characters retain classic sleep. Both paths use the same
no-bank-interest reset.

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
| Other Warriors / rankings | Hub pages across 100 accounts are ordered by dragon deeds, level, XP, PvP wins, PvP losses, and stable ties; kind 22 supplies deeds. The Hall shows the local hero plus only the current up-to-eight-entry page |
| Player challenge / inn attack | lease the selected cached directory opponent; an immutable battle snapshot is a future public-realm upgrade |
| PvP finish | idempotent outcome commit |
| Inbox / sent mail | list, read, mark-read, send by opaque ID |
| Friendship / team invitation / parting | consent-based team transaction |
| Adventure Club Hall | create/join/leave, one daily rally, friendly Banner Clash, cheer, member annotations, and paged club standings; all membership and scores remain hub-owned |
| Bank transfer | idempotent bounded transfer |
| Daily News / conversation | bounded sanitized feed |
| Sleep / daily reset | trusted realm-day transition; local sleep is disabled after actor binding |

Opaque remote IDs and leases remain inside validated P4RM messages and schema-5
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
little-endian header followed by the complete CRC-protected schema-3, schema-4,
or schema-5 save:

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

The codec itself remains transport-free. Version 1.8.0 submits it through a
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

- Save: launch an empty slot, migrate schemas 3 and 4, observe a copied schema-5 commit,
  relaunch with the committed snapshot, recover after interrupted replacement,
  prove a paid quiz/dice outcome and input remain locked through `QUEUED`, prove
  queue/terminal-status failure refunds without revealing, prove interruption
  after `COMMITTED` resumes in town without refund or replay, and prove
  conflict/read-only/unavailable behavior.
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
