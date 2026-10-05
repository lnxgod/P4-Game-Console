# M5Stack Tab5 port status

The dedicated display, touch, ES8388 audio and SD adapters compile into Console OS.
The shared launcher, cartridges, saves and Doom remain platform API clients.

Build: `make console-os-tab5-idf`.
Details, limitations, source pins and first-hardware workflow:
[Tab5 port notes](../../../docs/boards/M5STACK_TAB5.md).

A and B have full pre-install bridge-firmware backups and hashed identity bindings.
B passed exact readback, ST7123 driver initialization and launcher boot health;
physical screen/touch, audio, SD and gameplay acceptance remain pending. A awaits
its download port. Global `flash_authorized=false`; installations use explicit
per-unit artifact authorizations and `scripts/flash-console-os-tab5.py`.
