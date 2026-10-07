# Blast Circuit

[Game index](../README.md) · [Build and install](../README.md#build-and-play) · [Console OS](../../apps/console_os/README.md)

**Availability:** Included candidate. **Folder:** `GAMES/ARCADE`.

**Package:** `BLAST_CIRCUIT.P4G`. **Players:** Solo vs bots; 2–4 linked.

**Local preview:** `make play-game GAME=blast_circuit` from the repository root.

Linked counts describe the game profile; see [transport and hardware limits](../README.md#test-status-and-multiplayer).

An original four-player bomb arena game for P4 Console OS. Version 0.2 adds
three arenas, four individually animated robot designs, 30 original sound
cues, an arena workshop, three custom slots, and host-to-peer level sharing.
The 17×13 field uses 768×480 when available and supports 320×200. Solo play
fills the other three seats with bots. First to three round wins takes the match.

Version **0.2.7** is a locally play-tested, build-tested cartridge candidate
(`enabled: true`) included in generated bundles. The requested SNES Bomberman
benchmark remains a quality target, not an established equivalence.
No commercial game code, sprites, maps, music or sound samples are included.

## Play and create

```sh
make play-game GAME=blast_circuit
```

The generic SDL3 runner compiles this game's actual native sources. Arrows/WASD
are D-pad, Space/Z is A, X/Shift is B, Enter/P is Start, and Escape/Backspace/Q
is Back. Back always returns to the launcher.

| Screen | Controls |
|---|---|
| Title | A/Start opens arenas; B toggles sound |
| Arena picker | Left/Right selects one of six slots; A plays; B edits; Start returns to title |
| Copy template | Up/Down and A choose which custom slot to replace; B cancels |
| Workshop | D-pad moves cursor; A paints; B cycles brush; Start opens tools |
| Workshop tools | Up/Down and A select; B/Start returns to painting |
| Battle | D-pad moves; A places bomb; B toggles sound; Start pauses solo play |
| Solo workshop test | Start returns to the original editable map, including during countdown/results |
| Paused | A/Start resumes; B toggles sound |
| Match result | A rematches; Start returns to arenas; linked clients wait for the host |
| Eliminated in solo | Hold A to speed up the remaining round |

Menus, brush selection, painting/dragging, mirror, undo, tools and copy-slot
selection also accept touch. Battles retain the OS-normalized virtual controls.
Linked players cannot pause a shared match.

### Arenas and blocks

- **Orbital Greenhouse:** classic grid lanes, jade floors, wooden cargo.
- **Copper Foundry:** a more open center, charcoal/copper floors, extra armor.
- **Prism Vault:** wider staggered lanes, violet floors and cyan glass.

All presets are mirrored on both axes, with protected escape lanes at every
corner and permanent interior pillars that visibly match the outer walls.
Two layers of breakable obstacles separate starting areas. The cheapest route
between each pair of players crosses **three or four blocks**, within the
required three-to-five range; a reinforced crate counts as one block, though
it needs two bomb hits. The layouts have no permanently isolated floor areas.

- **Mint steel wall/pillar:** permanent; stops movement and blasts.
- **Wood / cyan glass:** one hit; may reveal an upgrade.
- **Ivory armor:** first hit visibly cracks it, second hit clears it.
- **Red ember crate:** one hit, then its square burns for **1.4 seconds**,
  one second beyond the ordinary 0.4-second blast. Its flame emblem, shrinking
  heat indicator and original fire hiss identify the hazard. It drops no item.

Each destructible block stops the blast that hits it. A later blast cannot
shorten an ember square's remaining burn. Players must wait until the flame
ends before crossing; bots also avoid burning cells. Cleared routes stay open,
while the permanent pillar lanes remain part of the map throughout the round.
Capacity is capped at three bombs per player, range at five cells, and the
world at twelve bombs. Chain reactions work in either bomb-array order.

Bombs use a 2.5-second nominal fuse. Rounds count down for three seconds and
last up to two minutes. At thirty seconds the next hazard ring flashes; at
twenty-five seconds the outer field begins burning inward every five seconds.
Draws award no point. Movement is interpolated, with buffered short taps,
animated fuses/fire, material-colored particles, short camera shake and knockouts.

### Arena workshop and saves

Edit a custom slot directly or copy a preset into an explicitly selected slot.
Nine brushes provide floor/erase, wall, wood, armor, ember, glass and the three
power-ups. Mirror paints all four quadrants. Tools provide play-test, save,
theme selection, mirror toggle, one-step undo/redo, clear-to-floor and return.
Fast touch strokes fill intervening cells, and Undo reverses the whole stroke.
The boundary and spawn escape lanes are protected. Validation requires every
usable cell to connect after destructible blocks are cleared; disconnected or
invalid maps cannot be played, saved or accepted from a peer.
Before starting or sharing a match, additional checks require both-axis symmetry
and three-to-five breakable blocks on the cheapest route between all six player
pairs. The editor displays the problem and rejects unfair play attempts.
Connected unfinished drafts may still be saved and old custom layouts remain
editable; fairness checks never silently rewrite them.

All three slots are stored in one bounded 675-byte schema-1 save in the OS's
`AUTO` slot. The game uses the immutable launch sequence and queued optimistic
commits. It shows Saved only after commitment, keeps newer edits dirty if they
were made during a pending save, and preserves edits on errors/conflicts.
Missing optional save support is explicitly reported as session-only.

**The SDL runner currently uses an in-memory save backend.** Its Save confirmation
is valid for that running host process; quitting the preview does not create a
persistent disk save. Persistent restart/reload depends on the target OS save
service. Automated tests separately prove launch-snapshot reload and corruption
handling. No game code receives a filesystem path or performs storage I/O.

## Four-player sharing

The manifest retains 2–4 linked humans and four total seats. Each device owns
one normalized local input stream; unused seats are bots. This does not add
four local controller streams to the Game API. Console OS owns Host/Join,
transport, exact cartridge matching and connection setup.

Protocol **3** sends the host's selected base arena before each match:

1. The host encodes theme plus all 221 cells (222 bytes) with a CRC32, unique
   transfer ID and five bounded chunks. No asset/code upload is needed; all
   players already have the same game and artwork.
2. Clients accept host-origin chunks only, assemble one transfer, verify its
   checksum and validate tile types, boundaries, escape lanes, connectivity,
   symmetry and starting separation.
   Incomplete or invalid data never replaces the live level.
3. Each client acknowledges the exact transfer ID and checksum. The host waits
   for **every** human slot before the round countdown begins. Missing
   acknowledgments time out after ten seconds with a link-ended screen.
4. The immutable base map is restored for subsequent rounds. Clients retain
   the received map for the match; received maps are not silently written over
   their saved custom slots.

The host simulates at 20 Hz. Clients send direction plus idempotent bomb-action
counters bound to the current level and round. Authoritative 554-byte snapshots
use twelve packets of at most 60 bytes and normally publish at 10 Hz. Queue
exhaustion resumes the same frozen snapshot/level at the next unsent chunk;
completed snapshots apply atomically. Reordered chunks, stale IDs, replayed
inputs and peer-authored state are rejected. Receive work is capped at 24
messages per update. Remote controls neutralize after 350 ms; clients stop
following a silent host after three seconds. Bounded host heartbeats allow arena
selection/editing before a match. Changed generation, seed, role, membership or
peer loss ends the session view; human seats never silently become bots.

Each cell now carries separate tile and fire-timer bytes so the longer ember
fire is transferred exactly. Protocol-2 sessions are rejected before gameplay;
the OS also requires the exact matching cartridge hash. The schema-1 675-byte
custom-level save envelope is retained, and existing tile IDs are unchanged.

Two-, three- and four-instance mocked sessions validate this game boundary.
Real four-device play still requires a compatible OS adapter/build. The checked-in
adapter examined for this work advertises two humans while the shared core/API
supports four; that does not change this game's four-player contract or establish
which previous device build the user played. No private transport was added.

## Art and sound

The original image atlases were made with the **built-in image-generation tool**:

- [runner.png](assets/runner.png): mint repair robot, four directional walk cycles.
- [ember.png](assets/ember.png): coral robot, horn and ivory scarf.
- [volt.png](assets/volt.png): cobalt runner, tall antennae and amber visor.
- [gilt.png](assets/gilt.png): gold heavy robot, goggles and apron.
- [greenhouse.png](assets/greenhouse.png): floors, wood, bombs and upgrade tokens.
- [biomes.png](assets/biomes.png): foundry/prism floors and material blocks.
- [effects.png](assets/effects.png): blast centers, fire jets and wood fragments.
- [reinforced.png](assets/reinforced.png): bright armored crate, intact and cracked states.
- [ember_crate.png](assets/ember_crate.png): red cargo with an ivory flame emblem.

Exact generation prompts, source hashes, saved filenames, grid/crop rectangles
and tool mode are in [assets/provenance.json](assets/provenance.json). The
converter uses nearest-neighbor sampling, measured crop-safe rectangles and
RGB565 transparency key `0xf81f`; PNGs are never decoded at runtime. Nine
compiled atlases total **276,480 bytes**.

[tools/synthesize_audio.py](tools/synthesize_audio.py) designs all sounds from
oscillators, filtered seeded noise, resonances and smooth envelopes. It uses no
external recordings. The **30 cues** include dry menu/paint clicks, bomb latches,
layered low explosions, wood/metal/glass impacts, three different upgrades,
three floor footsteps, four character knockouts, countdown/go, round/draw/
champion results, save/error/transfer and overload warnings, plus a filtered fire hiss for ember crates.

Music rotates through three electronic arrangements of public-domain Bach:

| Track | Source | Tempo | Length |
|---|---|---|---|
| Badinerie / Circuit Mix | BWV 1067, Badinerie | 150 BPM | 64 s |
| Invention No. 8 / Neon Relay | BWV 779 | 150 BPM | 81.6 s |
| Prelude in C minor / Foundry Rush | BWV 847, Prelude | 160 BPM | 96 s |

The **241.6-second playlist** continues across arena selection and rounds.
A 100-ms fade at each track edge brings the output to zero before changing
key/tempo and clearing old echoes. The approved Badinerie mix remains
byte-identical outside those brief fades. Neon Relay keeps the complete
34-bar two-part invention, with a syncopated three-beat groove and a stronger
answering voice on its second pass. Foundry Rush uses the driving first 32
bars of the prelude twice, with a percussion breakdown/build and fixed tempo;
the later cadenza/Adagio is omitted. Neither adds random harmonies.

Badinerie's melody was transcribed from [Sam Franko's 1911 violin edition](https://hdl.handle.net/1802/26605),
marked public domain by Sibley Music Library. The other arrangements use
Mutopia's public-domain engravings and score-derived MIDI:
[Invention No. 8, Allen Garvin](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=61)
and [Prelude No. 2, Davide Castellone](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=550).
Both source parts retain their written pitches and rhythms in the selected
passages. New accents, instruments and drums are original. No performance
recordings are used. Source files and exact hashes are retained under
`assets/music/`; their original public-domain notices remain intact.

[tools/arrange_music.py](tools/arrange_music.py) produces **4,388 compact events**,
four shared harmonic wavetables and **17,920 bytes** of shared percussion PCM.
[tools/music_score.py](tools/music_score.py) reads MIDI only at generation time;
there is no MIDI parser in the cartridge. Five bounded channels run from a
sample clock, so callback sizes and rejected FIFO writes cannot change tempo.
Music is softer in menus, fades out while paused/waiting, and obeys the existing
sound toggle. Exact adaptations and licensing are in
[assets/music-provenance.json](assets/music-provenance.json). The source scores
remain public domain; the original electronic arrangements use MIT.

The 16-kHz IMA ADPCM bank is **92,800 bytes**, decoded to independently mixed
PCM16 voices. Envelopes, DC removal and a bounded output limiter control clicks
and peaks. Sound queues are nonblocking; rejected PCM is dropped, never retried
in a busy loop. Tone-only hosts receive distinct short fallback pitches.
[assets/audio-provenance.json](assets/audio-provenance.json) records every cue.
Code and original assets use the game's [MIT license and public-domain notice](LICENSE).

```sh
python3 games/blast_circuit/tools/convert_assets.py  # Pillow required
python3 games/blast_circuit/tools/synthesize_audio.py \
  --audition build-host/blast_circuit/sound-audition.wav
python3 games/blast_circuit/tools/arrange_music.py
# After building the focused game tests, capture the actual C mixer:
build-host/blast_circuit/tests/blast_circuit_music all \
  build-host/blast_circuit/bach-arcade-playlist.wav
# Replace "all" with 0, 1 or 2 to export an individual track.
```

## Console cartridge

`make console-os-tab5-idf` builds the board-independent cartridge at
`apps/console_os/build-tab5/sd-card/GAMES/BLAST_CIRCUIT.P4G`. Version 0.2.6 is
**452,660 bytes** (442.1 KiB), within the 512-KiB limit. The actual Console OS
C package/ELF validator and embedded SHA-256 check pass. All imported symbols
are on the frozen runtime allowlist. The pinned RISC-V compiler enforces a
**2,048-byte maximum individual stack frame**; this is not a measured total
call-chain or runtime stack high-water mark. Exact hashes are recorded in
[LOCAL_TESTING.json](LOCAL_TESTING.json).

Sprite scaling now clips once and uses exact integer stepping instead of
dividing for every pixel. The earlier optimization pass matched all twelve arena/editor reference
frames byte-for-byte at both resolutions; the subsequent HUD and reinforced-block revisions intentionally change
those frames. Audio builds an active-voice
list once per update, avoiding scans of all 30 effects for every sample. These
changes reduce work without changing the art, samples, rules or protocol.

## Verification and remaining acceptance

```sh
cmake -S games/blast_circuit -B build-host/blast_circuit -G Ninja
cmake --build build-host/blast_circuit
ctest --test-dir build-host/blast_circuit --output-on-failure
cmake -S tools/p4-game-host -B build-host/play-blast_circuit -G Ninja \
  -DP4_GAME=blast_circuit -DP4_ALLOW_DRAFT_GAME=ON
cmake --build build-host/play-blast_circuit
ctest --test-dir build-host/play-blast_circuit --output-on-failure
make game-registry-check
```

All three game test targets and all three generic-host tests pass with ASan/UBSan. Tests
cover gameplay, permanent interior pillars, exact three/four-block shortest
routes for all six spawn pairs across 300 preset validations, two-hit armor,
28-tick ember fire versus 8-tick normal fire, safe crossing after expiry, bot
fire avoidance, invalid/asymmetric/over-open/over-dense maps, 5,000 mutated
level payloads, 8,000 mutated snapshots, save/reload/failures, editor input,
30 independently decoded sound cues, the complete three-track rotation and wrap, callback-partition
invariance, stereo output, FIFO rejection, round continuity, pause/mute/resume, padded/guarded rendering at both sizes,
and two-/three-/four-instance transfer and gameplay failure paths. Sixteen
seeded bot runs completed 32 rounds and 3,898 explosion ticks. State is 12,976
bytes on the tested Mac ABI, comfortably below the 128-KiB state limit.

Codex exercised the actual SDL game with keyboard and mouse: arena selection,
material painting/mirroring, undo, tools, save feedback, play-test, movement,
bombs and animated combat. See [LOCAL_TESTING.json](LOCAL_TESTING.json) for
exact source hashes and the final interactive checklist. Automated audio checks
prove decoding and bounded output, not that a human judged the mix pleasant.
The runner targets 31 FPS rendering and 16-ms service updates.

The exported 241.6-second playlist is captured from the real C mixer: 16-kHz
stereo, no clipped samples, peak -8.19 dBFS, RMS -24.26 dBFS. Both new tracks
individually pass the level checks and every track boundary ends at zero. These measurements
check signal bounds; they do not establish subjective music quality. The first
live preview nevertheless popped: the SDL runner generated PCM for actual
elapsed time while consuming a fixed 256 frames. Late callbacks overfilled its
FIFO and starved SDL. The shared runner now partitions updates into at most
16 ms, drains the matching sample count after each step, delivers input edges
once, and primes 64 ms before playback. Its regression reproduces 80 rejected
blocks/21,888 discontinuous frames in the old path; the fixed path preserves
all samples across 900 jittered updates with zero rejected blocks or underruns.
The user confirmed that fix removed the popping, approved Badinerie, and then
approved the expanded three-track playlist. This is simulator listening
acceptance, not evidence about the tablet's independent audio path.

The 0.2.4 keyboard playtest made five active round attempts: two in Greenhouse,
one in Foundry, one in Prism, then a Greenhouse replay after the HUD correction.
Codex placed bombs, escaped blasts, opened lanes, collected an extra-bomb upgrade
in Prism, and was eliminated in every attempt. Rival wins and automatic next-round
transitions were observed; this pass did not complete a first-to-three match.
Prism offered wider escape routes, Foundry brought rivals together sooner, and
Greenhouse required careful turns around pillars. Delays between CUA commands
limit conclusions about human reaction time and difficulty, so combat timing
was retained. Version 0.2.4 fixes the observed HUD confusion: bomb capacity and
fire range now sit under the local player's own panel, with readable labels
at high resolution and compact labels at 320x200. The corrected HUD, bomb
escapes, pause/resume and Back were replayed; the simulator was left at arenas.

The 0.2.6 visual pass reviewed all three corrected arenas at both resolutions.
Codex played a live Greenhouse round with keyboard movement, bomb placement
and retreat around a permanent pillar. The ember square remained visibly alight
after nearby flames disappeared; opponents opened routes while pillars stayed
intact. P1 was eliminated during the later crossing attempt, so this pass does
not claim a completed match or a clean live crossing. Focused tests separately
verify safe crossing after all 28 ticks and lethal early entry.

In the actual editor, Codex selected EMBER, verified BURNS +1 SEC, turned mirror
off, painted one cell, observed the symmetry warning, and confirmed Test Arena
refused the asymmetric layout. Undo restored the original fair map. The
simulator was left open at the corrected Greenhouse arena picker. Music remains
unchanged from the user-approved playlist; the new fire cue has signal checks
but no separate user listening approval.

The Tab5 Console OS build and verifier passed with this cartridge included.
Physical controls/touch, actual frame rate and total stack use, persistent device
saves, speaker listening and a sustained four-human device match remain
unverified. No tablet was flashed or installed. A scored comparison with the
requested SNES benchmark and hardware audio/balance tuning are still needed
before describing this as a finished AAA-quality release.

## Launcher presentation

The cartridge owns its title and `assets/launcher.p4i` icon. Source artwork,
conversion details and provenance live beside the packed icon. Reproduce it
with `python3 games/blast_circuit/tools/pack_launcher.py` (offline Pillow only).
