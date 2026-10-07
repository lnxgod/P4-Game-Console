---
name: esp32-fix-console
description: "Build, diagnose or change P4 Console OS, shared platform services, ESP-IDF integration or hardware behavior. M5Stack Tab5 is the maintained target; use other boards only for explicitly requested legacy maintenance. Use game skills for stable-API game work."
---

# ESP32 - Fix Console

M5Stack Tab5 is the only actively maintained Console OS target.
`make console-os-tab5-idf` names it explicitly; `make console-os-idf` is its
default alias. Preserve other boards' source and recovery contracts and use
[legacy board routes](references/legacy-boards.md) only for explicitly requested
legacy maintenance.

For first-time OS installation and provisioning, start with
[Set Up](../esp32-setup/SKILL.md). For a game that only consumes the stable P4
video, controls, tone, timing and lifecycle APIs, use
[Make Game](../esp32-make-game/SKILL.md) then
[Test Game](../esp32-test-game/SKILL.md) for authoring or changes, and
[Add Game](../esp32-add-game/SKILL.md) for packaging, installation or compatible
game-only updates. Use this platform skill when the task changes Console OS, a
shared service, the package/loader boundary, an ESP-IDF build or hardware behavior.

## Read the matching contracts

Read `AGENTS.md`, `toolchain.lock.json`, the selected board profile and its board
document before changing firmware. For Tab5, use
`hardware/boards/m5stack-tab5/board-profile.json` and
`docs/boards/M5STACK_TAB5.md`. The board document links exact-artifact records for
A/ST7121 and B/ST7123; use those records for installed-image and peripheral
claims. Acceptance does not carry to a successor image or another unit.

Read `docs/HARDWARE.md` and [hardware safety](references/hardware-safety.md) when
work touches pins, rails, connectors, peripherals or device writes. Use the
repository scripts and Make targets; [the workflow](references/workflow.md)
describes the pinned environment and evidence record.

An explicitly requested board port uses `scripts/board-port.py` and
`docs/BOARD_PORTING.md`: start with `check` and `matrix`, then feed a source-pinned
hardware specification to `plan` and `scaffold`. The adapter map keeps the shared
Console OS feature contract and exposes genuine driver gaps. A new target needs
its own ID and profile. Never reuse a backend based only on matching connector
or chip names; require hash-bound schematic compatibility evidence.

Keep these boundaries intact:

- If `pin_map_authorized` is false, restrict work to pin-independent bring-up
  and subsystems explicitly enabled under `peripheral_authorizations`. Use only
  the named resources; every other pin and peripheral remains locked.
- Preserve the silicon family selected by `toolchain.lock.json`. P4 `<3.0` and
  `>=3.0` images are incompatible. Enforce revision 1.0 through 1.99 in the build
  wrapper and never bypass esptool's revision gate with `--force`. Resolve
  flash/PSRAM capacity from the selected board and exact-unit record; a 16 MiB
  Elecrow or Tab5 measurement does not describe every board.
- Keep board code in reusable components. Games consume display, audio,
  storage, input and time services and never configure pins or own USB host
  handles, display drivers or raw peripheral callbacks.
- Keep generated `sdkconfig`, managed components, build output, firmware,
  WADs and local backups out of Git. Pin SDKs, components, third-party source
  and board references, and commit dependency lockfiles once generated.
- Every game on maintained Tab5 must render directly at 768×480 RGB565;
  follow `docs/GAME_ART.md`. Inspect the actual game framebuffer and launch
  record, not just high-resolution metadata or panel scaling. Trace optional
  capabilities, `P4_CONSOLE_NATIVE_*_LOW_RES` build flags, per-title gates and
  surface allocation when a game launches at 320×200. Correct the maintained
  path within the authorized task; never introduce or enable a resolution
  downgrade to repair readability/performance. Native cartridges require
  `video-highres` in the manifest and `P4_GAME_CAP_VIDEO_HIGH_RES` in the
  C descriptor; fail cleanly when unavailable.
  Keep canonical 320×200 input units and board-owned panel scaling unchanged.
  Preserve legacy board/ABI source and recovery contracts for explicit legacy
  work. Skill policy and a host build do not prove the installed OS changed.

## Build and verify the changed boundary

Use the smallest proof that covers the modified boundary:

- Documentation or skill changes need focused validators and reference checks;
  do not build firmware.
- A host-testable component change needs its existing `*-host` target.
- A Tab5 firmware or Console OS integration change needs the pinned environment
  and `make console-os-tab5-idf` once after focused checks pass. This target
  already runs `scripts/verify-console-os-tab5.py`. Use the standalone verifier
  when inspecting an existing build, rather than repeating it automatically.
- Run `make verify` when the environment has not already been proven. Run
  repo-wide `make check` only on request, for a deliberate toolchain or lock
  migration, or for a cross-cutting change spanning maintained applications.
- Flash when authorized on-device behavior must be established. Run one named
  acceptance for the changed behavior; repeat only after the image or test
  conditions change.

Preserve chosen feature selections. When preparing a successor for an existing
exact-unit authorization, retain its installed peripheral scope unless a change
is authorized: the build defaults `P4_TAB5_USB_HOST` and
`P4_TAB5_BLE_MULTIPLAYER` to `1`, so select values matching that scope. Bind the
chosen configuration to the artifact and installation authorization. Local
source work and builds do not grant flash authorization.

Do not invoke display, audio, USB or gamepad diagnostics merely because a game
uses stable APIs. Do not repeat an unchanged build or flash, or widen the task
because an unrelated check failed. Resolve incompatible APIs, implicit
declarations, invalid configuration and memory misuse without global warning
suppressions. Preserve uncommitted interface/game work; do not rebase or
overwrite a shared working tree to manufacture a clean base.

For OS interface, scrolling, versioning and firmware-only successors, read
[core successor contracts](references/core-successors.md).

Always apply the [app/game performance contract](../../../docs/GAME_PERFORMANCE.md)
to shared OS work: all scrollable surfaces must follow contact promptly and
remain smooth at panel rate, near 60 FPS. Plan and use relevant rendering,
cache/DMA and pacing resources plus both P4 application cores for independent
input, logic/render and audio through owned, joined services. Preserve native
game resolution, immutable-source/dirty proof and conservative buffer retirement;
never move hardware ownership into apps or cartridges.

The maintained Tab5 native-game candidate keeps game/update/render on core 0,
PPA presentation on core 1 at priority 2, and audio output on core 1 at priority 4.
Console OS owns exactly two native PSRAM game framebuffer leases. Read the
supplied pixel pointer and stride each frame; they may change on every frame.
Render the complete current frame, never cache the pixel pointer across
callbacks, and leave a committed source immutable until backend consumption.
Preserve bounded admission, backpressure, reuse fences and drained/joined
teardown before freeing OS-owned buffers/context or returning display ownership
to the launcher. The native backend retains only OS-owned callbacks and pixel
buffers, so cartridge unload does not invalidate an in-flight frame. A failed
join retains the worker and its resources until safe shutdown. Synchronous
recovery preserves direct 768x480; never add a low-resolution fallback.

Test rotating leases, source lifetime, concurrent producer/consumer work,
backpressure, stop/restart and failure paths with race detection where available.
On the exact OS/package/unit, record actual core IDs/priorities and accelerator
paths, completed-backend frame intervals and concurrent stage timing showing
game/update/render work overlapping presentation. Keep accepted submissions,
backend completions and physical scanout evidence distinct. Include
queue/backend timeouts, hard errors, audio queue rejections, underruns, clipping,
write failures and stack reserve. Exercise busy play, title/ready, pause and
results, plus synchronous native recovery. Apply the shared contract's separate
input/render/submission/pacing and glide/cold-cache measurements for scrolling.
Worker creation, enabled SMP and host tests cannot qualify device cadence or
readability; acceptance remains pending until measured.

## Guarded Tab5 installation and evidence

Firmware backups, snapshots and backup manifests are never a prerequisite for
flashing, on any board or route, including a new unit's first write. Do not
capture firmware or ask for backup confirmation. Backups run only as a separate
explicitly requested operation; recovery can rebuild old source. Preserve
existing recovery files and manifests. Never store or print a raw base identity
or erase the whole flash to solve a build or connection problem.

Use `scripts/flash-console-os-tab5.py` from the pinned environment with an
explicit unit, port, authorization file and digest. The default is a local-only
check; `--install` performs the guarded write and startup-log capture. The current script
uses A/B/C to select entries in the reviewed authorization. Each Tab5, including
a new unit, needs its own confirmed model and hashed live identity; never
inherit another unit's authorization. Neither a backup file nor a backup
manifest is read or required. Generic `make flash-app` and `make monitor` commands
use legacy build directories and are not the Tab5 installer or monitor route.

Preserve exact-unit identity, silicon revision, flash/security state, applicable
predecessor, partition/active-slot and immutable-artifact checks. App-only
predecessor checks use a reviewed earlier build artifact. A first-layout write
does not need the old firmware. An app-only write leaves the OTA selector
unchanged. A partition change requires a layout-aware migration that preserves
affected user content, without requiring a firmware backup. Honor installation
authorization already given in the session without asking for it again.

Routine authorized Tab5 app flashes use device checksum verification. Use
`--verification full-readback` only for recovery, diagnostics or an explicit
request. Record the method that ran; checksum evidence is not full readback.
Other boards retain their device/image verification contracts; none may require
or capture a firmware backup for flashing. Historical snapshot-based installers
remain recovery/history tools; prepare new writes through a backup-free route.

Before qualifying an OS update, check
[protected game payloads](../../../docs/GAME_SDK.md#protected-game-payloads).
Even a firmware-only build regenerates the Red Dragon allowlist from its paired
cartridge. Confirm any installed protected content matches that exact OS
lineage, or complete the authorized paired content update; preserve the guard
and save identity. This does not select development games for a standard install.

Load compatible content updates through the running launcher's native USB-C
route with [Add Game](../esp32-add-game/SKILL.md). Push the requested cartridge
and required resource sidecars; use `push-bundle` only for an explicitly
requested bundle. Selected Doom/Chex data uses `scripts/p4-usb-content.py`.
Retain verified device readback receipts; no card reader is needed.

Record Git state, toolchain/locks, exact app and content hashes, unit/panel,
verification method, result and serial markers. Label host, build, transfer and
physical acceptance separately: a build or successful write is not gameplay,
acoustic or controller proof. Preserve unchanged scoped C6 Bluetooth/local Wi-Fi
and 500 mA non-QC charging authorization; installation records and hardware
acceptance remain tied to their exact artifact and unit.
For USB-A HID support use [Controllers](../esp32-controllers/SKILL.md) and the
Tab5 board document; named controller acceptance remains distinct from host/build
checks.

A platform change is ready when its reusable boundary is clear, locks and docs
match it, and relevant checks pass. Firmware changes also need the selected
target build. Hardware-facing claims need serial or physical evidence; record
unresolved electrical dependencies and unmeasured acceptance as pending.

For game-related work, apply
[launch and remix quality](../../../docs/LAUNCH_QUALITY.md): preserve gameplay
and saves, keep incomplete titles out of default bundles, and distinguish
native-size art, operator feedback and measured P4 cadence.
