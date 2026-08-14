---
name: develop-esp32-p4-platform
description: Build, diagnose, flash, monitor, or extend firmware for this ESP32-P4 badge platform, including the Elecrow CrowPanel Advanced 10 in variant. Use for ESP-IDF apps, board support and pin changes, display, SD, audio, touch or USB bring-up, toolchain setup, recovery, and hardware verification on the Elecrow CrowPanel Advanced family.
---

# Develop the ESP32-P4 platform

Build every app on the same pinned platform and leave reproducible evidence. Treat an unknown board revision or an unverified electrical interface as a hardware gate, not a software detail.

## Start with the project contracts

Read these files before changing firmware:

1. `toolchain.lock.json`
2. `hardware/board-profile.json`
3. `AGENTS.md`
4. `docs/HARDWARE.md` when work touches pins, rails, connectors, or peripherals

Use the repository scripts and Make targets rather than ad-hoc `idf.py` commands. Read `references/workflow.md` for the standard command sequence and `references/hardware-safety.md` before flashing or changing hardware-facing code.

For the Elecrow 10.1-inch hardware, use the user-facing name **10 in variant** and read `references/elecrow-10-in-variant.md` before changing display, touch, audio, USB, or combined game firmware. Keep that reference current as new exact-unit evidence is recorded.

For speaker audio on this variant, also use `$use-elecrow-p4-audio`. It owns the
low-freedom factory I2S1/GPIO30 contract and prevents schematic codec labels
from replacing the exact pinned factory behavior.

## Choose the safe scope

- If `pin_map_authorized` is false, restrict work to pin-independent bring-up plus any subsystem explicitly enabled under `peripheral_authorizations`. Use only the resources named by that authorization; every other pin and peripheral remains locked.
- Preserve the explicit ESP32-P4 silicon family selection from `toolchain.lock.json`. This prototype is confirmed as revision v1.3 with 16 MiB flash and 32 MiB PSRAM; P4 `<3.0` and `>=3.0` images are mutually incompatible. The build wrapper must enforce revision 1.0 through 1.99, and esptool's revision gate must never be bypassed with `--force`.
- Do not copy a BSP or pin map from another CrowPanel size or PCB revision. Prefer a physically confirmed size/SKU/revision. A bounded subsystem may proceed without the final revision suffix only when commit-pinned official schematics prove its complete electrical path and effective driver settings invariant across every published revision, that evidence is reproducibly verified, and the board profile grants only that subsystem's exact resources. Factory-firmware evidence alone is insufficient.
- Keep board code behind reusable components. Games consume display, audio, storage, input, and time services; they do not configure board pins directly.
- Keep generated `sdkconfig`, build output, managed components, firmware binaries, WADs, and local backups out of Git.
- Pin ESP-IDF, managed components, third-party source, and board references. Commit dependency lockfiles once generated.

## Build and verify

Run, in order:

```sh
make verify
make check
make build APP=<app>
```

Resolve warnings that indicate incompatible APIs, implicit declarations, invalid configuration, or memory misuse. Do not hide them with global suppressions.

Label evidence accurately:

- `host-tested`: native unit tests passed.
- `build-tested`: the ESP-IDF image compiled and linked.
- `hardware-tested`: a named image ran on a named board revision and its serial acceptance markers were captured.

A successful build is not hardware proof.

## Flash and monitor

Before the first project write, require a complete flash backup with matching byte count, SHA-256, and hashed live-device identity in `hardware/backups/manifest.json`. Use an explicit serial port:

```sh
make flash-app APP=bringup PORT=/dev/cu.<port>
make monitor APP=<app> PORT=/dev/cu.<port>
```

Use `make flash` only when the test intentionally changes the bootloader or partition table and that broader write has been reviewed.

The flash script must verify the application readback before a run can be recorded as a PASS. Never erase the whole flash merely to solve a build or connection problem. Preserve the factory backup and record the exact app, toolchain, board identity hash, result, and observed serial markers after a hardware test. Do not store or print the raw base identity.

## Definition of done

A platform change is done only when its reusable boundary is clear, locks and documentation match it, relevant host tests pass, the target image builds from a clean configuration, and hardware-facing claims have serial or physical evidence. Record any unresolved electrical dependency explicitly.
