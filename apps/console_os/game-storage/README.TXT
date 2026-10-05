P4 CONSOLE GAME STORAGE
=======================

This FAT volume is shared safely between Console OS and a laptop through the
J16 USB GAME STORAGE port. Eject the volume in the laptop before launching a
game. If a cable is removed during a write, reconnect and eject cleanly, then
restart the console before using the changed files.

Installed games are single files in the GAMES directory:

  GAMES/*.P4G       P4 Game API v1 cartridge

Copy or replace a P4G file in GAMES, eject P4 GAMES cleanly, then open Game
Manager. Game Manager lists valid and broken copies and can remove a package
after a second confirmation. P4G files contain native code and are not a
security sandbox; install only packages you trust. Console OS also reads root
P4G files from older cards so upgrades do not strand installed games. The copy
under GAMES takes precedence when both locations contain the same game.

Open, source-included P4 Cart v1 files keep their original layout:

  P4/GAMES/*.P4CART   validated Lua-source cartridges

Game Manager verifies and lists these carts without renaming them to P4G.
The Lua 5.4 sandbox is not in this firmware yet, so P4CART entries are
catalog-only and cannot launch. BOUNCE-LAB.P4CART and QR-DODGE.P4CART are the
open reference carts.

Console OS updates use:

  UPDATE/P4UPDATE.P4U   verified update for the inactive OS slot

Copy the package into the UPDATE folder. After copying and ejecting, Program
Manager shows OS UPDATE READY on the Game Manager app. Open it and confirm
INSTALL. Games, WADs, and saves stay in this volume across the update.

Doom accepts this file at the volume root:

  DOOM1.WAD   Doom v1.9 shareware, exactly 4,196,020 bytes
  SHA-256     1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771

Do not copy registered or commercial Doom data unless you legally own it and
your use permits the copy.

File Manager can inspect other root files. The console deliberately hides host
metadata, does not delete directories, never auto-formats, and refuses file
operations while USB owns FAT.
