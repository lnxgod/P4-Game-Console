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
  D0/D1/D2/D3/CMD/CLK GPIO39/40/41/42/44/43.

The exact connected unit now passes the Console OS read-only microSD mount at
1 MHz in one-bit mode after enabling LDO channel 4. Omitting that power-control
handle produces an SD `send_op_cond` timeout before card identification; it is
not a filesystem-format error. The final acceptance record is
`hardware/test-runs/2026-08-16-waveshare-console-os-quake-sd-install.json`.

These values come from Waveshare's published BSP source, not from the Elecrow
profile. Source review alone is not local hardware acceptance. The read-only
storage subset now also has exact-unit runtime evidence; unrelated electrical
claims still require their own evidence. See the
[official Waveshare BSP repository](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3).

The last item matters: the OTG receptacle is configured as a sink/device in
the published schematic. It has VBUS, but the evidence does not authorize it
to source protected 5 V to a bus-powered controller. Until bench measurements
prove source role, current limiting, and backfeed behavior, use a powered,
current-limited host fixture and do not use a passive OTG adapter.

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

The first selectable build boundary is `PLATFORM_BOARD_TARGET`: Elecrow stays
the default for the existing Doom artifact, while Waveshare is an explicit
target that must select its own display, touch, audio, storage, and USB
implementations. The current Doom app still links the Elecrow implementations;
the `sdkconfig.waveshare.defaults` overlay now selects the Waveshare board,
display, touch, audio, and 32 MB flash contracts. The ST7701 component and
Waveshare audio adapter still need to be resolved into the locked IDF
dependency graph before that overlay is a buildable firmware candidate; this
is an intentional port gate, not a claim that Waveshare is already runnable.

The board profile now authorizes only the exact-unit, read-only Console OS
storage subset described above. Formatting, card writes, USB host power, and
every unrelated unqualified peripheral remain outside that storage grant.
