# Doom with native ESP32-P4 USB gamepad input

This is a separate build-only composite. It preserves the proven
`apps/doom_embedded` E1 source and artifact while adding a dormant reusable
native USB input path. With the committed configuration it runs the same
silent embedded-WAD/display baseline with neutral input; it makes no platform
USB call. No Mac relay, UART protocol, SD card, audio, or touch service is used.

The runtime dependency direction is:

```text
platform_usb_host (ESP32-P4 HS peripheral 0)
  -> platform_gamepad_usb (bounded HID lifecycle and reports)
  -> gamepad_core (canonical complete snapshots)
  -> doom_gamepad_input (Doom semantic events)
  -> DG_GetKey (engine key events)
```

The host starts with its P4 root data port disabled. The HID service acquires
its class lease and registers first; only then does the app explicitly enable
the root port. Shutdown reverses ownership safely: quiesce the root port,
stop HID and release its lease, then stop the host daemon. Disconnect and
snapshot failures produce releases before any new presses.

## Controls

- D-pad or left stick: move/turn
- South button or right trigger: fire; South also accepts menus
- East: use/open
- West or left-stick click: run
- Left trigger: strafe modifier
- Left/right shoulder: strafe left/right
- Back: menu back; Guide: automap; Start: pause
- North/right-stick click: next/previous weapon

The exact captured `0079:0011` controller profile currently exposes its
D-pad and ten buttons. Its five byte-wide axes all repeat ambiguous usages, so
the reusable parser deliberately ignores them instead of guessing stick
semantics. The general left-stick mapping applies to controllers whose
descriptors identify those axes unambiguously.

## Electrical gate

The committed USB configuration is deliberately inert. After bringing up the
authorized display/WAD/video baseline, it prints `P4_DOOM_GAMEPAD E3
INPUT_BLOCKED`, leaves `DG_GetKey` neutral, makes no platform USB call, and
continues into silent Doom. All flash and fixture authorization flags remain
false.

The artifact is still a meaningful native-input link proof: an app-local
flash-resident volatile USB authorization byte prevents the optimizer from
discarding the complete USB branch. Its committed value is zero. The compiled
fallback fixture record is also invalid by construction,
and the reusable host component performs a second volatile read of its own
zero build-authorization byte. Thus bypassing the first branch still cannot
install or enable USB. Both builds run a post-link audit that fails if the host
install, event daemon, HID start, or root-enable graph is discarded. An
authorized artifact must be rebuilt with reviewed fixture values; changing one
gate is insufficient.

J16 is the direct ESP32-P4 HS data path, but the CrowPanel cannot provide
protected 5 V VBUS to a bus-powered controller. Do not use a passive OTG
adapter or bridge panel 5 V onto J16. A hardware run requires a reviewed
fixture that routes J16 D+/D-/ground directly, leaves panel-side J16 VBUS
open, supplies controller VBUS through a regulated current-limited source,
blocks backfeed, and exposes overcurrent status. The evidence hash and every
fixture Kconfig property must be bound before this app can leave its fail-
closed path.

When that gate exists, build with the pinned toolchain and run the host suites
first:

```sh
make gamepad-host
make build APP=doom_embedded_gamepad
```

A successful build is not controller support evidence. Hardware acceptance
must name the exact image, controller, board/fixture, descriptor, mappings,
hotplug behavior, disconnect neutralization, serial markers, and app readback.

## Prepared app-only install gate

The exact silent baseline image is reproducible across the canonical build and
two independent clean build directories:

```text
application bytes: 4927824
application SHA-256: 32814a97b72b3b227e2ace02393222178ceebf02d18a4f0b35bead1f4584ea24
offset: 0x10000
```

Its central app-only route is prepared but committed inactive. Activation is
deliberately narrow: set only `flash_app_authorized` and
`one_shot_authorization_active` to true in `app-metadata.json`, and set only
`classification` to `user-requested-doom-e3-app-only-one-shot-active` plus
`active` to true in the E3 one-shot authorization record. Broad/full-project,
USB runtime, and USB fixture flags stay false. Then this one command verifies,
seals, writes only the saved factory app partition, performs ordered no-reset
chunked readback, and issues one explicit run:

```sh
make flash-app APP=doom_embedded_gamepad PORT=/dev/cu.<uart-port>
```

The durable one-shot ledger is consumed on success or failure. There is no
automatic second attempt. After the terminal attempt, restore the four
activation fields to their inactive values before recording the hardware
result. The WAD-bearing image is local-only and must not be redistributed.
