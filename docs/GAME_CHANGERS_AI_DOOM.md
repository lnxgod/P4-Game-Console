# Game Changers AI multiplayer arena

[Game README](games/arena/README.md) · [Console OS](../apps/console_os/README.md) · [Monorepo](../README.md)

This guide describes one Doom-based game mode in the monorepo. Console OS
is the shared platform that hosts it; Pure Hades is one of its content packs.

M5Stack Tab5 candidate, Console OS **0.58**. This is a special selection inside
**Multiplayer**, with no separate launcher tile. Ordinary Doom shareware and
Chex Quest retain their separate content and two-player adapters. Installation and physical acceptance are recorded separately below.

The [2026-10-06 multiplayer review](DOOM_ARENA_MULTIPLAYER_REVIEW_2026_10_06.md)
records local fixes for stalled guests, expired Wi-Fi routes, lobby departures
and lost start messages. Doom now answers valid late lobby retries during
configuration and waits for every engine to be ready before producing gameplay
ticks. The four-process loss regression passes; physical acceptance is pending.

## Playing and choosing arenas

Use **Multiplayer → Host → Game Changers AI**, select **Local Wi-Fi**, and let
the other consoles find and join that host before starting. Every console needs
the exact arena bundle on microSD. The host is both a player and the server:
one host plus up to three guests, two to four total. There is no separate fifth,
non-playing server.

The host's **Map** setting offers 29 choices:

- **Pure Hades MAP01–MAP05**: Shotguns (default), Rockets, Plasma, Pure Chaos,
  and Double-Barrel Finale. Start at any of the five maps, then loop through
  the pack indefinitely. Both ordinary and secret exits wrap MAP05 to MAP01.
  The finale has no BFG or obsolete BFG switches.
- **DWANGO 5 MAP01–MAP24**: start at the selected map, advance through that
  pack, and wrap MAP24 to MAP01. Both exit types stay inside the pack.

During play, open **Menu/Start → Choose Arena**, use the directional controls
and confirm to propose a map. Other active players open the menu and choose
**Vote Yes** or **Vote No**. A strict majority of active, connected players
changes the map immediately. The proposer supplies the first yes ballot; each
player gets one ballot. A tie does not pass. A vote lasts at most 20 seconds,
with a five-second delay before the next proposal. Break/departed players
cannot vote and do not count toward the majority. The menu includes Resume and
Doom Options / Quit. It does not pause the network simulation.

Votes and ordinary exits preserve active visit scores. Everyone sees the same
P1–P4 current-visit kills; another active human killed adds one, while suicides
and environmental deaths add none.

**15 seconds without movement or firing** ends a visit and clears its score.
The screen says **TAKE A BREAK**. Turning alone and Use do not reset the timer.
Movement input or held fire does reset it, even while standing against a wall
or firing without moving. A break player is frozen, invisible, nonblocking and
unshootable, but its console continues simulating. The host keeps serving during
its own break. **Release and press Use** to spawn with a new visit and zero
kills; holding Use across the timeout cannot immediately undo the break.

A departed or timed-out guest is removed at a host-authored tic. The remaining
players continue. Host shutdown ends the session; there is no host migration.
This is the accepted **connected break/return fallback**: new or physically
disconnected consoles must join in the initial lobby. True live admission into
an already-running world is not implemented. Bluetooth and the serial relay
retain their existing two-console scope; this four-slot mode uses local Wi-Fi.

## Exact content and storage

The three unchanged input WADs are:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| FREEDOOM2.WAD | 28,787,748 | `a8772e088847032510d97ba2312406a6998f21cbab44d4ff10696faa9c0ecd4b` |
| PUREHADES.WAD | 2,313,392 | `ab027fbeebe20787214a3bc1239bba73271030dc284cd108c20ae8bc73752fc8` |
| DWANGO5.WAD | 2,109,396 | `2b7658f126321fc2ccf953dbbe8d585b4162350080b9715b46fc617d32cdb13a` |

Pure Hades v0.6 replaces Pure Hell. Its authoritative source is
[lnxgod/pure-hades](https://github.com/lnxgod/pure-hades/tree/142c6ff56ff8fb9eee40e6b2ea7875a0424337e8).
The exact release, Python map sources, original music, artwork and notices are
vendored in [this monorepo](../game-data/pure-hades/README.md). Maps and music
use CC BY-SA 4.0, original Python source uses MIT, and Freedoom-derived artwork
retains its BSD notices. All 44 upstream files are hash-pinned in
`third_party/pure-hades.json`; the WAD is reproducible from those sources.

The USB bundle contains **16 required files**: three WADs plus the pack README,
manifest, credits, tracklist and licenses, and both DWANGO notices. Notices
retain their `licenses/` and `music/` paths under `/GCADOOM`. The five map MIDI
tracks are embedded in the WAD; the full source pack also carries their original
standalone files. Startup exact-hashes every required file, including notices.

DWANGO 5 comes from the [Doom2.net archive](https://www.doom2.net/doom2/wads/DWANGO5.ZIP).
Its archive digest, original credits and local paths are pinned in
`third_party/game-data.json`. Freedoom and DWANGO remain ignored local inputs.
Pure Hell is no longer selectable or provisioned. An old inactive `PUREHELL.WAD`
on an existing SD card is not loaded by this firmware.
No WAD is embedded in firmware. This integration does not grant redistribution
rights to DWANGO or its music.

The engine loads Freedoom Phase 2 plus both PWADs. It records each admitted
pack's map/music indices and isolates DWANGO's global lump names in memory.
Selecting a DWANGO arena selects its geometry and map music explicitly, while
Freedoom supplies textures, sounds, skies and interface graphics. DWANGO's old
global sound/graphic replacements do not overwrite Pure Hades. The input files
remain byte-for-byte unchanged. General DeHackEd/Boom/MBF/UDMF/ACS compatibility
and arbitrary user WAD selection are not implied.

The room identity hashes the three ordered WAD digests, with a versioned
prefix: `363c69fd5b0ae3faa5360d96624414e6dabd2d4c9c86d1eab518899d8095358b`.
The special mode advertises protocol 5; ordinary Doom keeps protocol 3.
The generated content header and USB admission table share the pinned metadata.

The base WAD cannot fit beside the engine's 6 MiB zone in Tab5's 32 MiB PSRAM.
The OS therefore retains read-only streams under its storage lease, hashes each
whole WAD and records SHA-256 per 4096-byte block during the same pass. Runtime
cache misses verify those digests. Corruption/short reads permanently fail the
reader. Three digest tables and block caches total about **266 KiB**, rather
than retaining 32 MiB of WAD snapshots. Ordinary Doom/Chex keep their existing
immutable PSRAM snapshots. USB/content writes remain excluded during play.

From a fresh clone, fetch the pinned external files and stage the complete
bundle without touching a device. Downloads and extracted files must pass
size and SHA-256 checks; conflicting existing files are never overwritten.

- [Freedoom 0.13.0 ZIP](https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip)
  ([official checksums](https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0-CHECKSUM)); use **freedoom2.wad**.
- [DWANGO 5 ZIP](https://www.doom2.net/doom2/wads/DWANGO5.ZIP); preserve both notices.
- [Pure Hades pack in this repository](../game-data/pure-hades/README.md).

Run:

```sh
python3 scripts/doom/arena-content.py --fetch
python3 scripts/doom/arena-content.py --stage build-host/game-changers-ai/sd-card
```

When separately installing approved content, the aggregate USB command checks
all local files before opening serial, installs each exact-hash file, and
keeps one USB connection open for the entire bundle. Firmware 0.58 returns
the transfer service to idle after each file without rebooting:

```sh
python3 scripts/p4-usb-content.py game-changers-ai \
  local-data/doom/freedoom2.wad \
  --pack game-data/pure-hades/v0.6 \
  --dwango local-data/doom/arena-inbox/dwango5 --port /dev/cu.<tab5>
```

Use the guarded Tab5 installation workflow for firmware; this command installs
content only and requires the matching 0.58 firmware.

## Synchronization and validation

Console OS owns discovery, the session and initial roster barrier. The arena
adapter validates the roster and waits for every engine. Guests send their own
commands; the fixed host publishes ordered four-slot batches and membership.
The 40-byte payload fits the existing 64-byte GAME_MESSAGE boundary. Source
routes, sequence windows, acknowledgments and bounded retries handle loss and
duplicates. Heartbeats continue while a missing guest stalls input. Runtime
JOIN cannot allocate a new member.

Votes use two reserved chat bytes in that same ordered tic stream, with bounded
opcodes, a generation token in a disjoint byte range and a three-tic partial-command lifetime. No second
transport or server is introduced. Departures, idle rules, vote counts and map
transitions run on identical 35 Hz simulation tics. The visit ledger is separate
from vanilla's per-level frags. The engine skips respawning in the old world
when a voted map load is already scheduled, and stops old-map triggers/thinkers
as soon as a vote passes.

Automated evidence includes:

- Sanitizer-backed rules, codec, storage, production VFS and shell tests;
  four-state equality, vote ties/denials/timeouts/stale tokens/cooldown, break
  eligibility, and score/visit retention.
- Four real adapter/session instances over lossy, duplicated socket traffic,
  including high-bit vote bytes, selected-map propagation, guest departure and
  host shutdown.
- Full engine address-sanitizer exercise: four seats, 15-second idle/fire rules,
  fresh return, ordinary/secret five-map Pure Hades looping, controller-menu proposal,
  majority switch, all 24 DWANGO maps and wraparound, and voting back to Pure
  Hades with the same score/visit/membership. Selected map/music lumps and Pure
  Hades weapon populations are checked. Separate starts cover Pure Hades MAP05
  and DWANGO MAP24.
- Exact input/header checks, three admitted WAD directory validators and
  complete local bundle staging. USB preflight tests cover required notices and
  same-size hash corruption before serial access.
- Reversible pinned engine patch verification, separate ordinary Doom/Freedoom
  smoke, and one matching Tab5 firmware candidate build. Source/SDK/component
  locks are unchanged.

Reproduce the full engine test with the pinned host compiler/SDK:

```sh
P4_DOOM_ASAN=1 P4_DOOM_ARENA_HOST_TEST=1 scripts/doom/build-host.sh
P4_DOOM_MAX_FRAMES=100000 build-host/doom/doom-arena-headless \
  -iwad local-data/doom/freedoom2.wad \
  -file game-data/pure-hades/v0.6/PUREHADES.WAD \
        local-data/doom/arena-inbox/dwango5/DWANGO5.WAD -warp 1 -skill 3
```

The historical [arena content receipt](../test-runs/2026-10-05-game-changers-ai-arena-content.json)
binds exact sources, inputs, logs and firmware. These are automated host tests
and captured-frame inspection, not interactive gameplay or physical acceptance.
Four Tab5s still need discovery/startup, controls, sound, SD latency, memory
headroom, sustained frame cadence, and loss/recovery checks. A build does not
qualify the device's 30 FPS release floor.

The Pure Hades successor is based on the merged multiplayer reliability fixes.
All consoles need the 0.58 firmware and replacement content bundle together.
The old Pure Hell bundle is incompatible with protocol 5. The earlier 0.53 receipts remain historical host/build evidence. The prior
[implementation receipt](../test-runs/2026-10-05-game-changers-ai-doom-implementation.json)
is historical and predates the supplied PWAD/voting work.

## Tab5 0.58 without per-file restarts

The [0.58 installation record](../test-runs/2026-10-06-tab5-no-reboot-content.json)
binds the corrected receiver/uploader, focused regression tests and both units.
The content receiver returns to idle after success or failure, and the uploader
keeps one connection for the complete batch. Storage caches refresh in place.
Opening native USB can cause one initial restart; files do not request reboots.

Both units passed device-checksum app verification, launcher startup and the
10-second service health gate. No full application readback was performed.
All 16 content files passed exact hashes on each unit using one USB connection
per final batch. Each observed one initial startup and zero mid-batch boots.
B had one earlier ACK timeout; its subsequent complete batch passed. The uploader
now allows one file retry only after the receiver explicitly returns to idle.
Physical multiplayer gameplay, controls, sound and sustained cadence remain
operator testing. The 0.57 per-file restart behavior below is superseded.

## Historical Tab5 0.57 Pure Hades installation

The [0.57 receipt](../test-runs/2026-10-06-pure-hades-integration.json) binds
firmware, Pure Hades source, host tests and the two separately backed-up Tab5s.
A/ST7121 and B/ST7123 passed app verification and launcher/health startup.
The operator stopped its content batch because the old service rebooted after
each file; 0.58 supersedes it with a continuous connection and no per-file reset.
Firmware SHA-256 is
`a535203480cfee9b2f54d2c40b038870f6389e0035bcc7e8058f19bacf535671`
(4,319,568 bytes). A completed full readback; B used device checksums after the
operator requested routine flashes avoid full readbacks. Physical gameplay,
controls, audio and sustained multiplayer cadence still need testing.

## Historical Tab5 0.56 installation (Pure Hell)

[The exact installation record](../test-runs/2026-10-06-doom-arena-install.json)
covers the two connected, separately backed-up Tab5s: A/ST7121 and B/ST7123.
Both received only the app range at `0x20000`; full readback, launcher startup,
the ten-second service health gate and post-content warm boot passed. All
11 content files were activated with matching device readback hashes on each
unit. Existing games/saves and the paired 0.55 Red Dragon cartridge were retained.

The installed 4,317,712-byte app has SHA-256
`98dae268cfbe1bfedd6721394eee5c16abd1cc4cacdf99a1415aa807a4f0e36e`.
The first A candidate failed before app startup because arena state consumed
internal memory needed for the DMA reserve. Its task-owned history and read
caches now occupy 23,672 bytes of PSRAM, and the build verifier rejects internal
placement. The corrected image passed on both units. A's first content transfer
timed out before activation; its retry completed and every file was verified.

A clean sparse clone of the published GitHub branch also fetched and staged all
11 files successfully. These are boot, content and reproducibility results;
physical multiplayer controls/audio and sustained game cadence remain pending.
