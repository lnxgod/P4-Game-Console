# Console OS content library

Console OS mounts microSD without formatting and keeps its catalog scanner
read-only. The firmware accepts at most 128 `.p4cart` candidates, lists at most
16 valid carts, and validates every container and payload hash. It does not
scan unrelated large game-data files at boot. A bad cart is counted and
ignored; it is never executed.

The background scanner adds valid carts to the launcher as non-removable
`CART` entries and reports rejected containers without treating them as native
games. Launch reopens the selected file, revalidates its complete container and
source hash, and runs that source through the bounded `p4-lua-5.4-v1` sandbox.
Native `.P4G` games continue to use the separate reviewed ELF loader.

Script save values currently survive relaunches during the same Console OS
boot. Durable SD-backed saves across reboot remain pending; the Lua game never
receives a filesystem path in either case.

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

## H1 verified content upload

Waveshare Console OS shares H1 between diagnostics, multiplayer, and an
OS-owned content receiver. Stop the multiplayer relay, leave the console at the
launcher, and select the exact H1 port when two consoles are attached:

```sh
python3 scripts/p4-usb-content.py doom \
  --port /dev/cu.wchusbserial...
python3 scripts/p4-usb-content.py quake
```

The manifest starts at the normal 115200 baud. After exact size and SHA-256
validation, both sides negotiate 921600 baud for 4096-byte CRC32-protected
chunks. Firmware writes only the fixed target's reserved temporary name,
syncs it, verifies the complete received hash, atomically renames it, performs
a full SD readback hash, and then reboots to restore ordinary H1 multiplayer.
Interrupted and invalid transfers never create a runnable target. WADs and
other game data remain ignored local inputs and are never committed.

The retained Quake command supports its pinned historical shareware PAK, but
Quake remains hidden from the current launcher.

## Device behavior

- A missing or unmountable card leaves Console OS and its built-in pages usable
  but exposes no executable P4G apps and marks removable storage unavailable;
  restoring the card rebuilds the catalog.
- Game Manager refresh performs a new bounded background P4 Cart scan without
  blocking the launcher or writing the card.
- H1 import never formats the card or accepts a host-supplied destination
  path; only exact OS-known content identities can select fixed targets.
- Mounted-card imports use the host tool's validate, stage, sync, read-back,
  and atomic-rename transaction.
