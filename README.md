# DEF CON ESP32-P4 game platform

This repository is the experimental firmware platform for a future ESP32-P4 badge. Doom is the first end-to-end acceptance game; USB controllers, display, storage, audio, and lifecycle services are reusable platform components rather than Doom-specific code.

## Current target

The attached board has been identified non-destructively as an ESP32-P4 revision 1.3 with 16 MB flash. Its saved pre-project image identifies itself as `ESP32-P4-Elecrow-Advance`; more importantly, it contains Elecrow's complete, commit-pinned 10.1-inch loading-background asset at flash offset `0x71004`, while the full 7- and 9-inch assets are absent. The working classification is therefore the 10.1-inch/DHE04310D **factory-firmware variant** with very high confidence. This is not a physical-label or PCB-revision claim: `pcb_revision` remains null and every pin map remains unauthorized until the board silkscreen is photographed.

The original 16 MB factory flash has been backed up locally, hashed, and bound to this unit by a SHA-256 of its normalized base identity. The raw identifier is not stored. The binary is intentionally Git-ignored; its metadata is in `hardware/backups/manifest.json`.

The pin-independent `bringup` image has passed on the connected board using the exact pinned ESP-IDF 5.5.3 toolchain: P4 revision v1.3, 16 MiB flash, 32 MiB initialized PSRAM, a 1 MiB PSRAM write/read test, and persistent PASS heartbeats. The tested build configured hex PSRAM at 200 MHz; M0 did not independently measure its bus clock. The app-only write targeted `0x10000`; a later 64 KiB readback from `0x0` matched the same region of the factory backup, and the installed app readback matched the built binary. Evidence is recorded under `hardware/test-runs/`.

The bounded display path is now hardware-tested. A reproducible custom image using Elecrow's commit-pinned V1.0–V1.2-invariant settings and Espressif's locked EK79007 1.0.2 driver initialized the attached 1024×600 RGB565 panel at two DSI lanes/900 Mbps and 51 MHz, then cycled vertical, horizontal, and BER patterns at 25% backlight. The app-only range read back exactly, serial cycling remained healthy, and the user supplied a live image of visible color bars. This pass authorizes no other peripheral and does not restore or reuse the factory UI.

The toolchain lock also fixes the image compatibility range to ESP32-P4 revision 1.0 through 1.99. The build wrapper rejects the incompatible revision-3.x family before a flash is attempted.

The D0 Doom host proof also passes: the immutable vendored doomgeneric source builds with the project-authored adapter under fatal warnings, and the ignored, hash-pinned Doom 1.9 shareware development WAD reaches eight headless frames. D0.5 additionally cross-compiles and whole-archive links all 80 selected engine objects for ESP32-P4 revision 1.x with the pinned IDF; the build-only target contains no WAD or hardware services and repository flash commands reject it. D0 remains host-tested and D0.5 is build-tested only—neither qualifies Doom runtime on the ESP32-P4, display, SD, audio, or controller input. A clean clone without local game data reports the D0 smoke test as skipped while still verifying provenance and building the engine.

The reusable tier-1 gamepad software path now builds under the pinned target: a singleton P4 high-speed USB Host owner, exclusive class leases, Espressif HID transport, the bounded descriptor/report parser, and complete canonical snapshots. Host sanitizer tests cover the fixture gate, lease/teardown state machine, transactional publication, reconnect sessions, and immediate disconnect neutralization. The committed `gamepad_diag` configuration remains electrically inert and both flash modes remain false. The published V1.0-V1.2 schematics prove that J16 carries the P4 HS DP/DM pair, but its two 5.1 kOhm CC resistors advertise a sink and its diode-isolated VBUS path feeds only into the board; it cannot power a controller. A passive OTG adapter or ordinary powered hub is not a safe fix. Physical input testing requires a current-limited, backfeed-isolated J16 data/power shim. The Mac USB tree currently exposes only the WCH serial bridge, not a controller.

## First milestones

1. Reproduce the toolchain and pass the serial/flash/PSRAM bring-up diagnostic.
2. Display baseline complete; record the physical product/PCB labels, then bring up touch, SD card, and audio in separate diagnostics.
3. Build a safe powered USB-host shim and pass generic HID gamepad diagnostics.
4. Run Doom with legal game data from SD, RGB565 video, I²S audio, and the shared input API.
5. Extract only proven procedures into repository skills and hardware acceptance tests.

See [the architecture](docs/ARCHITECTURE.md), [hardware facts and open questions](docs/HARDWARE.md), [the Doom acceptance design](docs/DOOM.md), and [the acceptance roadmap](docs/ROADMAP.md).

## Reproducible commands

Install the pinned SDK once, or point `P4_IDF_PATH` at an existing matching install:

```sh
make setup
make verify
make build APP=bringup
make build APP=display_diag
make gamepad-host
make gamepad-idf
```

Hardware writes always require an explicit port. For the first pin-independent
diagnostic, flash only the application partition so the factory bootloader and
partition table remain intact:

```sh
make backup PORT=/dev/cu.wchusbserial10
make flash-app APP=bringup PORT=/dev/cu.wchusbserial10
make monitor APP=bringup PORT=/dev/cu.wchusbserial10
make flash-app APP=display_diag PORT=/dev/cu.wchusbserial10
```

Both `make flash-app` and the full-project `make flash` verify the local factory
backup's hash and byte count, require the connected unit's stored identity
hash to match the backup manifest, probe the live ESP32-P4 and flash size, and
refuse to write on a mismatch or when secure boot/flash encryption is enabled.
After writing, they read the built application's exact byte range back and
compare its SHA-256. Readback transfers are capped at 512 KiB, each chunk is
checked against the corresponding source range and retried independently at a
lower baud, and the complete ordered byte count and SHA-256 must match before a
deferred image can launch. Use the full-project target only when a test requires
a new bootloader or partition table. Do not connect a controller with a passive
OTG adapter; see `docs/HARDWARE.md` first.

## Repository layout

```text
apps/          ESP-IDF applications and acceptance firmware
components/    Reusable badge services and board support
docs/          Architecture, decisions, and test criteria
hardware/      Board profile, schematics/host-shim notes, backup metadata
scripts/       Reproducible setup/build/flash/monitor commands
third_party/   Exact source pins and third-party licensing policy
.agents/skills Project-scoped Codex workflows learned from verified work
```
