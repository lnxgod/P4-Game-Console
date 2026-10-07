---
name: esp32-waveshare
description: "Use for explicitly requested legacy Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 firmware maintenance, board-service changes, diagnosis or exact-unit qualification."
---

# ESP32 - Waveshare

Use this skill for the Waveshare 4.3 in board only. Keep it separate from the
Elecrow 10 in variant. Read `hardware/board-profiles/waveshare-esp32-p4-wifi6-touch-lcd-4.3.json`,
`docs/WAVESHARE_P4_WIFI6_TOUCH_LCD_4_3_PORT.md`, `toolchain.lock.json`,
`AGENTS.md`, and `docs/HARDWARE.md` before changing board-facing code.

The repository contains backed-up, hash-bound exact-unit evidence plus named
display, touch, audio, SD, H1 transfer, H2 USB Host, controller, BLE, and Console
OS runs. Each dated record applies only to its named resources, artifact and
units. Reconcile the current board documentation, manifest and retained ledger;
do not infer live state or a new artifact authorization from past acceptance.
Follow `AGENTS.md`: firmware backups run only on explicit user request,
including for newly connected units, and are never a flashing prerequisite.
Preserve existing recovery artifacts; recovery can rebuild old source.
Never enable an Elecrow pin map under a Waveshare build flag.

## Port boundary

- Reuse the stable Console OS shell, Game API, storage API, and framebuffer
  contracts where they remain board-neutral.
- Add Waveshare-specific services under `components/` or a dedicated board
  layer; games never own DSI, I2C, codec, USB, or raw GPIO.
- The physical panel is 480x800 and is initialized as 800x480 landscape.
  Console OS renders its shell at the fixed 768x480 logical resolution; native
  games should request native 768x480 RGB565 through optional `video-highres`,
  retaining the 320x200 fallback. Follow `docs/GAME_ART.md`; input remains
  canonical 320x200. Do not rotate or shrink these contracts in game code.
- Keep game scaling, rotation, panel framebuffer selection, and any PPA use in
  `platform_display`. The reviewed fast path alternates two DSI-owned buffers
  and uses blocking PPA SRM for the 320x200 surface; preserve a bounded CPU
  fallback. Prove speed from timestamped runtime frame counters and PPA
  success/failure counters, not from a build or visual impression.
- Preserve the Elecrow `hardware/board-profile.json`; Waveshare has its own
  profile and exact-unit evidence. Changes to either board never authorize the
  other's peripherals.

## Build and install

The controller-first local Console OS build target is:

```sh
./scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host
```

The preserved release verifier below requires the original `0.42` source
contract. Use it only when reproducing that historical baseline, not as a
passing qualifier for a changed current candidate:

```sh
python3 scripts/verify-console-os-waveshare.py apps/console_os/build-waveshare-usb-host
```

For a current candidate, run the focused component checks, record build
evidence, and prepare matching scoped verification before a guarded install.
Do not relax a historical release check or report it passed for newer source.

H1 is the CH343 programming, monitoring, and content-transfer connector. It
uses 115200 baud while idle and negotiates 921600 baud for bounded transfers.
H2 is the runtime-switch connector: Console OS normally owns it as USB Host for
controllers, while the USB Drive app explicitly stops Host/HID, unmounts FAT,
and switches it to TinyUSB MSC. The Mac and Console OS must never own the SD
filesystem concurrently.

Firmware backups, snapshots and backup manifests are never required for
flashing. Do not capture firmware or ask for backup confirmation. Use a
reviewed backup-free route that checks the exact unit, candidate build artifact,
applicable predecessor, live partition layout and security state directly.
Dated snapshot-based installers remain historical/recovery tooling; do not
select them for new writes. The recorded
`0x20000` app offset is valid only when that route proves the matching layout.
If no matching authorization and route exists, stop before writing and prepare
a separately recorded successor authorization; do not repurpose a frozen
installer. Resolve each H1 port to its stored identity, flash units sequentially,
and require esptool hash verification. Parallel CH343 writes have caused a port
drop and are not an accepted workflow. A new or unknown unit needs its own
identity binding and guarded authorization, without any firmware backup
requirement. Recovery can use a rebuild of the reviewed old source.

Native cartridges can be installed live without changing firmware:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.wchusbserial...
```

The host and badge validate the bounded P4G structure and SHA-256, stage the
write atomically, read it back, and refresh the native catalog. Use
`--no-replace` when checking whether an exact package is already present.

Boot readiness gates only on display, SD mount, and native catalog readiness.
Doom and Chex files remain untrusted SD inputs: perform their exact full-file
SHA-256 on demand. Never start either scan merely because the generic
Multiplayer page opened. At terminal launch, validate only the selected title
while the same bounded pass captures its immutable PSRAM snapshot. Never
persist a size, timestamp, sample, or prior-boot receipt as authority for WAD
readiness.

## USB gate

H2 is the OTG Type-C connector. The official schematic shows P4 GPIO24/GPIO25
for USB data, H2 VBUS, and 5.1 kOhm pulldowns on CC1 and CC2. Treat that as a
sink/device port until measured otherwise. A VBUS pin is not a host-power
authorization. Host tests require a regulated, current-limited, backfeed-safe
fixture and must measure source/sink direction and fault behavior. Firmware
does not make H2 source VBUS. The recorded controller-first route uses external
power/backfeed protection, the full-speed host policy, bounded hub/HID parsing,
and immediate neutral state on disconnect. H1 is never the controller path.

Keep the H2 USB Host/HID controller role as the device default. Do not start
multiplayer BLE during boot. Opening the Multiplayer app lazily prefers BLE for
the game link; preserve wired UART as an explicit fallback and preserve the
known-good wired Doom path when changing radio or lobby code. In the BLE lobby,
preserve the explicit Host/Join role screen. Host owns game/settings/create;
Join uses a bounded wildcard scan, lists each room's resolved local game
identity, and
requires an explicit room selection before connecting. Join must never create
a room or expose a separate game filter. While unconnected, hosts use bounded
alternating
advertise/scan phases. If two displays each say `ROOM OPEN` and `1/2`, the
higher session ID must automatically yield and join the lower session ID;
never weaken session or content-identity checks to force convergence.

## Definition of done

Call only the behavior named in a dated record hardware-tested. A new feature,
controller, hub topology, board unit, or electrical arrangement needs its own
acceptance evidence. A performance claim needs a sustained measured interval,
zero hard display failures, and multiplayer transport drop/stall counters from
the same run. Build success alone is only `build-tested`.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
