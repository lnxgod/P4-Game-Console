# Core OS successor contracts

Read this reference for OS versioning, interface/rendering changes or a
firmware-only successor. The release numbers below identify the historical
origin of retained behavior; they do not direct a rollback.

## Interface, rendering and versioning

Use 0.44's nextgen Tab5 interface as the layout and styling baseline. Version
successors consistently in `apps/console_os/CMakeLists.txt`,
`apps/console_os/app-metadata.json`, `components/console_shell/include/console/brand.h`
and the generated update package. Retain 0.46's continuous Games/Files scrolling,
cartridge-derived category filters and protected file actions. Retain 0.47's
cached scrolling, native damage replay and bounded momentum; do not route every
scroll frame through a full scene redraw. Compare cached frames to the full
renderer and preserve the display buffer reuse fence when optimizing.

Ordinary-file Open must never lead to deletion. Removal belongs in the small
actions menu followed by a named confirmation with Cancel selected. Preserve
uncommitted interface/game work; do not rebase or overwrite a shared working
tree to manufacture a clean base.

## Firmware-only successors

For an app-only Tab5 successor during independent game remixes, use:

```sh
P4_TAB5_FIRMWARE_ONLY=1 make console-os-tab5-idf
```

When preparing it for an existing exact-unit authorization, also preserve its
chosen peripheral feature selections. This verifier excludes the removable
content bundle while retaining firmware, board, slot, peripheral and update
digest checks. Record `firmware_only: true` in the new app-only authorization.
It never authorizes installing an incomplete bundle.

Check [protected payload lineage](../../../../docs/GAME_SDK.md#protected-game-payloads)
even for firmware-only builds: the Red Dragon allowlist is regenerated from its
paired cartridge. Confirm installed protected content matches that exact OS
lineage, or complete the authorized paired content update. Preserve the guard
and save identity.
