# Legacy game installation

Target names are exact: `elecrow-crowpanel-advanced-10` is the Elecrow 10 in
device, `olimex-esp32-p4-pc` is the Olimex ESP32-P4-PC Rev.B development board,
and `waveshare-esp32-p4-wifi6-touch-lcd-4.3` is the Waveshare 4.3 in console.
Use these routes only for the explicitly requested legacy target. Never inherit
another board's physical identity, verification or flash authorization.

The board-specific Console OS builds write each enabled cartridge to:

```text
apps/console_os/build/game-storage-seed/GAMES/<PACKAGE>.P4G
apps/console_os/build-olimex-esp32-p4-pc/sd-card/GAMES/<PACKAGE>.P4G
apps/console_os/build-waveshare-landscape/sd-card/GAMES/<PACKAGE>.P4G
```

To install or update on Elecrow, connect the laptop to J16, copy the `.P4G`
file into `GAMES/` on `P4 GAMES`, verify the destination byte count or hash,
and eject cleanly. On Olimex, power the board off, move the microSD card to a
laptop reader, copy the cartridge into `GAMES/`, verify it, eject, reinstall,
and power on. The Olimex USB-C programming port is not storage and live card
removal is unsupported.

On Waveshare, keep controller-host mode as the default. Open the on-device USB
Drive app to stop Host/HID, unmount the card, and hand H2 to TinyUSB MSC. After
the FAT32 `P4GAMES` volume appears, copy only the selected validated `.P4G` and
its matching `.P4R`, when present, into `GAMES/`; copy the resource first.
Verify each destination byte count or hash, eject cleanly, and return from USB
Drive mode so Console OS remounts and rescans. Use
`make install-waveshare-sd-card SD_MOUNT=/Volumes/P4GAMES` only for an explicitly
requested whole-bundle installation. A powered-off card-reader copy is also
valid. Never let the Mac and Console OS own the filesystem at the same time.
Open Game Manager to refresh and launch the game. Do not flash the OS for a
game-only update.

Prefer H1 when the badge is running and only a cartridge needs to move. The
default transfer class is `p4g`; push a matching resource first with
`--class p4r` when present:

```sh
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.wchusbserial...
python3 scripts/p4-transfer.py push /absolute/path/GAME.P4G \
  --port /dev/cu.wchusbserial... --no-replace
```

Compatible game-only updates do not require an OS flash. Use the shared package
validation and hardware-acceptance rules in Add Game.
