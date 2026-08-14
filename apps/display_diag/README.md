# Display diagnostic (M1)

This app exercises only the scoped 10.1-inch display path. It initializes the
ESP32-P4 MIPI-DSI host and EK79007 panel while the backlight is dark, selects a
hardware-generated test pattern, then enables GPIO31 PWM at 25 percent. It
cycles vertical bars, horizontal bars, and a BER pattern every three seconds.

The code deliberately does not initialize touch, storage, audio, camera,
wireless, or USB. GPIO29 and GPIO41 are never configured or driven. The app
contains no factory UI, images, fonts, or other vendor assets.

Build with the locked SDK and managed dependency:

```sh
make build APP=display_diag
```

The dependency manifest requires ESP-IDF 5.5.3 and
`espressif/esp_lcd_ek79007` 1.0.2. The generated `dependencies.lock` is
committed so a later dependency resolution cannot silently select another
panel driver release.

Expected serial milestones are deliberately machine-searchable:

```text
P4_DISPLAY M1 START
P4_DISPLAY M1 SCOPE display-only gpio29=untouched gpio41=untouched
P4_DISPLAY M1 BACKLIGHT_DARK
P4_DISPLAY M1 POWER_READY
P4_DISPLAY M1 PANEL_READY
P4_DISPLAY M1 PATTERN name=bars-vertical
P4_DISPLAY M1 BACKLIGHT_SET brightness_percent=25 visibility=pending-observation
P4_DISPLAY M1 HEARTBEAT
```

Any initialization or cycling error emits `P4_DISPLAY M1 FAIL` or
`P4_DISPLAY M1 FAIL_DARK` and requests zero backlight duty. A failure to
request zero duty is reported separately as `P4_DISPLAY M1 BACKLIGHT_ZERO_FAIL`.
Software never labels the panel visible; that claim requires direct observation.
An on-device M1
acceptance result needs both the serial capture and a direct observation of
all three patterns on the exact recorded panel. A successful build alone is
not an M1 acceptance result.

The 2026-08-12 run passed on the connected board: the exact app range read
back correctly, all serial milestones and continuing pattern cycles were
captured, and the user visually confirmed the test patterns. Evidence is in
`hardware/test-runs/2026-08-12-display-m1.json`.

App-only flashing is authorized through a fail-closed verifier bound to the
reviewed reproducible artifact and display-only resource set. Full-project
flashing remains denied. A different artifact must be rebuilt twice, reviewed,
and recorded before the verifier will permit it.
The component also refuses initialization unless the app explicitly enables
the display-only authorization option; that option does not inherit the broad
board pin-map gate and cannot authorize another peripheral.
