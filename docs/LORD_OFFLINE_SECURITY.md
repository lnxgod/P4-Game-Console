# LORD offline-first synchronization and fair-play contract

## Goal

LORD must remain playable when a console cannot reach the Mac. When it later
joins the Mac realm, the server must preserve one authoritative character,
avoid duplicate ChompCoin or rewards, and reject ordinary save editing and
implausible stat jumps. A conflict must preserve both copies until a parent or
administrator chooses one.

This document separates the practical protection used for family play from
the stronger controls required for a hostile public service. An offline device
owned by the player cannot provide perfect proof of human play unless its boot
chain, storage, game package, and device key are also trusted.

## First secure tier

The LORD 1.8.0 first tier keeps P4MP v1, P4RM v3, LRSY v1, and Game API v1. It
combines independent client, server, and Console OS controls.

### 1. One authoritative revision lease

The persisted tuple

```text
actor ID + server revision + committed game-save sequence
```

is the console's single-use offline revision lease. The console may continue
the matching character offline. On reconnect, the Mac accepts that branch only
if its base is still the current server head. A copied or second device can race
for the same base, but compare-and-swap permits only the first valid commit;
the other branch becomes a conflict. Character and economy snapshots are never
field-merged.

### 2. Mac-owned realm days and shared economy

Once a character has joined a Mac realm, Inn sleep cannot manufacture another
realm day locally. The character remains playable offline for the actions left
in its current realm day. The Mac grants the next refresh when its hourly realm
clock advances. A character that has never joined a realm retains the classic
local-only sleep loop. Both the server-authoritative refresh and local sleep
use the same reset and pay no bank interest.

Mail delivery, player transfers, teams, mentoring, shared tavern/news, and PvP
settlement remain typed server operations. Client profile packets are presence
hints only. Public stats, carried ChompCoin, and vaulted ChompCoin are projected
from the last accepted LORD head. Public defense includes the implemented
friendship-badge guard: two points per five badges, capped at eight.

Adventure Clubs use a separate server-owned ledger. The cartridge can request
create, join, leave, one daily rally, one bounded cheer, or a friendly Banner
Clash, but it cannot submit a score or victory. The hub derives contribution
and clash strength from accepted character heads and recorded same-day
participation, then awards only club prestige, Banner Stars, season points, and
W/L/D. It never writes personal ChompCoin, XP, dragon deeds, PvP counters, or
combat stats. Same-club direct PvP is denied, preventing clubmates from farming
the ordinary duel settlement path.

Realm PvP pays only the hub-calculated carried-ChompCoin prize and one durable
win/loss prestige result; it pays no XP. Once an actor has an accepted realm
head, an uploaded PvP counter must equal the prior counter plus exactly the
cursor-consumed duel events. Because the stock cached-duel path bundles its
local XP, ChompCoin, and counter mutation in one snapshot, an unreceipted duel
rejects atomically. The same XP/ChompCoin change with unchanged PvP counters
remains eligible as ordinary offline solo progression.

Stock clients apply a queued economy debit only when its complete designated
balance is present. Transfer debits use the vault; shared-supplies and PvP-loss
debits use carried ChompCoin. A short balance advances neither the durable event
cursor nor its acknowledgement. Realm mode disconnects cleanly so solo play and
local banking continue, and reconnect retries the event after the balance is
restored. The accepted successor head is checked against the same pending
ledger. This prevents a normal unplug window from turning a debit into a free
credit, but it is not modified-firmware attestation.

A matching dirty offline branch always commits before the hub delivers queued
shared mutations. The cartridge freezes the exact upload, shows `SYNCING`, and
pauses gameplay or other serialized-state changes until the result arrives.
The hub then sends one durable event and waits for a successor snapshot whose
cursor proves application before sending another. Begin/body packet gaps also
pause input. Lost and duplicate acknowledgements are safe because receipt is
not application and a late ACK consults the already committed cursor.

A pending admitted PvP knockout is projected immediately even before its
target snapshot commits. It suppresses the target's alive/profile state and
new challenges across hub restarts, closing the receipt-to-save availability
window for stock clients.

### 3. Validated state transitions

The hub parses the complete LRSY and LDSV records. It checks structural and
absolute LORD limits, requires the embedded actor/revision/committed-generation
tuple to name the exact accepted base, and applies deliberately conservative
progression bounds between the known head and a proposed successor. A durable
per-actor realm-day anchor applies those bounds cumulatively for the hour, not
once per upload, so repeated commits cannot multiply an allowance. It rejects,
at minimum:

- a second local day inside the same server hour;
- changed character identity or class;
- a level gain greater than three, a gained level without the destination
  trainer's XP threshold, or an invalid dragon-victory reset;
- cumulative same-realm-day experience gain above 2,500,000;
- cumulative wealth gain above 25,000,000 plus 20% of anchored wealth;
- cumulative max-HP, strength, or defense gain above 5,000 each;
- cumulative gains above three PvP wins, 100 PvP losses, 64 forest fights, 40
  skill-mastery points, 64 skill uses, four friendship badges, or 500 each for
  charm, gems, and young heroes helped;
- a direct upload whose inner sync base is absent, stale, or forged; and
- a truncated record that only has valid outer CRCs.

These bounds are defense in depth, not proof of every fight. They must be high
enough for legitimate play and low enough to stop a save editor from changing a
small value to billions. Absolute record ceilings also reject ChompCoin or XP
above 1,000,000,000, combat stats above 100,000, forest fights above 255, and
skill uses above 255.

### 4. Device-local authenticated saves

Console OS writes new saves in an authenticated P4SAVE2 envelope. A random
256-bit key lives in an OS-owned NVS namespace and never reaches a cartridge.
The envelope authenticates the game, slot, schema, host sequence, and payload
with HMAC-SHA256. Pink and Green therefore cannot accept each other's sealed
SD save, and editing an LDSV payload plus recomputing its public CRC/SHA hashes
does not produce a valid object.

Existing P4SAVE1 objects are legacy-unsealed input. Each exact game/slot may be
migrated once while its OS-owned NVS downgrade window is open. A successful
P4SAVE2 install, an already authenticated object, or a verified empty slot
permanently closes that window in a fixed NVS registry. Later P4SAVE1 current
replacement or backup injection is rejected instead of being re-signed. The
first migrated legacy bytes are grandfathered and their earlier history is not
proven. Losing/erasing NVS also loses the local sealing identity, registry, and
freshness records; restore from the Mac head or require a new parent-approved
adoption.

Each authenticated slot is also bound to a device-local freshness record: the
highest durably installed P4SAVE2 sequence plus that exact object's SHA-256.
Recovery may reuse only that exact current/backup object or finish a valid
newer journal stage. It rejects an older authenticated backup and a different
same-sequence branch. Existing unanchored P4SAVE2 files form an explicit
one-time baseline and are anchored before gameplay receives their payload.
The registry records when an anchor must exist, so deleting only the anchor
fails closed.

If all SD save artifacts are lost while the NVS anchor remains, LORD starts
with no local snapshot but keeps the host sequence floor N. A reconstructed
offline state or a canonical Mac download can therefore be saved only as N+1;
the missing card never resets the outer sequence to one. This recovery state
does not make any older or alternate same-sequence object eligible. It is
entered only when current, backup, stage, and journal are all absent; a stale
or tampered leftover fails closed and is not loaded.

This is a plaintext-NVS software anchor. Rolling back the complete NVS
namespace with matching SD files, extracting the device key, or installing
modified firmware can still defeat it because secure boot, flash encryption,
and encrypted NVS are not enabled. The Mac revision lease and semantic bounds
remain required defense in depth.

## Parent-approved legacy adoption

A dirty legacy local copy and an existing server head must continue to show a
conflict by default. The hub may adopt the device copy only after an explicit
operator grant for that exact profile.

The adoption transaction must:

1. validate the complete local record and its absolute LORD limits;
2. archive the old server head with its revision and SHA-256;
3. consume the one-time parent grant;
4. install the selected local copy at a new monotonically increasing revision;
5. derive the authoritative profile from that selected head; and
6. leave no reusable grant that could authorize a later overwrite.

No ChompCoin, XP, mail, PvP, or team fields are merged. The normal no-grant
path never overwrites either copy. This is the required recovery path for the
current Pink local-generation-34/server-revision-17 conflict.

The grant, old-head archive, selected snapshot, derived public profile,
realm-day anchor, and consumed marker are one SQLite transaction. An exact
nonce/body retry returns the already committed revision. If the commit reply
is lost, the client retries the retained record; reconnect can also recognize
the stored save generation and safely download that exact head instead of
creating a false conflict.

## Stronger second tier

For a public or adversarial realm, add a server-issued one-use permit and a
bounded semantic action journal to LORD save schema 6. A permit binds the actor,
device branch, exact game ruleset digest, base revision and snapshot digest,
realm day, RNG base, and action/day budgets. Offline receipts describe menu
actions and committed text, not client-chosen stat deltas.

The Mac replays those receipts from the authoritative head with the same LORD C
reducer and accepts the resulting protected state only when it exactly matches
the upload. The transaction consumes the old permit, records the receipt hash,
stores the canonical successor, and issues one new permit atomically. A lost
commit response can then retry idempotently, while a changed upload under the
spent permit fails.

Cross-player operations remain server-owned even in this tier. If the journal
or offline budget fills, protected rewards pause with a friendly request to
sync; narrative/local play can continue.

## Hostile-device tier

A determined player can replace firmware, read plaintext NVS, automate valid
actions, or emulate the current client. Defending against that requires a
separate, reviewed provisioning program:

- signed Console OS and cartridge policy;
- ESP32-P4 secure boot and flash encryption;
- an eFuse-backed device key and authenticated pairing;
- encrypted/authenticated transport to any non-local service; and
- server-side receipt replay and moderation/rate limits.

Provisioning eFuses is irreversible. Do not enable or burn security keys on
Pink or Green without an exact-device inventory, recovery plan, and explicit
authorization.

## Acceptance checklist

Automated tests must prove valid offline progress, malformed and recomputed-hash
tampering rejection, post-migration P4SAVE1 reinjection rejection, huge and
cumulatively repeated stat/ChompCoin rejection, the maximum-three-level plus
trainer-threshold rule, the 2,500,000 cumulative XP ceiling, same-hour day
rejection, profile-packet laundering rejection, exact inner-base validation,
compare-and-swap races, dropped-commit retry, duplicate nonce idempotency,
sealed-save wrong-key/card rejection, and one-time adoption with archive
preservation. They must also prove exact pending transfer/supplies/PvP debits,
underfunded no-ack replay after reconnect, zero-value PvP settlement, startup
profile reconstruction, atomic rejection of bundled unreceipted PvP
XP/ChompCoin/prestige with same-counter solo progression still accepted,
committed-versus-pending legacy economy reconciliation, permanent orphan
quarantine, and fail-closed ambiguous legacy PvP.
They must also cover club-name uniqueness and reuse, membership capacity,
one-day eligibility and rejoin delays, leader succession, daily rally/cheer/
clash limits, pair cooldowns, season rollover, nonce replay, hub restart, and
the invariant that every club operation leaves all personal economy and combat
fields unchanged.
The multiplayer E2E must additionally use two independent client identities
and local states to prove dirty-offline-first ordering, one-event cursor
serialization, lost-receipt replay, mail bytes in a reopened authoritative LDSV,
and exactly-once ChompCoin debit after simulated power loss.

Hardware acceptance must record the exact Console OS and LORD.P4G hashes,
device identity, SD card, route, retained serial log, and these observations:

1. play and save offline, exit, and relaunch without losing progress;
2. realm-bound Inn sleep does not create a local day;
3. reconnect uploads a valid matching-base character;
4. a second/stale branch conflicts without overwriting the head;
5. a save copied between Pink and Green is rejected by the local seal;
6. an explicitly granted legacy adoption succeeds once and archives the old
   head; and
7. relaunch downloads the adopted canonical head and resumes ordinary sync.

A successful host build is not device or security acceptance.
