# Explicit legacy board maintenance

Read this reference only for explicitly requested maintenance of a legacy board.
Tab5 remains the only actively maintained Console OS target. Preserve each legacy
board's source, exact-unit recovery and authorization contracts; another board's
artifact or acceptance never authorizes a write or peripheral.

## Match the board and target

| Physical target | Console OS target |
| --- | --- |
| Elecrow CrowPanel Advanced 10.1-inch, user-facing **10 in variant** | `make console-os-elecrow-idf` / `elecrow-crowpanel-advanced-10` |
| Olimex ESP32-P4-PC **Rev.B development board** | `make console-os-olimex-idf` / `olimex-esp32-p4-pc` |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 landscape | `make console-os-waveshare-idf` / `waveshare-esp32-p4-wifi6-touch-lcd-4.3` |

Build exactly one physically matching target after focused checks. Build others
only when a shared change or a concrete board-selection risk requires them.
Any new target needs its own ID and source-pinned profile through
`scripts/board-port.py` and `docs/BOARD_PORTING.md`; an adapter does not permit
reusing a board identity.

## Elecrow 10 in variant

Read `hardware/board-profile.json` and
[the 10 in variant contract](elecrow-10-in-variant.md) before changing display,
touch, audio, USB or combined game firmware. Keep that exact-unit reference
current as new evidence is recorded; do not carry older acceptance into a new
image.

Do not copy a BSP or pin map from another CrowPanel size or PCB revision. Prefer
a physically confirmed size/SKU/revision. A bounded subsystem may proceed without
the final revision suffix only when commit-pinned official schematics prove its
complete electrical path and effective driver settings invariant across every
published revision, that evidence is reproducibly verified, and the profile
grants only that subsystem's exact resources. Factory-firmware evidence alone
is insufficient; `pin_map_authorized: false` remains in force outside the scope.

For speaker work, use [Elecrow Sound](../../esp32-elecrow-sound/SKILL.md). It owns
the factory I2S1/GPIO30 contract; schematic codec labels do not replace the exact
pinned factory behavior. Never recommend a passive OTG adapter: treat the panel's
USB-C port as sink-wired unless its exact PCB proves otherwise. Controller tests
need a powered, current-limited, backfeed-safe host fixture.

Legacy diagnostic commands use the app's generic build directory. For an
explicitly requested Elecrow diagnostic, select the board when building:

```sh
make build APP=<app> BOARD=elecrow-crowpanel-advanced-10
make flash-app APP=<app> PORT=/dev/cu.<port>
make monitor APP=<app> PORT=/dev/cu.<port>
```

These examples are not authorization. Follow `AGENTS.md`'s explicit-request-only
firmware backup policy. Preserve existing recovery artifacts, the app metadata's
write gates and exact-unit checks; choose the built app and verified artifact.
Use `make flash` only for an intentionally reviewed bootloader or partition-table
change. Retain the board's required readback and acceptance contract. These
build directories and commands do not install or monitor Tab5 Console OS.

## Olimex Rev.B

Read `docs/boards/OLIMEX_ESP32_P4_PC.md` and
`hardware/boards/olimex-esp32-p4-pc-rev-b.json`. USB-C is native Serial/JTAG;
the powered USB-A hub is the HID-host path. Persistent game storage is microSD,
not USB MSC. Before a newly connected P4-PC's first write, bind its hashed live
identity and deliberately authorize the profile's write gate. Do not capture
firmware unless the user explicitly requests a backup.

Do not use the Elecrow audio route. Use the official Rev.B ES8311 service in
`components/olimex/platform_audio` on shared I2C1 and I2S1. The external result is
the 3.5mm jack and remains hardware-unverified until a named acceptance run.

## Waveshare 4.3 landscape

Use [Waveshare](../../esp32-waveshare/SKILL.md). The deployed controller-first
profile uses the synthetic build target
`waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host`: H1 CH343 provides programming,
monitoring and verified content transfer; externally powered H2 is the
runtime-switch controller-host/USB-Drive connector. Neither its USB-C shape nor
host-mode firmware establishes H2 VBUS source capability.

Before a firmware write, follow [Waveshare's guarded-write contract](../../esp32-waveshare/SKILL.md):
require matching exact-unit authorization and a recorded guarded route for the
candidate artifact, current predecessor and live partition layout. The historical
`0x20000` app offset is usable only when that route proves the matching layout.

For the two recorded development units, resolve each H1 port to its stored
identity, preserve its registered recovery artifacts and flash sequentially. Never run
simultaneous CH343 writes: a parallel two-board attempt dropped a port and is
not an accepted install method. Require esptool's post-write hash verification
before resetting each unit. A new unit
needs its own hashed live-device binding and guarded authorization. Do not
create or refresh firmware backups as part of flashing. Backups, snapshots and
backup manifests are never required on any route. Use reviewed backup-free
installers for new writes; dated snapshot-based routes remain historical/recovery
tooling. Recovery can rebuild the reviewed old source.
