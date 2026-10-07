# Game Changers AI multiplayer arena

[Game README](games/arena/README.md) · [Console OS](../apps/console_os/README.md) · [Monorepo](../README.md)

This guide describes one Doom-based game mode in the monorepo. Console OS
is the shared platform that hosts it; Pure Hell is one of its content packs.

M5Stack Tab5 candidate, Console OS **0.56**. This is a special selection inside
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

The host's **Map** setting offers 26 choices:

- **Pure Hell: Shotguns** (default): supplied v0.5 MAP01, then MAP02 rockets,
  then MAP01, indefinitely. Both ordinary and secret exits follow this loop;
  there is no MAP03 or commercial finale.
- **Pure Hell: Rockets**: start with MAP02, then the same two-map loop.
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
| PUREHELL.WAD | 875,026 | `b0e8ab211fe263bdf263b219cd0f3584cac0071ec1cf3ce73defa843f00a3663` |
| DWANGO5.WAD | 2,109,396 | `2b7658f126321fc2ccf953dbbe8d585b4162350080b9715b46fc617d32cdb13a` |

Pure Hell comes from the supplied v0.5 Shotguns/Rockets/Metal archive. Its two
embedded MIDI lumps are the supplied **Beasts of Horizon** by Josephus
“DH4050” Astartes, matching the pinned Freedoom 0.13.0 music. The original
standalone MIDI, Freedoom COPYING and credits, Pure Hell README/build manifest,
and DWANGO5.TXT/FILE_ID.DIZ accompany the WADs under `/GCADOOM`: **11 files** in
all. Startup exact-hashes every required file, including the notices.

DWANGO 5 was acquired from the [Doom2.net archive](https://www.doom2.net/doom2/wads/DWANGO5.ZIP).
Its archive digest, original credits and local paths are pinned in
`third_party/game-data.json`. Freedoom and DWANGO remain ignored local inputs.
[Pure Hell v0.5](../game-data/pure-hell/v0.5/PUREHELL.WAD) and its original
MIDI, notices and build manifest are included with the owner’s authorization.
No WAD is embedded in firmware. This integration does not grant redistribution
rights to DWANGO or its music.

The engine loads Freedoom Phase 2 plus both PWADs. It records each admitted
pack's map/music indices and isolates DWANGO's global lump names in memory.
Selecting a DWANGO arena selects its geometry and map music explicitly, while
Freedoom supplies textures, sounds, skies and interface graphics. DWANGO's old
global sound/graphic replacements do not overwrite Pure Hell. The input files
remain byte-for-byte unchanged. General DeHackEd/Boom/MBF/UDMF/ACS compatibility
and arbitrary user WAD selection are not implied.

The room identity hashes the three ordered WAD digests, with a versioned
prefix: `6fbdec7a25f87088884382706fbe2c73dba284c3139ee04f1113d7fb7bba3960`.
The special mode advertises protocol 4; ordinary Doom keeps protocol 3.
The generated content header and USB admission table share the pinned metadata.

The base WAD cannot fit beside the engine's 6 MiB zone in Tab5's 32 MiB PSRAM.
The OS therefore retains read-only streams under its storage lease, hashes each
whole WAD and records SHA-256 per 4096-byte block during the same pass. Runtime
cache misses verify those digests. Corruption/short reads permanently fail the
reader. Three digest tables and block caches total about **255 KiB**, rather
than retaining 30 MiB of WAD snapshots. Ordinary Doom/Chex keep their existing
immutable PSRAM snapshots. USB/content writes remain excluded during play.

From a fresh clone, fetch the pinned external files and stage the complete
bundle without touching a device. Downloads and extracted files must pass
size and SHA-256 checks; conflicting existing files are never overwritten.

- [Freedoom 0.13.0 ZIP](https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip)
  ([official checksums](https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0-CHECKSUM)); use **freedoom2.wad**.
- [DWANGO 5 ZIP](https://www.doom2.net/doom2/wads/DWANGO5.ZIP); preserve both notices.
- [Pure Hell pack in this repository](../game-data/pure-hell/README.md).

Run:

```sh
python3 scripts/doom/arena-content.py --fetch
python3 scripts/doom/arena-content.py --stage build-host/game-changers-ai/sd-card
```

When separately installing approved content, the aggregate USB command checks
all local files before opening serial, installs each exact-hash file, and
reboots through the existing service:

```sh
python3 scripts/p4-usb-content.py game-changers-ai \
  local-data/doom/freedoom2.wad \
  --pack game-data/pure-hell/v0.5 \
  --dwango local-data/doom/arena-inbox/dwango5 --port /dev/cu.<tab5>
```

Use the guarded Tab5 installation workflow for firmware; this command installs
content only and requires the matching 0.56 firmware.

## Synchronization and validation

Console OS owns discovery, the session and initial roster barrier. The arena
adapter validates the roster and waits for every engine. Guests send their own
commands; the fixed host publishes ordered four-slot batches and membership.
The 40-byte payload fits the existing 64-byte GAME_MESSAGE boundary. Source
routes, sequence windows, acknowledgments and bounded retries handle loss and
duplicates. Heartbeats continue while a missing guest stalls input. Runtime
JOIN cannot allocate a new member.

Votes use two reserved chat bytes in that same ordered tic stream, with bounded
opcodes, a generation token and a three-tic partial-command lifetime. No second
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
  fresh return, ordinary/secret Pure Hell looping, controller-menu proposal,
  majority switch, all 24 DWANGO maps and wraparound, and voting back to Pure
  Hell with the same score/visit/membership. Selected map/music lumps and Pure
  Hell weapon populations are checked. Separate starts cover Pure Hell MAP02
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
  -file game-data/pure-hell/v0.5/PUREHELL.WAD \
        local-data/doom/arena-inbox/dwango5/DWANGO5.WAD -warp 1 -skill 3
```

The [arena content receipt](../test-runs/2026-10-05-game-changers-ai-arena-content.json)
binds exact sources, inputs, logs and firmware. These are automated host tests
and captured-frame inspection, not interactive gameplay or physical acceptance.
Four Tab5s still need discovery/startup, controls, sound, SD latency, memory
headroom, sustained frame cadence, and loss/recovery checks. A build does not
qualify the device's 30 FPS release floor.

The release branch is `codex/doom-arena-release`, based on the native 0.55
release. The earlier 0.53 receipts remain historical host/build evidence. The prior
[implementation receipt](../test-runs/2026-10-05-game-changers-ai-doom-implementation.json)
is historical and predates the supplied PWAD/voting work.

## Installed Tab5 0.56 successor

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
