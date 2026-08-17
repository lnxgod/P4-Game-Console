# Console OS content library

Console OS mounts microSD without formatting and keeps its catalog scanner
read-only. The firmware accepts at most 128 `.p4cart` candidates, lists at most
16 valid carts, and validates every container and payload hash. It does not
scan unrelated large game-data files at boot. A bad cart is counted and
ignored; it is never executed.

The current P4 Cart milestone is a safe catalog, not an executable cartridge
runtime. The restored background scanner adds valid carts to Game Manager as
non-removable `CART` entries and reports rejected containers without treating
them as native games. They remain non-runnable until the pinned Lua sandbox is
integrated. Native `.P4G` games continue to use the separate reviewed ELF
loader.

## Copy an open P4 Cart

Pack a source project, then let the repository tool validate, stage, sync, read
back, and atomically activate it on the mounted card:

```sh
python3 game-platform/scripts/p4cart.py pack \
  game-platform/templates/bounce-lab /tmp/bounce-lab.p4cart
python3 scripts/p4-content.py cart /tmp/bounce-lab.p4cart \
  --sd-root /Volumes/P4SD
```

The destination is `/P4/GAMES/bounce-lab.p4cart`. The tool refuses links,
invalid names, invalid containers, occupied staging files, and an existing
destination. Use `--replace` only when replacement is intentional. It never
formats the card or deletes unrelated content.

## Retired Quake compatibility tooling

The source tree retains the pinned Quake port and its exact-data validators for
historical development. They are not linked into current Console OS builds,
Quake is not shown in the launcher, and the device-side serial receiver is not
started. The old host command remains for isolated port work only:

```sh
python3 scripts/p4-usb-content.py quake
```

Do not use that command against current Console OS; no compatible receiver is
linked. The port's separate game data remains ignored and unredistributed.

## Device behavior

- A missing or unmountable card leaves Console OS and its built-in pages usable
  but exposes no executable P4G apps and marks removable storage unavailable;
  restoring the card rebuilds the catalog.
- Game Manager refresh performs a new bounded background P4 Cart scan without
  blocking the launcher or writing the card.
- Device-side USB import reports unavailable; it never formats the card or
  accepts a host-supplied destination path.
- Mounted-card imports use the host tool's validate, stage, sync, read-back,
  and atomic-rename transaction.
