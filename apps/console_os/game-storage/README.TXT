P4 CONSOLE GAME STORAGE
=======================

This FAT volume is shared safely between Console OS and a laptop through the
J16 USB GAME STORAGE port. Eject the volume in the laptop before launching a
game. If a cable is removed during a write, reconnect and eject cleanly, then
restart the console before using the changed files.

Doom currently accepts only this file at the volume root:

  DOOM1.WAD   Doom v1.9 shareware, exactly 4,196,020 bytes
  SHA-256     1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771

You may replace or remove data files without reflashing firmware. This volume
stores game data, not executable apps. Do not copy registered or commercial
Doom data unless you legally own it and your use permits the copy.

Console OS also includes a File Manager under SYSTEM. After ejecting P4 GAMES
on the laptop, use it to refresh the root listing or remove one regular file
with a two-step confirmation. The console deliberately hides host metadata,
does not delete directories, and refuses file operations while USB owns FAT.
