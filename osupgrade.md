# Remaining Console OS upgrades for full shared LORD services

## 2026-08-30 LORD 1.8.0 / Console OS 0.4.91 Adventure Club candidate

The source tree now declares Console OS 0.4.91 and LORD 1.8.0. This is an
unsealed, unflashed successor candidate, not an install or hardware-acceptance
claim. It keeps Game API v1, P4MP v1, P4RM v3, LRSY v1, and LDSV save schema 5.
No new cartridge storage or raw USB/BLE API is required: the existing
`multiplayer-session` tunnel carries additive P4RM kinds 23--26 and action kind
10, while ordinary solo progress continues to use the existing authenticated
local save.

The 1.8.0 scope adds server-authoritative Adventure Clubs without weakening the
offline-first security boundary:

- up to eight members per club and sixteen unique curated name codes, avoiding
  unmoderated player-authored club names;
- deterministic leader succession, one-day join/rejoin eligibility, one daily
  class-route rally per member, carried 12-point cooperative quests, at most
  one Banner Star bonus per club/day, and 24-realm-day seasons;
- paged standings, bounded +1/+2 club cheers, and friendly Banner Clashes with
  one outgoing and one incoming slot per club/day plus a three-day pair
  cooldown;
- server-scored clashes derived from accepted heads, normalized participation,
  majority route tactics, and a deterministic SHA-256 roll; and
- a separate club ledger that never awards or mutates personal ChompCoin, XP,
  HP, combat stats, dragon deeds, PvP counters, save generations, or snapshots.
  Ordinary direct PvP between current clubmates is denied.

The release security pass also bounds action nonces to SQLite's signed 64-bit
range, cancels expired PvP leases and leases whose actors became clubmates
before settlement, and makes season/quest rollover monotonic under concurrent
USB/BLE sessions. These rejected paths produce no player reward, event, or
economy mutation.

LDSV5 deliberately does not serialize club membership. A character remains
fully playable and locally persistent offline, but a cold offline Club Hall
asks for the Mac hub before displaying or changing shared club state. This
keeps club authority in one SQLite realm and avoids a new mergeable field in
offline character branches. New cartridges send no club mutation until an
actor-bound `GUILD_STATUS` proves the hub supports the extension; older hubs
therefore fail closed with an upgrade message.

The backend suite currently passes 67 tests, including the existing two-,
four-, and ten-client campaigns and a new fourteen-actor, thirty-day Adventure
Club campaign with unequal club sizes, solo quest carry, cooldowns, replay,
restart, season rollover, malformed requests, same-club PvP denial, and
byte-for-byte personal-state isolation. The focused ASan/UBSan C suite, final
SDL smoke/save tests, all-screen render capture, registry validation, and two
byte-identical pinned-toolchain cartridge builds pass. On 2026-08-30 the full
`make game-sdk-host` gate passed, followed by the integrated controller-first
0.4.91 Waveshare build and its fail-closed release verifier. The earlier Byte
Buddy stop was a stale pre-commit host executable;
rebuilding current commit `7b8179a` passed all five Byte Buddy tests, including
all 112 fail-closed authored-motion semantic digests, without changing a
renderer, art asset, or golden value. Promotion still requires committing the
dirty LORD successor, regenerating the commit-bound update package from that
final source identity, and guarded Pink/Green hardware acceptance.

### 0.4.91 candidate identity ledger

| Identity or evidence | 0.4.91 / LORD 1.8.0 status |
|---|---|
| Final source commit | **TBD** -- no commit or push of the dirty LORD successor has been authorized; the verified host candidate was built with HEAD `7b8179a9dc1daad22178e2531d31df75de6a56c3` plus the listed uncommitted LORD/platform changes |
| Console OS application SHA-256 and byte count | Integrated controller-first host candidate `5a474252b859477fbc875216934e804e3868a69405d83556a05390cbdfcc5ce2`, 1,835,360 bytes; fail-closed Waveshare verifier passed from `build-waveshare-usb-host`; not hardware-tested |
| `LORD.P4G` SHA-256 and byte count | `bf0d6729a319d017e5ebe16e15f414a4651f278fa3feaa727e3093fe4bb4a0c3`, 192,172 bytes; two pinned builds were byte-identical |
| LORD deterministic payload SHA-256 | `d72caabf0e38a1dc85200bf1766084019cdc4970f42f38a6842f4fcd9c73bcaa`, 191,916 bytes |
| P4MP content / compatibility SHA-256 | content `f39b6959362e57362d000438842f7d7a74f6e7661d27b2ef6cc920975e67799e`; compatibility `b53cac97249389913e9da145eab9e438197b5ddb025c7169a47ffdbae2afe551` |
| `P4UPDATE.P4U` SHA-256 and byte count | Integrated dirty-tree host candidate `6ffc9040a54697acdb2a0990eae3c99301a8548b03e4521689aa41bd5f207b25`, 1,835,616 bytes, containing the application above with version `0.4.91`, target `esp32p4`, and header build `7b8179a9dc1d`; regenerate after the final release commit because the header cannot identify uncommitted LORD/platform changes |
| Pink exact-device install ledger | **TBD** -- not flashed or hardware-tested |
| Green exact-device install ledger | **TBD** -- not flashed or hardware-tested |

For this integrated candidate, use only
`apps/console_os/build-waveshare-usb-host`. The older
`build-waveshare-landscape` directory still contains a 0.4.90 image and now
correctly fails the 0.4.91 visible-version verifier; it is stale and must not be
offered to either device.

## 2026-08-29 LORD 1.7.0 / Console OS 0.4.89 superseded host candidate

The source tree now declares Console OS 0.4.89 and LORD 1.7.0. This is a
successor-candidate handoff, not a sealed build, install, or hardware acceptance
claim. The candidate keeps Game API v1, P4MP v1, P4RM v3, LRSY v1, and LDSV
save schema 5. Combat intent and the in-progress dice/quiz round are transient,
so no serialized save field is added. With durable storage, paid Dice and
Aragorn outcomes remain hidden and all input is blocked until the debit save is
`COMMITTED`; queue or terminal status failure cancels and refunds without
revealing. Session-only profiles remain playable but have no durable
anti-preview guarantee. Existing valid 1.6.1 LDSV5 saves and
LRSY1 realm heads are intended to remain readable, but that compatibility must
be demonstrated by the focused codec and hub tests before promotion.

The 1.7.0 promotion scope is the engagement and fair-play successor to the
0.4.88/1.6.1 security baseline:

- compress the trainer, equipment, and Red Dragon curves so a normal-stat
  character has a credible first-dragon path in roughly 18--25 played days,
  while preserving level gates and rebirth;
- replace one-button battle repetition with visible enemy intent and meaningful
  Strike, Guard, class Technique, Feint, and Run choices, including bounded
  rewards and penalties for the chosen matchup;
- make the amulet, fairy lore, high spirits, friendship badges, class mastery,
  and best-friend/team state contribute bounded gameplay benefits rather than
  acting only as collection flags;
- make Dragon Dice a player-controlled push-or-hold game, make Aragorn's math
  challenge an actual four-choice puzzle, keep Dice at a negative expected
  value by returning 8 after a 5-ChompCoin winning stake, limit its charm/high
  spirits reward to the first daily win with a persisted bit, and show a real
  deed-first sorted ranking that includes the local character;
- charge forest adventures consistently, remove percentage-based local bank
  interest, and permit an unbound local-only Inn sleep only after that
  character has used the current day's forest adventures. A realm-bound
  character still receives its next day only from the Mac's hourly realm clock;
  and
- keep hub acceptance aligned with the new progression thresholds and derived
  bonuses, including bounded daily XP, receipt-proven shared PvP counters,
  per-day friendship gains, and semantic rejection of impossible
  level/stat/economy transitions.

The local Mac realm now has an authoritative prestige directory. The hub
projects dragon deeds from accepted heads, orders all profiles by deeds, level,
XP, PvP wins, PvP losses, and stable identity ties, and sends deeds in the
bounded 18-byte P4RM v3 kind-22 `DIRECTORY_DEEDS` sidecar. Existing summary and
stats packets are unchanged, so older cartridges ignore the extension. The
current Hall is intentionally smaller than a public realm-wide leaderboard: it
re-sorts only the local hero plus the current up-to-eight-profile directory
page. A hostile Internet-wide service still needs the authenticated accounts,
TLS, moderation, quotas, and typed `realm` adapter described below.

One stronger-proof improvement remains outside the cartridge-only 1.7.0
change: a final snapshot can show only a plausible offline Red Dragon deed,
not prove every combat round. The hub accepts a matching-base direct deed only
when its authoritative head was already level 12. If the head is below level
12, the player must sync once after reaching level 12 before finishing the
Dragon. After the level-12 head is accepted, the whole encounter may happen
offline without an intermediate `seen_dragon` upload, but the deed must advance
exactly once and every rebirth field must be canonical. A future append-only
deed journal or server-verifiable encounter receipt could provide stronger
proof without breaking fully offline level-12 play.

The constants in the reviewed 1.7.0 source and its deterministic focused tests
are authoritative. Do not copy progression constants from this handoff into a
second table that can drift. Promotion requires the deterministic estimate to
stay inside the 18--25 normal-day target and requires old-save decode, fresh
save round-trip, LRSY round-trip, clean-stale download, matching-base upload,
stale-dirty conflict, and hub semantic rejection coverage to pass together.

### 0.4.89 candidate identity ledger

No 0.4.89 firmware or update-package identity is recorded yet. The table does
record a twice-reproduced host LORD cartridge candidate, but it is not sealed,
committed, installed, or hardware-accepted. In particular, none of the 0.4.88
hashes in the next section may be copied into this ledger or used to authorize
the successor.

| Identity or evidence | 0.4.89 / LORD 1.7.0 status |
|---|---|
| Final source commit | **TBD** -- record after the candidate changes and tests are committed |
| Console OS application SHA-256 and byte count | **TBD** -- regenerate with the locked Waveshare build |
| `LORD.P4G` SHA-256 and byte count | Twice-reproduced host candidate: **183,336 bytes**, SHA-256 `c924f8902b973a6d0599fc321b337cf06491ecaddd5ba4e95b6433176e2c058e`; regenerate after the source is frozen and committed before treating this as a release identity |
| LORD deterministic payload SHA-256 | Twice-reproduced host candidate: **183,080 bytes**, SHA-256 `243d4694c146657ff2e7d90246f6985ea368fae221e59a58914feab1dafc72d7`; this is not a signed release or hardware acceptance claim |
| P4MP content / compatibility SHA-256 | Host-derived content `01f85ff1e4ea0695d37649399ce9dc4a516be0eef8ef928dea66ffc4a3228d75`; compatibility `3d4b6f80177c26f1c17e1200fa59672cd7c27b721bc2a81a0cb929d3cad03da6` |
| `P4UPDATE.P4U` SHA-256 and byte count | **TBD** -- regenerate after the final release commit so its embedded commit is exact |
| Pink exact-device install ledger | **TBD** -- no install or hardware claim may be inferred from a host test or build |
| Green exact-device install ledger | **TBD** -- no install or hardware claim may be inferred from a host test or build |

Before either device is offered the successor, run the focused sanitizer-backed
LORD/save/realm suites, the two-client protocol E2E, `make game-sdk-host`, the
locked 0.4.89 Waveshare build, and its release verifier. Then record the actual
artifact sizes and SHA-256 values above from those regenerated outputs. A later
guarded install must bind those exact artifacts to the already backed-up Pink
and Green devices and retain UART/manual gameplay evidence. Do not flash, reuse
an earlier authorization, or claim hardware acceptance merely because these
source and host gates pass.

## 2026-08-29 offline-first fair-play successor

Historical-evidence boundary: this complete section records the Console OS
0.4.88 / LORD 1.6.1 baseline. Its artifact hashes, byte counts, test results,
and unflashed-device statements are retained as historical provenance only;
they are not Console OS 0.4.89 / LORD 1.7.0 identities or authorization.

Console OS 0.4.88, LORD 1.6.1, and the Mac realm-hub successor implement the
first practical family-play security tier without changing Game API v1, P4MP
v1, P4RM v3, or LRSY v1.

Console OS now stores every new native-game save as `P4SAVE2`, authenticated
with HMAC-SHA256 under a random device-local key in the exact bounded plaintext
NVS namespace `p4_save_seal`. A fixed per-game/slot NVS registry permits one
legacy `P4SAVE1` migration, then permanently rejects later legacy current-file
or backup reinjection. Each authenticated slot also records the highest
installed sequence and exact P4SAVE2 object SHA-256, rejecting older backups
and alternate same-sequence branches while still allowing a newer journaled
commit to recover. Existing unanchored P4SAVE2 files are accepted once as a
baseline and then anchored. Key, registry, anchor, or verification failure
withholds the save capability; cartridges never receive the key. Console OS
also admits the protected LORD ID only when its exact deterministic payload
SHA-256 matches the official build, closing the unsigned same-ID signing
oracle. This prevents ordinary microSD payload editing, cross-device save
copying, and SD-only rollback. It deliberately does not claim protection
against modified firmware, plaintext-NVS extraction, or rollback of the
complete NVS namespace together with matching SD files.

If an anchored slot loses every SD save artifact, the OS retains sequence N as
an empty recovery floor and accepts only a reconstructed or Mac-restored N+1
snapshot. It does not reset the slot to sequence one or reopen an old backup.
Any stale or tampered current, backup, stage, or journal prevents this empty
recovery path and fails closed.

Realm-linked LORD characters remain playable offline with the actions left in
their current realm hour, but Inn sleep cannot mint a new realm day. The Mac
hourly clock grants the next refresh. Reconnect uses the persisted actor,
server revision, and committed save generation as a single-use offline branch
lease. The hub parses the complete fixed LDSV layout, requires the inner lease
to match the actual head, derives public stats/economy from accepted snapshots,
and applies progression bounds cumulatively from a durable realm-day anchor so
repeated uploads cannot multiply the allowance. Compare-and-swap selects only
one branch; a stale dirty branch remains `SYNC CONFLICT` with neither copy
overwritten.

For a matching dirty branch, the hub now enforces offline-first ordering: the
exact local snapshot commits before directory changes, action results, or
queued events can mutate cartridge state. LORD shows `SYNCING` and pauses input
while that snapshot or a split event body is in flight, while continuing to
service durable local saves. Durable events are delivered one at a time until
an accepted cursor snapshot proves each application; late duplicate ACKs and
reconnects cannot skip or double-apply them. Pending PvP knockouts also suppress
the target's effective alive state before the target snapshot catches up.

An explicit `--adopt-local PROFILE` operator grant is the only path that may
replace a conflicting head. The same SQLite transaction archives the old head,
installs the fully validated local snapshot, derives its profile/day anchor,
and consumes the one-time grant. Exact upload retries are idempotent, including
a lost commit response and the revision-zero first-enrollment bridge.

The repeatable friendship/Inn exploits are closed: team and NPC pact HP bonuses
are removed on parting, bartender riddles consume a daily friendship action,
Dragon Dice no longer mints unlimited friendship badges, and realm-bound local
sleep is blocked online and offline. ChompCoin wagering remains part of the
game.

Software status: focused sanitizer-backed save and seal tests pass for the
0.4.88 source successor. The independent two-console protocol E2E also passes:
both clients enroll, play offline, reconnect, exchange durable mail and a
100-ChompCoin transfer, survive simulated post-receipt power loss, and reopen
canonical heads with all three events and both economy rows committed. The SDL
runner separately exercises the real LORD C cartridge but does not yet expose
multiplayer-session callbacks. The final locked Waveshare build and release
verifier now pass. Exact candidates are Console OS application
`5b878c104a42ea72481b73f7c955bb5e518b5b60493b685d60425754c7ed36ad`
(1,808,480 bytes) and `LORD.P4G`
`2884c531638f5256ca1c3f1594b054b6fcf69eedbb131af3c5cf20735504baac`
(178,468 bytes; payload SHA-256
`9645c6f9bed0712b549eb298f90fde4f909a73c21ee6b2f8bef34796fccad5c0`).
`P4UPDATE.P4U` contains the same application plus a header that embeds the
source commit, so it is regenerated after the final release commit and its
exact hash belongs in the exact-device install ledger rather than hard-coded
into the source tree that determines that commit.
The guarded exact-device install and retained hardware acceptance remain; no
0.4.88 image has been flashed to Pink or Green.

`make game-sdk-host`, the focused realm/security suites, the locked build, and
the Waveshare verifier pass. The broader historical `make check` still stops at
the unchanged Doom E5 test that expects `flash_app_authorized=true` while its
sealed metadata intentionally records `false`; prior hardware evidence records
the same baseline. This candidate does not weaken or reopen that unrelated
one-shot flash authorization.

For a hostile public service, the next upgrade is schema-6 semantic receipts,
server-issued day permits, deterministic reducer replay, authenticated device
provisioning, secure boot, and flash encryption. Do not burn eFuses or change
the boot security policy on Pink or Green without a separate exact-device
recovery plan and explicit authorization. See `docs/LORD_OFFLINE_SECURITY.md`.

## 2026-08-27 persistent offline save and reconciliation candidate

Console OS 0.4.85 and LORD 1.6.0 close the session-only gap on the authorized
Waveshare units without changing Game API v1. The existing `save` optional
tail was already sufficient; the required OS change is to advertise its
implemented writable service while the application exclusively owns the FAT
volume. General cartridge storage remains read-only.

The OS-owned store is restricted to
`/SAVES/<validated-game-id>/<validated-slot>.P4SAVE` and its fixed staging,
backup, and journal siblings. It copies requests before returning, bounds them
to the existing two 16 KiB slots, validates IDs and SHA-256, uses optimistic
host sequences, fsyncs and validates the staged object, preserves one backup,
atomically replaces the current object, and recovers the one-operation journal.
It advertises no save capability while USB owns or is transitioning the card.
LORD uses only one `AUTO` payload below 4 KiB.

LORD save schema 5 adds the last accepted Mac actor ID, server revision, and
committed game generation. P4RM v3 carries that base at reconnect:

- if the local character changed offline and its base still equals the server
  head, the server accepts a normal compare-and-swap upload;
- if the local copy is clean but stale, the server downloads its head; and
- if both copies advanced or actor binding differs, synchronization stops at
  `SYNC CONFLICT` and overwrites neither side.

This deliberately does not field-merge ChompCoin, mail, PvP, team, or daily
state. Normal offline-first play assumes one active console copy per stable
profile between successful syncs. The host sanitizer tests and Mac backend
tests cover save migration, relaunch state, matching-base upload, stale-dirty
conflict, and clean-stale download. Exact-unit flash, retained-UART save logs,
power-cycle relaunch, H1/BLE reconnect, and interruption recovery still must be
recorded before hardware qualification.

Platform work still remaining for a public service is account authentication,
TLS, moderation, quotas, hostile-client validation, federation, and an
administrative conflict resolver. None is required for the trusted local Mac
BBS deployment.

## Historical: 2026-08-26 backend-hosted BBS realm status

LORD 1.5.0 now treats the Mac as the P4MP host. Every P4 console selects
**Join**; no console hosts the realm and no operator presses Start. The backend
accepts the console into slot 1, automatically initiates the existing
synchronized-start barrier, and keeps the point-to-point session alive as a
bounded `P4RM` v2 tunnel.

Each console has its own two-slot P4MP session, but all sessions share one
thread-safe SQLite realm. This separates transport capacity from realm
capacity: the backend admits exactly 100 stable player profiles, while the
cartridge pages the other 99 profiles eight at a time. Page records include
the authoritative opaque actor ID, profile/combat stats, ChompCoin, presence,
directional trust, and team state.

The Mac server works over repeated H1 USB workers and one encrypted BLE
peripheral. Console OS 0.4.84 adds the only required platform change: a narrow
UUID-only BLE room fallback for macOS CoreBluetooth, whose peripheral API
cannot advertise P4 service data. The fallback uses reserved session
`0x4c4f5244`; after connection, the ordinary P4MP Offer/Join exchange still
requires the exact installed LORD content and compatibility hashes. H1 uses
the existing discovery path and works with 0.4.83.

No new Game API ABI, capability bit, raw USB/BLE handle, socket, or P4MP packet
type is needed. The game still sees only `multiplayer-session`. The local Mac
hub owns SQLite action idempotency, durable event outboxes, full snapshots,
private carried/vaulted ChompCoin balances, friendship/team consent, PvP
leases, tavern/news feeds, and the hourly realm clock.

Remaining release work:

- install and hardware-accept the exact Console OS 0.4.84 application on each
  backed-up Waveshare unit before claiming backend-hosted BLE qualification;
- install the exact LORD 1.5.0 P4G on test units and record retained H1 and
  encrypted-BLE Join, automatic start, `MAC REALM`, reconnect, paging,
  cross-player action, rollover, and conflict evidence;
- retain the eight-record on-screen page even though the SQLite realm holds
  100 accounts; do not increase the saved C array to 100;
- finish durable Console OS save policy on board profiles that still withhold
  `save`; and
- add an authenticated OS-owned `realm` service only if the deployment becomes
  public Internet service requiring accounts, TLS, moderation, quotas,
  hostile-client validation, or federation.

The older sections below are retained as design history. Wherever they say a
console hosts the Mac peer or the P4RM action layer is missing, this section,
`docs/LORD_REALM_HUB.md`, and `games/lord/BACKEND_SYNC.md` supersede them.

## Historical: 2026-08-25 flash-candidate readiness

Console OS 0.4.73 plus LORD 1.3.0 is prepared from exact main commit
`fb5814215cacb38f7b608e3b9c8ba01d0d3b2e0a` for the two backed-up Waveshare
4.3 units. The isolated controller-first build, Waveshare release verifier,
LORD sanitizer/interactive host pass, realm-hub tests, shared Game API tests,
gamepad tests, and Console shell tests passed. Exact firmware and cartridge
artifacts are sealed mode 0400 under ignored `hardware/local-state`; no device
access or write was performed during preparation.

The static, firmware, content-readback, receive-only retained-UART, and manual
realm test routes are documented in `docs/LORD_REALM_FLASH_TEST.md`. The exact
unit authorization is
`hardware/evidence/waveshare-two-unit-console-os-0.4.73-lord-realm-20260825-exact-unit-authorization.json`.
This is a hardware-test candidate, not a hardware-qualified result.

## Historical: 2026-08-25 implementation status

LORD 1.3.0 now has a backward-compatible local Mac realm path. It declares
the existing optional `multiplayer-session` capability and exchanges bounded
`P4RM` v1 Game Messages with `scripts/p4-realm-hub.py` through the already
implemented P4MP H1 or BLE route. The Mac persists CRC-checked full character
heads in SQLite with compare-and-swap revisions and nonce idempotency, shares
bounded directory/presence/profile data including ChompCoin, and supplies the
trusted hourly realm day. This needed no new Game API ABI, capability bit,
P4MP packet type, USB ownership, or cartridge network access.

This document now describes the upgrades that remain after that compatibility
slice. In particular, full cross-player mail, atomic ChompCoin transfer,
leased PvP outcome, team consent, shared feeds, authenticated Internet service,
and external IGM handoff still need typed OS/server operations. The exact
implemented scope and operator commands are authoritative in
`docs/LORD_REALM_HUB.md` and `games/lord/BACKEND_SYNC.md`.

## Purpose

Upgrade Console OS and the native P4 Game API so the `games/lord` cartridge
can support durable characters, mail, shared player records, asynchronous PvP,
friendship/team state, real daily rollover, external IGMs, and optional RIP-style
scenes. The same upgrade also finishes the controller-first wired USB platform
for the Waveshare 4.3-inch console without giving a cartridge raw filesystem,
network, USB, clock, or display ownership.

This is an implementation handoff. LORD 1.3.0 now has the complete standalone
game, a persistent local realm, typed controller mail/text, seven built-in
source-pinned IGMs, expanded kid-friendly ANSI art, one ChompCoin economy, a
wired version-3 save client, and a tested version-1 `LRSY` sync record. OS
services are still required for remote/shared state and for durable
saves on board profiles that deliberately withhold writable storage. Do not
work around an unavailable service by opening files or sockets from
`games/lord`.

## Current state and blockers

### Already implemented

- Native games are validated `p4-native-elf-v1` cartridges.
- The cartridge host table has `struct_bytes` and safely supports optional
  tail fields.
- `.P4R` provides a validated, immutable resource payload.
- `components/p4_multiplayer` provides bounded P4MP v1 packet, byte-stream,
  peer, replay, CRC32, timeout, disconnect, and neutral-input handling.
- `components/p4_desktop` has Save Manager metadata, but not a save backend.
- The Game API v1 optional tail, host runner, and `components/p4_game_save`
  now implement copied save requests, status polling, deterministic containers,
  and simulated interrupted-write recovery without changing old cartridges.
- Console OS has a non-blocking per-cartridge save worker boundary in source;
  integrated firmware qualification is still pending.
- `components/p4_cp437` owns the pinned font shared by the bounded
  `components/p4_ansi`, `components/p4_bbs`, and Game API drawing paths.
- Console OS owns storage, input, display, audio, networking, and lifecycle.
- Waveshare H2 has mutually exclusive controller-first USB Host/HID and
  explicit **USB Drive** device-MSC modes; games see only normalized input.
- Exact-unit Console OS 0.4.22 proved stable powered-hub discovery, serialized
  bounded downstream retries, boot-loop containment, and normal OS startup.
  The low-speed child did not enumerate, so no HID controller was accepted.
- Console OS 0.4.23 is built and release-verified but intentionally unflashed.
  It adds a hash-gated ESP32-P4 HS-root FS/LS clock correction behind the
  existing one-boot recovery guard. See `docs/CONSOLE_OS.md` and
  `hardware/evidence/waveshare-console-os-0.4.23-guarded-hs-fsls-20260818-exact-unit-authorization.json`.
- Console OS 0.4.24 adds the BBS touch-arbitration repair: an absent USB mouse
  no longer invalidates GT911 input, doors are visibly tappable, and bounded
  Previous/Next touch controls expose every directory page. It requires a new
  exact-artifact authorization before any install; the 0.4.23 authorization
  must not be reused.
- Console OS 0.4.25 is installed on the bound Waveshare 4.3. It adds the exact
  `0079:0011` SNES D-pad axis profile and makes transient Waveshare display
  refresh-ack timeouts nonfatal during interactive redraws, preventing D-pad
  navigation from turning off the backlight and halting the launcher. Retained
  UART proved boot, READY, microSD, and controller enumeration; repeated manual
  directional acceptance is still pending.
- Console OS 0.4.26 is installed on the bound Waveshare 4.3: the exact SNES profile maps
  physical A to accept and B to back, Waveshare Doom consumes the retained
  OS-owned canonical gamepad snapshot, and a one-time settings migration
  restores boot volume 10/10 plus game volume 9/10 without disabling later
  Control Panel persistence. Build, full app readback, retained-UART READY,
  microSD catalog, and controller enumeration passed; manual A/B and Doom
  gameplay acceptance remain pending.
- LORD 1.3.0 owns an explicit little-endian schema-3 save codec capped at 4
  KiB, consumes launch snapshots, queues/polls copied `AUTO` commits, and has
  eight local warriors, twelve typed mail slots, daily news, full tavern/PvP/
  friendship/team/youth-mentoring state, seven built-in IGMs, twelve clipped
  ANSI/RIP-style scenes, and new CP437 inn/friend/dice/recovery/victory art.
- LORD 1.3.0 also owns a bounded `LRSY` envelope with opaque actor ID,
  one-use nonce, realm/save revisions, CRC, and the complete `LDSV` payload.
  Its codec remains transport-free; the P4RM state machine now carries it over
  the existing `multiplayer-session` service to the local Mac hub.

### Missing

- `P4_GAME_CAP_STORAGE` is read-only `.P4R`; it cannot store saves.
- LORD consumes the save snapshot and queues its codec; the SDL host proves
  commit polling and relaunch data independently of board storage policy.
- Save Manager does not yet scan durable slots at boot or perform management
  operations. The current source updates only the session catalog after a
  successful commit.
- The Waveshare profile does not authorize firmware-side FAT writes, so it
  correctly withholds `save` and remains session-only until storage ownership
  and board policy are deliberately expanded.
- `p4_multiplayer` is exposed to validated cartridges through the bounded
  `multiplayer-session` capability and has live H1/BLE firmware lobbies.
- P4RM v1 now freezes the local LORD snapshot/profile/directory/clock protocol
  inside P4MP Game Message. The typed cross-player action protocol is not yet
  frozen.
- Games receive one normalized input snapshot, not stable player slots.
- Games have no OS-owned text-entry request, but LORD has a complete
  controller/touch character picker for names, mail, announcements, sayings,
  and conversation. An OS modal is an optional keyboard acceleration path.
- Games have no generic trusted date/day API. LORD receives a hub-owned hourly
  day through P4RM when the Mac realm is connected.
- There is no typed IGM handoff or sandboxed module runtime.
- RIP graphics are not a Game API format. Raw RIPscrip must not be fed into
  the ANSI parser or allowed to execute terminal/file commands.
- No wired HID controller, keyboard, or mouse has completed retained-UART
  hardware acceptance on the Waveshare 4.3-inch board through the powered
  H2 fixture/hub.
- H1 serial framing, live P4MP lobby routing, the two-console relay, and the
  one-console Mac realm peer exist. The new realm path still needs retained
  on-device acceptance.

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
| 8 | `realm` | Directory, mailbox, friendship/team, and asynchronous PvP service |
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

LORD schema 3 is implemented as explicit little endian under game magic
`LDSV`, with total length, mutation generation, and payload CRC32. Its bounded
4 KiB staging area persists the full player and three skill trees, eight local
warriors, twelve typed mail records, twelve news records, daily counters,
friendship/team/youth-mentoring state, conversation, announcement, and realm
revision. It does not persist C pointers, raw enum storage, padding, or the raw
state structure.

The decoder validates magic, schema, length, CRC, strings, counts, stat ranges,
team invariants, and every bounded record before applying any data. A failed
snapshot keeps the new-character path; backup selection remains OS owned.

## 2. OS-owned text entry

LORD already provides a controller/touch ANSI picker for every text flow. Add
an asynchronous OS modal as an optional physical/touch keyboard acceleration
path rather than passing raw keyboard events or terminal buffers to games.

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
from real-time controller multiplayer. The local Mac compatibility path now
handles full-head snapshots through existing Game Messages. Add
`components/p4_realm` for authenticated/public deployments and typed
cross-player actions rather than exposing files, sockets, or P4MP packets.

### LORD sync-record boundary

Append an optional non-blocking `realm` tail after the frozen Game API v1
fields. The minimum snapshot operations are:

```text
read_head() -> status, actor_id[16], realm_revision, copied snapshot
queue_commit(expected_revision, nonce, copied LRSY record) -> ticket
read_commit(ticket) -> queued | committed(new_revision) | conflict | error
```

The host must copy cartridge buffers before returning, cap LORD records at
4,148 bytes, and expose no token, URL, file, or socket. Validate the `LRSY`
version-1 outer CRC and its schema-3 `LDSV` payload, bind the opaque actor ID
to the signed-in OS account, require a nonzero one-use nonce, and reject stale
or skipped realm revisions. A conflict must never field-merge snapshots or
duplicate ChompCoin. Journal uploads in Console OS and keep normal local saves
working while disconnected. The full game/server flow and acceptance matrix
are in `games/lord/BACKEND_SYNC.md`.

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
display name, level, class, alive/busy status, team availability, and
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

### Adventure teams and consent

Store best-friend/adventure-team state as a realm-owned transaction. Required
operations are invite, accept, decline, withdraw, and status. Both players
must opt in; a cartridge must not silently team two remote records. Preserve
block/privacy settings and do not reveal a declined player beyond the normal
result. Teamwork bonuses are calculated by LORD from a confirmed bounded
status, never by editing the other player's save.

### Daily rollover

Expose a trusted realm day ID and next-rollover status. The OS/realm applies
daily limits exactly once per character/revision. Do not use a cartridge’s
elapsed time, manual inn visits, or an untrusted RTC as the authority for
remote PvP, friendship actions, mail, or IGM limits.

When offline, LORD may use its existing local day loop but must label it
`LOCAL REALM`. Synchronize through revisioned actions when the service returns;
never overwrite a newer server record with an offline snapshot.

## 4. Live multiplayer-session service

The existing `components/p4_multiplayer` core is now exposed through the
bounded OS adapter and declarative native-game profile. Games still do not
receive P4MP datagrams, routes, serial ports, sockets, or transport identities.

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

LORD 1.3.0 temporarily uses Game Message as the transport-neutral carrier for
its local Mac peer. Server-authoritative asynchronous PvP should move to typed
`realm` actions. A future live duel/tournament may use a distinct
`multiplayer-session` mode.

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
the typed handoff. Result IDs are single-use and expire. Cap ChompCoin/stat/item
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

## 8. Controller-first USB and wired transport

USB remains an OS-owned platform service, not a new cartridge capability.
Games consume normalized `p4_game_input_t`, future stable player slots, and
OS-owned text input. They never receive USB devices, descriptors, endpoints,
transfers, host handles, UART handles, or role-switch controls.

### Waveshare 4.3-inch role policy

- H2 boots in controller-first USB host mode.
- The board must not source H2 VBUS. Host-mode testing requires the qualified
  externally powered, current-limited, backfeed-safe fixture/hub; a passive
  OTG adapter is not an acceptable substitute.
- One `platform_usb_host` instance owns the P4 HS controller and all class
  drivers. Games and apps request bounded services through OS adapters.
- Support one generic HID/DirectInput gamepad first, then a boot keyboard and
  boot mouse through the same hub. Profiled pads may follow only after the
  generic descriptor path is stable.
- Treat every descriptor and report as hostile input: bound all lengths,
  counts, collections, usages, and report IDs. A disconnect or malformed
  report neutralizes the affected input generation immediately.
- Hub and child recovery is bounded. Never reset or free the parent hub while
  a control/status transfer is live. Exhaustion leaves hot-plug available and
  must never reboot-loop the console.
- Keep the locked ESP-IDF/component versions authoritative. Any local USB
  source overlay must be generated from an exact hash-bound input, tested,
  and proven to be the translation unit actually compiled; never edit managed
  component sources in place.
- The durable one-boot guard must cover every experimental root-clock,
  downstream-retry, or hub-scheduler change. If a candidate fails before the
  stable-loop confirmation, the next boot suppresses the experiment and still
  reaches Console OS.

### Explicit USB Drive mode

Keep **USB Drive** as an app the user deliberately opens. It performs this
fail-closed transition:

1. neutralize controller/keyboard/mouse state and stop new host leases;
2. drain and uninstall HID, hub, and USB host ownership;
3. unmount Console OS from the microSD card;
4. lazily allocate and start TinyUSB MSC device mode;
5. require clean host eject or physical disconnect before leaving;
6. stop TinyUSB, remount microSD, rescan the dynamic game directories, and
   restart controller-first Host/HID.

Host/HID and TinyUSB MSC must never overlap. Console OS and a laptop must
never mount the same FAT volume concurrently. Any failed transition leaves H2
quiesced and reports the reason; it must not format or repair the card
implicitly.

### Wired BBS and multiplayer transports

Keep H1 CH343 USB-UART available for logs and the first framed BBS/P4MP relay
while H2 remains controller-first. A Mac, PC, or small Linux host can relay two
H1 links. A powered USB hub alone cannot route traffic between two USB-device
ports. A future H2 CDC/vendor multiplayer mode is an explicit, mutually
exclusive device-role app/session and cannot run while H2 owns controllers.

Transport adapters expose only bounded lobby, player-slot, text, realm, or
file-transfer services. Boot logs and framed application traffic need an
unambiguous mode boundary. Physical UART between consoles is allowed only
through a documented voltage-safe connector/pin profile and the same bounded
framing; do not infer one from a USB-C receptacle.

### Current USB checkpoint

Do not flash as part of an ordinary build/test pass. Console OS 0.4.26 is the
installed exact-unit candidate. It preserves the 768x480 shell, boot/audio
settings, touch, dynamic SD catalog, and USB Drive role while accepting the
named low-speed controller through the powered hub. Finish labeled A/B, Doom,
repeated D-pad, and hot-plug acceptance before promoting it; do not weaken the
identity, descriptor, report-bound, disconnect-neutralization, or VBUS guards.

## 9. Console UI changes

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

Add visible USB state to the existing built-in pages: current H2 role,
controller/keyboard/mouse presence, safe-mode state, last bounded host error,
and whether USB Drive is waiting for eject. Do not claim a controller is ready
merely because the hub enumerated.

## 10. LORD integration after the OS services land

LORD 1.3.0 has the complete standalone implementation. Completed game-side
work includes:

- character creation and a controller/touch ANSI text editor;
- all 131 monsters, the fifteen forest-event families, all three skill trees,
  town progression, the full tavern, Dragon Dice, Red Dragon, and rebirth;
- eight persistent local warrior records, typed inbox/sent mail, ChompCoin
  transfers, PvP, and friendly inn sparring;
- Seth/Violet best-friend paths, player trust, adventure teams, youth
  mentoring, and daily teamwork bonuses;
- seven bounded adaptations of the pinned Synchronet add-ons;
- twelve ANSI/RIP-style scenes, a generated/dithered title, and added CP437
  inn, friendship, Dragon Dice, recovery, and victory compositions;
- deterministic schema-3 save encoding, CRC/range/corruption/round-trip tests,
  launch decode, copied queue, ticket polling, optimistic host sequence, and
  commit-aware dirty-state clearing; and
- a tested 4,148-byte maximum `LRSY` sync record with opaque actor ID,
  one-use nonce, realm/save revisions, nested save CRC, and record CRC.
- a host-tested P4RM client for full-head sync, hub directory/presence,
  ChompCoin profiles, conflict state, retry, and trusted hourly rollover.

The OS integration pass should replace optional backends, not rebuild these
screens or rules. Save wiring is already complete in the cartridge. Map local
directory/mail/PvP/friendship/team operations onto opaque realm tickets while
retaining the arrays as the offline snapshot. The OS text modal may accelerate
physical/touch keyboard entry, but the in-game picker remains the fallback.
Replace inn-authoritative remote social resets with the trusted realm day and
add `OFFLINE`, `SYNCING`, and `CONFLICT` state when a realm adapter exists.

External IGM handoff remains an OS adapter task; all seven built-in modules
must continue to work without it. LORD must remain playable when every
optional service is absent and must never receive filesystem, socket,
transport, clock, USB, or display-driver ownership.

## 11. Required tests

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
- adventure-team consent, decline, block, and conflict paths;
- offline outbox replay after reconnect;
- daily rollover exactly once.

### Multiplayer session

- compatible-content matching;
- four-player capacity;
- stable slots and neutral disconnect/reconnect barrier;
- replay, bad CRC, bad route, timeout, malformed frames, and desync;
- two-process host relay with loss, duplication, delay, and disconnect.

### USB host, HID, and role switching

- exact old/new `struct_bytes` Game API tests remain independent of USB;
- fuzzed/truncated HID descriptors and reports, oversized input, and unknown
  report IDs;
- gamepad, boot keyboard, and boot mouse through the named powered fixture;
- per-device disconnect neutralization, reconnect barrier, repeated hot-plug,
  and hub-child removal in every order;
- bounded enumeration exhaustion with the launcher, display, touch, audio,
  microSD, and dynamic game catalog still alive;
- interrupted-boot safe-mode recovery with no panic, watchdog, brownout, or
  reboot loop;
- Host/HID to USB Drive to Host/HID round trip, clean eject enforcement, no
  concurrent FAT ownership, and no implicit formatting;
- H1 framed relay while H2 retains a controller, including malformed frame,
  partial read, replay, disconnect, and neutral-input behavior;
- retained UART and exact artifact/device identity for every hardware claim.

### LORD

- save round-trip and corrupt-save fallback;
- local/offline and realm-backed mail flows;
- PvP win, loss, conflict, and daily limit;
- friendship and adventure-team consent and persistence;
- ChompCoin non-duplication across retry, conflict, and simultaneous devices;
- each built-in IGM and invalid external result;
- vector command/scene bounds;
- renderer guards, Start/B/Back, touch mapping, and tone fallback;
- fresh `.P4G` build with no unsupported ELF imports.

## 12. Implementation order

1. Keep the verified, unflashed Waveshare 0.4.23 USB checkpoint frozen while
   OS APIs are upgraded. Use 0.4.24 or later for integrated UI/service work;
   never reuse the 0.4.23 authorization for a changed artifact, and do not
   flash without an explicit request.
2. Add durable save component, host-table tail, capability bit, package
   validation, SDL in-memory backend, and Save Manager integration.
3. Add OS text-entry modal and host-runner implementation.
4. Hardware-qualify the implemented LORD P4RM Mac hub over H1 and BLE,
   including relaunch, retry, conflict, and hourly rollover evidence.
5. Add typed local realm actions for mail, atomic ChompCoin transfers, PvP
   leases/outcomes, friendship/team consent, and shared feeds with tests.
6. Expose those typed actions through an optional `realm` Game API tail for
   authenticated/public backends while retaining the Mac compatibility path.
7. Add same-console stable player slots and any later live LORD duel mode
   without changing the P4RM snapshot contract.
8. Finish H1 framed relay integration and qualify controller-first H2 host,
   HID gamepad/keyboard/mouse, and explicit USB Drive role switching without
   exposing USB ownership to games.
9. Add typed external IGM handoff and a bounded shared vector-scene service
   only if other cartridges need them; LORD's built-in IGMs and scenes already
   work without these optional services.
10. Run focused host/sanitizer tests, `make game-sdk-host`, BBS/console-shell
   tests, one matching Console OS build, and only then the guarded hardware
   workflow if explicitly requested.

## Definition of done

- Existing v1 cartridges still launch unchanged.
- LORD can load, queue, exit, relaunch, and recover a durable character.
- Save writes survive simulated interruption without losing both current and
  prior valid objects.
- Mail, PvP, friendship, adventure teams, and ChompCoin transfers use realm
  revisions and idempotent actions.
- Live multiplayer exposes only lobbies, slots, sanitized input, seed, and
  status—never a transport handle.
- External IGMs exchange one typed, bounded, single-use result.
- RIP-style data cannot execute terminal, file, or network operations.
- UI reports offline/read-only/conflict states honestly.
- Waveshare H2 boots controller-first, safely handles the named powered hub
  and accepted HID devices, neutralizes input on disconnect, and never claims
  that hub discovery alone proves a working controller.
- USB Drive is an explicit mutually exclusive role switch; clean eject,
  remount, and dynamic game rescan work without concurrent FAT ownership or
  implicit formatting.
- H1 can carry a bounded wired BBS/P4MP relay while H2 retains controller
  ownership, and games receive no USB/UART handles.
- Host and package tests pass under sanitizers.
- Any physical-device claim names the exact board, artifact hash, serial
  evidence, and observed behavior. A build alone is not hardware acceptance.
