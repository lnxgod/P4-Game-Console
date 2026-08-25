# Console controller architecture

## Layers

1. `platform_usb_host` or `platform_ble_host`: one transport owner and lifecycle.
2. `platform_gamepad_usb` or `platform_gamepad_ble`: bounded HID discovery and report transfer.
3. `gamepad_core`: descriptor parsing, field extraction, profiles, normalization, and transport snapshots.
4. `platform_gamepad`: deterministic USB-priority/BLE-fallback broker.
5. Game adapter: maps canonical controls to semantic actions or engine events.

Only layer 5 belongs to Doom or another game.

## P4 controller choice

Current managed `espressif/usb` supports the P4 HS and FS host controllers. Published CrowPanel Advanced reference schematics route the exposed USB data pair to the dedicated HS PHY. The FS controller uses GPIO26/GPIO27, which may conflict with board functions and is not a fallback unless the exact board profile and fixture authorize those pins. Do not enable either path on the unconfirmed unit from reference data alone.

The Waveshare 4.3 profile is separate: H2 carries the authorized full-speed
Host/HID runtime path when external, backfeed-safe 5 V is supplied. It does not
become a powered host merely because firmware selects host mode. Keep H1 CH343
for programming, serial, and bounded file transfer. H2 can switch to USB-device
MSC only from the USB Drive app after Host/HID stops and Console OS releases
the SD filesystem.

Hubs introduce a second untrusted descriptor/lifecycle boundary. Bound hub
ports, downstream descriptors, control transfers, retries, and teardown. Keep
the working forced-full-speed policy unless new electrical and protocol
evidence authorizes another route. One controller behind one recorded powered
hub does not prove every transaction-translator topology or multi-pad case.

## Waveshare BLE HID

The ESP32-P4 reaches the onboard ESP32-C6 only through the profile-authorized
SDIO Hosted link. `platform_ble_host` is the sole NimBLE owner and registers
both the controller central and multiplayer GATT service before startup. Its
configuration permits two encrypted links, four bounded bonds, and sixteen
CCCDs. Controller pairing pauses an idle multiplayer browser because NimBLE
has one scan procedure, then restores browsing after pairing completes.

`platform_gamepad_ble` accepts only connectable HID service/appearance
candidates, discovers bounded report-map/reference/CCCD state, and parses the
same untrusted report map as USB. Persistent reconnect may use a saved name to
find a rotating advertisement, but encrypted peer identity must match the
saved address before a snapshot becomes ready. Never expose peer addresses or
bond keys to games or ordinary logs.

## Canonical state requirements

- connection and stable device identity;
- monotonically increasing sequence plus timestamp;
- 64-bit button set;
- centered D-pad/hat representation;
- signed 16-bit normalized axes;
- unsigned 16-bit triggers;
- atomic or lock-protected snapshot publication.

Neutral is disconnected, no buttons, centered axes/D-pad, and zero triggers. Publish it immediately on disconnect or fatal decode failure according to policy. The broker selects connected USB first and connected BLE second; transport changes never merge partial states.

## Profile policy

Descriptor-derived mappings are the generic baseline. VID/PID profiles may correct known quirks but must not bypass bounds validation. Store captured descriptor bytes and a human-readable mapping fixture in tests; do not require the physical controller for every parser regression run.
