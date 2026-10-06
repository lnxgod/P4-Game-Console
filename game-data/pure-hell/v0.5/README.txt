PURE HELL - SHOTGUNS + ROCKETS + METAL MIDI
Version 0.5 | 2026-10-06 | PUREHELL.WAD

WHAT IS INCLUDED
One WAD contains both versions:
  MAP01 - Pure Hell: Shotguns. The v0.4 map data is unchanged: eight
          regular shotguns, eight double-barrel super shotguns, sixteen
          shell boxes, the existing health pickups and the secret BFG.
  MAP02 - Pure Hell: Rockets. The same map with all sixteen shotgun
          pickups changed to rocket launchers and all sixteen shell
          boxes changed to rocket boxes. Layout, health, starts, doors,
          switches and the four-switch secret BFG are unchanged.

Both maps play the original community MIDI "Beasts of Horizon" by
Josephus "DH4050" Astartes (Freedoom Phase 2 0.13.0, MAP20).
It is a 140 BPM guitar-led track with distorted/overdriven guitar, bass
and drums. It replaces the previous MAP01 "Not My First Rodeo..." tune.
This is an original community composition, not a Metallica cover.
The MIDI bytes are unchanged and the original license and credits are
included. Its sound depends on your engine's MIDI synthesizer.

DOWNLOAD THE FREE GAME DATA
Official download page:
  https://freedoom.github.io/download.html
Direct stable release used for testing (Freedoom 0.13.0):
  https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip
Release notes and checksums:
  https://github.com/freedoom/freedoom/releases/tag/v0.13.0

Unzip that download and use freedoom2.wad (Phase 2). Phase 2 is the
Doom II-compatible version with the double-barrel shotgun. Do not use
freedoom1.wad or the Doom I shareware WAD for this two-map pack.
Freedoom supplies its own freely licensed art, sounds and weapons.
No purchase of commercial Doom II is needed when using Freedoom Phase 2.

GET AN ENGINE AND PLAY
The engine tested here is Chocolate Doom 3.1.1:
  https://www.chocolate-doom.org/wiki/index.php/Downloads
The official page has Windows downloads and macOS Homebrew instructions.
Freedoom's download page also links to Crispy Doom, Eternity and GZDoom.

Unzip this package. Put PUREHELL.WAD and freedoom2.wad in a convenient
folder. In your Doom engine or launcher, select freedoom2.wad as the base
IWAD and PUREHELL.WAD as the additional PWAD, then choose MAP01 or MAP02.
Do not load the older PUREHELL2.WAD at the same time.

With Chocolate Doom available on your command path, run from that folder:
  chocolate-doom -iwad freedoom2.wad -file PUREHELL.WAD -warp 1
For the rocket version:
  chocolate-doom -iwad freedoom2.wad -file PUREHELL.WAD -warp 2
On Windows, the executable is chocolate-doom.exe.

The original hidden exit advances MAP01 to MAP02; MAP02's exit advances
to Freedoom's MAP03. For deathmatch, use your engine's multiplayer setup
and select the desired map. Every player needs this same WAD version.
Alternate deathmatch (-altdeath) replenishes items. This classic engine
supports up to four players; there are eight possible deathmatch starts.

BFG AND SECRETS
Use the switch on the outside wall in each of the four satellite rooms.
After all four switches, use the central pillar to lower the BFG vault.
Hidden medikit doors are in the red outer walls. The hidden exit is on
the north-room outer wall, 96 map units left/west of its switch.

CREDITS AND PROVENANCE
Layout concept and rough 2012 sketch: Will.
New reconstruction and map implementation: Codex for Will, 2026.
This is a new reconstruction from the sketch and memory, not the
recovered historical PUREHELL WAD.
Original v0.4 is preserved. Version 0.5 adds MAP02 and the music override.

Music: "Beasts of Horizon" by Josephus "DH4050" Astartes.
Source: official Freedoom Phase 2 0.13.0, MAP20/D_MESSAG.
Credits: https://github.com/freedoom/freedoom/blob/v0.13.0/CREDITS-MUSIC
License: BSD 3-Clause. See the included unmodified Freedoom COPYING
and contributor/music credits. No commercial IWAD or engine is bundled.

VALIDATION
Both serialized maps preserve the original geometry and map actions.
The rocket pickups retain all eight rotation/reflection symmetries.
Both music lumps exactly match the source MIDI. Engine test details,
source hashes and the WAD checksum are in build-manifest.json.
The rocket variant still needs human balance testing.
