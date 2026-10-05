# M5Stack Tab5 port status

The dedicated display, touch, ES8388 audio and SD adapters compile into Console OS.
The shared launcher, cartridges, saves and Doom remain platform API clients.

Build: `make console-os-tab5-idf`.
Details, limitations, source pins and first-hardware workflow:
[Tab5 port notes](../../../docs/boards/M5STACK_TAB5.md).

Hardware verification, complete factory backup, hashed live-device binding and
first-write authorization are still pending. `flash_authorized=false`.
