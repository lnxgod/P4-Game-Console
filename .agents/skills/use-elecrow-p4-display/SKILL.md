---
name: use-elecrow-p4-display
description: Add, change, diagnose, flash, or test screen output on the Elecrow ESP32-P4 10 in variant (10.1-inch DHE04310D). Use for MIPI-DSI, EK79007, RGB565 framebuffers, display timing, backlight control, display_diag, LVGL, Doom or other game video, blank/corrupt/tearing screens, and any firmware that renders to the attached panel.
---

# Use the Elecrow P4 display

Use the hardware-tested display service and the pinned vendor/Espressif sources. Do not recreate the EK79007 driver or copy panel code into a game.

If a game only renders through the existing P4 surface and `p4/draw.h` APIs,
use `$develop-p4-games` and its focused game tests. Do not run panel diagnostics
unless the request changes the display service or diagnoses physical output.

## Load the display contract

Read these repository files before changing display-facing code:

1. `hardware/board-profile.json`
2. `hardware/evidence/elecrow-10.1-display-path.json`
3. `hardware/test-runs/2026-08-12-display-m1.json`
4. `components/platform_display/include/platform/display.h`
5. `references/display-contract.md`

Also use `$develop-esp32-p4-platform` for toolchain, build, flash, recovery, and general hardware rules.
For combined display/touch/audio firmware, read that skill's `references/elecrow-10-in-variant.md`; this display authorization never expands another subsystem.
When combined firmware enables speaker sound, also use
`$use-elecrow-p4-audio`; display initialization does not authorize or own I2S1
or GPIO30.

## Preserve the proven boundary

- Keep panel rails, MIPI-DSI, GPIO31 backlight, clocks, and scanout inside `components/platform_display`.
- Let apps and games consume a platform video API; never initialize display pins or rails directly from Doom or another game.
- Use `espressif/esp_lcd_ek79007` 1.0.2 from `apps/display_diag/dependencies.lock` and keep the direct requirement in `apps/display_diag/main/idf_component.yml`. Do not vendor Elecrow's factory UI, images, fonts, or LVGL stack merely to turn on the panel.
- Treat the cross-revision authorization as display-only. It permits DSI bus 0, LDO3 at 2.5 V, LDO4 at 3.3 V, and GPIO31 PWM. It does not authorize GPIO29, GPIO41, touch, SD, audio, camera, wireless, USB, or any other GPIO.
- Leave `.reset_gpio_num = GPIO_NUM_NC`; the tested driver performs software reset. Never drive GPIO29 or GPIO41 for this path.

## Initialize safely

Preserve this order:

1. Configure GPIO31 PWM at zero duty.
2. Acquire LDO3 at 2500 mV, then LDO4 at 3300 mV.
3. Create DSI bus 0 with two lanes at 900 Mbps.
4. Create 8-bit DBI command I/O and the 1024×600 RGB565 DPI panel at 51 MHz using the recorded porches.
5. Perform EK79007 software reset and initialization.
6. Produce a known first frame or hardware test pattern.
7. Enable the backlight only after every prior step succeeds.

Allow zero brightness during cleanup, but reject nonzero brightness until panel initialization completes. On every failure, request zero duty and emit a stage-specific serial marker.

Do not call `esp_lcd_panel_disp_on_off()` with EK79007 1.0.2: this driver does not install that operation and ESP-IDF 5.5.3 returns `ESP_ERR_NOT_SUPPORTED`. The official Elecrow Lesson07 also omits it.

## Build and test proportionally

Choose checks at the boundary that changed:

- Game pixels, layout, or animation through stable P4 APIs: run only that
  game's host tests.
- Scaling, submit, cache, or framebuffer ownership in `platform_display`: run
  the focused component tests and build the exact consuming app.
- Panel power, MIPI-DSI, DPI timing, pixel format, or backlight: run the
  established diagnostic sequence below and one physical acceptance.

For that panel-level diagnostic:

```sh
make verify
python3 scripts/verify-display-path.py
make build APP=display_diag
python3 scripts/verify-display-diag.py apps/display_diag/build app-flash
```

Do not run repo-wide `make check` by default or run unrelated audio, touch,
storage, USB, and controller suites. Reserve a full suite for an explicit user
request, lock/toolchain work, or a genuinely cross-cutting platform change. Do
not repeat an unchanged build or diagnostic. Keep reproducible builds enabled
and update reviewed reproducibility evidence only when that evidence is part of
the changed contract.

Use only the guarded app-partition write for the diagnostic:

```sh
make flash-app APP=display_diag PORT=/dev/cu.<port>
```

The preflash verifier must pass after the flash-time rebuild. Do not use full-project flash for a display-only test.

## Qualify runtime behavior

Require all of the following before recording `hardware-tested`:

- exact application readback hash matches the reviewed binary;
- `PANEL_READY` reports 1024×600, RGB565, 900 Mbps, and 51 MHz;
- `BACKLIGHT_SET` occurs only after the first pattern/frame;
- vertical bars, horizontal bars, and BER patterns continue cycling without a failure marker;
- a person or camera directly confirms visible output.

Software logs cannot prove visibility. Record a still image as gross scanout evidence only; do not infer tear-free motion or long-duration stability from one frame.

## Extend from patterns to game video

The M1 pass validates panel initialization and the DSI hardware pattern generator, not a game framebuffer. For Doom or another game:

- extend `platform_display` with a bounded RGB565 surface/submit contract;
- keep the 1024×600 scanout mode and place scaling below the game API;
- prefer a centered 3× nearest-neighbor expansion of Doom's 320×200 frame to 960×600 with 32-pixel black side margins for the first proof;
- qualify PSRAM buffer ownership, cache synchronization, frame completion, and tearing separately;
- keep DMA2D disabled until an explicit build and hardware test qualifies it;
- do not add LVGL unless the app actually needs retained UI widgets.

The current `platform/display.h` includes the bounded, serialized framebuffer
submit path used by the Doom variants. Preserve these semantics unless
measurements justify a separately reviewed design:

- input is a compact 320×200 array of standard RGB565 numeric words (`R[15:11]`, `G[10:5]`, `B[4:0]`) with an explicit pixel stride;
- the caller retains ownership, must not mutate the input during the call, and may reuse it after the call returns;
- `platform_display` owns every panel framebuffer and raw ESP-IDF handle, performs the 3× scaling and black margins, and returns a real timeout or state error instead of blocking forever;
- the service serializes initialization, submit, brightness, and teardown; no app may write a live scanout buffer concurrently;
- use ESP-IDF's DPI draw path for its cache writeback behavior. If a future zero-copy path writes a DPI framebuffer directly, document its alignment, cache synchronization, refresh-completion, and buffer handoff state machine before enabling it;
- keep the backlight dark and release or neutralize every pending submit on initialization or runtime failure.

The pinned Doom engine's `-gfxmode rgb565` conversion is standard RGB565. Its
16-bit branch masks and packs channels as `R[15:11]`, `G[10:5]`, `B[4:0]`;
the reversed red/blue offsets printed by its mode metadata are not used by that
branch. Keep the D1 runtime in explicit `rgba8888` mode, where each
`DG_ScreenBuffer` word is numeric `0x00RRGGBB`, and convert the 320×200 words
to standard RGB565 in project-owned adapter code. This is a deliberate,
host-testable engine/platform isolation seam, not a workaround for an upstream
color bug. Prove the adapter and the exact pinned RGB565 branch independently,
including red, green, blue, white, black, byte order, stride, and scaling. Do
not modify the pinned engine.

Re-run the display diagnostic after changing panel power, DSI, DPI, pixel format, backlight, or buffer lifecycle code. Then run a framebuffer-specific diagnostic before claiming Doom video works. That diagnostic must include distinct red/green/blue/white/black fields, labeled corners and edges, the exact 32-pixel side margins, a moving high-contrast tear pattern, a frame counter, and serial counts for submits, completions, timeouts, underruns, and failures. Record the run duration and observed counts; a still photograph alone is insufficient.

## Diagnose without guessing

- If serial stops before `POWER_READY`, inspect only the recorded LDO steps.
- If it stops during DSI creation, use the configured watchdog reset evidence; do not change lane rate or pins speculatively.
- If `PANEL_READY` appears but the panel stays dark, verify power margin and GPIO31 PWM before changing software reset or touching GPIO29/41.
- If colors are wrong, verify RGB565 byte/order conversion at the platform boundary.
- If geometry is unstable, restore the exact recorded Elecrow timing instead of using the generic EK79007 timing macro.

Keep touch, storage, audio, and controller investigations in their own diagnostics and skills so a display pass never becomes an unsupported whole-board claim.

## Game Changers AI OS release quality

For game-related work, apply [the launch and remix gates](../../../docs/LAUNCH_QUALITY.md).
Preserve gameplay and saves, keep incomplete titles out of default bundles,
and distinguish native-size art, operator feedback and measured P4 cadence.
