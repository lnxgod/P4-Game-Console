# Console OS content library

Console OS uses native `.P4G` cartridges and their declared `.P4R` resource
sidecars. See [the SDK](GAME_SDK.md) and
[the installation skill](../.agents/skills/develop-p4-games/SKILL.md) for
validated packaging and board-specific transfer. Native game changes do not
require an OS reflash while their API remains compatible.

The old Lua runtime, games, authoring tools and skill are removed. Console OS
supports native cartridges only; no Lua inspection, migration or creation
workflow is retained in the repository.

For Tab5, leave the card inserted and use the explicit connected port:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G --port /dev/cu.usbmodem...
```

The transfer validates package structure and hashes before atomic activation;
never use the general file-exchange area to bypass game validation.

General file exchange keeps transaction files under `TRANSFER/.P4FT/`.
Names such as `NOTE.TXT.P4T` and `NOTE.TXT.P4B` remain ordinary user files
under `TRANSFER/`; uploading `NOTE.TXT` must never delete or restore them.
Interrupted transactions created by older firmware may leave suffix files in
`TRANSFER/`. They are preserved because the new receiver cannot distinguish
them from user data. Inspect and recover those old files manually if needed.
A media-sync failure closes the upload descriptor, reports failure, and leaves
an existing target intact.

## H1 verified content upload

Waveshare Console OS shares H1 between diagnostics, multiplayer, and an
OS-owned content receiver. Stop the multiplayer relay, leave the console at the
launcher, and select the exact H1 port when two consoles are attached:

```sh
python3 scripts/p4-usb-content.py doom \
  --port /dev/cu.wchusbserial...
python3 scripts/p4-usb-content.py quake
```

The manifest starts at the normal 115200 baud. After exact size and SHA-256
validation, both sides negotiate 921600 baud for 4096-byte CRC32-protected
chunks. Firmware writes only the fixed target's reserved temporary name,
syncs it, verifies the complete received hash, atomically renames it, performs
a full SD readback hash, and then reboots to restore ordinary H1 multiplayer.
Interrupted and invalid transfers never create a runnable target. WADs and
other game data remain ignored local inputs and are never committed.

The retained Quake command supports its pinned historical shareware PAK, but
Quake remains hidden from the current launcher.

## Device behavior

- A missing or unmountable card leaves Console OS and its built-in pages usable
  but exposes no executable P4G apps and marks removable storage unavailable;
  restoring the card rebuilds the catalog.
- Game Manager refresh rebuilds the native game catalog.
- H1 import never formats the card or accepts a host-supplied destination
  path; only exact OS-known content identities can select fixed targets.
- Mounted-card imports use the host tool's validate, stage, sync, read-back,
  and atomic-rename transaction.
