# Hardware facts and constraints

## Facts measured from the connected unit

- Chip: ESP32-P4 revision 1.3, 40 MHz crystal.
- Flash: 16 MiB.
- PSRAM: 32 MiB initialized and a 1 MiB write/read test passed. The tested build configured hex mode at 200 MHz; M0 did not independently measure the bus clock.
- Factory partitioning: 24 KiB NVS, 4 KiB PHY, 11 MiB factory app, 4 MiB SPIFFS.
- Factory app: `esp_brookesia_demo` 1.0.1, compiled with ESP-IDF 5.5.
- Factory strings: `ESP32-P4-Elecrow-Advance`, 1024×600, EK79007, GT911.
- Factory-firmware variant: 10.1 inch/DHE04310D with very-high confidence; this does not identify the physical PCB revision.
- Recovery binding: the full factory backup is tied to this physical unit by a stored SHA-256 of its normalized base identity; the raw identifier is not retained.

The M0 app wrote only the factory application offset at `0x10000`. A subsequent 64 KiB readback at `0x0` matched the corresponding factory-backup bytes, and a 174,928-byte application readback matched the built binary. These checks prove the recorded M0 write and readback; they do not qualify any display, touch, SD, audio, radio, or USB path.

ESP32-P4 silicon before revision 3.0 is not binary-compatible with revision 3.x. Every maintained firmware app for this prototype must select the `<3.0` family and a minimum revision of v1.0 explicitly; relying on the ESP-IDF 5.5.3 default produces a v3.1-only image that esptool correctly refuses on this unit.

## Display-only path hardware-tested

Elecrow's pinned V1.0, V1.1, and V1.2 schematics prove the complete 10.1-inch display path invariant across published PCB revisions. The authorized subset is limited to MIPI-DSI bus 0, internal LDO3 at 2.5 V, internal LDO4 at 3.3 V, and GPIO31 backlight PWM. GPIO29 and GPIO41 are deliberately untouched.

On 2026-08-12 the connected unit passed the custom `display_diag` image with the locked EK79007 1.0.2 driver: 1024×600 RGB565, two lanes at 900 Mbps, 51 MHz DPI, and 25% backlight. The exact app binary read back correctly; serial showed repeated vertical-bar, horizontal-bar, and BER pattern cycles; and the user visually confirmed the patterns with a live Photo Booth image. The captured still shows vertical color bars filling the active area without gross static corruption. It does not prove moving-image tearing, framebuffer submission, or power margin. See `hardware/test-runs/2026-08-12-display-m1.json`.

## Storage path build-authorized; hardware test pending

The commit-pinned V1.0, V1.1, and V1.2 schematics prove the complete one-bit
microSD path invariant: SDMMC slot 0 uses GPIO43 through R192 to J5 SCLK,
GPIO44 through R193 to J5 DI/CMD, and GPIO39 through R191 to J5 DO/D0. Each
series resistor is 0 Ω, J5 is powered from the existing 3.3 V rail, and its
ground pins are on GND. V1.0 lacks the optional clock pull-up and three small
shunt capacitors added in V1.1/V1.2; the official examples for every revision
nevertheless use the same internal-pull-up, one-bit, 10 MHz configuration.

That comparison authorizes only SDMMC slot 0 at or below 10 MHz on GPIO43,
GPIO44, and GPIO39. D1-D7, card-detect, write-protect, formatting, and every
other peripheral remain denied. The reusable service mounts at `/sdcard` with
`format_if_mount_failed=false` and exposes only read/hash operations. ESP-IDF's
FAT VFS is not a hardware-enforced read-only mount, so this is an API boundary,
not a claim that arbitrary application code cannot write.

Two independent `storage_diag` builds under the pinned ESP-IDF produced
byte-identical application binaries and ELFs. The app checks card geometry
plus the exact Doom shareware WAD at `/sdcard/DOOM1.WAD`, then
`/sdcard/DOOM/DOOM1.WAD`. These runtime names are uppercase 8.3-compatible.
Its exact source and artifact gate is integrated
into the central flash path. The exact image was then app-only flashed and
read back successfully. On two controlled boots, however, the inserted card
timed out on the first sector read while SDMMC waited for the card to become
idle. The filesystem was therefore not classified and the WAD search never
ran. This is not evidence that the card needs formatting: a timeout or disk
I/O error can be caused by card seating, compatibility, power, or signal
margin. The subsequently authorized one-shot formatter initialized the same
card, created and raw-verified FAT32, and remounted it without formatting at a
measured 1 MHz. Its later file probe failed only because the probe used a
hidden long filename while FatFs was configured for 8.3 names. The reusable
storage service now conservatively uses that proven 1 MHz operating point. See
`hardware/evidence/elecrow-10.1-storage-path.json`,
`test-runs/2026-08-12-storage-d1-build.json`, and
`hardware/test-runs/2026-08-12-storage-d1.json`.

## Touch path narrowly runtime-authorized; independent hardware test pending

The commit-pinned V1.0, V1.1, and V1.2 schematics prove an equivalent GT911
touch path across all published 10.1-inch revisions. FPC2 routes I2C1 SDA on
GPIO45, SCL on GPIO46, reset on GPIO40, interrupt/address latch on GPIO42,
ground, and an effective 3.3 V supply. V1.0/V1.1 call the supply `LCD_VDD` but
populate R65=0R to `VDD_3V3`; V1.2 connects FPC2 directly to `VDD_3V3`.

GPIO45/GPIO46 are a shared board-control bus, also routed through level
shifters to the candidate ES8311 codec and board headers. `platform_i2c_shared`
is therefore the sole I2C1 owner. `platform_touch` borrows its bus handle,
adds only a 400 kHz GT911 device, and never creates or deletes the bus. The
pinned Elecrow Lesson05 implementation for V1.0/V1.1/V1.2 actively resets on
GPIO40 and latches GPIO42 low for primary address 0x5d, with a high-latched
0x14 fallback. The platform mirrors that known-good active-low sequence while
cleaning the failed primary panel-IO before fallback. GPIO42 becomes an input,
but runtime installs no ISR callback; operation remains polling-only.

The public touch contract is 1024x600 with at most five simultaneous contacts.
Every failed read yields an explicit invalid, zero-contact frame so Doom cannot
retain a stale touch action. The device owner reports that the supplied factory
firmware's touchscreen worked on this exact unit. The saved exact-unit factory
image also links the GT911 factory service. That user-attested history, the
exact-variant factory source, and the cross-revision electrical review grant a
narrow runtime authorization for the factory-compatible touch path. It is not
an independent project observation of coordinates or gestures; that still
requires the touch diagnostic or the combined Doom acceptance run. See
`hardware/evidence/elecrow-10.1-touch-path.json` and
`hardware/evidence/elecrow-10.1-factory-touch-audio-runtime-basis.json`.

## Factory audio working on the exact unit; cross-unit topology unresolved

The populated speaker path is not a direct digital I²S amplifier. Across the
three published revisions, GPIO21/22/23 and GPIO24 route LRCK, BCLK, data, and
MCLK through 0 Ω resistors to an ES8311 codec. Its control bus reaches
GPIO45/46 through BSS138 level shifters. The codec's differential analog output
then feeds the populated NS4263B power amplifier, whose shutdown input is
reached from GPIO30. The alternative NS4168 footprints and their I²S routing
resistors are marked not fitted.

The published schematic's expected-population path is not, however, the full
initializer selected by Elecrow's pinned factory software. That initializer
first creates and enables I2S0 PDM RX at 16 kHz with an approximately
1.024 MHz clock on GPIO24 and input on GPIO26, then creates I2S1 at 16 kHz
signed PCM16 stereo on LRCK GPIO21, BCLK GPIO22, and DOUT GPIO23. I2S1's TX
MCLK output is unused, but GPIO24 is not quiet and may feed the codec-MCLK net
through board strapping. It drives GPIO30 low after the channel starts and
uses an in-memory codec-control shim, so it performs no ES8311 I2C transaction.
The saved image from this bound unit links that factory speaker service, and
the device owner reports that its supplied factory firmware played audio on
this exact unit.

That evidence does not replace a power-off population inventory and is not an
independent acoustic measurement. It does justify a factory-compatible,
build-tested backend: `platform_audio_factory` first latches
and pad-readback-verifies GPIO30 high, preloads a complete 1536-frame zero DMA
ring, makes one high-to-low transition, holds exact zeros for at least 350 ms,
then rechecks low before accepting PCM. Volume step 10/10 preserves the complete
signed PCM16 range `[-32768,32767]` bit-for-bit without amplification; this Doom
image deliberately uses step 6/10 for proportional final-output attenuation.
Doom sound effects and validated MUS lumps from the exact embedded WAD are mixed
at 16 kHz by a bounded 16-voice procedural software synthesizer above the stable
platform boundary. It requires no external MIDI hardware or SoundFont and does
not claim bit-exact OPL emulation. The PDM RX channel is initialized and clocked
exactly like the factory source, but Doom does not consume its microphone samples. No
external codec I2C transaction is made and no alternate codec path is selected.
Any failure requests high shutdown before retaining or releasing resources.

The bound tablet has now exercised this path successfully under explicit
operator-accepted exact-unit releases. The owner confirmed audible Doom sound
effects and recognizable MUS music at reduced volume; Console OS Game API v1
was subsequently installed with the same complete factory initializer and an
optional native-game tone session. The launcher keeps the amplifier off and
opens audio only for the foreground game. The latest badge-free folder image
has its own exact-artifact release and passed guarded startup with the
amplifier still off; this is delivery/safety evidence, not a new acoustic game
test. See
`hardware/evidence/doom-embedded-touch-audio-e6-exact-unit-audio-release.json`,
`hardware/evidence/console-os-folder-clean-exact-unit-audio-release.json`,
`hardware/test-runs/2026-08-14-console-os-mvp-install.json`, and
`hardware/test-runs/2026-08-14-console-os-folder-clean-install.json`.

This is a working, authorized path for the device identity `4ea036…`; it is no
longer classified as runtime-blocked on that tablet. The unresolved amplifier
population/topology still prevents treating it as a reusable cross-unit pin
authorization. Every changed WAD-bearing or audio-capable firmware artifact
must receive a fresh exact-artifact release, preserve rollback, use the guarded
same-handle app-only write/readback/launch route, and keep the global pin map
locked. Software counters remain delivery evidence rather than acoustic proof.

## Firmware variant identified; physical PCB still to confirm

The generic `1024x600`, EK79007, GT911, `./main/main.cpp`, and misspelled `1024x600 Drak` factory strings do not distinguish Elecrow's 7-, 9-, and 10.1-inch products. Their official factory source archives have byte-identical `main/main.cpp`, `sdkconfig`, and BSP files. The only source difference is the background chosen in `elecrow_ui.c`.

The saved 16 MiB pre-project flash contains the complete 1,843,200-byte `loading_background_inch10_1` array at flash offset `0x71004`. That byte range has SHA-256 `a202c0cfd6552091ecfb7064c0029e7261747dc1a54ed8030269e1a483580d3c`. Full-array searches found no occurrence of the pinned 7-inch asset (`66cc6cb25c3dcf74f64f9687930fdc90a8420480b73d1c3fd22263c28f237892`) or 9-inch asset (`718f96264e6b00ce3225cdf5f43bb607af1e85344c605e944bc5bee2b5d89dd3`). Elecrow's selector source independently maps those three assets to the corresponding product repositories. This identifies the saved image as the **10.1-inch/DHE04310D factory-firmware variant** with very-high confidence.

Firmware can be installed on a mismatched panel, so this evidence does not prove the physical diagonal, label, or PCB revision. The profile deliberately records `physical_screen_sku_confirmed: false`, `pcb_revision: null`, and `pin_map_authorized: false`. Before any revision-sensitive board layer is enabled, record a clear photo of the product label, measured diagonal, and PCB silkscreen revision.

| Elecrow variant | SKU | display path |
| --- | --- | --- |
| 5.0 inch | DHE04005D | 800×480 RGB/ST7265 |
| 7.0 inch | DHE04107D | 1024×600 two-lane MIPI-DSI/EK79007 |
| 9.0 inch | DHE04209D | 1024×600 MIPI-DSI |
| **10.1 inch** | **DHE04310D** | **1024×600 MIPI-DSI; factory-firmware match** |

V1.0, V1.1, and V1.2 change ESP32-C6 SDIO and expansion-radio routing. Do not infer the revision from screen resolution.

The committed evidence contains only paths, selectors, sizes, offsets, and cryptographic hashes—none of Elecrow's image bytes. With the ignored factory backup present, the local check rehashes the selected flash slice:

```sh
python3 scripts/verify-factory-variant.py
```

For a full independent rerun, including exact negative searches for the 7- and 9-inch assets, download the three commit-pinned official source archives into a temporary directory and discard them afterward:

```sh
python3 scripts/verify-factory-variant.py --fetch-references
```

## Unauthorized family-reference pins

The official 7-, 9-, and 10.1-inch factory archives use the same BSP values below. They are reference data, not an active board profile, until this unit's physical PCB revision is confirmed:

| Function | Mapping |
| --- | --- |
| GT911 | SDA GPIO45, SCL GPIO46, INT GPIO42, reset GPIO40 |
| Backlight | GPIO31, 30 kHz PWM |
| MIPI-DSI | 2 lanes, 900 Mbps, 51 MHz pixel clock, RGB565 |
| Speaker codec (reference only) | ES8311: LRCLK GPIO21, BCLK GPIO22, data GPIO23, MCLK GPIO24; I²C SDA GPIO45/SCL GPIO46 through level shifters |
| Populated speaker amplifier (reference only) | NS4263B analog input from ES8311; shutdown path from GPIO30 |
| PDM microphone | clock GPIO24, data GPIO26 |

## USB Host power limitation

The two USB-C receptacles are not interchangeable. J1 terminates through 0-ohm
R6/R7 at the CH340K USB-UART and is only the programming/console connection.
J16 carries the ESP32-P4 dedicated HS DP/DM pair, so it is the correct data
path for a future controller host. Across the published V1.0, V1.1, and V1.2
schematics, however, J16 CC1 and CC2 each use a 5.1 kOhm resistor to ground,
and J16 VBUS reaches the board only through inward-facing Schottky diodes. The
board has no populated source switch, current limiter, enable GPIO, or fault
input and cannot place protected 5 V on J16 VBUS.

Consequently firmware alone cannot enumerate a bus-powered controller already
plugged into J16. Do not use a passive OTG adapter, bridge J16 VBUS to board
5 V, remove the CC resistors, or assume a powered hub provides correct
backfeed isolation. Qualification requires an external shim that carries J16
D+/D-/ground directly, leaves upstream J16 VBUS physically open, supplies the
controller side from a regulated current-limited 5 V switch, blocks backfeed,
and exposes a measurable overcurrent fault. The board can remain powered and
programmed through J1 while that shim is tested.

Gamepad bandwidth is low, but this connector is the dedicated high-speed PHY.
Espressif's current high-speed hub implementation lacks a transaction
translator, so full/low-speed devices behind a high-speed hub are not a
reliable baseline. Test a controller directly before qualifying any hub.

### USB device game storage and File Manager

J16's sink advertisement and inward VBUS path are appropriate for the opposite
role: a laptop can be the USB host and power/enumerate the ESP32-P4 as a USB
device. On 2026-08-14 the exact bound tablet enumerated Console OS as a writable
9,289,728-byte FAT16 MSC volume with 512-byte sectors. Laptop copy/remove,
exact Doom identity, FAT verification, repair of a pre-existing orphan-cluster
condition, a second clean mutation cycle, and clean eject passed. The app-only
firmware transaction preserved the live `game_data` partition byte-for-byte.
See
`hardware/test-runs/2026-08-14-console-os-program-manager-usb-install.json`.
J1 remains the CH340 serial/programming path.

The File Manager successor adds a build-tested on-device root listing and
confirmed regular-file deletion behind that same exclusive owner. It never
mounts beneath the laptop, never deletes directories, and rejects path-like or
unrepresentable names. Its exact app-only installation passed on 2026-08-14:
the complete 7 MiB application span read back exactly, retained-UART startup
reported eight apps with display/touch healthy, no backup was created, and the
live `game_data` digest was preserved. See
`hardware/test-runs/2026-08-14-console-os-file-manager-install.json`.
Panel/touch observation, deletion of a disposable probe, contention rejection,
clean J16 remount, Doom re-verification, and reboot persistence remain before
this new UI can be called fully hardware-tested. An intentional cable-pull test
must still demonstrate fail-closed status and filesystem repair behavior; it
must not be
recorded as safe simply because the next boot mounts, and it must use a
disposable probe rather than the real WAD as its only copy. The frozen
`scripts/console-os-file-manager-install.py` route creates no new backup and
accepts only the exact currently installed predecessor. Controller-host
testing still requires the powered isolation shim described above and must not
be combined with this device-mode test.

## Olimex ESP32-P4-PC Rev.B development target

The additive `olimex-esp32-p4-pc` profile is based on Olimex's Rev.B schematic,
manual, and production-test source at commit
`99a802ec029f531692102b2c754e20dae2226038`. It describes an ESP32-P4NRW32 with
16 MiB flash and 32 MiB PSRAM. The source documents and their SHA-256 values are
pinned in `hardware/boards/olimex-esp32-p4-pc-rev-b.json`; the review record is
`hardware/evidence/olimex-esp32-p4-pc-rev-b-source-review.json`.

The port uses the onboard LT8912B bridge over I2C1 GPIO7/GPIO8 for
1280x720p60 HDMI, with two MIPI-DSI lanes and internal LDO3 at 2.5 V. Console
OS keeps its 320x200 RGB565 logical surface and scales it to a centered 960x600
viewport before RGB888 scanout. The microSD interface is four-bit SDMMC on
CLK/CMD/D0-D3 GPIO43/44/39/40/41/42 with its active-low GPIO45 power gate and
LDO4 at 3.3 V. Mount retries use decreasing clock rates; runtime formatting and
hot-removal are intentionally disabled.

The ESP32-P4 high-speed USB root is wired to an onboard, powered FE1.1s
four-port USB-A host hub. GPIO21 holds the hub in reset until the host service
is ready; the board supplies protected VBUS through its CH217K path. Console OS
can therefore own one generic HID gamepad, one boot keyboard, and one boot
mouse at the same time. Descriptor/report lengths are bounded and each device
is neutralized on disconnect. The USB-C connector is native USB Serial/JTAG
for programming and monitoring; it cannot expose the microSD card as MSC.

Audio uses Olimex's official ES8311 path on the display-owned I2C1 bus
(GPIO7/GPIO8) and I2S1 MCLK/BCLK/LRCLK/DOUT/DIN GPIO13/12/10/9/11. GPIO53 is
held low until the codec has opened muted and the bounded audio stream is
ready. Console OS and Doom share the reusable `platform_audio` API; no game
owns the codec or pins. This source path builds with pinned
`espressif/esp_codec_dev` 1.5.4, but audible output at the 3.5mm jack remains a
required physical acceptance item.

This target is source-reviewed and build-tested only. The connected USB serial
endpoint has been observed, but a successful ROM-loader identity read, HDMI
observation, card test, audible audio test, and named HID device run are still
required. `flash_authorized` must remain false until exact-unit identity and a
target-specific guarded authorization are established. Firmware backups run
only as a separately requested operation and are never required for flashing;
do not create or refresh one as part of flashing. Recovery can rebuild old source. See `docs/boards/OLIMEX_ESP32_P4_PC.md` for the operator
workflow.

## Primary references

- [Olimex ESP32-P4-PC product page](https://www.olimex.com/Products/IoT/ESP32-P4/ESP32-P4-PC/open-source-hardware)
- [Olimex ESP32-P4-PC official repository](https://github.com/OLIMEX/ESP32-P4-PC)
- [Olimex ESP32-P4-PC Rev.B schematic](https://github.com/OLIMEX/ESP32-P4-PC/blob/main/HARDWARE/ESP32-P4-PC-Rev.B/ESP32-P4-PC_Rev_B.pdf)
- [Olimex ESP32-P4-PC user manual](https://github.com/OLIMEX/ESP32-P4-PC/blob/main/DOCUMENTS/ESP32-P4-PC-user-manual.pdf)

- [Elecrow 10.1-inch product repository](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen)
- [Exact Elecrow 10.1-inch factory-source archive used by the verifier](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/c5a437311b951aaa9d17115bf420877a8f1f7b83/factory_sourcecode/V1.0/ESP32-P4-Adcance-brookesia_phone_inch10_1.zip)
- [Elecrow 10.1-inch product wiki (DHE04310D)](https://www.elecrow.com/wiki/CrowPanel_Advanced_10.1inch_ESP32-P4_HMI_AI_Display_1024x600_IPS.html)
- [Elecrow 10.1-inch V1.2 schematic at the pinned commit](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen/blob/c5a437311b951aaa9d17115bf420877a8f1f7b83/Eagle_SCH%26PCB/1.2/ESP32-P4%20Display%2010.1%20inch%20V1.2.sch)
- [Espressif USB Host guide](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- [ESP32-P4 hardware design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32p4/index.html)

## M5Stack Tab5 development target

The separate `m5stack-tab5` Console OS build uses the official C145/K145 wiring
and detects ILI9881C/GT911, ST7123 or ST7121 panels. Its core display, touch, SD
and ES8388 audio adapters are build verified. A (ST7121) and B (ST7123) have
exact readback, mounted SD, launcher health and native USB game-transfer evidence.
The operator confirmed touch after B's mirror correction. Doom gameplay, audio
and sustained scrolling acceptance remain pending. A/B have separate full
pre-install backups and exact-unit authorizations; the global flash gate stays closed. See [Tab5 port notes](boards/M5STACK_TAB5.md).
