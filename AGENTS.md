# ESP32-P4 badge platform invariants

M5Stack Tab5 is the only actively maintained Console OS board target. All
other board targets are legacy; preserve their source and recovery contracts,
and use their board-specific workflows only for explicitly requested legacy
maintenance. New development, setup, and default validation target Tab5. Use
`make console-os-tab5-idf` (or its default alias `make console-os-idf`),
`docs/boards/M5STACK_TAB5.md`, and the Tab5 native USB content/guarded-flash
workflow. Elecrow uses `make console-os-elecrow-idf` explicitly. Never inherit
another board's flash authorization. New game scaffolds are drafts until
play-tested; follow `docs/GAME_LIBRARY.md` for names, categories and release
quality. Retired game identities in `games/retired.json` stay reserved.

Game creation is native-only: use C and `.P4G` through the stable Game API.
Free-form custom 2D engines, software 3D and raycasting are allowed within the
same surface, lifecycle and resource contracts; helpers are optional. The old
Lua game-creation stack, tooling and skill are removed.
C++ ports need an explicitly tested C-ABI/toolchain adapter; current packaging
supports C sources. See `docs/GAME_SDK.md` and `docs/GAME_PERFORMANCE.md`.

Use [`installos`](.agents/skills/installos/SKILL.md) as the default entry point
for Console OS installation, provisioning and first-time setup. Ask whether the
user will have a microSD card unless already answered. The default content is
basic games plus the pinned Doom shareware WAD, including its verified download
when missing. Offer optional Chex Quest only with explicit selection and SD.
Current Tab5 game storage requires microSD. The intended future no-SD route
would use internal flash and support adding SD at a later restart without
reflashing; verify implementation before promising that route. Creating the
skill does not itself enable that firmware behavior.

Use the repository skills in `.agents/skills` whenever their descriptions match:

- `develop-p4-games` for creating, porting, modifying, packaging, installing, or testing storage-installed `.P4G` games.
- `develop-esp32-p4-platform` for toolchain, build, flash, monitor, recovery, or board bring-up work.
- `use-elecrow-p4-audio` for the 10 in variant factory I2S1/GPIO30 speaker path, Doom sound effects, audio diagnostics, or acoustic acceptance.
- `use-elecrow-p4-display` for the 10 in variant panel, framebuffer, backlight, or game-video path.
- `add-usb-gamepad-support` for USB/Bluetooth HID controllers, pairing,
  canonical controller input, or game input integration.
- `test-console-os-builds` for fresh-session Console OS/Game API build tests,
  guarded successor installs, recovery, and honest manual game acceptance.
- `develop-p4-console-games` for creating or changing native games, choosing
  their `GAMES/<TYPE>` folder, and integrating Game API drawing, controls, and
  sound without giving games raw hardware ownership. Accept free-form game
  ideas; use `docs/GAME_STARTERS.md` as optional guidance, not a mandatory
  template menu. New games should map gameplay and menus to OS-normalized
  controls so supported controllers work without per-game USB/BLE code.
- `create-p4-game-art` for native-resolution art, launcher icons, provenance
  and deterministic packing without changing gameplay or save identities.
- `develop-p4-multiplayer-games` automatically alongside game authoring when
  a game needs linked-console co-op, versus or synchronized play. Reuse the
  OS-owned Host/Join, session and registration services; implement only the
  game's bounded protocol and rules. Do not promise an unavailable board link.
- `test-p4-games-locally` for the required SDL3 play-test, sanitizer smoke,
  and gameplay-tuning loop before building native-game firmware candidates.

Keep these rules true for every change:

1. Treat `toolchain.lock.json` and committed ESP Component Manager lockfiles as authoritative. Do not silently upgrade SDKs or components.
2. Do not hard-code an Elecrow pin map unless the exact size/SKU/revision is recorded, or a narrowly scoped peripheral authorization proves the relevant circuit invariant across every published revision. A scoped authorization never unlocks unrelated pins or peripherals.
3. Never write to a new board before preserving its complete factory flash and recording its size, SHA-256, and hashed live-device binding in a manifest.
4. Never recommend a passive OTG adapter for this Elecrow panel. Treat its USB-C port as sink-wired unless the exact PCB proves otherwise; controller tests need a powered, current-limited, backfeed-safe host shim.
5. Put reusable services in `components/`; games consume stable platform APIs and never own USB host handles, display drivers, or raw peripheral callbacks.
6. Treat USB and BLE HID descriptors/reports as untrusted input. Bound all lengths/counts, require encrypted identity-bound BLE pairing, and neutralize controller state immediately on disconnect.
7. Keep copyrighted commercial Doom WADs out of the repository. Use Freedoom or a user-supplied legally owned WAD outside Git.
8. A build is not hardware verification. Record the serial evidence and exact hardware used for every on-device acceptance result.
9. For game work, follow [the ESP32-P4 performance contract](docs/GAME_PERFORMANCE.md): native 768×480 with 320×200 fallback, a 60 FPS target and actual-device 30 FPS release floor. Prefer bounded shared rendering and retain fractional motion. Host CPU results do not qualify device cadence; preserve exact package/OS/unit evidence and leave unmeasured acceptance pending.

10. Every released game needs recognizable launcher artwork and a coherent opening/ready view, legible game identity during play, and appropriate pause/results screens. Follow [the complete presentation contract](docs/GAME_ART.md#complete-game-presentation); native games carry their own validated launcher icon. Preserve direct-touch card/board play and multiplayer start barriers.

11. Apply [launch and remix quality](docs/LAUNCH_QUALITY.md) when preparing Game Changers AI OS releases: preserve gameplay/save identities during visual remixes, exclude incomplete prototypes from default bundles, use artwork matching the actual title, and retain failed device acceptance until retested.

## Set up Doom game data on a fresh clone

WAD files are local inputs, except the owner-authorized Pure Hades v0.6
pack under `game-data/pure-hades/v0.6/`, with its unchanged music and notices.
That narrow exception does not permit other WADs or WAD-bearing firmware in Git. Keep them under the
ignored `local-data/doom/` directory and confirm `git check-ignore` succeeds
before building. The exact identities and upstream URLs are authoritative in
`third_party/game-data.json`; do not substitute an unverified mirror or commit
a downloaded WAD.

The embedded 10 in Doom apps currently require the unmodified Doom v1.9
shareware IWAD at `local-data/doom/doom1.wad` (4,196,020 bytes, SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`).
One reproducible local-development acquisition recorded by this repository is:

```sh
mkdir -p local-data/doom
curl --fail --location \
  'https://archive.org/download/wadarchive/DATA/5b.zip/5b%2F2e249b9c5133ec987b3ea77596381dc0d6bc1d%2F5b2e249b9c5133ec987b3ea77596381dc0d6bc1d.wad.gz' \
  | gzip -dc > local-data/doom/doom1.wad
echo '1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  local-data/doom/doom1.wad' \
  | shasum -a 256 -c -
git check-ignore -q local-data/doom/doom1.wad
```

The historical publisher archive is `doom19s.zip`; its URL and archive hash
are also pinned in `third_party/game-data.json`. Preserve its original
shareware notices. Do not use or redistribute registered/commercial IWADs
unless the user supplies a legally owned copy and the requested use permits it.

For a fully redistributable alternative, download the official Freedoom 0.13.0
release and extract `freedoom1.wad` locally:

```sh
p4_wad_tmp="$(mktemp -d)"
curl --fail --location \
  'https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip' \
  --output "$p4_wad_tmp/freedoom-0.13.0.zip"
curl --fail --location \
  'https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0-CHECKSUM' \
  --output "$p4_wad_tmp/CHECKSUM"
(cd "$p4_wad_tmp" && shasum -a 256 -c CHECKSUM --ignore-missing)
mkdir -p local-data/doom
unzip -j "$p4_wad_tmp/freedoom-0.13.0.zip" '*/freedoom1.wad' \
  -d local-data/doom
git check-ignore -q local-data/doom/freedoom1.wad
```

Use `make doom-smoke WAD=/absolute/path/to/freedoom1.wad` for the host proof.
The current embedded E5/E6 app build remains deliberately pinned to the exact
shareware `doom1.wad`; supporting Freedoom there requires a separately reviewed
artifact/evidence update. For SD-backed firmware use `DOOM1.WAD`,
`FREEDOOM1.WAD`, or `FREEDOOM2.WAD` at the SD root as documented in
`docs/DOOM.md`. Never push WADs, generated WAD assembly, WAD-bearing firmware
binaries, or local recovery images to GitHub.
