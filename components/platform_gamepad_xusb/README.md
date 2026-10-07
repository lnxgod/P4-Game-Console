# Wired XUSB controller service

This is the Console OS adapter for the wired Xbox 360 packet format used by
XInput-mode controllers. It is a host/build-tested candidate. No named
Xbox-compatible controller has completed Tab5 hardware acceptance yet.

Tab5 starts it beside USB HID before enabling USB-A power. It uses the existing
USB host daemon, owns one vendor-class lease and selects one alternate-zero
interface with class/subclass/protocol `ff/5d/01`. The configuration parser
bounds every descriptor and requires a usable interrupt IN endpoint.
Xbox One/Series GIP (`ff/47/d0`), wireless receivers (`ff/5d/81`) and
model-specific startup commands are outside this adapter.

Input becomes the existing canonical buttons, D-pad, sticks and independent
triggers, then passes through the shared remapping/broker API. Priority is
USB HID, wired XUSB, then BLE. Games and Doom need no XUSB-specific code.
This adapter sends no rumble, LED or vendor initialization commands.

The USB client task alone owns handles and transfers. Callbacks copy at most
64 bytes; parsing happens in its bounded work step. Disconnect neutralizes
immediately. Teardown halts/flushes the input endpoint and waits for its
completion before releasing the interface, closing the device or freeing
the transfer. Uncertain cleanup retains resources and reports a fault rather
than freeing live USB memory. Stop requires a quiescing shared host.

The console logs `XUSB_READY` and `XUSB_CONNECTED` with VID/PID, interface and
configuration SHA-256. Those markers prove software state only; use the
[controller acceptance checklist](../../.agents/skills/esp32-controllers/references/acceptance.md)
for physical buttons, axes, hotplug and sustained gameplay.

Run `make gamepad-host`. It covers the protocol's strict framing and all stick
values, malformed descriptor/report cases, actual class-client work-step
behavior against a simulated USB stack, delayed cancellation, stale
completions, fast reconnect, cleanup fault retention, and broker remapping.
The simulation does not prove ESP-IDF scheduling, USB electrical operation or
the user's controller. Build with `make console-os-tab5-idf`.

Protocol facts are pinned in
[third_party/xusb-protocol.json](../../third_party/xusb-protocol.json).
The implementation is project-owned MIT code; no Linux driver code is vendored.
