# Tab5 Bluetooth and standalone Wi-Fi core candidate

The user approved the bounded C6 radio proposal, which has been applied and
extended into Console OS 0.45 on the existing 0.44 interface. `candidate.patch`
is the historical proposal; active component sources and exact-artifact evidence
are authoritative. Host tests pass; two-device wireless acceptance is pending.

The previous radio-disabled Tab5 build forced `P4_CONSOLE_BLE_MULTIPLAYER=0` and did not link
the shared BLE service. That caused the USB-only selector. The candidate selects the
existing encrypted NimBLE P4MP service and lazy startup, supplies Tab5's own
SDIO pins, and removes the ESP-Hosted pre-main constructor. Radio starts only
when selected in Multiplayer. It does not add Bluetooth controller support.

Evidence is in `proposal.json`. The hash-pinned official C145 schematic, sheet 1,
connects CLK/CMD to GPIO12/13, D0–D3 to GPIO11/10/9/8, and C6 EN through R4 to
GPIO15. LPW5209 switches the WLAN 3.3 V rail from expander 0x44/P0. The pinned
Espressif BSP feature-enable implementation also sets P0 high to enable radio.
The selected 10 MHz SDIO rate is conservative; ESP-Hosted 1.4.7 hardcodes slot 1.
Its Kconfig explicitly defines `RESET_ACTIVE_HIGH` as high-low-high, i.e. pulling
C6 EN low resets it. No inferred Waveshare pin map is used.

The proposed power operation holds the existing board mutex, masks only P0 in
latch/direction/high-impedance registers, and verifies readback. It never resets
the expander or changes USB, charging or shutdown bits. Its pure C model was
extracted to `/tmp/p4-tab5-radio-model` and passed ASan/UBSan tests for all 256
latch patterns and read/write/readback failures. These tests use memory only.

The pinned Hosted 1.4.7 / Wi-Fi Remote 0.14.5 dependency versions remain unchanged.
The C6 stays lazy: selecting Multiplayer starts Bluetooth, while Local Wi-Fi
switches to the shared OS Wi-Fi adapter. Hosting creates a standalone local AP;
joining discovers and associates automatically. No router or internet is needed.
The current implementation supports two consoles and uses the same P4MP service
for native cartridges and Doom. Games retain their own bounded gameplay protocol.

Radio power and charging have separate masked expander controls and tests. C6
SDIO slot 1 cleanup is isolated from microSD slot 0. USB-A remains independent.
The exact-artifact install guard binds BLE, Wi-Fi and 500 mA charger selection.
No C6 flash write or upgrade is included. Existing C6 firmware must demonstrate
HCI and Hosted Wi-Fi support on hardware before either is claimed operational.
Verify both roles, connection, game start, racing, Back and disconnect alongside
SD/display health. The open local Wi-Fi AP exposes only the bounded P4MP UDP
service; it provides no internet or file-transfer service.

Primary sources:
- [C145 schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf)
- [Pinned BSP feature control](https://github.com/espressif/esp-bsp/blob/73ee07b1ad56f865f13a2739be8bb6808c1da58a/bsp/m5stack_tab5/src/bsp_feature_en.c)
- ESP-Hosted 1.4.7 Kconfig and `host/api/include/esp_hosted_config.h`, from the
  existing component cache with lock hash `acda00d70be52c148b5f1c22e34c83fd182650c8e96ef83e81173627b1b13a0f`.
