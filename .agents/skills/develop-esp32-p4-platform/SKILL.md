---
name: develop-esp32-p4-platform
description: Build, diagnose, flash, monitor, or extend firmware for this ESP32-P4 platform, with M5Stack Tab5 as the primary target, plus the Elecrow 10 in variant, Olimex ESP32-P4-PC Rev.B, and Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3. Use for ESP-IDF apps, board support and pin changes, display, SD, audio, touch or USB bring-up, toolchain setup, recovery, and hardware verification.
---

# Develop the ESP32-P4 platform

M5Stack Tab5 is the permanent primary Console OS target. `make console-os-idf`
is its default alias; `make console-os-tab5-idf` names it explicitly. Keep older
boards available through their explicitly named targets.

Build every app on the same pinned platform and leave reproducible evidence. Treat an unknown board revision or an unverified electrical interface as a hardware gate, not a software detail.

## Start with the project contracts

Read these files before changing firmware:

1. `toolchain.lock.json`
2. `hardware/board-profile.json` for Elecrow, or the selected profile under
   `hardware/boards/`
3. `AGENTS.md`
4. `docs/HARDWARE.md` when work touches pins, rails, connectors, or peripherals

Use the repository scripts and Make targets rather than ad-hoc `idf.py` commands. Read `references/workflow.md` for the standard command sequence and `references/hardware-safety.md` before flashing or changing hardware-facing code.

For the Elecrow 10.1-inch hardware, use the user-facing name **10 in variant** and read `references/elecrow-10-in-variant.md` before changing display, touch, audio, USB, or combined game firmware. Keep that reference current as new exact-unit evidence is recorded.

For Olimex, select `olimex-esp32-p4-pc` explicitly. Read
`docs/boards/OLIMEX_ESP32_P4_PC.md` and
`hardware/boards/olimex-esp32-p4-pc-rev-b.json`. Its USB-C connector is native
Serial/JTAG, while the powered USB-A hub is the HID-host path; persistent game
storage is microSD, not USB MSC. Never flash a newly connected P4-PC until its
complete 16 MiB factory image and hashed live identity have been recorded and
the profile's write gate has been deliberately authorized.

For Waveshare, select `waveshare-esp32-p4-wifi6-touch-lcd-4.3` explicitly and
use `$develop-waveshare-p4-4.3`. The deployed controller-first profile uses
the synthetic build target
`waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host`: H1 CH343 is programming,
monitoring, and verified content transfer, while externally powered H2 is the
runtime-switch controller-host/USB-Drive connector. Do not infer H2 VBUS source
capability from its USB-C shape or from host-mode firmware.

Keep the build target and physical target paired exactly:

- `elecrow-crowpanel-advanced-10` / `make console-os-elecrow-idf` means the Elecrow
  CrowPanel Advanced 10.1-inch device only.
- `olimex-esp32-p4-pc` / `make console-os-olimex-idf` means the Olimex
  ESP32-P4-PC **Rev.B development board** only.
- `waveshare-esp32-p4-wifi6-touch-lcd-4.3` /
  `make console-os-waveshare-idf` means the Waveshare 4.3 landscape device;
  use the `-usb-host` build-script target for the proven controller-first image.
- Any other target needs its own ID and source-pinned profile through the board
  port contract. Reusing an adapter never permits reusing a board identity.

For Tab5, select `m5stack-tab5` explicitly and read
`docs/boards/M5STACK_TAB5.md` and
`hardware/boards/m5stack-tab5/board-profile.json`. Use
`make console-os-tab5-idf`. A/ST7121 and B/ST7123 have boot/readback, mounted-SD and native-USB transfer
evidence; the operator confirmed touch after B's mirror correction. Doom lifecycle,
sustained scrolling and B speaker sound still need acceptance; the operator confirmed A startup and Doom music/effects on the speaker-repair candidate. A/B are separately
backed up and have exact-artifact test-install
authorizations. Load games through the connected USB-C cable with
`scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <port>`
and Doom/Chex through `scripts/p4-usb-content.py`; no card reader is needed.
Use `scripts/flash-console-os-tab5.py` with an explicit unit, port
and authorization digest; its default is a local-only check. Preserve and bind
any new unit before its first write. USB-A power/host and C6 radio remain disabled in this configuration.

For another ESP32-P4 board, use `scripts/board-port.py` and
`docs/BOARD_PORTING.md`. Start with `check` and `matrix`, then feed a
source-pinned hardware spec to `plan` and `scaffold`. The generated adapter map
keeps the Console OS feature contract shared and exposes only genuine board
driver gaps. Never reuse a backend merely because connectors or chips have the
same names; require hash-bound schematic compatibility evidence.

For speaker audio on the Elecrow 10 in variant, also use
`$use-elecrow-p4-audio`. It owns the
low-freedom factory I2S1/GPIO30 contract and prevents schematic codec labels
from replacing the exact pinned factory behavior.

For Olimex audio, do not use the Elecrow audio route. Use the official Rev.B
ES8311 service in `components/olimex/platform_audio` on shared I2C1 and I2S1;
the external result is the board's 3.5mm audio jack and remains hardware
unverified until a named acceptance run.

For a game that only consumes the stable P4 video, controls, tone, timing, and
lifecycle APIs, use `$develop-p4-games` instead. Escalate to this platform skill
only when the task changes Console OS, a shared service, the package/loader
boundary, an ESP-IDF build, or physical hardware behavior.

## Choose the safe scope

- If `pin_map_authorized` is false, restrict work to pin-independent bring-up plus any subsystem explicitly enabled under `peripheral_authorizations`. Use only the resources named by that authorization; every other pin and peripheral remains locked.
- Preserve the explicit ESP32-P4 silicon family selection from `toolchain.lock.json`. This prototype is confirmed as revision v1.3 with 16 MiB flash and 32 MiB PSRAM; P4 `<3.0` and `>=3.0` images are mutually incompatible. The build wrapper must enforce revision 1.0 through 1.99, and esptool's revision gate must never be bypassed with `--force`.
- Do not copy a BSP or pin map from another CrowPanel size or PCB revision. Prefer a physically confirmed size/SKU/revision. A bounded subsystem may proceed without the final revision suffix only when commit-pinned official schematics prove its complete electrical path and effective driver settings invariant across every published revision, that evidence is reproducibly verified, and the board profile grants only that subsystem's exact resources. Factory-firmware evidence alone is insufficient.
- Keep board code behind reusable components. Games consume display, audio, storage, input, and time services; they do not configure board pins directly.
- Keep generated `sdkconfig`, build output, managed components, firmware binaries, WADs, and local backups out of Git.
- Pin ESP-IDF, managed components, third-party source, and board references. Commit dependency lockfiles once generated.

## Verify in proportion to the change

Choose the smallest proof that covers the modified boundary:

- Documentation or skill changes need only their focused validators and
  reference checks; do not build firmware.
- A host-testable component change needs that component's existing `*-host`
  target, not every host suite.
- An ESP-IDF app change needs `make verify` when the environment has not already
  been proven, then `make build APP=<app>` and that app's focused verifier.
- A package or full Console OS integration change needs its documented build
  target once after focused host checks pass.
- Run repo-wide `make check` only when the user requests it, a toolchain or lock
  changes, or a genuinely cross-cutting change spans maintained applications.
- Flash only when on-device behavior must be established. Run one named
  acceptance for the changed behavior and repeat only after the image or test
  conditions change.

Do not invoke display, audio, USB, or gamepad diagnostics merely because a game
uses their stable APIs. Do not repeat an unchanged build or flash. Stop when
the risk-matched checks pass and report unrelated failures without widening
the task.

Resolve warnings that indicate incompatible APIs, implicit declarations, invalid configuration, or memory misuse. Do not hide them with global suppressions.

Label evidence accurately:

- `host-tested`: native unit tests passed.
- `build-tested`: the ESP-IDF image compiled and linked.
- `hardware-tested`: a named image ran on a named board revision and its serial acceptance markers were captured.

A successful build is not hardware proof.

For Console OS profiles, use exactly one physically matching target
after focused tests:

```sh
make console-os-tab5-idf     # M5Stack Tab5, primary target
make console-os-elecrow-idf          # Elecrow CrowPanel Advanced 10.1-inch
make console-os-olimex-idf   # Olimex ESP32-P4-PC Rev.B development board
make console-os-waveshare-idf # Waveshare 4.3 landscape bundle
```

Build additional targets only when a shared change or a concrete board-selection
risk requires them. Do not repeat builds after documentation-only edits.

## Flash and monitor

Before the first project write, require a complete flash backup with matching byte count, SHA-256, and hashed live-device identity in `hardware/backups/manifest.json`. Use an explicit serial port:

```sh
make flash-app APP=bringup PORT=/dev/cu.<port>
make monitor APP=<app> PORT=/dev/cu.<port>
```

Use `make flash` only when the test intentionally changes the bootloader or partition table and that broader write has been reviewed.

Do not repeat a factory backup for an exact device already bound in the backup
manifest. For the two recorded Waveshare development units, resolve each H1
port to its stored identity, write the verified app artifact at `0x20000`, and
flash sequentially. Never run simultaneous CH343 writes: a parallel two-board
attempt dropped a port and is not an accepted install method. Require esptool's
post-write hash verification before resetting each unit.

The flash script must verify the application readback before a run can be recorded as a PASS. Never erase the whole flash merely to solve a build or connection problem. Preserve the factory backup and record the exact app, toolchain, board identity hash, result, and observed serial markers after a hardware test. Do not store or print the raw base identity.

## Definition of done

A platform change is done only when its reusable boundary is clear, locks and documentation match it, relevant host tests pass, the target image builds from a clean configuration, and hardware-facing claims have serial or physical evidence. Record any unresolved electrical dependency explicitly.
