# Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 port

## Selected Console OS 0.5.12 production baseline

The operator selected 0.5.12 after comparing the later native-scrolling
experiments. The exact unit-3 rollback chain restored 0.5.12; it remains the
current production source and installed baseline. Versions 0.5.13, 0.5.14,
and 0.5.15 are rejected/not accepted experiments. Their capture tooling,
source history, and changelog remain available, but device-specific install
wrappers, authorizations, and raw serial captures are omitted from the public
branch. They are not enabled by the selected production source.

## Console OS 0.5.12 baseline restore and runtime cadence

Operator testing rejected GT911 filter 4: scrolling was less smooth and
touch-follow latency was unchanged. Console OS 0.5.12 restores the exact unit-3
sealed filter-8/checksum-`0x79` baseline. The guarded restore accepts only an
exact filter-4/checksum-`0x7d` block or an already-original block, performs one
full-block write with `Config_Fresh=1`, reads back the complete block, and fails
closed without retrying on any error or mismatch. The controller update is not
power-loss atomic, and rolling firmware back does not itself roll back GT911
configuration NVM.

The default release disables the synchronous three-second runtime-statistics
burst because captures showed 132--134 ms main-loop stalls (about eight
frames). Diagnostic builds opt in with
`-DP4_CONSOLE_RUNTIME_STATS_BUILD=ON`. Battery shell display uses 50 mV
hysteresis; raw mV changes no longer dirty Home, avoiding five-second noise
redraws, while Power detail continues to show raw mV. Full-resolution 768x480
and all game surfaces remain unchanged.

Host tests, the exact USB-host build, guarded app-only install, complete
1,884,160-byte readback, single baseline restoration, and reset-persistent
no-write verification pass. Two 60-second captures contain no periodic
runtime-stat records, resets, panics, or rejected runtime markers. The next
hardware result confirmed that the periodic 3--4 second stutter was gone,
while continuous touch-follow and reversal lag remained.

## Console OS 0.5.11 staged GT911 filter experiment

The first exact-unit tuning candidate uses the complete controller snapshot
captured by 0.5.10. It requires product `911`, firmware `0x1060`, 480x800
identity and configuration geometry, the known identity prefix plus a vendor
byte bound from the same live controller before the write, configuration
version 65, the complete captured baseline, and a valid checksum. It modifies
only the low six normal-filter bits at `0x8050`, moving from 8 to the
conservative first stage 4 while preserving the upper first-filter bits.

The firmware recalculates the two's-complement checksum, sets
`Config_Fresh=1`, writes the complete 186-byte block in one transaction, waits
for application, and rereads the complete configuration. Candidate success
requires every byte other than the intended filter/checksum/fresh fields to
match the sealed preimage. A write or verification failure immediately attempts
and verifies a full original-block restore; an identity or baseline mismatch
refuses before any write. Report rate, thresholds, debounce, full-resolution
rendering, and all game contracts remain unchanged.

GT911 saves changed configuration content carrying the same version, so filter
4 can persist across a controller reset. The reviewed 186-byte restoration
baseline is encoded in the platform-touch implementation for an explicit
recovery build. Application-firmware rollback alone does not restore that
controller state, and power loss between a failed candidate write and its
restore cannot be made atomic.

The exact 0.5.11 build, verifier, guarded unit-3 install/readback, retained-UART
startup apply, and reset-persistence check pass. The captures record vendor
`0x00`, an 8/`0x79` to 4/`0x7d` apply, then an already-target 4/`0x7d` state after
reset with no restore. Operator latency/jitter testing remains pending; no
user-visible improvement is claimed. The complete record is
summarized without device-specific identifiers in
`hardware/evidence/waveshare-console-os-0.5.12-public-validation-20260905.json`.

## Console OS 0.5.10 read-only GT911 characterization

The 0.5.9 mailbox trace proved only that the application polled frequently: the
pinned GT911 driver repeats its cached coordinates when the controller's
data-ready status is clear. Console OS 0.5.10 preserves the timestamp of the
last real data-ready report across those repeated polls and reports the cadence
of unique reports. Neutral frames remain valid and age-free, so a long idle
period cannot trigger the contact freshness guard.

At startup, Console OS reads and logs the GT911 product/firmware identity and
the complete 0x8047--0x8100 configuration block in bounded chunks. It validates
the stored checksum and decodes the configured report period, press/release
debounce, normal/first filter values, and X/Y movement thresholds. This is a
read-only capture: no controller configuration is modified. Full-resolution
shell rendering and every game display/input contract remain unchanged.

The exact unit-3 build, verifier, guarded application-only install/readback,
and retained-UART startup gate pass. The captured controller is product `911`,
firmware `0x1060`, with configuration version 65 and a valid `0x79` checksum.
It uses a 10 ms report period, X/Y movement thresholds of zero, and normal
filter 8. The exact 186-byte pre-tuning configuration and a restore payload
with `Config_Fresh=1` are encoded in the reviewed restoration implementation.

## Console OS 0.5.9 touch freshness and reversal response

The accepted full-resolution shell and all game display contracts remain
unchanged. Home content drags now use raw 800x480 contact displacement instead
of first quantizing motion into the stable 320x200 UI coordinate space. The
tap/drag decision uses an explicit four-physical-pixel threshold; after that
decision, a one-pixel direction reversal immediately changes the fractional
scroll position. The logical coordinate path remains authoritative for button,
tile, and scrollbar hit testing.

A low-priority 120 Hz launcher task is the sole GT911 reader and publishes only
its newest complete frame under a short critical section. This continuously
acknowledges controller data while a native shell frame is being rendered or
transformed, without creating an input backlog. The task is stopped and joined
before native games or Doom use their unchanged direct polling
contracts, and restarted on launcher return. A 500 ms timeout fails closed
instead of allowing concurrent readers or touch destruction.

The display handoff can optionally carry the timestamp of a touch sample that
actually changed launcher state. The next confirmed DSI refresh records
input-to-refresh and handoff-to-refresh totals, maxima, and last values along
with full/partial classification and replay count. These bounded counters are
reported in the existing periodic serial statistics; the ISR performs no
logging and ordinary game submissions do not carry the metadata.

The exact build, static verifier, guarded unit-3 app-only install/readback, and
retained-UART startup gate pass. During interactive scrolling the mailbox
reported zero read failures, zero stale samples, and a worst sample age of
8.849 ms. Confirmed-refresh latency settled near 39.8--39.9 ms on average;
handoff-to-refresh averaged about 8.6 ms and remained within one 16.7 ms panel
interval. The final cumulative report classified 333 interactive presents as
partial and 11 as full. Operator testing still found the same finger-follow
and reversal delay as 0.5.8, so application-side polling freshness is not the
primary cause and 0.5.9 is not an accepted latency fix. The next investigation
must isolate GT911-reported coordinate timing from any post-refresh scanout
phase before changing the renderer again.

## Console OS 0.5.8 dirty-region presentation and touch response

The Windows shell remains a native 768x480 RGB565 source. Cached Home renders
now return a conservative changed rectangle covering the moving tile band,
scrollbar, and (when the logical row changes) status footer. The display
backend transforms that damage into the inactive DSI framebuffer, replays any
missing generation from the current authoritative source, coalesces contained,
overlapping, or adjacent rectangles, and selects a full transform whenever the
cache-aware partial work would be no cheaper. Both buffers therefore remain
exact, and ordinary scrolling uses a partial transform only when that bounded
work costs less than transforming the whole source.

Drag recognition now begins after two logical pixels and the first visible
frame uses the complete displacement from touch-down. A potential tile tap no
longer triggers a speculative press frame before it becomes a drag, and a held
drag can continue shifting cached rows across an integer boundary. Settled
endpoints still converge on the authoritative native renderer. The BBS path,
structural shell frames, and all game presentation paths are unchanged.

Host frame-difference tests cover fractional movement, row/footer changes,
scrollbar press/release, and thumb dragging. Console Shell, Console OS, display
layout, exact build, and static verification gates pass. The exact unit-3
app-only install/readback and retained-UART startup gate also pass with one
0.5.8 start, the native-content and partial-present markers, and no display
timeout, display failure, accelerator failure, panic, or reboot marker. The
idle startup capture reports `partial_submits=0`; interactive dirty-region
timing and operator-visible scrolling/game acceptance remain pending.

## Console OS 0.5.7 native shell cache and game handoff

The Windows shell returns to a native 768x480 RGB565 source while each game
retains its existing 320x200 or negotiated 768x480 geometry. The shell source
framebuffer persists between frames. A fractional scroll step translates
cached tile rows by whole native pixels and redraws only the newly exposed
band; scrollbar and footer changes are clipped redraws. Selection/press
changes and each settled endpoint redraw the complete tile viewport. Pointer,
battery, catalog, and structural changes invalidate the cache and fall back to
an authoritative full-frame redraw. This avoids a full-frame CPU redraw for
ordinary scroll motion without allowing rounding error to accumulate across
interactions.

The shell keeps the pipelined display handoff. Standard 320x200 games and
negotiated 768x480 games use refresh-synchronous handoff, restoring the
baseline timing contract that avoids added gameplay latency from the shared
display pipeline. The exact unit-3 0.5.7 app-only install/readback and
retained-UART startup gate pass. Operator-visible sustained scrolling and
representative 320x200/768x480 game acceptance remain pending.

## Console OS 0.5.6 compact raster correction

The Windows shell retains the 384x240 source and exact 2x PPA path introduced
in 0.5.4. Its 5x7 font now rasterizes connected horizontal, vertical, and true
diagonal bitmap runs with a fixed compact stroke weight instead of independently
stretching each bitmap cell. Authored one-pixel folder, bevel, scrollbar, and
rule edges likewise remain one compact pixel. This removes phase-dependent
two- versus four-panel-pixel strokes without changing the 768x480 viewport,
touch mapping, display-buffer ownership, native games, or BBS path.

## Console OS 0.5.4 logical shell frames

Ordinary launcher and detail pages now draw into a compact 384x240 source and
use a dedicated PPA 2x path. The rotated result is the exact centered 480x768
native block, preserving the established 768x480 shell and touch viewport.
The optional 80x30 BBS home view continues to use the exact 768x480 content
path. This removes three quarters of the source pixels from Windows-style
scrolling without changing games, high-resolution cartridges, or the BBS
terminal geometry. An overdue UI iteration resets the FreeRTOS wake anchor so
the fractional 60 Hz scheduler does not emit catch-up bursts, and the Windows
scrollbar thumb remains captured for the complete drag gesture.

## Console OS 0.5.3 animation cadence

The 60 Hz shell scheduler now runs on a 1 kHz FreeRTOS tick. This represents
its rational cadence as 16/17/17 ms rather than the 10/20/20 ms pattern forced
by the former 100 Hz clock. The 30 MHz DSI timing produces a physical refresh
every approximately 16,704 us, so tick quantization is no longer a continuous
source of launcher judder. Runtime timing counters report actual refresh,
source render, PPA transform, buffer-reuse wait, and panel-handoff durations;
the existing refresh-confirmed double-buffer ownership remains unchanged.

## Console OS 0.5.2 pipelined launcher frames

The launcher still renders a 768x480 source and uses blocking PPA rotation into
the existing pair of DSI-owned native framebuffers. The refresh fence now sits
before reuse of the former scanout buffer rather than after every handoff. This
allows scanout to overlap preparation of the next source frame while the
refresh callback remains the sole authority that promotes a pending buffer to
confirmed active. A stale semaphore wake cannot release a buffer because the
ownership state is rechecked under the ISR-safe lock.

The physical timing remains capped at approximately 59.87 Hz. Hardware
acceptance must use timestamped submit/completion counters during sustained
scrolling and still requires zero timeouts, hard failures, and PPA failures;
the build alone is not a frame-rate result.

## Console OS 0.4.98 BLE radio handoff

The shared NimBLE host now has a Console OS ownership transition between a
disconnected saved controller reconnect and multiplayer discovery. Entering
Multiplayer cancels queued controller reconnect work, waits until its GAP scan
is actually idle, and only then enables the room browser. Leaving Multiplayer
disables its discovery and resumes the saved controller reconnect. An already
connected encrypted pad remains attached and can coexist with one game peer.
This removes the timing-dependent Host/Join path that could let two clients
start competing GAP procedures after boot.

## Console OS 0.4.97 launcher PPA path

The shell remains 768x480. On this portrait-native panel that surface can be
rotated at exact 1:1 scale into 480x768, centered with 16 native pixels above
and below. Console OS now sends that conversion through the blocking PPA SRM
client already owned by `platform_display`; a bounded CPU layout remains the
fallback. This removes the per-pixel divisions that made animated launcher
scrolling lag on the physical panel. Runtime acceptance must show increasing
`display_accelerated`, zero `display_accel_failures`, zero hard display
failures, and operator-confirmed responsive touch scrolling.

## Console OS 0.4.52 SD Card utility

The `SYSTEM/SD CARD` app exposes storage checks without baking game content
into firmware. `CHECK CARD` issues an SD status command, reads FAT free-space
metadata, walks the root, and revalidates game data without writing. `RETRY
CARD` releases a faulted SDMMC0 session and performs the existing bounded
frequency fallback without formatting.

`REPAIR FAT` is an explicitly confirmed, on-device FAT32 repair pass. The card
is unmounted first and the UI warns the operator to keep power connected. It
can restore an unambiguous primary/backup boot-sector copy, replace one
structurally invalid FAT mirror from its structurally valid peer, and rebuild
FSInfo hints. Every changed 512-byte sector is read back immediately. It
refuses FAT12/FAT16/exFAT, formatting, directory deletion, lost-chain or
cross-link guesses, and two structurally plausible but divergent FAT copies.
Those cases remain visible as `NEEDS FULL FSCK` instead of risking game data.

This is a separate board target from the Elecrow 10 in variant. The existing
Elecrow firmware is not safe to copy as a pin map: the Waveshare board has a
480x800 portrait-default MIPI-DSI panel, a different touch path, an ES8311 /
ES7210 audio design, an ESP32-C6 Wi-Fi coprocessor, and a separate OTG Type-C
connector.

## Current evidence

The official Waveshare product page and schematic identify:

- ESP32-P4 with 32 MB flash and 32 MB PSRAM;
- 480x800 4.3 in capacitive display, portrait by default;
- two-lane MIPI-DSI LCD connector;
- ES8311 audio codec, ES7210 echo-cancellation chip, and 8 ohm / 2 W speaker header;
- H1 Type-C USB-UART and H2 Type-C USB OTG;
- H2 USBD_N/USBD_P on ESP32-P4 GPIO24/GPIO25;
- H2 VBUS and a `USB1_5V` board rail; and
- 5.1 kOhm CC1/CC2 pulldowns on H2.

The official Waveshare engineering BSP adds the port-level details needed for
the first firmware target:

- ST7701 over two-lane MIPI-DSI at 500 Mbps per lane, 480x800 portrait, with
  MIPI PHY LDO3 at 2500 mV;
- LCD backlight GPIO26 and LCD reset GPIO27;
- GT911 touch on I2C SDA GPIO7 / SCL GPIO8, reset GPIO23, with no interrupt
  GPIO;
- ES8311/ES7210 audio on MCLK/BCLK/LRCLK/DOUT/DIN GPIO13/12/10/9/11 and
  amplifier control GPIO53; and
- microSD socket power from ESP32-P4 on-chip LDO channel 4, followed by SD
  D0/D1/D2/D3/CMD/CLK GPIO39/40/41/42/44/43; and
- ESP32-C6 Wi-Fi transport over four-bit SDIO on P4
  CLK/CMD/D0/D1/D2/D3 GPIO18/19/14/15/16/17, with the C6 enable/reset line on
  P4 GPIO54. This matches Espressif's P4 Function-EV hosted-radio preset.

Both recorded development units pass the Console OS app-owned microSD mount at
10 MHz in one-bit mode after enabling LDO channel 4. Firmware retains bounded
5/1/0.4 MHz fallback for card and signal-margin differences. Omitting that
power-control handle produces an SD `send_op_cond` timeout before card
identification; it is not a filesystem-format error. The exact-unit records are
`hardware/test-runs/2026-08-17-waveshare-console-os-0.4.10-correct-org-brand-install.json`
and `hardware/test-runs/2026-08-20-waveshare-unit2-console-os-0.4.37.json`.

These values come from Waveshare's published BSP source, not from the Elecrow
profile. Source review alone is not local hardware acceptance. The storage
subset now also has exact-unit runtime evidence; unrelated electrical
claims still require their own evidence. See the
[official Waveshare BSP repository](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3).

The last item matters: the OTG receptacle is configured as a sink/device in
the published schematic. It has VBUS, but the evidence does not authorize it
to source protected 5 V to a bus-powered controller. Until bench measurements
prove source role, current limiting, and backfeed behavior, use a powered,
current-limited host fixture and do not use a passive OTG adapter.

## H2 controller-host experiment

Console OS now has a separate controller-first build target:

```sh
./scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host
```

It starts the ESP32-P4 high-speed root on H2, keeps H1 as programming/UART,
mounts the microSD for Console OS, and never enables a firmware-controlled H2
VBUS source. TinyUSB MSC is linked but not installed at boot. The guarded USB
Drive app quiesces and uninstalls Host/HID before TinyUSB can claim H2, and the
reverse transition uninstalls TinyUSB before Host/HID can restart.

The exact unit reached Console OS READY and enumerated an operator-supplied
powered USB-C hub. With one-level hub support enabled, the P4 then identified
the controller on downstream port 2 as low-speed and rejected it with
`transaction translator (TT) is not supported`. This proves the native H2
host data path and hub attach; it does not prove controller input through that
hub. The pinned `espressif/usb` 1.5.0 stack cannot carry a low/full-speed
controller behind a high-speed hub. See
`hardware/test-runs/2026-08-17-waveshare-h2-powered-hub-controller-host.json`.

A previous local experiment added incomplete DWC split-transaction fields to
the generated 1.5.0 component and caused a boot loop. Those edits have been
removed. `verify-console-os-waveshare.py` now checks every manifest-listed
`espressif/usb` source byte against `CHECKSUMS.json`, rejects extra source
files, and scans the built application for the experimental TT strings. A
stale or locally patched scheduler image therefore fails before flashing.

The controller-first image now sets the P4 DWC2 `HCFG.FSLSSupp` host policy
before enabling the root port. This makes an HS-capable powered hub negotiate
as a full-speed hub, so its low/full-speed HID children use the stack's
supported FS-hub route instead of TT split transactions. H2 is deliberately
capped at 12 Mbit/s; that is ample for gamepads, keyboards, and mice. This is
a candidate until retained UART proves that the actual powered hub enumerates
as FS and repeated HID hot-plug stays stable.

Console OS 0.4.17 proved the hub takes that full-speed route and the OS reaches
READY, but its already-attached low-speed child failed the first short device
descriptor transfer twice across cold boots. Hardware then disproved the
0.4.18 assumption that the pinned stack's reset-attempt integer would retry
enumeration: it counts failures of the port reset operation itself, while a
failed short descriptor disables the already-reset port. Console OS 0.4.19
kept the 500 ms interval and added at most two complete OS-owned Host/HID
recycles. The exact H2 fixture exhausted both attempts: because the hub is
externally powered, restarting the P4 host stack did not reset its disabled
downstream port.

Console OS 0.4.20 instead generates a fail-closed overlay from the exact
locked `espressif/usb` 1.5.0 `ext_port.c`; the managed package remains
byte-for-byte unchanged. Only after a real enumeration failure disables a
still-connected child does the overlay wait for the failed USBH node to be
freed, reset that downstream port, and retry. The budget is two attempts,
physical reconnect refreshes it, and exhaustion returns to ordinary hot-plug
without recycling the whole host or looping. The verifier checks the generated
bytes and proves the build compiled the overlay instead of the managed source.
Hardware acceptance still requires the child to enumerate and disconnect state
to neutralize.

The exact 0.4.20 candidate failed that acceptance. Retry 1 raced the external
hub's own control endpoint, produced `ESP_ERR_INVALID_STATE`, and repeatedly
asserted in `usbh_dev_close` with a control transfer in flight. Console OS
0.4.21 therefore restores the byte-exact managed `ext_port.c` and sets the
automatic retry budget to zero. Controller Host/HID remains active for normal
hot-plug, but this low-speed child/hub combination is not qualified. Any later
recovery must be scheduled by the external-hub action queue after control-pipe
completion, never directly from the port recycle callback.

Console OS 0.4.22 implements that deferred boundary without changing
`hub.c`: retry cannot dispatch until the failed USBH node is free and the
parent hub's feature/GetStatus sequence has cleared both port status flags.
The generated overlay is reproducible from the exact locked `ext_port.c`, and
the verifier proves the original managed `hub.c` is the only scheduler source
compiled. A durable NVS marker is armed before Host/HID; an incomplete boot
makes the next boot suppress the overlay and recover with normal hot-plug
only. Two serialized attempts remain the maximum per physical connection.
The exact-unit run proved that boundary: one boot, two serialized attempts,
clean exhaustion, Console OS READY, and guard confirmation with no invalid
state, assertion, panic, or reboot. The low-speed child still failed its short
descriptor, so 0.4.22 is accepted only as boot-loop containment, not as HID
controller support. See
`hardware/test-runs/2026-08-18-waveshare-console-os-0.4.22-serialized-enum-retry-stable-no-hid.json`.

Console OS 0.4.23 adds a second exact-input build overlay for the locked
`hcd_dwc.c`, based on Espressif's P4 forced-full-speed correction at commit
`6306c4f6d1660b58ccc431877f4c42f202714072`. Before every root reset it
reapplies `HCFG.FSLSSupp=1` and selects the 30 MHz UTMI clock. Once the root
port is enabled and speed is valid, it selects the low/full-speed UTMI clock,
sets the one-millisecond `HFIR` interval to 5999/29999, and reports an
unexpected HS result as FS. Every new register write is gated by the same
durable NVS probe as downstream retry. If a candidate boot fails before the
marker confirms, the next boot executes the stable 0.4.22 register path with
both HCD reapplication and downstream retry suppressed. The managed USB
package and `hub.c` remain byte-for-byte unchanged. Hardware acceptance still
requires retained UART plus the named HID and role-switch cases.

The Console OS product policy is controller host by default. USB Drive is an
explicit launcher app: it neutralizes controller state, stops Host/HID, hands
microSD to device MSC, and keeps touch available for the on-device return.
Return is blocked until the Mac ejects or disconnects. Any teardown or remount
failure leaves H2 quiesced; firmware never starts both roles or silently
formats the card.

The current unflashed UI candidate renders Console OS natively at 768x480 with
an 80x30 CP437 BBS home and ANSI connection boot sequence. This does not alter
the 320x200 game surface. H1 is reserved as the first BBS/server serial link so
H2 can remain controller-first; see `docs/BBS.md`.

The Multiplayer app starts no BLE work at boot. Opening it lazily prefers BLE
for room discovery and gameplay, while the Match Settings page keeps wired
UART available as a deliberate fallback. This policy does not change H2's
controller-first USB Host role or the separate, explicit USB Drive app.

## Direct multiplayer UART candidate

The official schematic exposes otherwise-unused ESP32-P4 GPIO28 at J3 pin 16,
GPIO29 at J3 pin 18, and ground at J3 pin 24. Console OS 0.4.55 adds a bounded
dual-UART transport that can use UART1 on those pins for direct 3.3 V
board-to-board multiplayer while retaining H1/CH343 as an automatic relay
fallback and upload/debug path. No supply rail is part of the direct cable.

The software route is fail-closed: generic builds leave direct UART disabled
and require a board profile to provide all controller/pin values. The exact
Waveshare profile is enabled following the operator's explicit confirmation of
the J3-16/J3-18/J3-24 crossover contract. Direct input
is decoded with the same bounded P4MP framing and CRC checks as H1. Direct is
polled first, H1 fallback is held off for three discovery frames, and the first
valid discovery/offer permanently binds the match route until timeout/reset.
Hardware acceptance still requires retained H1 logs from both consoles for a
direct match and a separate cable-absent H1 relay fallback run.

## Passive Wi-Fi scan candidate

The stable Console OS image does not link or start this candidate. A retained
0.4.49 boot exposed a stale generated configuration selecting ESP-Hosted's
fallback SPI transport on GPIO7/2/6/10/26/4/5 instead of the board's SDIO
transport. That fallback overlaps GT911 SDA plus display/audio resources and
made the launcher appear to lose touch after boot. Signal scan is now an
explicit build opt-in, and configuration fails unless the generated transport
is SDIO on reset/CLK/CMD/D0-D3 GPIO54/18/19/14/15/16/17 exactly.

`components/p4_signal_scan` converts each driver observation into a sanitized
display label and a per-boot keyed token before game code can see it. The
Waveshare-only `platform_signal_scan` candidate performs passive scans on a
background task, keeps raw observations in one fixed 32-record buffer, and
publishes no more than eight results. It requests named networks only and
defensively removes zero-length or all-space SSIDs before results reach a
game; RSSI and opaque BSSID-derived identity remain available for those named
networks. It never configures credentials or calls connect, and Console OS
advertises the optional cartridge capability only after initialization
succeeds. ESP-IDF 5.5.3, `esp_hosted` 1.4.7, and `esp_wifi_remote` 0.14.5 are
exact locked dependencies.

`make console-os-waveshare-idf` proves this transport and Byte Buddy package
compile together; it is not live-radio acceptance. Before enabling a hardware
claim, capture retained UART showing the exact C6 app description, successful
transport initialization, repeated passive scans, bounded result counts, SD
coexistence, touch responsiveness, game exit/relaunch, and a clean offline
degradation test. Do not log raw SSIDs, BSSIDs, or session tokens.

## Port order

1. Record board SKU, PCB revision, LCD FPC marking, touch-controller marking,
   and chip/flash identity.
2. Add a board-specific pin-independent bring-up image.
3. Qualify the LCD controller, DSI timing, reset, backlight rail, and RGB565
   scanout with a dedicated display diagnostic.
4. Qualify the touch controller and I2C/reset/interrupt path separately.
5. Qualify ES8311/ES7210 audio with the Waveshare vendor initialization; do not
   reuse the Elecrow GPIO30/I2S1 factory-audio contract.
6. Measure H2 VBUS in sink, source, unplugged, and powered-fixture states.
7. Only then port the Console OS shell and native games behind the existing
   platform APIs.

The selectable build boundary is `P4_BOARD_PROFILE`: Tab5 is the Console OS
default, while explicit Waveshare builds select their own display, touch, audio, and microSD
implementations. `make console-os-waveshare-idf` now resolves the pinned
ST7701, GT911, ES8311, ESP-Hosted, remote-Wi-Fi, ELF-loader, and ESP-IDF 5.5.3
graph and emits the
launcher firmware plus a validated microSD app bundle. Executable `.P4G`
packages are never linked into OTA. This proves the
software candidate only. Each changed binary still needs a fresh exact-unit
authorization, guarded install, retained-UART boot capture, and manual game
launch/return acceptance before it inherits any hardware claim.

The board profile authorizes the exact-unit app-owned mount, including only
the OS-owned journaled `/SAVES` namespace, plus an exclusive H2 TinyUSB MSC
device transfer mode. Console OS 0.4.85 exposes `save` while the app owns FAT;
cartridges receive copied callbacks rather than paths or filesystem handles.
Each save uses validated IDs and sequence, SHA-256, a synchronized staging
file, one backup, atomic replacement, and a recoverable journal. This source
candidate is not hardware acceptance until exact-unit retained-UART and
power-cycle relaunch evidence is recorded. The firmware unmounts VFS before
the Mac receives the LUN after the user starts the USB Drive app,
synchronously reads back every host write, and permits USB Mode off only after
Finder ejects the volume or H2 disconnects. Formatting, arbitrary cartridge
paths, USB host/VBUS-source
behavior, concurrent app/host ownership, and unrelated peripherals remain
outside that grant.
