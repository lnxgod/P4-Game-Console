# Tab5 bundles and resource sidecars

Read this when the requested game has a `.P4R` resource sidecar or the user has
selected a bundle/content installation. First-time Console OS provisioning uses
Set Up; a one-game update does not imply installing other games or Doom data.
Keep the card in the powered Tab5 and select its actual USB-C Serial/JTAG port.

## Resource before cartridge

Validate the resource and its matching cartridge, then transfer the resource
first. Replace these exact local paths and `<port>` with the selected artifacts
and intended unit's current port:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4R --class p4r --port <port>
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G --port <port>
```

Each format has its own directory and validator. Never use `exchange` to bypass
validation. Resource activation refreshes its catalog; Doom content activation
reboots. Native USB can reboot on open on macOS, which the tools tolerate.

## Requested bundle or Doom installation

The matching Tab5 build prepares `apps/console_os/build-tab5/sd-card`. For an
explicitly requested standard bundle, use one validated connection:

```sh
python3 scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <port>
```

`push-bundle` validates all selected native `.P4G` and `.P4R` files before
sending resources first. Preserve the main skill's standard/development content
rules; this command does not imply opt-in to WIP games.

For explicitly selected Doom content, follow the pinned data/licensing contract
in `AGENTS.md`, `third_party/game-data.json` and `docs/DOOM.md`, then provision:

```sh
python3 scripts/p4-usb-content.py doom --port <port>
```

USB Drive/MSC remains disabled on Tab5. The separate USB-A HID/XUSB host-power
path has its own board-owned exact-artifact and named-controller acceptance.
Native USB-C content transfer does not depend on USB-A host mode.
