# Native interface implementation

Visual thesis: retain the approved midnight navy, cyan, bright text and cyberspace artwork as a functional 1280 × 720 console.
Content plan: Home features one installed game, Games exposes the complete catalog, the rail provides five stable routes, and large settings and utility views show real service state.
Interaction thesis: immediate touch/controller focus, bounded swipe paging with no accidental launches, a short nonblocking boot indicator, and no artificial loading delay.

Artwork is encoded from the generated production illustrations in components/console_shell/assets/nextgen. UI text and controls are drawn natively; screenshots are not used as buttons. The Tab5 native path supersedes the old desktop/BBS theme; other board renderers remain available for their existing hardware.

## Implemented in 0.44

The Tab5 native boot, Home, complete game library, Settings, Sound, Appearance,
Controllers, Battery, Storage, Files, game manager, transfer, USB-drive status,
System, Sensors, saved games, achievements, Terminal/keyboard, multiplayer views,
display/touch diagnostics and destructive confirmations all use the new renderer.
The old Tab5 BBS theme is compiled out. Other hardware ports retain their existing
renderers. Input uses one shared scene/control map for drawing, focus and hit testing.

Four generated production illustrations are packaged into firmware. Arimo font
atlases are rasterized at three native sizes. The 1024×576 hero uses indexed RGB565
color; buttons and text stay independent of the artwork. No online assets or image
decoding are needed at startup.

Existing service availability still applies: BLE, USB mass storage, offline
terminal network access and other unavailable services are shown as unavailable.
Appearance options apply to the session. Volume uses the existing persistent OS
settings. Native games keep their original viewport and APIs.

Validation: ASan/UBSan UI and SDL smoke suites; full-resolution rotation/bounds
tests; startup rendering/audio checks; pinned Tab5 firmware build and exact-artifact
verification. Native preview fixtures are explicitly labeled in native/index.html.
Battery telemetry remains active but charger control is disabled and absent from
the linked UI firmware pending compatible-pack qualification.

The exact image and serial installation results are recorded in
hardware/evidence/tab5-nextgen-0.44-testing.json. Two inherited boot diagnostic
strings still print “surface=rgb565-320x200” and “version=0.43”; these are stale
log labels. The ESP image descriptor, actual UI, update package and new INTERFACE
marker identify 0.44 and native 1280×720. They do not indicate an older installed
image.
