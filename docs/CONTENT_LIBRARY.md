# Console OS content library

Console OS mounts microSD without formatting and keeps its catalog scanner
read-only. A separate OS-owned USB copy service may write only reviewed fixed
staging/destination paths after a bounded transfer and exact hash check. The
firmware accepts at most 128 `.p4cart` candidates, lists at most 16
valid carts, validates every container and payload hash, and enables Quake only
when the exact separately supplied shareware PAK passes its size and SHA-256
gate. A bad file is counted and ignored; it is never executed.

The current P4 Cart milestone is a safe catalog, not an executable cartridge
runtime. Valid carts appear in Library diagnostics, but remain non-runnable
until the pinned Lua sandbox is integrated. Native C games remain compiled into
the firmware.

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

## Copy Quake shareware data over USB

Quake is an optional easter egg, not part of the open P4 Cart SDK. The project
does not commit, embed, or redistribute its game data. With the exact local
development input described in `third_party/game-data.json`:

```sh
python3 scripts/p4-usb-content.py quake
```

Keep the microSD card in the running badge and use its H1 programming USB-C
port. The host verifies the exact Quake v1.06 shareware PAK, negotiates
921600-baud transfer through the CH343 USB-UART bridge, and sends CRC-protected
4 KiB chunks. Console OS writes only the reserved `P4Q.TMP` staging file,
syncs and read-back validates it, atomically renames it to
`/GAMES/QUAKE/ID1/PAK0.PAK`, and reboots. The boot catalog hashes the full file
again before enabling Quake. The command reports live progress and waits through
slow-card validation, which can take several minutes after the payload reaches
100%. The P4 USB host pins and VBUS policy are unchanged.

The mounted-card `scripts/p4-content.py` route remains available for offline
development, but is no longer required for Quake installation.

## Device behavior

- A missing or unmountable card leaves built-in games usable and marks Library
  storage unavailable.
- Library refresh retries the no-format mount and performs a new bounded scan.
- The USB service never formats the card and never accepts a host-supplied
  destination path.
- Interrupted copies cannot become runnable: only the exact final SHA-256 is
  atomically activated. A later retry may remove only the reserved `P4Q.TMP`.
