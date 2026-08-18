# USB Game Manager and OS updates

The `P4 GAMES` volume on J16 is the persistent user-storage boundary. It is
the existing wear-levelled FAT partition at flash offset `0x710000`; this
design does not move or erase it. The Console OS and a laptop never mount the
filesystem simultaneously.

## Stored games

Games use one `GAMES/*.P4G` executable and may declare a same-basename `.P4R`
read-only resource sidecar. Console OS scans the `GAMES` directory
first, then accepts root `*.P4G` files from older cards for compatibility. Format
`p4-native-elf-v1` consists of a fixed 256-byte little-endian header followed
by one ESP32-P4 RISC-V ELF payload. The header contains bounded launcher
metadata and the payload SHA-256. Firmware checks the complete layout, text
fields, capabilities, digest, ELF headers, load ranges, relocations, and
undefined-symbol allowlist before the Espressif ELF loader receives bytes.

ELF text and data are relocated into PSRAM. A cartridge gets one versioned
host table for sanitized input, the 320x200 RGB565 surface, presentation, and
bounded tone/copied-PCM audio. It never owns display, touch, audio, USB, or filesystem
handles. Native cartridges are not a security sandbox: structurally valid
development packages can execute machine code. The Game Manager labels this
trust level and rejects partial or corrupt copies.

The SHA-256 detects damaged or partially copied packages; it is not a digital
signature. Kid-facing distribution should use one trusted catalog or an
adult-controlled source until a signing-key policy is added.

Install or update a game by copying/replacing its declared `.P4G` and `.P4R`
files in `GAMES` while the laptop owns `P4 GAMES`, then ejecting the volume.
The catalog is rebuilt only after the app regains ownership. Removal is a
confirmed Game Manager action; it removes a declared resource sidecar before
its executable so an old resource cannot be inherited by a later package.

## Atomic OS updates

The original 7 MiB factory-app range is split into two OTA slots while the
`game_data` boundary remains unchanged:

| Partition | Offset | Size |
| --- | ---: | ---: |
| NVS | `0x9000` | 24 KiB |
| OTA data | `0x10000` | 8 KiB |
| OTA 0 | `0x20000` | `0x370000` |
| OTA 1 | `0x390000` | `0x380000` |
| P4 GAMES | `0x710000` | `0x8f0000` |

`UPDATE/P4UPDATE.P4U` has a bounded header plus one raw ESP-IDF app image.
After a clean USB eject, the Game Manager verifies it, changes its Program
Manager subtitle to `OS UPDATE READY - OPEN`, temporarily stops the USB
device, streams only to the inactive OTA slot, verifies the ESP image, and
changes the boot selection only after every byte succeeds. A reset or power
loss before that final selection continues booting the old slot. Bootloader
rollback remains armed until the new OS initializes storage, display, and its
first frame successfully.

The build emits the user-copyable artifacts together:

```text
apps/console_os/build/game-storage-seed/GAMES/MAZE.P4G
apps/console_os/build/game-storage-seed/GAMES/INVADERS.P4G
apps/console_os/build/P4UPDATE.P4U
```

After the migration, update the OS by copying only `P4UPDATE.P4U` into the
`UPDATE` folder on `P4 GAMES`, ejecting cleanly, opening System > Game Manager,
selecting the OS row, and confirming Install. The file is integrity checked
but is not cryptographically signed on this development unit.

After a successful install, Console OS removes the consumed
`UPDATE/P4UPDATE.P4U` before rebooting when it still owns the volume. If a host
retakes the volume first, the update remains harmlessly visible and can be
removed from Game Manager after the next clean eject.

The pinned `esp_tinyusb` storage helper uses one reusable deferred-write
buffer. Console OS wraps its WRITE(10) callback: each bounded, sector-aligned
transfer is erased, written, and read back before USB success is returned.
Invalid ranges fail closed, flash errors are reported to the laptop, and the
app/USB ownership gate remains in force. This trades peak copy speed for a
storage path that cannot silently acknowledge an unwritten range.

The first migration to this partition table still requires J1 because the
bootloader and partition table themselves must change once. Later OS updates
use J16 and preserve all files in `P4 GAMES`.

For the exact bound development tablet, the guarded migration route is:

```sh
install -d -m 700 \
  hardware/local-state/console-os-game-manager-dual-ota-20260814
python3 scripts/console-os-game-manager-migrate.py \
  --port /dev/cu.wchusbserial10 \
  --transaction-directory \
    hardware/local-state/console-os-game-manager-dual-ota-20260814
```

Run it from a clean committed build with J16 disconnected or cleanly ejected.
It reuses the already-recorded complete backup, creates no new flash backup,
and has no write target at or above `0x710000`. It verifies the installed
predecessor, exact target readback, preserved NVS/PHY and storage-header
samples, then retains the same J1 descriptor through first-frame acceptance.
