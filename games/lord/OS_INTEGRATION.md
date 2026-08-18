# LORD 0.2.0 OS integration contract

LORD is feature-complete as a playable local-realm cartridge. This file is
the short game-side wiring guide for the Console OS work specified in
[`../../osupgrade.md`](../../osupgrade.md). The shared OS/API must remain the
owner of storage, text entry, realm transport, time, module launch, display,
and networking.

## Current fallback behavior

- `video` and `controls` are the only required capabilities.
- `audio-tone` is optional.
- Four bounded local warrior records back the directory, PvP, romance, and
  preset mail flows.
- Inn rest advances the local day and resets forest, skill, PvP, romance, and
  IGM limits.
- Five RIP-style scenes use existing clipped Game API drawing primitives and
  require no asset or vector-scene capability.
- UI labels this mode `LOCAL REALM` and `SESSION ONLY`.

Do not remove this fallback when optional OS services arrive.

## Save adapter

The game exports these functions from its canonical translation unit:

```c
size_t lord_save_encode(const lord_state_t *state, uint8_t *bytes,
                        size_t capacity);
bool lord_save_decode(lord_state_t *state, const uint8_t *bytes,
                      size_t length);
```

Schema 1 is a fixed 302-byte, explicit little-endian payload inside game magic
`LDSV`. Its 16-byte game header carries schema, total length, payload CRC32,
and the local mutation sequence. The maximum accepted buffer is 512 bytes.
It serializes player stats, daily counters, realm revision, all four local
records, spouse state, and all six compact mailbox slots. It never serializes
raw `lord_state_t`, pointers, enum storage, or padding.

At game start:

1. Call normal `lord_initialize()`.
2. If `save` is present and the launch snapshot is nonempty, pass the copied
   bytes to `lord_save_decode()`.
3. A valid decode lands on the town screen with `save_dirty == false`.
4. A rejected snapshot must leave the new-character path intact; let the OS
   offer its prior valid backup outside the cartridge.

After a successful mutation, `save_dirty` is true and `save_sequence`
advances. At safe UI boundaries, encode into a 512-byte stack/host staging
buffer and pass a copied payload to the non-blocking OS `queue_save()` call.
Clear `save_dirty` only after a committed status. Keep the OS container
sequence separate from the game header and use the OS sequence for conflict
checks.

## Realm adapter

The local UI model is `lord_realm_player_t realm[4]`, compact
`lord_mail_t mail[6]`, `selected_player`, `selected_mail`,
`realm_revision`, and `spouse_index`. Replace local actions with asynchronous
realm tickets as follows:

| Game flow | Realm operation |
|---|---|
| Other Warriors list | paged directory snapshot |
| Player challenge | PvP lease/snapshot request |
| PvP finish | single idempotent outcome commit |
| Mailbox | headers after cursor, then read/mark-read |
| Preset letter | send mail with idempotency token |
| Compliment/gift/proposal | revisioned relationship request |
| Inn social reset | trusted realm day transition |

Copy and sanitize every bounded result into the game model. Opaque remote IDs,
match IDs, revisions, and tickets belong in a future adapter substructure;
never reinterpret array indexes as remote identity. A conflict must not award
gold or apply marriage locally. Keep asynchronous PvP on `realm`; the
`multiplayer-session` service is only for a later live-duel mode.

## Text, daily rollover, and IGMs

When `text-input` is present, the Compose action should request at most 512
UTF-8 bytes and render only sanitized returned text. Preset mail remains the
controller-only fallback.

When `realm` provides a trusted day ID, apply social/IGM reset exactly once per
day/revision. Forest rest may remain local, but it must not manufacture remote
PvP or romance actions.

The Goblin Dice, Old Wizard, and Herbalist are built in and need no module
capability. When `module-handoff` is present, add a separate menu section for
compatible external modules and use only the typed, single-use context/result
flow in `osupgrade.md`.

## Manifest transition

Do not add unknown capability names to `game.json` before the package parser,
registry, loader, host table, and host runner all support them. After that OS
work lands, add `save`, `text-input`, `realm`, and `module-handoff` as optional
capabilities. Built-in scenes do not require `vector-scenes`.

The game is ready for OS-side integration when a host test can load a schema-1
snapshot, mutate one feature, observe a copied queued 302-byte save, relaunch,
and recover the same character. Realm acceptance additionally requires stale
revision, duplicate outcome, decline, conflict, offline, and reconnect tests.
