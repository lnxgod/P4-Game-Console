> Current result (2026-10-05, OS 0.45): the user confirmed attached batteries and
> requested charger enablement. The guarded, hash-bound update passed on both
> Tab5 units. Charger readback succeeded; A recovered to 6.814–6.931 V at roughly
> -480 mA, and B to 6.641–6.840 V at roughly -502 mA. Negative means charging on
> this shunt. Full charge and unplugged runtime remain untested. See
> [0.45 evidence](../../hardware/evidence/tab5-0.45-core-testing.json).
> The investigation and exclusions below describe earlier images.

# Tab5 battery investigation — 2026-10-05

**A firmware charger-control gap was found and a candidate fix builds successfully. Actual charging and battery operation remain unverified. No firmware was flashed in this task.**

## Evidence and limits

The installed 0.43 firmware initializes and reads the INA226 battery monitor, but had no Tab5 charger-enable initialization. Official M5Unified configures the separate 0x44 I/O expander for charging. The official schematic shows IP2326 EN held low by R59 until firmware drives CHG_EN. This explains a charging-control gap.

It does **not** fully explain the reported low voltage. The INA226 VBUS sense point is on SYS_BAT_PRE, on the battery side of the current shunt. A healthy attached pack should still be visible while charging is disabled. An absent, disconnected, protected or discharged pack remains possible; pack presence has not been confirmed by the user.

[Live serial samples](research/battery-live-read.json), captured 2026-10-05T19:50:53Z:
- Port /dev/cu.usbmodem1101 (last-recorded unit A): INA226 initialization OK, 1233–1234 mV, 0 mA, invalid battery voltage.
- Port /dev/cu.usbmodem2101 (last-recorded unit B): INA226 initialization OK, 1211 mV, 0 mA, invalid battery voltage.
- No new identity binding was performed. Opening the serial connection unexpectedly coincided with a reboot on both boards, despite no commands or reset request being sent. These were **not uninterrupted monitoring sessions**.

Do not turn these values into “0% battery,” claim the monitor is disconnected, or assert charging success. The new artwork's 1.14 V is a historical failure illustration. Most other screens' 72% is sample design data.

## Candidate change

`components/platform_tab5/src/charger_control.c` implements the official **500 mA selection with QC disabled** on I2C expander 0x44:
- P7 CHG_EN output high after configuration verification.
- P5 nCHG_QC_EN output high (QC disabled).
- P6 CHG_STAT input; no drive and no internal pull.
- Masked updates to latch, direction, high-impedance and pull-enable registers. Unrelated USB power, radio and shutdown pins are preserved.
- First configure with charging disabled, verify all affected registers, then enable and verify again.
- On a bus or readback failure, report failure and attempt to clear enable. If the bus remains unavailable, shutdown cannot be guaranteed and no success is reported.
- Reuse the existing 0x44 handle and board mutex shared with USB power; retry failed initialization from the sensor task every five seconds.

The control readback log proves only requested expander settings. It does not measure actual charging current or charge completion. Existing voltage validity and stale-reading rejection are preserved.

Source provenance: [pinned M5Unified power control](https://github.com/m5stack/M5Unified/blob/fd40d58b8405ad1e7ed7afab548dab5e7cec5bbb/src/utility/Power_Class.inl), `setChargeCurrent` Tab5 case; [official schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf), power page and expander pins. Exact hashes are recorded in `third_party/tab5-sensors.json`.

## Validation

- Tab5 focused host suite: **9/9 passed**, including ASan/UBSan charger tests, shared-pin preservation across 256 register seeds, every transaction failure, corrupt readback, stuck enable, device-handle reuse and service retry.
- Full `P4_TAB5_USB_HOST=1 make console-os-tab5-idf` completed with the pinned ESP-IDF 5.5.3 toolchain. Included required host suites passed.
- `scripts/verify-console-os-tab5.py`: build candidate verified, USB host enabled, **hardware_verified=false**, **flash_authorized=false**.
- App: 2,033,136 bytes; SHA-256 `fb6be1211e6427c88bfd0ed31af9b770dbe9457345e3d73f19c5db5a403da95b`.
- Candidate includes the pre-existing shared-workspace changes; the checksum identifies the exact build, not an isolated battery-only release.

The installed app remains the earlier 0.43 image, SHA-256 `b93336fb04b8d66bf2fe38d121cef29ae1c7563887516344523df385bdd2ba43`, according to the latest recorded install. It is not the candidate above.

## Remaining hardware qualification

Confirm whether a battery is attached to each unit and whether it runs when USB is unplugged. This distinguishes the UI/charger defect from pack connection or protection state.

Existing exact-artifact installation scopes exclude charger control. A charger-specific, identity-bound scope and the normal guarded successor-install evidence are required before flashing this new peripheral behavior; older authorization hashes must not be reused. See [platform skill](../../.agents/skills/esp32-fix-console/SKILL.md), “Use only the resources named by that authorization; every other pin and peripheral remains locked.”

After an authorized exact-artifact install, record unit and battery model, CHARGER_INIT readback, valid pack voltage, observed current direction with USB connected, unplugged operation, and return to USB charging. Readback alone is not acoustic, display or battery acceptance. No battery capacity or charge-time claim is made from this build.
