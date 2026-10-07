# Shared console services

[Back to P4 Game Console](../README.md) · [Console OS](../apps/console_os/README.md)

These reusable components serve the OS and multiple games. Native cartridges
consume the stable [Game API](p4_game_api/README.md), documented in the
[SDK](../docs/GAME_SDK.md), rather than owning raw display, audio, storage or
radio drivers.

| Area | Main components | Contract |
| --- | --- | --- |
| Game runtime | `p4_game_api`, `p4_game_platform`, `platform_game_loader` | Bounded lifecycle, rendering/input/audio callbacks and cartridge loading |
| Packages and catalog | `p4_game_package`, `platform_game_catalog`, `p4_game_save` | Validated identities, launcher metadata, resources and saves |
| Launcher and system UI | `console_shell`, `console_startup`, `p4_desktop`, `p4_bbs`, `p4_ansi`, `p4_cp437` | Shared shell, startup and text/ANSI drawing |
| Audio and music | `platform_audio`, `p4_game_platform`, `p4_midi` | OS-owned output, copied PCM commands and MIDI synthesis |
| Multiplayer | `p4_multiplayer`, `p4_multiplayer_registry`, `platform_multiplayer_wifi`, `platform_multiplayer_ble` | [P4MP session and transport ownership](../docs/MULTIPLAYER.md) |
| Controllers | `gamepad_core`, `platform_usb_host`, `platform_gamepad_usb`, `platform_gamepad_ble`, `platform_gamepad_xusb` | [Normalized input and board-specific support](../docs/CONTROLLERS.md) |
| Storage and updates | `platform_game_storage`, `p4_usb_content_transfer`, `platform_os_update`, `platform_readonly_blob` | Validated transfers, one storage owner and protected read paths |
| Engine adapters | `doom_*`, `p4_quake` | Integrated engine boundaries; game/data guides live in the [root inventory](../README.md#engine-games-and-ports) |
| Hardware | `platform_board`, `platform_tab5`, `platform_display`, `platform_touch`, `platform_battery`, board dependency components | [Exact-board profiles](../docs/boards/M5STACK_TAB5.md), pinned drivers and peripheral ownership |
| Optional game services | `p4_signal_scan`, `platform_signal_scan`, `platform_dice_ble` | Bounded radio snapshots and dice accessories |

The Tab5 foreground runs native game callbacks on P4 core 0; the shared audio
worker runs on P4 core 1. The C6 handles radio duties. Keep copied, bounded
commands and joined teardown; see [performance](../docs/GAME_PERFORMANCE.md).
A game should not create a private task or transport to work around an API limit.

Components with their own READMEs/tests keep their detailed contracts locally.
For changes, follow [ESP32 - Fix Console](../.agents/skills/esp32-fix-console/SKILL.md)
and run the focused host target plus the appropriate Tab5 build when required.
Successful host tests do not establish hardware acceptance.
