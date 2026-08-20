# Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 port

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

The exact connected unit now passes the Console OS app-owned microSD mount at
1 MHz in one-bit mode after enabling LDO channel 4. Omitting that power-control
handle produces an SD `send_op_cond` timeout before card identification; it is
not a filesystem-format error. The final acceptance record is
`hardware/test-runs/2026-08-16-waveshare-console-os-quake-sd-install.json`.

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

## Passive Wi-Fi scan candidate

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

The selectable build boundary is `P4_BOARD_PROFILE`: Elecrow stays the
default, while Waveshare selects its own display, touch, audio, and microSD
implementations. `make console-os-waveshare-idf` now resolves the pinned
ST7701, GT911, ES8311, ESP-Hosted, remote-Wi-Fi, ELF-loader, and ESP-IDF 5.5.3
graph and emits the
launcher firmware plus a validated microSD app bundle. Executable `.P4G`
packages are never linked into OTA. This proves the
software candidate only. Each changed binary still needs a fresh exact-unit
authorization, guarded install, retained-UART boot capture, and manual game
launch/return acceptance before it inherits any hardware claim.

The board profile authorizes the exact-unit app-owned read-only mount plus an
exclusive H2 TinyUSB MSC device transfer mode. The firmware unmounts VFS before
the Mac receives the LUN after the user starts the USB Drive app,
synchronously reads back every host write, and permits USB Mode off only after
Finder ejects the volume or H2 disconnects. Formatting, USB host/VBUS-source
behavior, concurrent app/host ownership, and unrelated peripherals remain
outside that grant.
