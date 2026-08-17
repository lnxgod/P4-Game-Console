P4 CONSOLE OLIMEX MICROSD
=========================

This FAT32 card holds games and Console OS update packages. The ESP32-P4-PC
does not expose this card through its USB-C programming connector. To add
files, power the board off, move the card to the laptop, copy the files, eject
the card cleanly, reinstall it, and then power the board on. Never remove the
card while Console OS is running.

Installed games are single files in the GAMES directory:

  GAMES/MAZE.P4G       Maze Chase
  GAMES/INVADERS.P4G   Space Invaders
  GAMES/*.P4G          Other P4 Game API v1 cartridges

Game Manager lists valid and broken packages and can remove a game after a
second confirmation. P4G files contain native code and are not a security
sandbox; install only packages you trust.

Console OS also scans root-level `*.P4G` files from 0.3-era cards, with the
copy under GAMES taking precedence when both locations contain the same game.

The original open P4 Cart layout is also preserved:

  P4/GAMES/*.P4CART   validated Lua-source cartridges

Game Manager verifies and lists these carts. The Lua 5.4 sandbox is not in
this firmware yet, so P4CART entries remain catalog-only. The bundle includes
the open BOUNCE-LAB.P4CART reference cart.

Console OS updates use:

  UPDATE/P4UPDATE.P4U   verified update for the inactive OS slot

Game Manager verifies the package before writing the inactive OTA partition.
Games and other card contents are not erased by an OS update.

The development bundle also carries the verified Doom v1.9 shareware data:

  DOOM1.WAD   exactly 4,196,020 bytes
  SHA-256     1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771

The Doom launcher verifies DOOM1.WAD before its exclusive handoff and uses the
board's ES8311 audio path plus connected USB gamepad, keyboard, or mouse. Do not
copy registered or commercial Doom data unless you legally own it and your use
permits the copy.

File Manager can inspect other root files. Console OS never auto-formats this
card and does not delete directories.
