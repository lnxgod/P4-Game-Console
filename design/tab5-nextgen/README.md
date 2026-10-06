# GameChangersAI OS — Tab5 interface direction

23 screen concepts generated with the built-in ImageGen tool on 2026-10-05.
Open [the walkthrough](index.html), or run `python3 -m http.server 8876 --bind 127.0.0.1 --directory design/tab5-nextgen` from the repository and visit http://127.0.0.1:8876.

**Status:** the native implementation is now installed as **GameChangersAI OS 0.44 on Tab5 A and B**, with exact readback and boot-health checks passed. [View the actual C-rendered screens](native/index.html), [implementation details](IMPLEMENTATION.md), and [installation evidence](../../hardware/evidence/tab5-nextgen-0.44-testing.json). This original walkthrough retains the 23 design concepts; its values are illustrative. Battery charging remains unresolved and the charger candidate is disabled in the installed UI build. Version 0.42 remains the starting release; 0.44 is this native interface upgrade.

## Design intent

**Visual thesis:** a midnight-blue cyberspace game console with luminous cyan landmarks, generous white text and welcoming game artwork, taking atmosphere from *Hackers* while keeping everyday controls calm.

**Content plan:** Home gives one featured game a dominant Play action and three recent games; the five-route rail stays in place; settings and files use large rows or tiles; diagnostics keep raw detail in their own views. No floating Windows-style windows, decorative terminal chatter or fake live data.

**Interaction thesis:** selection should respond immediately, with a short 80–120 ms focus change; launch artwork appears only while real loading happens; optional motion stops entirely under Reduce motion. Startup sound must never hold back an interactive Home screen. These are implementation targets, not measured firmware timings.

## Hardware research

| Item | Verified fact and design consequence |
| --- | --- |
| Display | 5-inch IPS, 720 × 1280 physical orientation, used as **1280 × 720 landscape** over MIPI DSI. Use the full landscape width for the OS. |
| Density | Approximately **294 ppi**, calculated from diagonal and pixel dimensions. Landscape glass area is approximately 110.7 × 62.3 mm. Desktop monitor previews cannot establish physical readability. |
| Memory | ESP32-P4 with 32 MB PSRAM and 16 MB flash. Decode only visible artwork and nearby thumbnails; never load 23 full screenshots as a UI implementation. |
| Input | Capacitive touch and OS-normalized game controls. Every visible action needs both a comfortable touch target and a coherent controller focus path. |
| Battery | Removable NP-F550-type 7.4 V 2S pack, INA226 monitor, IP2326 charger. Voltage percentage is an estimate, not measured capacity. Standalone board and kit contents differ. |
| Panel variants | Existing A/ST7121 and B/ST7123 board paths must both remain supported; artwork does not authorize changing display timing or touch drivers. |

Primary sources: [M5Stack Tab5 documentation](https://docs.m5stack.com/en/core/Tab5), [M5Stack power API](https://docs.m5stack.com/en/arduino/m5tab5/power), [official schematic](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf), and [commit-pinned M5Unified power implementation](https://github.com/m5stack/M5Unified/blob/fd40d58b8405ad1e7ed7afab548dab5e7cec5bbb/src/utility/Power_Class.inl). Local reference files and license are in [research](research/).

Repository inspection found that the existing sharper UI is rendered at **1152 × 720**, centered with 64-pixel side margins, but still derives its layout from **320 × 200 logical coordinates** (`CONSOLE_SHELL_LAYOUT_WIDTH` in the shell header). Sharper text alone cannot fix the inherited spacing. A new native OS layout needs independent 1280 × 720 geometry and matching touch hit regions. Preserve the existing 320 × 200 / 768 × 480 game viewport contracts.

## Implementation specification

- Navy `#07121C` background; `#122735` quiet surfaces; `#F4FAFC` primary text; `#B4CBD5` secondary text; `#55E6EC` action/focus.
- Main labels 28–32 native pixels; titles 40–48 pixels; supporting text at least 24 pixels. Font size and contrast must be tested on the physical 5-inch screen, including Large text.
- Essential touch targets at least **80 × 80 native pixels** (about 6.9 mm); primary actions preferably 96 pixels tall (about 8.3 mm). The generated artwork is a composition reference: do not copy undersized back buttons or rail rows literally.
- Use a regular 24-pixel spacing rhythm, with 32-pixel separation between unrelated groups. Scroll long lists instead of squeezing more rows into the screen.
- Cyan fill uses dark text. Selected controls also need a shape/outline indicator, not color alone. Disabled labels must remain legible; use state text and reduced emphasis without reducing essential text below 4.5:1.
- Keep dense diagnostics off the main launcher. No scanlines, flicker, busy imagery behind routine labels or tiny faux-code decoration.
- Cover illustrations are conceptual packaging, not screenshots or promises about game graphics. Preserve actual game names and registry identities.
- All on-screen labels, percentages and controls must be real rendered text and widgets. Do not ship whole-screen generated PNGs as the functioning UI.
- At RGB565, a 1280 × 720 frame is 1,843,200 bytes (1.758 MiB). Two such frames are 3.516 MiB. Twenty-three full frames would be 40.43 MiB, exceeding PSRAM before games and drivers. Keep compressed assets on storage, load a bounded visible set, and leave required DMA allocations in internal memory.
- New settings such as text scaling, reduced motion, proposed terminal commands, and device-side clock sync presentation still need implementation. Wireless/MSC availability must come from actual platform capabilities.

### Palette contrast

Calculated from nominal sRGB values and from nearest 5/6/5 quantization. This verifies the palette, **not every generated pixel** or on-panel sunlight readability.

| Foreground / background | sRGB ratio | RGB565 ratio |
| --- | ---: | ---: |
| text / ink | 17.91:1 | 18.39:1 |
| text / surface | 14.57:1 | 14.74:1 |
| secondary / surface | 9.10:1 | 9.04:1 |
| ink / cyan | 12.51:1 | 12.78:1 |
| cyan / surface | 10.18:1 | 10.25:1 |

Essential text target: at least 4.5:1, preferably 7:1. Avoid cyan text on pale highlights. Art gradients need a solid text backing where contrast cannot be guaranteed.

## Screen coverage

All 19 current unique shell pages have a design counterpart; additional concepts cover boot, appearance, management and delete confirmation. `CONSOLE_PAGE_LIBRARY` is an alias of `CONSOLE_PAGE_GAMES`, not a missing page.

| Concept | Existing page or proposed subview | Notes |
| --- | --- | --- |
| 01 Home | `CONSOLE_PAGE_HOME` | Featured game and recent games. |
| 02 Games | `CONSOLE_PAGE_GAMES` | Game library; cover art is conceptual. |
| 03 Boot | `startup` | Brand first; no artificial delay or dial-up sequence. |
| 04 Settings | `CONSOLE_PAGE_CONTROL_PANEL` | Six clear routes into device settings. |
| 05 Battery | `CONSOLE_PAGE_POWER` | Failure-state example based on historical 1.14 V telemetry; not a live reading. |
| 06 Sound | `CONSOLE_PAGE_AUDIO` | Startup and game volume; sound controls shown as a design proposal. |
| 07 Appearance | `new appearance view` | Proposed text-size and reduced-motion controls. |
| 08 Files | `CONSOLE_PAGE_FILES` | Readable file rows and separated actions; sample files. |
| 09 Multiplayer | `CONSOLE_PAGE_MULTIPLAYER` | Unavailable wireless features stay explicit; this artwork does not add transport support. |
| 10 Controllers | `CONSOLE_PAGE_CONTROLLERS` | USB and Bluetooth availability; Bluetooth remains unavailable in current firmware. |
| 11 Storage | `CONSOLE_PAGE_STORAGE` | Sample capacity only; no card was modified. |
| 12 Saves | `CONSOLE_PAGE_SAVES` | Sample save list; safe confirmation before deletion. |
| 13 Achievements | `CONSOLE_PAGE_ACHIEVEMENTS` | Illustrative progress, not the user's actual achievements. |
| 14 File transfer | `CONSOLE_PAGE_FILE_TRANSFER` | Transfer workflow design; no live transfer. |
| 15 Terminal | `CONSOLE_PAGE_TERMINAL` | Proposed help content; commands in the artwork are not a firmware capability claim. |
| 16 Sensors | `CONSOLE_PAGE_SENSORS` | Sample telemetry; no live feed in this preview. |
| 17 System | `CONSOLE_PAGE_SYSTEM` | 1280 × 720 device details; clock control is a proposed UI over the existing host-assisted service. |
| 18 USB drive | `CONSOLE_PAGE_USB_DRIVE` | Unavailable MSC mode stays explicit. |
| 19 Touch test | `CONSOLE_PAGE_TOUCH` | Touch calibration and target layout concept. |
| 20 Display test | `CONSOLE_PAGE_COLORS` | Color and readability test with a single selected navigation route. |
| 21 Launch | `CONSOLE_PAGE_EXTERNAL` | Game launch transition; artwork is not an actual running game. |
| 22 Game manager | `game management subview` | Install/remove game workflow; no real game modification. |
| 23 Delete confirmation | `destructive action modal` | Cancel first; confirmation changes nothing in this walkthrough. |

## Preview and source assets

The preview provides working navigation hotspots, a complete screen picker, related-screen buttons and a 1280 × 720 CSS-pixel mode. It never accesses the device, launches games, changes settings, transfers files or deletes data. Some controls are visual only; the “Show click targets” option identifies wired navigation.

[Exact generation prompts](prompts.json) record the built-in tool, original source paths, final output hashes, dimensions and revision prompts. All 23 PNGs are copied into [screens](screens/); originals remain in the ImageGen output directory. Artwork is approximately 16:9 at 1672 × 941; implementation uses native layout and separately prepared assets.

Artwork QA corrected game cover identity, achievement counts, navigation selection and muted controller text. Before implementation, verify all text contrast and enlarge compact back/rail hit regions. Browser checks establish preview navigation only. Physical readability, touch accuracy, focus flow and memory/performance need device acceptance after implementation.

## Preview verification — 2026-10-05

The browser walkthrough loaded all 23 screen images successfully. Native mode's DOM bounds measured exactly 1280 × 720. Home → Settings → Battery navigation worked; the save confirmation opened by Enter, Cancel returned to Saves, Escape returned from the modal, and the Delete preview displayed “Nothing was deleted.” No browser warning/error logs were reported. Fit mode was visually inspected on Home and Battery. Asset SHA-256 checks passed and the coverage table includes all 19 unique shell page enum values. These are browser/design checks, not firmware UI or on-device usability acceptance.
