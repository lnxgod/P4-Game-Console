# USB input architecture

## Layers

1. `platform_usb_host`: controller selection, PHY choice, host daemon, power/fixture status, install and shutdown.
2. `platform_gamepad_usb`: HID interface lifecycle and bounded report transfer.
3. `gamepad_core`: descriptor parsing, field extraction, profiles, normalization, and state publication.
4. Game adapter: maps platform controls to semantic actions or engine events.

Only layer 4 belongs to Doom or another game.

## P4 controller choice

Current managed `espressif/usb` supports the P4 HS and FS host controllers. Published CrowPanel Advanced reference schematics route the exposed USB data pair to the dedicated HS PHY. The FS controller uses GPIO26/GPIO27, which may conflict with board functions and is not a fallback unless the exact board profile and fixture authorize those pins. Do not enable either path on the unconfirmed unit from reference data alone.

## Canonical state requirements

- connection and stable device identity;
- monotonically increasing sequence plus timestamp;
- 64-bit button set;
- centered D-pad/hat representation;
- signed 16-bit normalized axes;
- unsigned 16-bit triggers;
- atomic or lock-protected snapshot publication.

Neutral is disconnected, no buttons, centered axes/D-pad, and zero triggers. Publish it immediately on disconnect or fatal decode failure according to policy.

## Profile policy

Descriptor-derived mappings are the generic baseline. VID/PID profiles may correct known quirks but must not bypass bounds validation. Store captured descriptor bytes and a human-readable mapping fixture in tests; do not require the physical controller for every parser regression run.
