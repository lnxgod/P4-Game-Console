# LORD 1.3.0 realm flash and hardware test

## Candidate

The prepared candidate is Console OS 0.4.73 plus LORD 1.3.0 from source
commit `fb5814215cacb38f7b608e3b9c8ba01d0d3b2e0a`.

- application: 1,791,024 bytes, SHA-256
  `c30ed4d987bda6021712dd132d441c0848ef2b0a24799ef3a6921e2814628206`
- application mutation span: 1,794,048 bytes at `0x20000`
- LORD.P4G: 168,260 bytes, SHA-256
  `f8bd0f55c2dc29a3364eeb223ed6043d277d53c28683dc28d88d2450c8c278e5`
- board profile:
  `waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host`
- immutable local artifacts:
  `hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142/`

The immutable directory is intentionally Git-ignored. The tracked exact-unit
authorization is
`hardware/evidence/waveshare-two-unit-console-os-0.4.73-lord-realm-20260825-exact-unit-authorization.json`.
Preparation did not open either UART or write either console.

## Safety boundary

- Flash and content transfer use H1 only. Stop the realm hub, monitor, relay,
  and content client before another process opens the same H1 port.
- Install one console at a time. The route has a shared local lock and refuses
  any device whose live identity, flash geometry, security state, bootloader,
  partition table, or 0.4.70 predecessor differs.
- The firmware route writes only the application at `0x20000`. It does not
  write the bootloader, partition table, OTA data, NVS, or microSD.
- H2 remains controller-first. Connect a controller only through the recorded
  externally powered, current-limited, backfeed-safe fixture. Never use a
  passive OTG adapter on this panel.
- The complete 1,794,048-byte preimage is saved before each application write.
  Automatic rollback is not authorized.

## Static preflight

Run from the repository root:

```sh
./scripts/install-waveshare-console-os-0.4.73-lord-realm.sh --check-only
./scripts/deliver-lord-1.3.0-realm.sh --check-only
```

Both commands must report `PASS` and explicitly say that no device access or
write occurred.

## Unit 1

With only the intended H1 owner active:

```sh
./scripts/install-waveshare-console-os-0.4.73-lord-realm.sh \
  --unit unit1 \
  --port /dev/cu.wchusbserial5C371865781
```

The route preserves the exact preimage, performs one application-only write,
verifies the application, reads back the complete padded span, and leaves the
console in the loader. Arm the receive-only capture, then physically tap RESET
once when prompted:

```sh
python3 scripts/capture-waveshare-console-os-0.4.73.py \
  --port /dev/cu.wchusbserial5C371865781 \
  --raw hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-unit1/startup.raw \
  --summary hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-unit1/startup.json
```

Only after the capture reports `result=pass`, deliver and pull back the exact
cartridge:

```sh
./scripts/deliver-lord-1.3.0-realm.sh \
  --unit unit1 \
  --port /dev/cu.wchusbserial5C371865781
```

## Unit 2

Repeat the same sequence after unit 1 is complete:

```sh
./scripts/install-waveshare-console-os-0.4.73-lord-realm.sh \
  --unit unit2 \
  --port /dev/cu.wchusbserial5B901593451

python3 scripts/capture-waveshare-console-os-0.4.73.py \
  --port /dev/cu.wchusbserial5B901593451 \
  --raw hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-unit2/startup.raw \
  --summary hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-unit2/startup.json

./scripts/deliver-lord-1.3.0-realm.sh \
  --unit unit2 \
  --port /dev/cu.wchusbserial5B901593451
```

Do not continue to unit 2 if unit 1's write, padded readback, retained UART, or
LORD pullback fails.

## Manual console acceptance

On each unit:

1. Confirm the launcher displays normally with working touch, speaker, and no
   panic or reboot loop.
2. Confirm the exact `LORD.P4G` appears under Games/Adventure and launches.
3. Exercise title art, controller name entry, hero/class setup, town, forest,
   inn, tavern, Seth, Violet, Dragon Dice, friendship/team, mail, news, PvP,
   the seven built-in IGMs, recovery, and victory scenes.
4. Confirm Dragon Dice shows its complete result sentence above the menu.
5. Confirm all currency says ChompCoin and friendship wording contains no
   kissing or sexual content.
6. Exercise D-pad, A, B, Start, touch Back, tones, and return to the launcher.
7. Attach the approved H2 controller fixture and repeat navigation, accept,
   back, disconnect neutralization, and hotplug checks.

Record observations as manual hardware results. A passing build or UART log is
not display, touch, controller, audio, or gameplay acceptance.

## Two-console Mac realm acceptance

First create a wired LORD lobby on each console under Multiplayer. Use a
permanent profile label for each character, then start the Mac hub:

```sh
python3 scripts/p4-realm-hub.py \
  --usb alice=/dev/cu.wchusbserial5C371865781 \
  --usb bob=/dev/cu.wchusbserial5B901593451
```

When each console shows the Mac guest, press Start Match. Verify:

1. both games show `MAC REALM` rather than remaining at `SYNCING`;
2. each new character uploads once and receives a server revision;
3. Alice and Bob appear in the other character's directory with online
   presence, level, PvP record, and ChompCoin;
4. a ChompCoin or progression change survives exit and relaunch through a
   validated download;
5. offline play followed by reconnect commits once without duplication;
6. a stale revision produces `SYNC CONFLICT` and cannot overwrite the newer
   head;
7. crossing one real hourly boundary grants exactly one realm-day refresh,
   and missing several hours never grants a catch-up loop;
8. disconnecting the Mac leaves the standalone game playable and reconnecting
   resumes bounded synchronization.

Do not run the content client or serial capture while the realm hub owns H1.
The database is `local-data/realm/lord.sqlite3`; stop the hub before copying it
for evidence or backup.

## Honest multiplayer boundary

This candidate synchronizes each actor's complete snapshot, ChompCoin,
directory/profile presence, compare-and-swap revisions, and trusted hourly
realm day. Mail, local PvP, friendship/team, tavern/news, and IGM fields travel
with that actor's snapshot, but version 1.3.0 does not yet perform authoritative
cross-actor mail delivery, atomic ChompCoin transfer, leased asynchronous PvP,
two-party friendship/team consent, or a shared tavern/news feed. Those require
the typed server-owned action layer in `osupgrade.md` and must not be reported
as passing multi-user behavior.
