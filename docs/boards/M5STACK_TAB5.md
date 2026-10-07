# M5Stack Tab5 Console OS port

M5Stack Tab5 is the permanent primary target. `make console-os-idf` aliases
`make console-os-tab5-idf`; Elecrow now requires `make console-os-elecrow-idf`.

The curated-library successor is **0.59**. It features Blast Circuit, retains
Tide Maze, and excludes Byte Buddy, Red Dragon and Skyline Leap from standard
bundles. Explicit developer installs use a separate build output. See the
[0.59 curation and installation record](../../test-runs/2026-10-06-tab5-curated-library.json)
for exact A/B results and [developer installs](../../games/README.md#developer-installs).

The underlying arena successor is **0.58**, based on the native 0.55 launch and
merged multiplayer reliability fixes. A/ST7121 and B/ST7123 passed device-checksum
application verification, launcher startup and the 10-second health gate in
[the 0.58 installation record](../../test-runs/2026-10-06-tab5-no-reboot-content.json).
The [Game Changers AI mode](../GAME_CHANGERS_AI_DOOM.md) uses the five-map
Pure Hades v0.6 replacement plus optional DWANGO 5. All consoles need protocol-5
firmware and the 16-file content bundle. The existing Red Dragon payload is
unchanged; physical multiplayer gameplay and device cadence remain to be tested.

USB content batches keep one connection open and return to idle between files
without rebooting. Both units completed all 16 files on one connection each,
with no mid-batch restart. Routine app installs use device checksums; full app
readback
is optional for recovery/diagnostics. The prior 0.57 content batch was stopped
when the operator reported its per-file restarts; 0.58 corrects that behavior.

The earlier Tab5 presentation release was **0.52**. Both A/ST7121 and B/ST7123
have exact 4,508,176-byte application readback, launcher boot, post-service
health, and eleven verified presentation-cartridge uploads each in
[the installation evidence](../../hardware/evidence/tab5-0.52-game-presentation-testing.json).
Both subsequently register the OS-paired Red Dragon 1.8.3 payload successfully.
The source catalog has seventeen icon-bearing native entries (fourteen games
and three utilities), plus dedicated original Doom/Chex covers and branded
loading screens. Invaders and Air Hockey have explicit opening screens;
Hockey pauses offline only, preserving linked-session flow. Card/board games
retain direct-touch play. Native/fallback host checks and simulator play are
recorded; no active physical-game timing interval was captured, so the 30 FPS
device floor and physical art/touch/audio acceptance remain unqualified.
Chex stays optional, dragon games remain WIP, and Wacky Wheels stays separate.

The preceding combined Tab5 release was **0.51**. A/ST7121 and B/ST7123 both have
exact 4,015,392-byte application readback, versioned launcher and post-service
health evidence in `hardware/evidence/tab5-0.51-combined-testing.json`. A migrated
to startup/game level 3; B preserved its later saved startup 8/game 10 as required
by the one-time policy. Both use three display buffers and initialize the scoped
charger; catalogs contain 17 valid cartridges on A and 19 on B. The release
preserves the 0.50 boot/game-performance work and earlier OS features; physical
interaction and wireless gameplay acceptance remain separate. Historical release
records below retain their original version-specific observations.
The initial boot exposed an existing Red Dragon payload mismatch. Its exact
0.51-paired cartridge was subsequently installed on both units and accepted by
the unchanged launch/save/multiplayer protection policy; registration logs and
transfer hashes are linked from the combined evidence. Saved data was untouched.

A (ST7121) and B (ST7123) have exact app readback, mounted SD, launcher and
runtime-health evidence. The operator confirmed display/touch and A startup
sound plus Doom music/effects on the speaker-repair image. The new sensor
service communicates with INA226, BMI270 and RX8130CE on both units; pack
voltage limitations are recorded separately. Both clocks now have verified UTC
sync and warm-restart retention. A boot is not
full gameplay, sound-quality or sustained-scroll acceptance.

Both units are ESP32-P4 v1.3 with 16 MiB flash and 32 MiB PSRAM.
Global `flash_authorized` stays false; only hash-bound exact-unit installs apply.
The preceding GameChangersAI OS 0.42 install introduced the native 1152×720 interface,
refreshed boot mark and original one-second treasure-discovery fanfare. The
rejected modem noise and ATDT/dialing are absent from the Tab5 startup.
Exact app-only installs are bound by
`hardware/evidence/tab5-gamechangers-os-0.42-treasure-authorization.json`;
readback and startup health passed on both panels, recorded in
`hardware/evidence/tab5-gamechangers-os-0.42-treasure-testing.json`.
A reached its fully populated desktop at 6.323 s; B at 5.251 s. These are
single USB-reset log measurements, essentially matching the short-modem build.
A completed the fanfare at saved volume 6; B retains mute. Full game validation
remains before desktop presentation, with the decorative hold/wipe removed.
The proposed 3–4 s interactive path is not yet implemented. Physical speaker,
touch and sustained-scrolling acceptance remains pending. Existing Control
Panel, RTC sync and SD content are preserved.
Earlier clock and library evidence remains in
`hardware/evidence/tab5-console-os-20261004-clock-testing.json` and
`hardware/evidence/tab5-console-os-20261004-control-panel-testing.json`.
The USB-A controller candidate below is a separate feature configuration and
has matching 0.43 A/B authorization. The installer verifies that its
`usb_host_enabled` selection (false for earlier authorizations) matches the image.

## Build

Use the existing locked ESP-IDF 5.5.3 installation and its Python environment:

```sh
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.11_env"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
make console-os-tab5-idf
```

For a display/audio successor that preserves the installed native-USB-only
configuration, use `P4_TAB5_USB_HOST=0 make console-os-tab5-idf`. The wrapper
regenerates its SDK configuration when switching between host-off and the
default host-on candidate. The exact-artifact authorization must match this
selection; selecting a build does not grant installation or peripheral approval.

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

## 0.43 status indicators (installed on A/B)

Both recorded units now run **0.43**, with exact readback, clean launcher boot,
and the ten-second runtime health gate verified on A/ST7121 and B/ST7123.
The top bar shows **SD FREE** and the percentage of usable FAT space available
beside the battery; BBS uses **SD n% FREE**. This is persistent game storage,
not RAM or the reserved firmware/OTA slots. With no mounted SD or an invalid
space query, it shows --; this does not implement the planned no-SD game volume.

The storage service obtains a free-cluster count during background storage
initialization and refreshes the cached FAT allocation count every five seconds
while app-owned. Mount failure, USB/other ownership and maintenance hide the
estimate. Remount resets it. No formatting or game-data deletion is involved.
Header changes invalidate the native scroll cache as well as the page redraw.

Battery percentage/validity changes now redraw all shell pages, including
Colors, Touch, Achievements and Saves. A failed INA226 initialization is retried
every five seconds without allocating another I2C handle. Invalid samples still
show no percentage, and Battery details expose the measured millivolts.

The captured 0.42 A/B logs report about **1.14/1.19 V and 0 mA**, outside the
valid pack range despite successful INA226 communication. These logs do not
prove an attached healthy pack or a frozen UI. The owner's installed pack and
battery-powered operation still need confirmation; an update alone is not a
verified repair for that voltage.

[Install evidence](../../hardware/evidence/tab5-console-os-0.43-memory-fix-testing.json)
records the complete current Core OS image, source hashes, sequential app-only
writes and serial results. Existing SD catalogs remain at 16 valid packages on
A and 18 on B. USB host, generic HID and wired XUSB services start on both;
specific-controller gameplay and physical UI acceptance remain pending.

The initial 0.43 image failed on A before app_main because its internal/DMA
reserve could not be allocated. B was untouched until a corrected image passed
on A. The correction places 13,580 bytes of CPU-only HID parser/descriptor/report
storage in PSRAM, preserving internal lifecycle state and the 64 KiB DMA reserve.
The verifier now rejects incorrect buffer placement. The rejected image is
archived separately; the downloadable 0.43 package is the corrected image
SHA-256 b93336fb04b8d66bf2fe38d121cef29ae1c7563887516344523df385bdd2ba43.
Final battery readings remain invalid (A 1136 mV, B 1194 mV); this install does
not establish battery-pack operation.

## Core implementation

| Service | Tab5 implementation |
| --- | --- |
| Display | Official ILI9881C, ST7123 and ST7121 initialization tables; 720×1280 scanout, clockwise landscape 1280×720; RGB565 double buffers |
| UI/games | GameChangersAI OS 0.43: native 1152×720 UI/boot with antialiased text; 768×480 high-res games and 320×200 games retain their existing modes; centered viewport with 64-pixel side margins |
| Touch | Panel-matched GT911 at 0x14 or ST712x at 0x55; up to five contacts, matching rotation, invalid frames release input |
| Audio | ES8388 at 0x10, I2S1 stereo 16 kHz; MCLK30/BCLK27/LRCLK29/DOUT26; speaker enable through expander 0x43 P1 |
| Sensors | INA226 2S voltage/current estimate, BMI270 acceleration/rotation/die temperature, RX8130CE clock with invalid-time detection; shared I2C1 |
| Storage | Four-bit SDMMC slot 0, LDO4 supply, CLK43/CMD44/D0–3=39–42; no automatic formatting |
| Shared control | One persistent board-owned I2C bus on SDA31/SCL32; touch/audio clients borrow it across launcher/Doom handoffs |
| Game/runtime features | Shared game catalog, native cartridges, saves, themes, BBS/Windows launcher, Doom and update service |

Display detection follows the pinned official BSP: 0x55 firmware byte 1 selects
ST7121, byte 3 selects ST7123; otherwise 0x14 selects ILI9881C/GT911. An unknown
0x55 firmware value fails closed. LCD reset is expander 0x43 P4: assert low,
then release as input with pull-up, never drive high. Touch reset is P5.
Only the ILI/GT911 assembly gets the official GPIO23-low resistor workaround.
After an observed initial expander transaction failure on A, board setup now
allows at most three constructor attempts, with an SDK I2C bus reset and 50 ms
delay between attempts. A failed SDK bus clear now deletes/recreates the
controller only while no other clients exist. Recovery runs only before touch/audio borrow the bus;
initialization still fails closed if the bounded attempts do not succeed.
Both units passed three USB warm restarts on the successor image. B reproduced
the initial failure in cycle 1, recovered on attempt 2, and passed the launcher
and ten-second health gates. These checks do not qualify cold power cycles.

The ST712x adapter bounds report slots to ten before reading at most 70 bytes,
then publishes at most five valid contacts. It uses the official driver's
initialization/teardown but bypasses its 1.0.2 `read_data` callback, whose register
count is not bounded before a fixed stack-array read. GT911 repeated reports
retain their original timestamp; a failed poll clears cached input before reuse.
The existing exact-unit Waveshare GT911 restoration is unavailable on Tab5.

Audio keeps the amplifier off during initialization, writes a complete DMA ring
of zeros before enabling it, and checks the live expander direction, latch and high-impedance registers.
PI4IOE5V6408 input status always reads low for output pins; using it for speaker
verification was the cause of the silent boot/Doom path. Readback proves control
state, not electrical voltage. User steps 0–10 scale
PCM linearly; codec output is fixed at 60/100 for this candidate. Factory
GPIO30/PDM telemetry fields remain zero: GPIO30 is Tab5 MCLK, not amplifier
shutdown. Audio cleanup retains ownership on failure for retry. Acoustic quality,
headphone routing, volume and pop-free transitions still need measurement.
Doom now preserves Console OS volume 0 as mute and rejects invalid volume values
by remaining silent. The standalone default of 8/10 no longer replaces console
mute. Volume is an additional constraint on the unchanged hardware gates.

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

Arena random WAD reads use the pinned FatFs fast-seek cluster map. The earlier
64-word default covered only 31 fragments; on unit B in the 0.62 Arena test,
`fast-seek not activated reason code: 17` meant the file needed a larger map,
so reads fell back to walking the FAT. This was not a failed heap allocation.
Tab5 now reserves 112,456 words (449,824 bytes) per read-only file, preferring
PSRAM. This is `2 + 2 * ceil(28,787,748 / 512)` words: enough for the largest
pinned Arena WAD even when every 512-byte cluster is a separate fragment.
Existing cards need not use the mount configuration's 16 KiB formatting hint.
Three open WADs cost about 1.29 MiB, and all eight storage handles bound map
allocation at 3,598,592 bytes (3.43 MiB). Closing a file frees its map. The SDK
can fall back to internal memory if PSRAM allocation fails; this option does
not remove that SDK behavior. Larger future files need the bound reviewed;
insufficient maps retain normal seeking. This sizing change still requires
on-device timing verification and does not establish gameplay acceptance.

## Unified Control Panel

Tab5 has one Control Panel rather than individual system icons in All Programs.
Overview combines battery, storage, motion and clock state. Preferences exposes
saved startup/game volume and Appearance; Controls, Storage and Connections
group everyday actions. Advanced holds detailed diagnostics and utilities.
Touch and normalized controller input use the same action path; Back returns to
the same group, including after an external utility. Existing confirmations for
file deletion and SD repair stay in force.

## Battery, motion and clock

The Battery page uses INA226 bus/shunt readings over the schematic's 5 mOhm
resistor. Its 0..100% is the M5Unified 6.6..8.2 V estimate for a 2S pack, not
measured remaining capacity. Positive current means discharge; negative means
charge. Invalid/absent pack voltage displays no percentage and asks to check
the battery pack, rather than showing a fabricated level. A responding INA226
alone does not establish that a battery is attached.

Sensors initializes Bosch's pinned 8192-byte BMI270 configuration, verifies
identity and settings, and publishes acceleration, rotation and die temperature
four times per second. It preserves invalid RTC status until an explicit,
verified clock setting. USB UTC sync corrected A's historical date and B's
invalid clock. Both units retained the time through a captured warm restart;
subsequent read-only samples advanced and stayed within one second of the Mac.
Full power-off retention, long-term clock accuracy and battery capacity
calibration remain follow-ups. Motion data do not rotate the screen or alter
touch calibration.

The service owns device handles on the existing shared bus and initializes
asynchronously after the launcher. No game owns these devices. Telemetry has a
two-second freshness limit; I2C errors hide stale values. Sources and settings
are pinned in `third_party/tab5-sensors.json`; Bosch's BSD-3-Clause notice is
preserved in `third_party/bmi270/LICENSE`.

Doom quit now suppresses process-exiting ENDOOM and defers audio/WAD/display
cleanup until the engine tick has returned. A fresh quit-to-launcher check is
still needed for this successor; old speaker-image evidence showed the game
returning but retaining its WAD handle during cleanup.

## Historical library/sensor candidate exclusions (before 0.45)

- C6 Wi-Fi/Bluetooth, wireless multiplayer and BLE controllers/dice.
- USB Drive/MSC mode. USB-A controller support is a build-tested candidate below.
- Charger/rail management, microphone, camera and expansion ports.
- USB-A power/HID on the installed library/sensor configuration; the separate
  controller candidate below enables it. Battery telemetry, IMU and RTC reads
  are now implemented.
- Battery capacity calibration; telemetry, sensor reads and explicit USB UTC
  clock setting are implemented.

The multiplayer core is shared; native USB supplies its wired relay channel.
Physical multiplayer acceptance remains pending. Content transfer uses the
existing USB-C Serial/JTAG cable while Console OS owns the mounted SD card.
It does not expose a writable disk to the Mac. Do not remove a card while the
console is running or saving. Firmware binaries, WADs, generated SD content and
pre-install backups remain local and ignored by Git.


## Set the device clock

With the launcher running, use the existing transfer tool and an explicit port:

```sh
python scripts/p4-transfer.py clock --port /dev/cu.usbmodem1101
python scripts/p4-transfer.py clock --sync --port /dev/cu.usbmodem1101
```

The first command only reads status. `--sync` uses the Mac's current UTC time,
queues work on the board-owned sensor task, and reports success only after the
RTC calendar reads back within two seconds of the Mac. The UI labels the clock
as UTC. No time is invented at boot; an invalid clock remains visibly invalid
until a successful setting. Use a fresh explicit sync after a reported write
failure. Native-USB warm-restart retention is verified on A and B; full
power-loss retention is separate acceptance. Read-only status waits for the
asynchronous sensor probe before reporting presence or a real probe error.

## USB-A controller candidate

The default Tab5 build now enables `CONFIG_P4_TAB5_USB_HOST` and the shared
native USB Host/HID and wired XUSB services. Connect one compatible wired gamepad directly to the
USB-A socket. The launcher, native cartridges and Doom consume the
same normalized OS input. The Controllers panel supports button remapping
without Bluetooth. USB-C remains the independent Serial/JTAG programming,
content-transfer and wired multiplayer relay connection.

This is **host-tested and build-tested, not hardware-tested**. The isolated
candidate and exact source hashes are recorded in
`hardware/evidence/tab5-usb-gamepad-20261004-build.json`. The later candidate
`hardware/evidence/tab5-usb-gamepad-20261004-doom-input-build.json` also fixes
combined touch/controller input in Doom: an action stays held until both
sources release it. The next candidate,
`hardware/evidence/tab5-usb-gamepad-20261004-hid-collections-build.json`,
fixes valid secondary HID reports causing disconnects and preserves bit offsets
across application collections. Its isolated image predates the primary
checkout's newer native-resolution UI work.
The next XUSB candidate is recorded in
`hardware/evidence/tab5-usb-gamepad-20261004-xusb-build.json`. It adds the wired
Xbox 360-format transport (`ff/5d/01`) through the shared input broker, with
configuration/report bounds, canonical controls and disconnect-safe cleanup.
Its physical controller acceptance is also pending. Existing A/B install
authorizations bind older artifacts and do not authorize this image. The
integrated build preserves the current audio, sensor and game-library changes;
prepare a new exact-artifact authorization before a successor install.

The official C145 schematic routes the native HS pair to USB-A J10. Its
`SYS_USB5V` rail is supplied through U27/MT9700, controlled by
`USB5V_EN` on the second PI4IOE5V6408 at 0x44/P3. The pinned Espressif BSP uses
that same active-high output. The board service performs masked, serialized
register writes and readback; it never resets the second expander or changes
its charging, power-off or C6 pins. Host startup first switches USB-A power
off, installs Host/HID, then enables the root port and USB-A power. Failed
enable attempts roll back; an unverified rollback leaves the host faulted.
Shutdown switches USB-A power off before releasing the host.

The candidate supports the existing bounded HID parser and profiles. It does
not add Xbox One/Series GIP, wireless receivers, rumble, multiple simultaneous pads, or
hub support. Those need their own implementation and evidence. Do not infer
controller compatibility from its brand or from this build result.

Before recording support for a controller, use the controller skill's
`references/acceptance.md`: record the exact Tab5 unit/panel, controller model,
VID/PID, descriptor hash and image hash. Capture `USB_A_POWER`,
`USB_INPUT_READY` and `GAMEPAD_CONNECTED`; exercise launcher navigation,
button remapping, a native game and Doom. Unplug with controls held,
verify immediate neutral input, reconnect repeatedly, and check cold-plug and
reboot behavior. Keep USB-C content transfer working during the run. Until then,
controller compatibility and electrical behavior on A/B remain pending.

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
before flashing. `push-bundle` currently installs 17 native `.P4G` files (14 games and three
utilities; Skyline Leap is disabled) and the Byte Buddy `.P4R` sidecar through one open connection.
Game content is native-only; the old Lua runtime, games, tools and skill are
removed. See the [game library](../GAME_LIBRARY.md) for alphabetical categories
and draft authoring rules, and the [content contract](../CONTENT_LIBRARY.md)
for supported package formats.
Doom/Chex data are separate exact-hash local inputs. The device validates each
format, stages writes, verifies the digest, activates atomically and reads back.
Successful native installation refreshes the game catalog. Individual transfers
use `push --class p4g` for cartridges or `--class p4r` for resources;
`exchange` remains isolated in `/TRANSFER` and is not a game-install bypass.
Opening native Serial/JTAG can reset the board on this Mac; tools wait for the
service and tolerate that startup window. Avoid repeatedly reopening monitors
during a play test. Content installation restarts the launcher after activation.

Remove a requested game through the same cable:

```sh
python scripts/p4-transfer.py remove /absolute/path/EXACT.P4G --port /dev/cu.usbmodem1101
```

The exact local size and SHA-256 must match the device file. `.P4R` resources
must be supplied explicitly and are removed before `.P4G`. Saves and WADs
are outside this command. Unknown temporary upload
files cause refusal, and are never removed as a side effect. A subsequent
catalog refresh applies the new library without an OS reflash.

Tab5 P4U packages use the distinct target tag `esp32p4-tab5`. The Tab5 runtime
rejects legacy `esp32p4` packages, and existing runtimes reject Tab5 packages.
This prevents accidental cross-board updates; it is not a cryptographic signature.

## Verification and first hardware session

`make tab5-host` covers existing-board regressions plus Tab5 identity, all three
surface sizes, pixel rotation/scaling, untouched destination padding, touch
coordinate bounds, BBS navigation, storage policy, malformed device counts and
retryable touch cleanup, and cross-board update rejection. Driver tests use mocks. `make board-port-check` checks
the shared 20-feature software contract with hardware acceptance still false.
`scripts/verify-console-os-tab5.py` checks the built artifact and the enabled native cartridges. It validates host-on and host-off builds
against their selected components, source adapters and linked entry points.

The E5 gate test checks the closed generic flash policy and immutable
historical evidence. It does not require enabling the older authorization.
The broader `make check` also builds Elecrow diagnostic applications; it is
not a prerequisite for a focused Tab5 change. Select the host checks for the
changed boundary and build Tab5 once when firmware inputs change.

Before the first write, identify the physical Tab5 and its panel sticker; read
its live chip revision and flash size; preserve its complete factory flash;
record the backup byte count, SHA-256 and hashed live-device binding in a manifest.
Then review the exact Tab5 artifact and installation authorization. No Elecrow,
Olimex or Waveshare unit identity/authorization applies to this device.

On-device acceptance must record the image SHA-256, board/panel identity and serial
log. Check boot/backlight/colors/orientation, all touch corners and releases,
card mount/save persistence, boot audio/volume/mute, native games, Doom,
return to launcher, and repeated cleanup/reinitialization. Qualify the USB-A
controller and wireless paths separately. The 0.45 successor below enables the scoped
C6 radio and charger; other power-management controls remain excluded.

## Exact-unit install and current testing state

Use `scripts/flash-console-os-tab5.py` with the pinned IDF Python environment.
Its default mode checks local inputs only. `--install` requires an explicit port,
unit A/B, authorization file and its SHA-256. It stages immutable image bytes,
checks the full recovery snapshot, verifies live identity/revision/flash/security,
and uses the same open connection for predecessor comparison, write and device
checksum verification. Full app readback is optional with
`--verification full-readback` for recovery or diagnostics. A failed check leaves the unit unmodified or in the
loader after a write failure. The app-only route also verifies the bootloader, partitions and
CRC-valid active OTA slot; it preserves the existing OTA selector.

Both units' current artifact, install receipts, SD sizes, upload hashes and
acceptance limits are recorded in the current evidence JSON. Full pre-install
snapshots contain the existing USB bridge firmware, not factory firmware; their
byte counts, hashes and unit bindings are in `hardware/backups/manifest.json`.
All binaries remain ignored locally.

A startup/game volume 4 was operator-confirmed audible. B was last recorded
muted; use Control Panel > Preferences to select a nonzero game volume for its speaker test. The generic `TOUCH_READY controller=gt911` text is
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
and the dedicated high-speed pair routed to USB-A. Page 5 shows the switched
USB-A supply. The controller candidate adds this USB-A path while retaining
native USB Serial/JTAG on USB-C.

## 2026-10-05 charger-control build candidate

The next-interface battery investigation found missing IP2326 enable initialization. A masked 0x44 expander sequence now selects 500 mA with QC disabled and verifies controls before and after enabling. It preserves USB/radio/shutdown bits and retries transient initialization failures. The focused Tab5 suite passed 9/9 and the USB-host-enabled firmware build completed. This successor is **not installed or hardware-qualified**; previous installed-image statements above remain historical. Live battery readings were about 1.2 V with pack presence unconfirmed. See [battery findings](../../design/tab5-nextgen/BATTERY.md) and [exact build evidence](../../hardware/evidence/tab5-charger-500ma-build.json). Charger control is outside the older exact-artifact install scopes.

## Native next-generation interface — OS 0.44

The Tab5 OS now renders directly at 1280×720 with high-contrast navy/cyan styling,
native text and touch targets, generated game illustrations, and a consistent
navigation rail across launcher and system screens. The Tab5 BBS renderer is
disabled; 320×200 and 768×480 games retain their centered 1152×720 viewport. The
display adapter clears the side bands when returning from full-width OS rendering
to a centered game.

See [implementation and current limitations](../../design/tab5-nextgen/IMPLEMENTATION.md),
[actual C-rendered screen gallery](../../design/tab5-nextgen/native/index.html),
and [exact install evidence](../../hardware/evidence/tab5-nextgen-0.44-testing.json).
The artwork encoder retains hero resolution while fitting the existing update
package size bound. No update size, partition or SDK checks were weakened.

The earlier charger candidate is now behind CONFIG_P4_TAB5_CHARGER_500MA, default
off. Its disabled-path host test confirms zero charger calls, and firmware
verification checks that charger initialization is absent from the linked image.
The guarded flash tool also binds this feature selection to the authorization.
The UI upgrade does not qualify or enable battery charging.

The guarded 0.44 installation completed on A (ST7121) and B (ST7123): exact
app readback, launcher readiness and the ten-second boot-health gate passed.
Measured desktop readiness was 6607 ms on A with startup volume 6 and 5320 ms
on B with its saved startup mute. These are boot-log measurements, not a
universal cold-boot guarantee. A Texas Hold’em launch and clean launcher return
were observed; its frame deadline misses are recorded and do not qualify gameplay
performance. Physical readability, touch and speaker acceptance remain pending.
Both battery readings remain invalid, around 1.15–1.19 V.

## Console OS 0.45 core successor candidate

This successor builds on the installed 0.44 native 1280x720 interface. It adds
shared Bluetooth and standalone local Wi-Fi Host/Join transport, plus the
explicitly requested 500 mA non-QC charger control. See `docs/MULTIPLAYER.md`
and the exact 0.45 evidence for actual device results; host tests and a build
alone do not establish charging or wireless operation. Radio uses the reviewed
C6 SDIO slot 1/P0 rail; microSD remains slot 0. Their teardown paths are isolated.

Cartridge titles and optional `.p4icon` artwork are loaded from validated `.P4G`
files. Game remixes can update their launcher presentation without an OS build;
see `docs/GAME_SDK.md`. Existing 0.44 fallback artwork remains for old cartridges.

The Tab5 update envelope now uses its actual 0x7f0000-byte OTA slot limit; legacy
board targets retain 0x370000. The installed 0.44 parser had the smaller bound,
so this first larger successor uses the guarded native USB app-only install.
Firmware-only builds use `P4_TAB5_FIRMWARE_ONLY=1 make console-os-tab5-idf` to
avoid replacing content under active remix work. They verify the OS and update
package but do not qualify a new game bundle.

The guarded 0.45 app-only install now passed exact readback and boot health on
A/ST7121 and B/ST7123. Both emitted charger configuration readback success and
recovered valid pack readings: A 6.814–6.931 V at about -480 mA, B 6.641–6.840 V
at about -502 mA after startup. Negative current means charge on Tab5's pinned
shunt wiring. These are observed charge-current/voltage results, not full-charge,
capacity or unplugged-runtime qualification. The user confirmed attached packs.
Wireless pairing and two-tablet game acceptance remain pending. Exact artifacts,
receipts and limits: `hardware/evidence/tab5-0.45-core-testing.json`.

## OS 0.46 navigation successor

The nextgen Games library now has continuous touch scrolling, a draggable
scrollbar and controller navigation through off-screen rows. Category filters
come from installed cartridge folder metadata. Files has the same bounded touch
scrolling. A small `...` menu holds Delete/Remove; the separate confirmation names
the selected file and defaults to Cancel. Open on an ordinary file no longer
enters the delete path. Listing changes invalidate pending actions.

This core-only update retains 0.45 charging and radio controls and existing game
content. `hardware/evidence/tab5-0.46-ui-testing.json` records exact install and
test results; native previews use synthetic fixtures. The game artwork remix is
independent and does not require an OS reflash when cartridge icons are ready.

0.46 is now installed on A/ST7121 and B/ST7123. Both passed full app readback,
launcher boot and the stability health gate, retained Wacky Wheels in the catalog,
and resumed charge current (about -484 mA / -507 mA respectively). Touch feel and
physical navigation acceptance await operator feedback; no files were deleted to
test the UI. The host tests exercise action emission against synthetic listings.

## 0.47 scrolling successor

The operator reported that 0.46 scrolling was chunky. The native 0.44-style
interface now reuses the old OS's persistent-raster approach: translate cached
viewport rows and repaint the exposed strip. Catalog/category, selection,
file revision, visible status, font size, pointer and modal changes force an
authoritative redraw. Files retain the small actions menu and named confirmation.

Swipes track the finger and finish with bounded, frame-rate-independent momentum.
A new touch stops motion without launching; invalid input, stale samples, long
stalls, changed folders and Reduce motion cancel it. Native UI damage is passed
to the Tab5 PPA rotation path, replaying the previous generation into alternating
buffers. The two-refresh scanout reuse fence remains. Errors and game/pattern
transitions discard display history and force full reconstruction.

Host tests compare 240 cached frames to complete redraws, exercise motion and
stop-touch safety, and compare alternating rotated partial frames to full frames.
The first host run reduced renderer time from 2628.69 ms to 199.31 ms (13.19x);
this is a host renderer measurement, not device FPS. Each physical swipe emits
one `P4_CONSOLE_OS SCROLL` timing summary after it stops. Exact installation and
physical smoothness evidence belongs in `hardware/evidence/tab5-0.47-scroll-testing.json`.

0.47 was installed on both A/ST7121 and B/ST7123 with exact application readback,
version markers and the post-service runtime health gate verified. No PPA,
submit-timeout or frame-ack errors appeared in the 90 s A / 60 s B captures.
No physical swipe occurred during those captures; device scrolling FPS and
operator smoothness acceptance remain pending. Charging current remained
negative on both units. The installation evidence above contains the receipts.

## Audio/status and loading carry-forward (0.50–0.51)

The status bar shows the game master volume as `Game N/10`, or `Muted` at zero.
Its separate touch/controller target opens Sound settings; both volume controls
there use the same 0–10 level notation. Changes invalidate cached UI frames, so
scrolling cannot preserve an old volume label. This is the OS level consumed by
native games and Doom, not a per-game override.

Tab5's default startup and game levels are both 3. Volume policy 4 applies that
baseline once to existing devices, including previously muted devices, and then
preserves later user adjustments. Other board policies remain unchanged. NVS
failures return level-3 defaults without erasing user settings or pretending a
write succeeded.

The legacy loading marquee uses elapsed time independently of five-step callers,
reaches both edges, and fills at `Ready`. Tab5's newer native boot uses the 0.50
perspective joystick flight, stationary status and activity dots. Its worker
finishes before Home takes the framebuffer, without an artificial completion
hold. Both renderers retain explicit completion and incremental-repaint checks.
Host coverage also includes one-time settings migration, saved edits, NVS failure
behavior, volume navigation and pixel-exact scrolling.

0.51 carries forward the complete 0.50-spans boot renderer, reviewed cartridge
timing/audio, shared drawing optimizations, triple display buffering and
per-buffer damage replay, plus the earlier category/file safety and volume work.
The firmware release does not replace independently installed game cartridges.
The selected OS component source inventory, build hashes, validation and per-unit
install results are recorded in `hardware/evidence/tab5-0.51-combined-testing.json`.
Physical game smoothness, tilt feel and two-device radio play remain separate
acceptance checks; a firmware build or boot does not qualify them.

## 0.50 motion-cartridge candidate

The existing BMI270 background service now targets 50 motion samples/second
(20 ms delay between sampling passes); its chip configuration remains pinned at
100 Hz, +/-4 g and +/-2000 dps. Power and clock reads remain approximately once
per second. A separate IMU timestamp and sequence feed a copied optional native
Game API motion snapshot with a 150 ms freshness limit. The game thread performs
no I2C operations. Existing source axes and display/touch calibration are retained.
This supersedes the four-Hz motion publication described in the historical sensor
section. Physical cadence, comfortable tilt direction and additional shared-bus
load remain hardware acceptance work.

Tide Maze adds a tilt marble labyrinth with bounded shallow-water physics and
host-authoritative two-console co-op. It uses the existing OS Multiplayer flow;
no transport qualification is inherited. See `games/tide_maze/LOCAL_TESTING.json`
for local/build evidence. This candidate was not installed by the game-authoring
task; an exact-artifact, exact-unit guarded OS update is needed before loading
a cartridge with the new motion capability on older firmware.

## 0.60 renderer successor

The native launcher keeps three physical framebuffers and the two-refresh
retirement fence before reusing a previous scanout buffer. PPA rotation completes
before its logical source can change. Each physical buffer replays its missing
damage, then publishes the affected rotated row span through the owned-framebuffer
DPI path. CPU fallback and margin writes publish the full frame. The redundant
application-wide cache invalidate/writeback after PPA is removed; the pinned
driver supplies the required cache operations. C2M writeback is chunked at 32 KiB
to limit uninterrupted cache-maintenance work. Panel timing remains pinned.

Games and Files can use separate prepared raster windows in one optional,
64-byte-aligned PSRAM arena capped at 4 MiB. Their content signatures and readiness
are tracked independently. Idle preparation paints at most 16 rows per command;
incomplete or stale windows are never copied. A ready source stays immutable
through a joined, nonoverlapping PPA copy into the writable logical raster.
Destination row writeback preserves nearby CPU-painted pixels before DMA, and
selection/focus overlays follow the copy. The portable CPU renderer remains the
fallback. Dragging allocates no cache memory, and the arena is released only after
work joins, before game or external-page handoff.

Touch sampling remains independent on core 0 at priority 2. A core-1 worker at
priority 1 owns rendering, cache preparation and display submission for each
joined command; the caller resumes shell/raster access only when ownership
returns. New touch samples wake UI processing and take precedence over idle cache
preparation. A join timeout retains the surfaces instead of freeing in-flight
memory.

Timing reports separate successful animation submission intervals from general
dirty-frame/service cadence and actual display refresh callback intervals. Scroll
summaries include render/submit cost, animation gaps, sampled-input-to-submit and
drag-onset-to-submit timings. Input-to-refresh measurements wait for two callbacks
after publication: they are conservative attribution bounds because the callback
does not identify which framebuffer became visible, not exact visible-frame
acknowledgments. Callback cadence must be measured rather than assuming 60 Hz.

Focused host coverage checks rotation/damage replay, retirement, copy bounds and
dirty-neighbour cache preservation. On the separately backed-up owner Tab5/ST7121,
0.60 passed device checksum, launcher and boot health verification. A continuous
31-frame swipe measured 49.576 ms between successful submissions (20.17 Hz),
26.544 ms rendering and 21.577 ms submission. Refresh callbacks measured about
57.25 Hz. The owner reported improved onset but scrolling remained unsmooth;
the scrolling target is not met. See the
[exact artifact and swipe record](../../hardware/evidence/owner-tab5-0.60-renderer-testing-20261007.json).
These are submission timings, not optical FPS. Earlier acceptance records above
remain historical evidence for their stated versions.

## 0.61 prepared-copy measurement

The owner unit installed 0.61 with an application-only write at `0x20000`,
verified by the device checksum and launcher/OTA boot health. The prepared
DMA2D copy backend initialized; idle source-publication failures and CPU-copy
fallbacks were zero during the captured gestures. Warm scrolling still took
about 23 ms to render and 21 ms to submit. The copy hook alone took about
18.7 ms, including per-row destination cache preservation/invalidation and
DMA completion. This metric does not isolate DMA transfer throughput.

The owner reported no perceptible onset or smoothness improvement over 0.60,
and no glitches after launching and exiting a game. Preserve that failed
smoothness acceptance in the
[exact 0.61 artifact and swipe record](../../hardware/evidence/owner-tab5-0.61-renderer-testing-20261007.json).
The next candidate shifts a released physical framebuffer from the selected
immutable frame and rotates only newly exposed content, with complete redraw
when stationary state or buffer history cannot be proven equivalent.

## 0.62 physical scroll composition candidate

During steady Games/Files motion, the native renderer can leave its logical
viewport interior incomplete and return a scroll descriptor. It paints only
the newly exposed strip and the scrollbar. The display service copies the
shifted interior from its immutable selected framebuffer into a distinct
retired buffer, then rotates the exposed strip and scrollbar into that buffer.
The selected source remains immutable until the DMA completion joins; the
destination must satisfy the existing two-refresh retirement fence.

Each buffer carries its exact logical source/stride, stationary context, list
offset and viewport geometry. The context includes chrome, focus/selection,
content revisions and file endpoint controls. Both physical buffers must match
the current context; the selected offset must match the descriptor's previous
offset. Unmatched slots reject before mutation and keep their existing tags
while an authoritative redraw warms another slot. Other errors cannot submit
stale logical pixels. A regular renderer call reconstructs the complete source
before any ordinary presentation, and modal/page/content changes rebuild it.

The physical fast path writes only driver-owned buffers that have remained
under DMA ownership without CPU access since accelerated reconstruction. Its
cache cleanliness is tracked separately from the content tag. Logical source
strips still preserve neighbouring CPU pixels and use bounded cache operations;
idle raster publication epochs and 32 KiB C2M chunks remain unchanged.

The physical panel stride is 1440 bytes, so its private DMA copy uses picture
bases and pixel offsets rather than the generic logical copy's 64-byte-stride
cache simplification. Odd RGB565 scroll offsets are permitted for nonencrypted
memory by the [ESP32-P4 v1.3 TRM](https://documentation.espressif.com/esp32-p4-chip-revision-v1.3_technical_reference_manual_en.pdf),
sections 6.4.2 and 6.4.11 (pages 382 and 392). Full driver-owned allocations
remain four-byte aligned. The generic logical-copy alignment guards remain
unchanged; encrypted devices reject this physical fast path.

Timing separates cache preparation, private DMA enqueue and joined wait.
Scroll summaries retain the complete gesture cadence and onset, and separately
report optimized-frame render/submit averages and intervals between consecutive
optimized frames. Host differential tests compare composed rotated pixels with
authoritative complete redraws, including odd/even motion, Files endpoints,
cache changes, incomplete-source fallback and game/source handoff. Device
cadence and owner smoothness acceptance remain pending for this exact candidate.

The owner unit's exact 0.62 candidate passed checksum and boot health.
Consecutive optimized-frame intervals in captured Games swipes measured
17.381-19.391 ms (about 53-57 successful submissions/s), with optimized
rendering around 4 ms and submission around 11-13 ms. Complete gesture
statistics include the slower warm-up and intentional pauses. The owner
reported much smoother steady motion but a startup hop, jumping fast swipes
and missing release momentum; the complete interaction target is not yet met.
Two ordinary warm-up fallbacks took about 116 ms each. See the
[exact 0.62 image, source snapshot and gestures](../../hardware/evidence/owner-tab5-0.62-renderer-testing-20261007.json).

## 0.63 startup and release-momentum candidate

A retired target with older stationary context can now copy the exact
stationary complement from the selected immutable frame in up to four
disjoint DMA rectangles. The selected context and previous offset must still
match. This repairs an old or untagged slot without the two complete logical
redraws that caused 0.62's opening hop. An initial bounded M2C operation
establishes DMA-only cleanliness for the driver's already cleared/published
allocations; CPU writes still revoke that proof. Every copy joins before
another uses the private DMA descriptor state, and the normal retirement
fence applies to all target pixels.

The core-0 sampler retains a gesture sequence, actual report timestamps,
last down contact and recent release velocity. The joined UI path can replay
one missed final contact from the same consumed gesture before processing
release. Errors, multiple contacts, changed gestures and stale samples revoke
the hint. Movement-to-release freshness is bounded to 80 ms, and release
consumption to 150 ms to tolerate a known slow frame without inventing motion.
The shell accepts momentum only after an actual content drag crossed its
threshold. Fractional, time-based deceleration preserves smooth easing across
frame cadences; taps, motion-disabled settings and endpoint bounds remain
authoritative. Inactive press identities no longer invalidate stationary
frames because they do not change their pixels.

Host checks cover the stationary complement, first-use/stale target repair,
missed-report replay, release freshness, duplicate samples, stop-fling taps
and cadence-independent deceleration. Exact device timings and physical
acceptance remain pending for 0.63.

The exact 0.63 image passed its application checksum, but failed display
initialization because the pinned cache API forbids `M2C | UNALIGNED`.
No scrolling acceptance was requested for that unusable image. Its source,
installation receipt and failed boot are preserved in the
[0.63 failure record](../../hardware/evidence/owner-tab5-0.63-renderer-testing-20261007.json).
The 0.64 correction uses `DIR_M2C` alone after validating cache-line alignment
of the allocation and its full size. If the optional startup cleanliness proof
cannot be established, the normal PPA warm-up remains available. Bounded
32 KiB chunks and all refresh-retirement fences are unchanged. Physical
startup and release-momentum acceptance remains pending for 0.64.

The owner unit's exact 0.64 image subsequently passed checksum and boot
health. Its capture contains 396 physical delta frames, 26 stationary repairs,
133 glide frames and eight same-gesture release replays. Warm repairs took
about 7–8.5 ms; an example quick-release burst produced 13 glide frames at
17.984 ms between consecutive optimized submissions. The owner reported a
smaller opening hop, but quick flicks still jumped after a perceptible delay
and then coasted slowly. The full interaction target is not accepted. See the
[exact 0.64 image and feedback](../../hardware/evidence/owner-tab5-0.64-renderer-testing-20261007.json).

Zero unsupported-scroll fallbacks does not mean zero opening cost: ordinary
authoritative UI frames still cost about 23–25 ms of warm logical drawing and
42–44 ms of full-frame PPA. Scroll summaries count changed-offset submissions;
they exclude the initial press-only frame, and their onset timing ends at
submission start, before display transformation. They do not establish physical
finger-down-to-visible latency.

## 0.65 content-touch onset and flick candidate

The independent core-0 sampler keeps waking the joined UI path during a
bounded 24 ms ambiguity window after ordinary content DOWN. The shell retains
the displayed content focus while the touch becomes a drag; a held press
commits its highlight at the deadline, and an eligible UP tap commits before
activation. Press identity, catalog revision, drag threshold, stop-glide taps
and input ownership remain authoritative. Optional services defer through this
short window. Content cards/rows have no pressed fill, so their transient hit
state does not invalidate unchanged stationary pixels.

Ordinary authoritative region updates can clone the selected immutable
physical frame into a retired DMA-clean frame, then rotate only current damage.
This applies when source/stride match and the retired history is absent or
large. Small damage replay stays available; unsupported ownership or a joined
operation failure reconstructs from the complete authoritative logical source.
Incomplete logical interiors are reconstructed before ordinary presentation.
Copies join before PPA, source release and full-row publication, preserving the
existing refresh fence. Clone counts and timings are reported separately.

The ST7121 touch path stamps each successful acquisition, including unchanged
coordinates. Release velocity now decays linearly with elapsed stillness over
80 ms instead of halving with every identical-coordinate poll. The last actual
movement velocity is retained separately; motion estimates use a 16 ms integer
time-weighted smoother. Direction changes, sample gaps, invalid input, gesture
identity and release freshness still bound momentum. Signed accepted release
velocity is recorded with gesture timing, and no per-frame logging is added.
Host tests compare 60/120 Hz acquisition, neutral lift tails, stopped holds,
first-drag pixels, held-tap damage, glide completion, source handoff and cloned
triple-buffer composition. Exact device acceptance remains pending for 0.65.

The exact owner-unit 0.65 image passed its application checksum and boot health.
The bounded recording contains seven scroll bursts, 374 physical delta frames,
178 glide frames, 16 stationary repairs and 14 cumulative authoritative region
clones, with no recorded crash, fatal hold, display timeout or PPA failure.
First consumed motion to submission start measured 4.034–12.201 ms; an example
quick release retained about 2.01 logical pixels/ms and produced ten glide
frames. Whole physical clones took at most 14.124 ms. The captured pair
averages include pauses and slow fractional movement, and do not establish
optical FPS or physical finger-down-to-visible timing. Qualitative onset,
flick and game-return acceptance remains pending. See the
[exact 0.65 image, checks and recording](../../hardware/evidence/owner-tab5-0.65-renderer-testing-20261007.json).

The owner subsequently reported that Files did not have the speedup, so the
interaction target remains unaccepted for 0.65. Its root directory has five
entries and only 84 pixels of travel. A captured Files burst took 56.460 ms
average rendering plus 27.044 ms submission, with no optimized frames or
raster copies. The production renderer changes stationary context at list
endpoints because Prev/Next enabled states change, forcing an ordinary redraw.
The old telemetry does not record offsets or cache readiness, so the exact
endpoint sequence and cold-cache cause remain inferred; no repeated directory
scan was observed during that burst.

## 0.66 Files endpoint patch

Files endpoint transitions retain the complete stationary context hash, but
authorize an explicit Prev/Next damage rectangle at logical `(368,558,288,88)`.
The shell derives the prior endpoint context from the unchanged base signature;
only an exact match to the last rendered context permits physical translation.
It paints the exposed strip, scrollbar and the two buttons with their exact
shared renderer. Changes to selection, directory, chrome or other state retain
the authoritative fallback. Cold caches can draw the exposed strip directly
without rebuilding the entire file viewport.

The display requires the selected frame's exact previous context, offset,
source and geometry. A destination with another context gets the existing
stationary-complement repair, then joined interior DMA, strip/bar PPA and the
explicit stationary patch. A changed context without a valid disjoint patch
rejects before mutation. The new context commits only after the complete
composition is published; publication includes patch rows. Games and interior
Files motion use the unchanged no-patch path and retirement fence.

Each scroll summary now includes the first successful changed-scroll frame's
page, file count, previous/current offset and raster validity/prepared/published
rows. This distinguishes cold source preparation from endpoint rejection
without adding per-frame logging. Host tests cover five-entry and longer
directories, cold/warm caches, both endpoint directions, exact rotated pixels,
context mismatch, guarded fallbacks and normal file controls. Device acceptance
remains pending for this exact image.

The exact 0.66 owner-unit image passed application checksum, launcher startup
and OTA boot health. Three recorded Files bursts contain ten moving frames;
all ten use physical scroll translation and prepared raster copies, with no
unsupported fallback or recorded display error. Their first-frame snapshots
identify the five-entry root, departures from offsets 84 or 0, and a clean
768-row prepared/published raster. Average rendering is 4.627–5.369 ms and
submission is 12.065–15.553 ms. Consecutive optimized pairs average
22.272–25.538 ms, so this recording does not establish panel-rate scrolling.
Four stationary repairs take up to 9.822 ms. First consumed motion to submission
start is 5.059–5.565 ms; it excludes display completion and optical latency.
Owner smoothness, endpoint control and game-return acceptance remains pending.
See the [exact 0.66 image and Files recording](../../hardware/evidence/owner-tab5-0.66-renderer-testing-20261007.json).

## 0.67 previous-context repair shortcut

An explicit endpoint patch proves that every stationary difference between the
previous and new context lies inside that rectangle. A retired DMA-clean
destination with an exact previous-context tag, source, stride and geometry
already has the required stationary pixels outside the patch. Its old moving
offset does not matter: selected-frame translation and the exposed strip
replace the complete viewport, and the scrollbar and patch are still rewritten.
That case can skip the stationary-complement copy. A destination with unknown
or another context still requires repair. Refresh retirement, joined copy
ownership, cache publication and new-tag commit order remain unchanged.

A separate successful-presentation counter records previous-context reuse, so
the experiment can distinguish actual avoided repairs from other timing
changes. The exact 0.67 owner-unit image passed application checksum, launcher
startup and OTA boot health. All six display host suites and the final focused
pixel/ownership suite passed under ASan/UBSan; affected shell version fixtures,
pinned shell/display/main syntax and the firmware build passed. Its bounded
device recording contains no scrolling gestures, so actual repair skips,
current-image timing and owner smoothness acceptance remain pending. The 0.66
Files measurements above apply to that image. See the
[exact 0.67 image and startup recording](../../hardware/evidence/owner-tab5-0.67-renderer-testing-20261007.json).
