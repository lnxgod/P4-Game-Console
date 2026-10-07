# Firmware applications

[Back to P4 Game Console](../README.md)

These are ESP-IDF applications and hardware diagnostics, distinct from the
storage-installed native games in [`games/`](../games/README.md).

| Area | Role | Start here |
| --- | --- | --- |
| `console_os/` | Maintained Tab5 launcher, shared services and integrated engines | [Console OS](console_os/README.md) |
| `dice_core2/` | M5Stack Core2 dice accessory firmware | [Accessory README](dice_core2/README.md) |
| `bringup/`, `framebuffer_diag/`, `usb_host_diag/` | Focused board/toolchain diagnostics | [Platform guide](../docs/ARCHITECTURE.md), [board records](../hardware/) |
| `display_diag/`, `touch_diag/`, `gamepad_diag/` | Exact-board peripheral tests | [Display](display_diag/README.md), [touch](touch_diag/README.md), [gamepad](gamepad_diag/README.md) |
| `audio*_diag/` | Historical speaker-path diagnostics | [Audio diagnostic](audio_diag/README.md), [audio levels](audio_level_diag/README.md) |
| `storage_probe/`, `storage_diag/`, `sd_format_diag/`, `wad_provisioner/` | Storage inspection/provisioning experiments | [Probe](storage_probe/README.md), [storage](storage_diag/README.md), [format diagnostic](sd_format_diag/README.md), [WAD provisioner](wad_provisioner/README.md) |
| `doom*/` | Retained engine build and board-acceptance applications | [Doom game guide](../docs/games/doom/README.md), [historical acceptance](../docs/DOOM.md) |

A diagnostic is not an alternative Tab5 installer. Its presence does not
authorize flashing, formatting storage or using another board's pin map.
Use the [Tab5 guide](../docs/boards/M5STACK_TAB5.md) and
[ESP32 - Fix Console](../.agents/skills/esp32-fix-console/SKILL.md) for the
selected unit and task. Preserve old evidence without calling it current
hardware qualification.
