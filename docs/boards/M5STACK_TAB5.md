# M5Stack Tab5 Console OS port

Status: both A (ST7121) and B (ST7123) have verified app readback, mounted SD,
launcher startup and ten-second runtime health proof (2026-10-04). The operator
confirmed display/touch, including B after its mirror correction. USB game
loading is verified. Doom gameplay, speaker sound and sustained scrolling
acceptance remain pending; a clean boot is not gameplay proof.
Both units are ESP32-P4 v1.3 with 16 MiB flash and 32 MiB PSRAM.
Global `flash_authorized` stays false; only hash-bound exact-unit installs apply.
See `hardware/evidence/tab5-console-os-20261004-usb-game-testing.json` for current evidence.

## Build

Use the existing locked ESP-IDF 5.5.3 installation and its Python environment:

```sh
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.11_env"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
make console-os-tab5-idf
```

The target is `console_os m5stack-tab5`. Outputs are isolated in
`apps/console_os/build-tab5/`; the component resolution is committed in
`apps/console_os/dependencies-tab5.lock`. Other boards retain their locks.
The build enforces the repository's P4 revision 1.0–1.99 family, 16 MiB flash,
32 MiB PSRAM and two 0x7f0000-byte OTA slots. A revision 3.x Tab5 needs a
separate reviewed toolchain/target decision; never bypass the revision gate.

The verifier checks the selected drivers, pinned vendor files and dependencies,
binary revision bounds, flash/OTA layout, update digest, and every enabled native
game's presence and payload digest. These are software checks, not permission to
install or evidence of working hardware. The SDK's printed generic flash command
is not the first-install workflow.

## Core implementation

| Service | Tab5 implementation |
| --- | --- |
| Display | Official ILI9881C, ST7123 and ST7121 initialization tables; 720×1280 scanout, clockwise landscape 1280×720; RGB565 double buffers |
| UI/games | Shared 768×480 launcher/content, 384×240 shell and 320×200 games; centered 1152×720 viewport with 64-pixel side margins |
| Touch | Panel-matched GT911 at 0x14 or ST712x at 0x55; up to five contacts, matching rotation, invalid frames release input |
| Audio | ES8388 at 0x10, I2S1 stereo 16 kHz; MCLK30/BCLK27/LRCLK29/DOUT26; speaker enable through expander 0x43 P1 |
| Storage | Four-bit SDMMC slot 0, LDO4 supply, CLK43/CMD44/D0–3=39–42; no automatic formatting |
| Shared control | One persistent board-owned I2C bus on SDA31/SCL32; touch/audio clients borrow it across launcher/Doom handoffs |
| Game/runtime features | Shared game catalog, native cartridges, Lua cartridges, saves, themes, BBS/Windows launcher, Doom and update service |

Display detection follows the pinned official BSP: 0x55 firmware byte 1 selects
ST7121, byte 3 selects ST7123; otherwise 0x14 selects ILI9881C/GT911. An unknown
0x55 firmware value fails closed. LCD reset is expander 0x43 P4: assert low,
then release as input with pull-up, never drive high. Touch reset is P5.
Only the ILI/GT911 assembly gets the official GPIO23-low resistor workaround.

The ST712x adapter bounds report slots to ten before reading at most 70 bytes,
then publishes at most five valid contacts. It uses the official driver's
initialization/teardown but bypasses its 1.0.2 `read_data` callback, whose register
count is not bounded before a fixed stack-array read. GT911 repeated reports
retain their original timestamp; a failed poll clears cached input before reuse.
The existing exact-unit Waveshare GT911 restoration is unavailable on Tab5.

Audio keeps the amplifier off during initialization, writes a complete DMA ring
of zeros before enabling it, and checks expander readback. User steps 0–10 scale
PCM linearly; codec output is fixed at 60/100 for this candidate. Factory
GPIO30/PDM telemetry fields remain zero: GPIO30 is Tab5 MCLK, not amplifier
shutdown. Audio cleanup retains ownership on failure for retry. Acoustic quality,
headphone routing, volume and pop-free transitions still need measurement.

Display handoff waits two refresh boundaries and retains a pending buffer after
timeout so it cannot be overwritten while potentially scanning. PPA performs
landscape rotation and 1.5x/3x scaling. A small CPU prescale converts 320x200 games
to 384x240 before exact 3x scaling. CPU tiles remain the error fallback. B's
first transform measured about 20 ms, with first presentation at 50 ms;
this is not a sustained frame-rate or tear-free claim. Shell submissions can
return after queuing; framebuffer reuse still waits for the refresh fence.

ST7123's vendor table starts mirrored. Post-init mirror normalization aligns
its pixels with touch. Repeated display initialization now asserts the reset
pin through open-drain output before releasing it as an input, fixing the
captured Doom handoff failure (`Pin[4] can't set level in input mode`). A fresh
Doom play test is still required. The backlight GPIO is released on teardown.

SD startup defers whole-WAD hashing until Doom/Chex is requested, retaining full
SHA-256 verification and the PSRAM snapshot before engine use. B's SD initialization
measured 47 ms; populated-card launcher startup fell from about 25 seconds to
about six seconds. Muted boot skips the eight-second audio animation.

## Not enabled in this candidate

- C6 Wi-Fi/Bluetooth, wireless multiplayer and BLE controllers/dice.
- USB-A host power and HID controllers, and USB Drive/MSC mode.
- Battery/charging management, microphone, camera, IMU, RTC and expansion ports.

The multiplayer core is shared; native USB supplies its wired relay channel.
Physical multiplayer acceptance remains pending. Content transfer uses the
existing USB-C Serial/JTAG cable while Console OS owns the mounted SD card.
It does not expose a writable disk to the Mac. Do not remove a card while the
console is running or saving. Firmware binaries, WADs, generated SD content and
pre-install backups remain local and ignored by Git.

## Load games through the connected USB cable

Use the pinned Python environment (with pyserial), the explicit current port,
and a running launcher. No card reader is required:

```sh
python scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card \
  --port /dev/cu.usbmodem1101
python scripts/p4-usb-content.py doom --port /dev/cu.usbmodem1101
python scripts/p4-usb-content.py chex --port /dev/cu.usbmodem1101
```

The recorded A/B ports were `1101`/`2101`; port names may change. Bind the unit
before flashing. `push-bundle` installs 19 native `.P4G` files, the `.P4R`
resource sidecar and two `.P4CART` source games through one open connection.
Doom/Chex data are separate exact-hash local inputs. The device validates each
format, stages writes, verifies the digest, activates atomically and reads back.
Native and Lua catalogs refresh separately. Individual transfers use `push`
with `--class p4g`, `p4r` or `p4cart`; `exchange` remains isolated in `/TRANSFER`.
Opening native Serial/JTAG can reset the board on this Mac; tools wait for the
service and tolerate that startup window. Avoid repeatedly reopening monitors
during a play test. Content installation restarts the launcher after activation.

Tab5 P4U packages use the distinct target tag `esp32p4-tab5`. The Tab5 runtime
rejects legacy `esp32p4` packages, and existing runtimes reject Tab5 packages.
This prevents accidental cross-board updates; it is not a cryptographic signature.

## Verification and first hardware session

`make tab5-host` covers existing-board regressions plus Tab5 identity, all three
surface sizes, pixel rotation/scaling, untouched destination padding, touch
coordinate bounds, BBS navigation, storage policy, malformed device counts and
retryable touch cleanup, and cross-board update rejection. Driver tests use mocks. `make board-port-check` checks
the shared 20-feature software contract with hardware acceptance still false.
`scripts/verify-console-os-tab5.py` checks the built artifact and the 19 currently
enabled native cartridges.

The broader `make check` currently stops at the existing
`scripts/tests/test-doom-e5-gate.py:43`: it expects
`apps/doom_embedded_touch_audio/app-metadata.json` to set
`flash_app_authorized=true`, but that committed gate is false. The Tab5 port
does not change this authorization; the full repository check is not passing.

Before the first write, identify the physical Tab5 and its panel sticker; read
its live chip revision and flash size; preserve its complete factory flash;
record the backup byte count, SHA-256 and hashed live-device binding in a manifest.
Then review the exact Tab5 artifact and installation authorization. No Elecrow,
Olimex or Waveshare unit identity/authorization applies to this device.

On-device acceptance must record the image SHA-256, board/panel identity and serial
log. Check boot/backlight/colors/orientation, all touch corners and releases,
card mount/save persistence, boot audio/volume/mute, native and Lua games, Doom,
return to launcher, and repeated cleanup/reinitialization. Qualify radio, USB and
power-management services separately before enabling them.

## Exact-unit install and current testing state

Use `scripts/flash-console-os-tab5.py` with the pinned IDF Python environment.
Its default mode checks local inputs only. `--install` requires an explicit port,
unit A/B, authorization file and its SHA-256. It stages immutable image bytes,
checks the full recovery snapshot, verifies live identity/revision/flash/security,
and uses the same open connection for predecessor comparison, write and exact
readback. A failed check leaves the unit unmodified or in the loader after a
write failure. The app-only route also verifies the bootloader, partitions and
CRC-valid active OTA slot; it preserves the existing OTA selector.

Both units' current artifact, install receipts, SD sizes, upload hashes and
acceptance limits are recorded in the current evidence JSON. Full pre-install
snapshots contain the existing USB bridge firmware, not factory firmware; their
byte counts, hashes and unit bindings are in `hardware/backups/manifest.json`.
All binaries remain ignored locally.

Boot and game volume were saved at zero on both units. Set game volume to 3 for
the pending speaker/Doom test. The generic `TOUCH_READY controller=gt911` text is
a legacy label; driver-specific logs identify A's ST7121 and B's ST7123.

Recovery: with USB-C connected, hold reset about two seconds until the green LED
flashes rapidly, then release. The guarded installer identifies the selected unit
by its stored hash and uses the P4 watchdog reset to leave download mode.

## Sources

- [M5Stack Tab5 manual](https://docs.m5stack.com/en/core/Tab5)
- [Official C145 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf)
- [Pinned Espressif Tab5 BSP](https://github.com/espressif/esp-bsp/tree/73ee07b1ad56f865f13a2739be8bb6808c1da58a/bsp/m5stack_tab5)

`hardware/boards/m5stack-tab5-port-spec.json` records source hashes and wiring;
`third_party/tab5-bsp.json` pins the Apache-2.0 vendor tables and source references.
Schematic pages 1 and 4 show the native full-speed USB device pair at GPIO24/25
and the dedicated high-speed pair routed to USB-A. The initial port uses the
native USB Serial/JTAG console and does not enable USB-A power.
