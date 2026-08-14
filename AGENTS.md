# ESP32-P4 badge platform invariants

Use the repository skills in `.agents/skills` whenever their descriptions match:

- `develop-esp32-p4-platform` for toolchain, build, flash, monitor, recovery, or board bring-up work.
- `use-elecrow-p4-audio` for the 10 in variant factory I2S1/GPIO30 speaker path, Doom sound effects, audio diagnostics, or acoustic acceptance.
- `use-elecrow-p4-display` for the 10 in variant panel, framebuffer, backlight, or game-video path.
- `add-usb-gamepad-support` for USB HID controllers or game input integration.

Keep these rules true for every change:

1. Treat `toolchain.lock.json` and committed ESP Component Manager lockfiles as authoritative. Do not silently upgrade SDKs or components.
2. Do not hard-code an Elecrow pin map unless the exact size/SKU/revision is recorded, or a narrowly scoped peripheral authorization proves the relevant circuit invariant across every published revision. A scoped authorization never unlocks unrelated pins or peripherals.
3. Never write to a new board before preserving its complete factory flash and recording its size, SHA-256, and hashed live-device binding in a manifest.
4. Never recommend a passive OTG adapter for this Elecrow panel. Treat its USB-C port as sink-wired unless the exact PCB proves otherwise; controller tests need a powered, current-limited, backfeed-safe host shim.
5. Put reusable services in `components/`; games consume stable platform APIs and never own USB host handles, display drivers, or raw peripheral callbacks.
6. Treat USB descriptors and reports as untrusted input. Bound all lengths/counts and neutralize controller state immediately on disconnect.
7. Keep copyrighted commercial Doom WADs out of the repository. Use Freedoom or a user-supplied legally owned WAD outside Git.
8. A build is not hardware verification. Record the serial evidence and exact hardware used for every on-device acceptance result.

## Set up Doom game data on a fresh clone

WAD files are local inputs, never repository content. Keep them under the
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
