# Proven Elecrow 10.1-inch display contract

## Proven target

- Board family: Elecrow CrowPanel Advanced ESP32-P4 HMI AI Display.
- Working SKU classification: 10.1-inch DHE04310D; physical printed SKU and PCB revision remain unresolved.
- Hardware-tested chip: ESP32-P4 revision 1.3 with 16 MiB flash and 32 MiB PSRAM.
- Display-only hardware test: `hardware/test-runs/2026-08-12-display-m1.json`.

## Primary provenance

- Official Elecrow repository: `Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen`.
- Pinned Elecrow commit: `c5a437311b951aaa9d17115bf420877a8f1f7b83`.
- Published schematics compared: V1.0, V1.1, and V1.2.
- Schematic SHA-256 values:
  - V1.0: `a31422d5b0e9ec7fe75c7f5c973b0c5b40d5e5148605caa817aedc1a6e4455a3`
  - V1.1: `c4f7e1d61f490290ada1b312d6200a6b2838802f4d780af9699ca3797e35cadd`
  - V1.2: `7f9ac57dab96110467e4a9728cfb71b46024ea57f28a2d9d8b545b3ff0d0c172`
- Pinned ESP-IDF: 5.5.3, commit `2c211b236707889e8400c4dc5644dd5c4ee071e0`.
- Pinned panel component: `espressif/esp_lcd_ek79007` 1.0.2, component hash `07c1afab7e9fd4dd2fd06ff9245e65327c5bbd5485efec199496e19a9304d47b`.
- Committed component resolution: `apps/display_diag/dependencies.lock`; direct requirement: `apps/display_diag/main/idf_component.yml`.

The exact machine-readable evidence is `hardware/evidence/elecrow-10.1-display-path.json`; prefer it over prose when values conflict.

## Cross-revision electrical invariants

| Property | Proven value |
| --- | --- |
| Panel/controller | EK79007 |
| Active resolution | 1024×600 |
| Pixel format | RGB565, 16 bits per pixel |
| DSI | Bus 0, two dedicated data lanes |
| Lane rate | 900 Mbps |
| DPI clock | 51 MHz |
| Horizontal back/pulse/front | 160 / 70 / 160 |
| Vertical back/pulse/front | 23 / 10 / 12 |
| DSI PHY rail | Internal LDO3, 2500 mV |
| Panel rail | Internal LDO4, 3300 mV |
| Backlight | GPIO31, MT9201, 30 kHz, 11-bit PWM |
| Reset | Software reset; driver GPIO `-1` |

GPIO29 is not required: Q11 is marked `AO3401_NC` and R184=0R directly supplies the backlight rail in all published revisions. GPIO41 exists on the reset net but the current official Lesson07 and the hardware-tested path use software reset. Do not drive either GPIO.

## Driver behavior that matters

EK79007 1.0.2 performs software reset, selects two lanes with register B2=0x10, sends vendor registers 0x80–0x86 as `8B 78 84 88 A8 E3 88`, and exits sleep with 0x11 plus a 120 ms wait. It wraps an ESP-IDF DPI panel but does not install `disp_on_off`; calling the generic operation fails.

ESP-IDF 5.5.3 DSI bus creation contains hardware wait loops without a software timeout. The diagnostic enables task-watchdog panic so a failed PLL/lane transition resets instead of hanging forever. Keep the backlight dark before entering this code.

## M1 hardware result

The tested app-only image at offset 0x10000 was 226,560 bytes with SHA-256 `b41f24f629344d2b34181c649721f5cfa26f0adc5ad49f127202ced4c6781584`; its ELF SHA-256 was `9b69c38aa81fec43cbcb9edd8307b0682ab2fca63c450dfe0ec706bea856a23b`. Two clean builds were byte-identical, the flashed range read back exactly, serial showed every initialization milestone and repeated pattern cycle, and the user supplied a live Photo Booth view of visible vertical color bars.

This proves the bounded display path and pattern generator. It does not prove framebuffer submission, moving-image tearing behavior, touch, SD, audio, wireless, camera, or USB.

`components/platform_display/include/platform/display.h` therefore exposes patterns and brightness only today. A game framebuffer API is a new qualification boundary, not an implied capability of the M1 result.

The pinned doomgeneric tree's `-gfxmode rgb565` conversion is compatible with
this contract: its 16-bit branch masks and packs standard RGB565 words. The
red/blue offsets shown in its mode metadata are reversed, but that metadata is
not used by the explicit 16-bit conversion branch. The D1 platform adapter
nevertheless consumes Doom's explicit `rgba8888` numeric `0x00RRGGBB` words
and converts them to standard RGB565. That project-owned conversion is a
deliberate, host-testable engine/platform isolation seam, not a workaround for
an upstream color bug. Vendored engine code remains unchanged, and a native
test compiles the exact pinned `i_video.c` conversion to verify its primary
colors and little-endian byte layout independently.
