# Console OS upgrade plan for full LORD support

## Purpose

Upgrade Console OS and the native P4 Game API so the `games/lord` cartridge
can support durable characters, mail, shared player records, asynchronous PvP,
romance state, real daily rollover, external IGMs, and optional RIP-style
scenes without giving a cartridge raw filesystem, network, USB, clock, or
display ownership.

This is an implementation handoff. The current LORD 0.2.0 cartridge has
playable local realm records, mail, PvP, romance, built-in IGMs, code-drawn
RIP-style scenes, and a versioned save codec. OS services are still required
for persistence and shared/remote state. Do not work around the missing
services by opening files or sockets from `games/lord`.

## Current state and blockers

### Already implemented

- Native games are validated `p4-native-elf-v1` cartridges.
- The cartridge host table has `struct_bytes` and safely supports optional
  tail fields.
- `.P4R` provides a validated, immutable resource payload.
- `components/p4_multiplayer` provides bounded P4MP v1 packet, byte-stream,
  peer, replay, CRC32, timeout, disconnect, and neutral-input handling.
- `components/p4_desktop` has Save Manager metadata, but not a save backend.
- `components/p4_ansi` and `components/p4_bbs` provide bounded ANSI/BBS UI.
- Console OS owns storage, input, display, audio, networking, and lifecycle.
- LORD 0.2.0 owns an explicit little-endian save codec capped at 512 bytes,
  local fallback records for four warriors, a six-message mailbox, daily PvP
  and romance counters, three built-in IGMs, and five clipped RIP-style scenes.

### Missing

- `P4_GAME_CAP_STORAGE` is read-only `.P4R`; it cannot store saves.
- No Game API callback loads or queues writable game state.
- Save Manager is a read-only metadata model.
- `p4_multiplayer` is not exposed to cartridges and has no live firmware
  transport/lobby.
- The BBS transport has no frozen message, realm, or lobby protocol.
- Games receive one normalized input snapshot, not stable player slots.
- Games have no OS-owned text-entry request for names or mail composition.
- Games have no trusted date/day value.
- There is no typed IGM handoff or sandboxed module runtime.
- RIP graphics are not a Game API format. Raw RIPscrip must not be fed into
  the ANSI parser or allowed to execute terminal/file commands.

## Compatibility decision

Keep package format `p4-native-elf-v1` and `P4_GAME_API_VERSION == 1` for this
upgrade. Append optional fields to `p4_cartridge_host_v1_t` and gate every new
service with both:

1. a new capability bit; and
2. `struct_bytes`/field-presence checks in `cartridge_main.c`.

Old cartridges will ignore the new host-table tail. New cartridges must treat
every new service as optional unless their manifest marks it required. Do not
reorder, resize, or reinterpret existing fields. If an implementation cannot
honor this append-only rule, introduce a parallel v2 host table and retain the
complete v1 loader; never silently change the v1 ABI.

## New capability bits

Reserve these bits in `p4_game_capability_t` and every manifest/package
validator:

| Bit | Manifest name | Meaning |
|---:|---|---|
| 6 | `save` | Namespaced durable save snapshot and queued commit |
| 7 | `text-input` | OS-owned bounded text-entry modal |
| 8 | `realm` | Directory, mailbox, relationship, and asynchronous PvP service |
| 9 | `multiplayer-session` | Live OS-owned lobby and player-slot input service |
| 10 | `module-handoff` | Typed exit-to-IGM and return-result service |
| 11 | `vector-scenes` | Optional bounded vector/RIP-style scene renderer |

Keep `storage` as the existing read-only `.P4R` capability. Do not overload it
with writes.

Update capability validation in at least:

- `components/p4_game_api/include/p4/game.h`
- `components/p4_game_api/src/game_runtime.c`
- `components/p4_game_package/`
- `scripts/build-game-package.py`
- `scripts/generate-game-registry.py`
- `scripts/new-game.py`
- `game-platform/runtime/cartridge_main.c`
- `components/platform_game_loader/`
- the Console OS cartridge-host construction in
  `apps/console_os/main/console_os_main.c`

## 1. Durable save service

### Game-facing contract

Expose one OS-owned snapshot at game start and one copied, non-blocking commit
request. A game never receives a path or writable host pointer.

Recommended bounds:

```c
enum {
    P4_GAME_SAVE_MAX_BYTES = 16 * 1024,
    P4_GAME_SAVE_MAX_SLOTS = 2,
    P4_GAME_SAVE_SLOT_ID_BYTES = 16,
};

typedef enum {
    P4_GAME_SAVE_NONE = 0,
    P4_GAME_SAVE_READY,
    P4_GAME_SAVE_QUEUED,
    P4_GAME_SAVE_COMMITTED,
    P4_GAME_SAVE_CONFLICT,
    P4_GAME_SAVE_UNAVAILABLE,
    P4_GAME_SAVE_ERROR,
} p4_game_save_status_t;
```

Append to `p4_game_services_t`:

- `save_context`
- immutable launch snapshot: bytes, length, schema version, sequence
- `queue_save(context, slot_id, schema, expected_sequence, bytes, length,
  ticket_out)`
- `read_save_status(context, ticket, status_out, committed_sequence_out)`

Append matching adapter callbacks to `p4_cartridge_host_v1_t`. The wrapper
must verify that the complete field is present before exposing `save`.

`queue_save` must copy the complete payload before returning. It must never
retain cartridge memory and must never block on FAT/SD I/O. Console OS may
finish the commit after the cartridge exits because it owns the copied bytes.
Only one pending commit per game/slot is required initially.

### On-disk format

Use an OS-owned container, not a raw dump of `lord_state_t`:

```text
/SAVES/<game-id>/<slot>.P4SAVE
```

The fixed header should include:

- magic and format version;
- total/header/payload sizes;
- exact game ID and slot ID;
- game-defined schema version;
- monotonic sequence;
- payload SHA-256;
- header CRC or complete-object digest;
- reserved zero bytes.

Bound the total file to header plus 16 KiB. Validate ASCII IDs, all sizes,
reserved bytes, digest, and sequence before exposing a snapshot.

### Atomicity and recovery

Implement saves in a reusable OS component such as `components/p4_game_save`.
The component must:

1. stage to a reserved non-runnable filename;
2. flush/sync;
3. read back and validate the complete object;
4. preserve the previous valid object as a bounded backup;
5. atomically rename the staged object into place;
6. maintain a recoverable one-operation journal;
7. recover deterministically after power loss at every transition.

Formatting is never recovery. If storage is host-owned, read-only, absent, or
not authorized for the board profile, remove the `save` capability and let
the game continue in session-only mode.

### LORD save schema

LORD should serialize fields explicitly in little endian. Do not persist C
enums, pointers, padding, or the raw state structure. Version 1 needs:

- class, level, weapon, armour, HP/max HP, strength, defence;
- gold, bank, experience, forest fights, skill uses, dragon kills, day;
- daily PvP/romance/IGM counters;
- bounded local/realm relationship state;
- last applied realm revision and mailbox cursor.

Validate ranges before accepting a decoded save. A failed save must start a
new character or offer the prior valid backup; it must not partially apply.

## 2. OS-owned text entry

Mail and player names need text beyond the eight-button input API. Add an
asynchronous OS modal rather than passing keyboard events or terminal buffers
to games.

Suggested callbacks:

- `request_text_input(context, request)` returns a ticket;
- `read_text_input(context, ticket, result)` returns pending/accepted/cancelled.

The request supplies a bounded title, prompt, initial text, byte limit, and
flags such as printable ASCII or UTF-8. Start with 128 UTF-8 bytes. Console OS
copies all request strings, owns the keyboard UI, validates UTF-8/control
characters, and returns a copied bounded result. Only one modal may be active.

Reuse the existing terminal keyboard UI where practical. Physical keyboard,
touch keyboard, and controller character picker must produce the same result.

## 3. Realm service for classic BBS features

LORD multiplayer is primarily asynchronous shared-record gameplay, distinct
from real-time controller multiplayer. Add `components/p4_realm` and expose
typed snapshots/actions rather than files, sockets, or P4MP packets.

### Bounded snapshots

Initial limits:

- 16 directory entries per page;
- 16 mailbox headers per page;
- 128 UTF-8 bytes per display name;
- 64 UTF-8 bytes per subject;
- 512 UTF-8 bytes per message;
- opaque 64-bit player/message/action IDs;
- monotonic realm revision and per-record revision;
- one outstanding request of each class per foreground game.

Directory entries may expose only gameplay fields approved by the realm:
display name, level, class, alive/busy status, relationship availability, and
public ranking. Do not expose account IDs, addresses, routes, or credentials.

### Mail

Provide asynchronous operations for:

- list headers after a cursor;
- read one message by opaque ID;
- send bounded mail to an opaque player ID;
- mark read;
- delete/archive own mail.

Every mutation uses an idempotency token. The OS owns an outbox journal and
may retry without duplicating a message. Sanitize control sequences before
text reaches ANSI rendering. A remote message is data, never a prompt or
command.

### PvP

PvP needs a transaction/lease, not direct opponent-record writes:

1. Game requests a challenge against an opaque player ID.
2. Realm returns an immutable opponent battle snapshot, match ID, authoritative
   random seed, record revision, expiry, and allowed actions.
3. Game runs a bounded battle and reports outcome plus a compact transcript or
   state hash.
4. Realm validates the lease/revision, applies rewards/death atomically, and
   returns the new realm revision.
5. Expired, duplicated, conflicting, or already-consumed match IDs fail closed.

The first local implementation may trust installed native code, but the
protocol must still prevent accidental double rewards and stale-record loss.

### Romance and consent

Store courtship/marriage as a realm-owned relationship transaction. Required
operations are invite, accept, decline, withdraw, and status. Both players
must opt in; a cartridge must not silently marry two remote records. Preserve
block/privacy settings and do not reveal a declined player beyond the normal
result. Relationship bonuses are calculated by LORD from a confirmed bounded
status, never by editing the other player’s save.

### Daily rollover

Expose a trusted realm day ID and next-rollover status. The OS/realm applies
daily limits exactly once per character/revision. Do not use a cartridge’s
elapsed time, manual inn visits, or an untrusted RTC as the authority for
remote PvP, flirting, mail, or IGM limits.

When offline, LORD may use its existing local day loop but must label it
`LOCAL REALM`. Synchronize through revisioned actions when the service returns;
never overwrite a newer server record with an offline snapshot.

## 4. Live multiplayer-session service

Expose the existing `components/p4_multiplayer` core through an OS adapter.
Games still must not receive P4MP datagrams, routes, serial ports, sockets, or
transport identities.

The Game API should offer:

- list compatible lobbies;
- host/join/leave by opaque lobby token;
- exact game ID, API version, package payload SHA-256, player count, and mode
  matching;
- authoritative session seed and stable player-slot assignment;
- submit local tick-stamped normalized input;
- read a bounded snapshot of all player-slot inputs/events;
- optional state-hash submission;
- explicit desync, timeout, reject, and disconnect statuses.

Disconnect must neutralize the affected slot in the same update. Reconnect
must insert a neutral barrier update. Limit sessions to four players and keep
the current three-second default timeout.

Implement same-console stable player slots before remote transport. Then add
the already-planned H1 relay endpoint or H2 USB-device relay. Never connect two
sink/device ports directly and never claim the current packet core is a live
transport.

Asynchronous LORD PvP should use `realm`, not this real-time service. A future
live duel/tournament may use `multiplayer-session`.

## 5. IGM handoff

Do not load arbitrary native code into LORD’s address space and do not let an
IGM open LORD saves.

Implement built-in IGMs inside `games/lord` first. For external IGMs, use an
OS-owned typed handoff:

1. LORD queues its save and submits a bounded `p4_igm_context` containing only
   approved stats, a nonce, schema, and return game ID.
2. LORD exits normally.
3. Console OS validates and launches an installed IGM package that declares
   compatibility with LORD’s exact handoff schema.
4. The IGM returns a bounded signed/result object to an OS-owned inbox.
5. LORD relaunches, consumes the result once, validates every delta against
   declared limits, applies it, and queues a new save.

Prefer the planned `p4-lua-5.4-v1` sandbox for third-party IGMs. If native IGMs
are allowed, treat them as fully trusted packages but still isolate data with
the typed handoff. Result IDs are single-use and expire. Cap gold/stat/item
deltas so a malformed module cannot overflow LORD state.

## 6. RIP-style scene support

Built-in LORD scenes can be drawn with existing clipped RGB565 primitives and
need no API change. Do not vendor unapproved upstream `.LRD` or `.ICN` data.

If reusable RIP-style assets are required, define a new bounded vector scene
format in `.P4R` or an OS component. It may contain only drawing commands such
as clear, line, rectangle, circle, polygon, palette color, and bounded text.
It must not contain terminal commands, paths, file operations, scripts,
downloads, mouse callbacks, or host escape sequences.

Recommended limits:

- 320×200 logical canvas;
- 1,024 commands per scene;
- 64 polygon points;
- 64-byte text command;
- 64 scenes and 256 KiB decoded data per game;
- clipped coordinates and saturating arithmetic;
- complete format/version/length validation before rendering.

Keep remote ANSI parsing and vector-scene decoding separate. Untrusted BBS
bytes must not switch the terminal into an executable RIP mode.

## 7. BBS transport additions

Freeze realm/mail/lobby framing separately from P4MP input packets. Reuse its
bounded framing principles—magic resynchronization, declared payload limit,
sequence, CRC32, and route binding—but use distinct magic and message types.

Transport requirements:

- TLS/authentication when a network transport is added;
- pinned server identity or explicit enrollment;
- no credentials exposed to games;
- bounded offline outbox/inbox;
- idempotent mutations and monotonic revisions;
- malformed-frame counters and disconnect cleanup;
- ANSI strings sanitized after decoding and before rendering;
- package transfer remains separate from messages and realm actions.

CRC32 is corruption detection, not authentication.

## 8. Console UI changes

Upgrade the existing built-in pages:

- Save Manager: actual validated slots, sequence, size, last result, backup,
  delete confirmation, and writable/read-only reason.
- Multiplayer: local player slots, transport state, compatible lobbies,
  latency/timeout, disconnect reason, and an honest offline state.
- BBS: mailbox/realm connection state and pending outbox count.
- Game detail: show required/optional capabilities and why a required service
  is unavailable.

Games remain 320×200. Text-entry and system confirmations are OS overlays;
they must restore the game surface/input generation safely when dismissed.

## 9. LORD integration after the OS services land

LORD 0.2.0 now has the complete playable local fallback. The following game
work is complete:

- explicit deterministic save encoder/decoder with CRC, truncation, corrupt,
  range, and round-trip tests;
- Other Warriors directory and bounded challenge/PvP flow;
- mailbox read, preset-compose, reply, and event-message flow;
- courtship, gifts, proposals, marriage, and local spouse bonuses;
- local daily PvP, romance, and IGM limits;
- three built-in bounded IGMs;
- a generated-and-dithered 16-color dragon/title scene plus clipped
  code-drawn ANSI/RIP-style town, forest, inn, and battle scenes;
- visible `LOCAL REALM` and session-only labels.

The OS integration pass should replace backends, not rebuild these screens or
rules. Wire the immutable launch save snapshot to `lord_save_decode()` and
copy the result of `lord_save_encode()` into `queue_save()`. A valid decoded
snapshot enters the town; missing or rejected data keeps the new-character
flow. Map local directory/mail/PvP/relationship operations onto realm tickets
while retaining the arrays as the offline snapshot. Replace preset compose
with the OS text modal when available, and replace inn-authoritative social
counter resets with the trusted realm day. Add `OFFLINE`, `SYNCING`,
`CONFLICT`, and `SAVED` status text from service state.

External IGM handoff remains an OS adapter task; the three built-in IGMs must
continue to work without it. LORD must remain playable when every optional
service is absent and must never receive filesystem, socket, transport, clock,
or display-driver ownership.

## 10. Required tests

### Save component

- round-trip and deterministic encoding;
- every truncated length and invalid reserved field;
- wrong game/slot ID and digest mismatch;
- maximum payload and quota rejection;
- expected-sequence conflict;
- duplicate ticket/idempotency behavior;
- power loss after every journal/stage/rename transition;
- backup recovery and no implicit format;
- storage ownership/read-only/unavailable behavior.

### Game API and cartridge compatibility

- old v1 host with old cartridge;
- new extended host with old cartridge;
- old host rejects a cartridge requiring a new capability;
- new host runs a cartridge with optional unavailable services;
- every truncated `struct_bytes` tail boundary;
- callbacks copy cartridge buffers before returning;
- invalid sizes, IDs, UTF-8, tickets, and status transitions;
- package capability parser/registry/scaffolder coverage.

### Realm/mail/PvP

- pagination and hard limits;
- sanitized text/control-sequence rejection;
- duplicate mail/action idempotency;
- stale record conflict;
- PvP lease expiry/replay/double-reward rejection;
- relationship consent, decline, block, and conflict paths;
- offline outbox replay after reconnect;
- daily rollover exactly once.

### Multiplayer session

- compatible-content matching;
- four-player capacity;
- stable slots and neutral disconnect/reconnect barrier;
- replay, bad CRC, bad route, timeout, malformed frames, and desync;
- two-process host relay with loss, duplication, delay, and disconnect.

### LORD

- save round-trip and corrupt-save fallback;
- local/offline and realm-backed mail flows;
- PvP win, loss, conflict, and daily limit;
- romance consent and persistence;
- each built-in IGM and invalid external result;
- vector command/scene bounds;
- renderer guards, Start/B/Back, touch mapping, and tone fallback;
- fresh `.P4G` build with no unsupported ELF imports.

## 11. Implementation order

1. Add durable save component, host-table tail, capability bit, package
   validation, SDL in-memory backend, and Save Manager integration.
2. Add OS text-entry modal and host-runner implementation.
3. Add local `p4_realm` backend with directory/mail/PvP/relationship tests;
   expose it through the Game API.
4. Add LORD save codec and full local realm UI; keep network status offline.
5. Add revisioned BBS realm transport and trusted daily rollover.
6. Expose same-console player slots, then the existing P4MP core as the
   multiplayer-session adapter and lobby.
7. Add built-in LORD IGMs, typed external handoff, and bounded vector scenes.
8. Run focused host/sanitizer tests, `make game-sdk-host`, BBS/console-shell
   tests, one matching Console OS build, and only then the guarded hardware
   workflow if explicitly requested.

## Definition of done

- Existing v1 cartridges still launch unchanged.
- LORD can load, queue, exit, relaunch, and recover a durable character.
- Save writes survive simulated interruption without losing both current and
  prior valid objects.
- Mail, PvP, and romance use realm revisions and idempotent actions.
- Live multiplayer exposes only lobbies, slots, sanitized input, seed, and
  status—never a transport handle.
- External IGMs exchange one typed, bounded, single-use result.
- RIP-style data cannot execute terminal, file, or network operations.
- UI reports offline/read-only/conflict states honestly.
- Host and package tests pass under sanitizers.
- Any physical-device claim names the exact board, artifact hash, serial
  evidence, and observed behavior. A build alone is not hardware acceptance.
