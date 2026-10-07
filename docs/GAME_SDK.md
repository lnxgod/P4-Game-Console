# P4 Game SDK v1

P4 Game SDK v1 is the stable C interface for storage-installed Console OS
games. New and visually upgraded games draw native 768x480 RGB565 frames,
negotiated through the existing optional high-resolution capability, with a
tested 320x200 fallback. A game owns bounded gameplay state. Console OS owns the panel, touch, audio, timing, USB, filesystem, and
app lifecycle.

## Supported authoring path

Create games in native C and package them as `.P4G`. Game designs are free-form:
a custom 2D engine, software 3D rasterizer or raycaster can render directly into
the supplied RGB565 surface. The shared drawing, text and animation helpers are
optional building blocks, not a required engine. Preserve the negotiated
resolution/stride, complete-frame lifecycle, normalized input and bounded
memory/resource contracts; raw hardware and display ownership remain in the OS.
Use [the performance contract](GAME_PERFORMANCE.md) for active-play evidence.
Neither a language nor a rendering style proves the actual-device frame rate.

The current manifest, packager and host runner support C sources. A C++ port
requires an explicitly implemented and tested C-ABI/toolchain adapter, including
runtime/import and resource bounds; C++/STL package support is not established.
Do not silently substitute a script format for a native game.

The old Lua game-creation stack is removed, including its runtime, games,
tools and authoring skill. The repository provides native game creation only.
A source change alone does not establish what an older installed OS loads.

## Package format

Native games use `p4-native-elf-v1`. A `.P4G` file contains a fixed 256-byte
little-endian header followed by one stripped ESP32-P4 ELF32 `ET_DYN` payload.
The header carries bounded launcher metadata, capability masks, and the
payload SHA-256.

Before invoking Espressif's pinned `elf_loader` 1.3.1, firmware verifies:

- the entire package layout and reserved bytes;
- ASCII IDs, titles, versions, licenses, and two-level folder names;
- package size, payload digest, and duplicate launcher/game IDs;
- RISC-V ELF class, machine, headers, load ranges, memory limit, entry point,
  section/string bounds, relocation count/types, and referenced symbols;
- an undefined-symbol allowlist limited to the libc calls used by the SDK
  wrapper.

The relocated cartridge receives only a versioned host table containing the
RGB565 surface and callbacks for sanitized input, frame presentation, bounded
tone audio, achievements, and completion. It receives no display, touch,
audio, USB, or filesystem handles.

An enabled game may also declare a same-basename `.P4R` resource sidecar. The
sidecar has a fixed 128-byte `P4RES01` header, a maximum total size of 8 MiB,
the owning game ID, format version, exact lengths, and a payload SHA-256.
Console OS loads it from the package's directory, validates the complete
layout, ID, and digest, and exposes only an immutable payload pointer for the
duration of the foreground game. It never exposes a FAT path or handle. A
missing optional sidecar leaves the storage capability unavailable so a game
can use built-in fallback assets; a present invalid sidecar rejects launch.

Native machine code is not a security sandbox. A structurally valid malicious
cartridge can still execute CPU instructions, so install only packages you
trust. This format is not UF2; UF2 is a flashing container, while `.P4G` is a
Console OS runtime package.

Doom remains a special legacy case. Its engine is linked into the OS, but its
WAD is read from `P4 GAMES`; it uses an exclusive one-way handoff until the
engine has a reviewed reentrant teardown.

## Protected game payloads

Red Dragon (`org.p4console.lord`, `LORD.P4G`) is bound to an exact payload
SHA-256 in the running OS. The build generates
`generated/protected-games/p4_protected_game_lineage.h` from its paired
cartridge. Package/ELF validation and matching game ID/version do not establish
that this additional launch, save and multiplayer check will pass.

Before installing this protected cartridge, compare its payload digest with
the frozen lineage header or equivalent evidence for the **installed OS**.
Use the exact paired cartridge when repairing a mismatch. Preserve the game ID
and save namespace; do not disable the guard, add an unreviewed digest or rename
the game to bypass it. A payload change requires a reviewed paired OS/cartridge
release. Unprotected games retain their normal game-only update workflow.

For a metadata-only recategorization, preserve the complete approved ELF
payload. Recompiling from otherwise unchanged game sources can incorporate
new shared helpers and change the payload. A same-source comparison with a
second fresh build does not prove compatibility with the installed OS.

A firmware-only build still generates this protected cartridge for its
allowlist. While Red Dragon is held back, that paired cartridge is built under
`dev-games/GAMES/LORD.P4G`, outside the standard SD bundle. It is not installed
by default. An OS upgrade must account for the paired cartridge even when other
game files are intentionally left alone. Record the OS image, paired payload
and device transfer/registration evidence; retain a pending result if they do
not match. A successful upload alone does not prove protected-game acceptance.

## Make and install a game

From the repository root:

```sh
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE --dry-run
python3 scripts/new-game.py "Star Hop" --folder GAMES/ARCADE
python3 scripts/new-game.py "Card Table" --folder GAMES/CARDS --high-res
cmake -S games/star_hop -B build-host/star_hop -G Ninja
cmake --build build-host/star_hop
```

The starter begins as an unpublished draft (`enabled: false`). Preview it with
`make play-game GAME=star_hop`; this explicitly allows drafts in the local runner.
Before publishing, follow [the game library quality bar](GAME_LIBRARY.md), replace
its placeholder subtitle, and set `enabled: true`. The starter compiles locally. Before adding nontrivial game
logic, follow an existing game's `tests/` and CMake wiring, then run
`ctest --test-dir build-host/star_hop --output-on-failure`.

The creator chooses the next free launcher ID and writes:

```text
games/star_hop/
  CMakeLists.txt
  game.json
  README.md
  src/star_hop.c
```

The Console OS build validates every enabled manifest and produces either:

```text
apps/console_os/build/game-storage-seed/GAMES/STAR_HOP.P4G
apps/console_os/build-olimex-esp32-p4-pc/sd-card/GAMES/STAR_HOP.P4G
apps/console_os/build-waveshare-landscape/sd-card/GAMES/STAR_HOP.P4G
```

When a manifest declares resources, the build also emits the matching name,
for example `GAMES/STAR_HOP.P4R`. Install, update, and remove the `.P4G` and its
same-name `.P4R` as one game. Game Manager removes the sidecar before the
executable so stale resources cannot be inherited by a later package.

Tab5 is the default Console OS target. Run `make console-os-tab5-idf` after the focused game test when a distributable
cartridge or device install is needed. Use `make game-registry-check` for a
manifest or generator change and `make game-sdk-host` for shared API, package,
loader, or cross-game changes. Do not run repo-wide `make check` for an
isolated game change.

On Tab5, leave the SD inserted and use
`python3 scripts/p4-transfer.py push-bundle apps/console_os/build-tab5/sd-card --port <port>`.
Use `remove /path/EXACT.P4G --port <port>` to remove a cartridge whose exact
local size and SHA-256 match. Include its `.P4R` first when present. Removal
preserves save data and refuses unknown file versions.

On Elecrow, build explicitly with `make console-os-elecrow-idf`, connect the laptop to J16, copy `STAR_HOP.P4G` into the `GAMES`
directory on `P4 GAMES`, eject the volume cleanly, and open Game Manager. On
Olimex Rev.B, power off, move the microSD card to the laptop, copy the cartridge
into `GAMES` (or rebuild the complete card bundle and run
`make install-olimex-sd-card
SD_MOUNT=/Volumes/P4GAMES`), eject it, reinstall it, and power on. Replacing the
file updates the game; removing it in Game Manager uninstalls it. Neither path
requires an OS reflash.

Waveshare uses the same powered-off microSD workflow and keeps the card
read-only while Console OS runs. Use `make install-waveshare-sd-card
SD_MOUNT=/Volumes/P4GAMES` for the validated complete bundle. The installer
requires an external FAT32 volume named `P4GAMES` and rejects ExFAT.

For a live Waveshare cartridge update, prefer the H1 CH343 verified-transfer
path and leave controller-first H2 in Host mode:

```sh
python3 scripts/p4-transfer.py push /absolute/path/STAR_HOP.P4G \
  --port /dev/cu.wchusbserial...
```

Tab5 uses the same verified protocol through native USB-C Serial/JTAG; select
its `/dev/cu.usbmodem...` port explicitly. `push --class p4r` installs a validated
resource sidecar.
`push-bundle apps/console_os/build-tab5/sd-card --port <port>` installs the native
bundle over one connection, with sidecars before cartridges. The device
keeps exclusive SD ownership and reuses each format's validator before and after
atomic activation. The `exchange` class remains confined to `/TRANSFER`.


`push` defaults to class `p4g`. The host and badge independently validate the
uppercase safe name, package bound and geometry, API version, embedded payload
digest, and complete-file SHA-256. Console OS writes through a staging file,
reads the result back, atomically installs under `/GAMES`, and refreshes the
native catalog without rebooting. Add `--no-replace` to prove an exact package
is already present without overwriting it. Use H2 USB Drive mode only when a
mounted FAT volume or complete bundle workflow is actually needed.

Executable `.P4G` packages are never linked into the OTA application. The
launcher and loader support storage-backed native entries.

Packaging fails if a cartridge imports a symbol outside the frozen runtime
allowlist (`calloc`, `free`, `memcmp`, `memcpy`, `memset`, and `strcmp`). This
keeps generated bundles aligned with the on-device ELF validator instead of
letting an unloadable game reach the SD card.

`game.json` is the source/package contract. Its important fields are:

- `enabled`: true for standard inclusion; `GAMES/WIP` is always developer-only.
  Disabled drafts require explicit packaging `--allow-dev` or
  [developer installation](../games/README.md#developer-installs);
- `format`: `p4-native-elf-v1`;
- `api_version`: `1`;
- `version`: a bounded semantic version;
- optional `sources`: 1–16 unique C source basenames from the game's `src/`
  directory; when omitted, packaging compiles `<component>.c` as before;
- optional `stack_frame_limit_bytes`: a 128–16384 byte ceiling enforced by
  the real RISC-V cartridge compiler; use it for games whose launch/runtime
  stack budget is part of their acceptance contract;
- `package_file`: an uppercase `.P4G` basename stored under `GAMES`;
- optional `resource_file`: the matching uppercase `.P4R` basename;
- optional `resource_payload`: a safe game-relative input path used to build
  that sidecar; both resource fields must appear together and the manifest must
  request the optional or required `storage` capability;
- unique `id` and `launcher_id`;
- bounded `title`, `subtitle`, `folder`, `license`, and RGB565 accent;
- required and optional capabilities.

Use optional `video-highres` for a dual-resolution game, or place it in
`required_capabilities` only when the game cannot render at 320x200. The
creator generates the portable optional form by default; `--high-res` remains
a compatible explicit option and `--low-res` is a legacy opt-out.

Use `scripts/new-game.py --dry-run` to inspect a starter plan. The creator
never overwrites an existing game.

For optional scaffolds, examples and free-form authoring guidance, see
[Game starters and OS services](GAME_STARTERS.md). Linked-console game requests
also use the [multiplayer authoring skill](../.agents/skills/esp32-multiplayer/SKILL.md);
ordinary controller mappings consume the existing normalized input API.

## API at a glance

Include only headers under `components/p4_game_api/include/p4/`:

- `p4/game.h`: descriptor, lifecycle, capabilities, launcher return, service
  calls, and the optional immutable resource payload view;
- `p4/input.h`: Up, Down, Left, Right, A, B, Start, Back, and standard
  on-screen controls;
- `p4/draw.h`: clipped pixels, shapes, text, shared CP437 glyphs, and RGB565
  sprites;
- `p4/presentation.h`: cartridge-local antialiased text, measured text widths,
  rounded rectangles and scaled RGB565 sprites; see [Game art](GAME_ART.md);
- `p4/visual.h`: fixed-point motion, atlas frames, animation timing, easing,
  camera shake, and caller-owned particles;
- `p4/audio.h`: the host-owned eight-voice tone and copied-PCM mixer;
- `p4/audio_pack.h`: original reusable PCM effects;
- `p4/feedback.h`: original reusable animated feedback overlays;
- `p4/achievements.h`: the bounded OS-owned session catalog.

`update` receives measured, bounded elapsed time plus complete `held`,
`pressed`, and `released` snapshots. Console OS targets 60 Hz, reports the
actual wall-clock delta clamped to `1..P4_GAME_MAX_FRAME_DELTA_MS`, and does
not issue catch-up bursts after a slow frame. Return
`P4_GAME_EXIT_TO_LAUNCHER` when Back is pressed.
`render` receives the caller-owned surface; supplied drawing primitives clip
to its bounds.

The native cartridge entry is
[`components/p4_game_api/runtime/cartridge_main.c`](../components/p4_game_api/runtime/cartridge_main.c).
OS frame scheduling lives in
[`components/p4_frame_scheduler`](../components/p4_frame_scheduler), with
`p4/frame_scheduler.h`; games keep using the supplied lifecycle callbacks
rather than owning a private frame loop.

## High-resolution video mode

The ABI's fallback and input contract remains `P4_GAME_SURFACE_WIDTH` ×
`P4_GAME_SURFACE_HEIGHT` (320×200). New game authoring defaults to native
768×480; see [the presentation standard](GAME_ART.md). A game that declares the
`video-highres` manifest capability and
`P4_GAME_CAP_VIDEO_HIGH_RES` descriptor capability may receive
`P4_GAME_SURFACE_HIGH_RES_WIDTH` × `P4_GAME_SURFACE_HIGH_RES_HEIGHT`
(768×480) instead. Check `surface->width` and `surface->height` during every
render; do not infer the selected mode from the board.

Declaring high resolution as optional is the preferred portable form. Console
OS selects 768×480 when the target supports it and otherwise starts the game
with 320×200. Declaring it as required makes launch fail cleanly on a target
without that surface. The Tab5 and Waveshare 4.3 Console OS paths and the SDL3
host runner support the high-resolution surface now. Other targets retain the
fallback until their platform implementation supports negotiation.

Input deliberately does not change modes. `p4_game_input_t` touch points and
the standard control hit regions always use canonical 320×200 coordinates.
For custom high-resolution hit testing, keep gameplay/UI geometry in that
canonical space or convert with `pixel_x = touch_x * surface_width / 320` and
`pixel_y = touch_y * surface_height / 200`. The standard control renderer
automatically scales its artwork to either surface.

Tone and PCM stream audio are optional. `p4_game_play_tone()` and
`p4_game_submit_pcm16_stereo()` may return false when their requested service
is unavailable. Console OS owns the 16 kHz PCM16-stereo session, opening it
only for the foreground game and restoring the reviewed board-safe state when
the game exits. A cartridge never receives a raw audio or GPIO handle.

Achievements are optional and bounded. Call `p4_game_unlock_achievement()`
with a short stable ID, title, and description. The host binds the event to
the running game ID, rejects mismatches, and de-duplicates repeat reports.
The achievement catalog lasts for the boot session. Save data uses the
separate optional `save` service: games receive one immutable launch snapshot
and may queue a copied, non-blocking commit without receiving a path or FAT
handle. The API, host backend, and power-loss-safe save component exist, but
Console OS exposes the capability only on a board profile that authorizes
firmware-side writes. Console OS 0.4.85 authorizes the OS-owned `/SAVES`
namespace for the two bound Waveshare 4.3 test units. It stages and validates
each bounded object, preserves one backup, journals replacement, and withholds
`save` while storage is host-owned or transitioning. Games must still work in
session-only mode on profiles that do not authorize this namespace.

Console OS 0.4.88 seals every new device commit as `P4SAVE2` with
HMAC-SHA256. The OS owns one random 256-bit device-local key in the dedicated
plaintext NVS namespace `p4_save_seal` (`master_v1`); the same namespace has
one fixed 1,176-byte P4LMRK2 `legacy_v1` registry for at most 32 full SHA-256
game/slot identities. Each anchored slot adds one 88-byte P4HWAT1
`h<14hex>` record that stores the full identity, highest installed sequence,
and exact P4SAVE2 object SHA-256. Cartridges never receive these records or any
new API. The authenticated bytes bind the validated game ID,
slot ID, schema version, optimistic sequence, payload SHA-256, and payload, and
the verifier compares the HMAC tag in constant time. If NVS key loading or
creation fails, Console OS fails closed and omits the existing Game API v1
`save` capability for that launch. Before accepting a legacy object, the store
queries the exact game/slot marker. A still-open valid `P4SAVE1` object is
committed back as `P4SAVE2` at the next sequence; only after that sealed commit
succeeds does the OS append and read back its permanent marker and remove the
legacy backup. An already authenticated object or verified empty slot also
closes its legacy window. A valid P4LMRK1 registry migrates once to P4LMRK2.
An existing authenticated P4SAVE2 slot with no expected anchor is accepted once
as an explicit baseline, then anchored before its payload is returned. After
that, recovery rejects `P4SAVE1`, any authenticated object below the anchored
sequence, and a different object digest at the same sequence. A newer
journaled stage remains eligible so an interrupted commit can finish; after
its verified install it becomes the new anchor. Registry/anchor corruption,
missing paired records, capacity exhaustion, or read/write failure withholds
saving. The first legacy migration still grandfathers its old unkeyed bytes.

If every SD artifact for an already anchored slot is absent, the service
exposes an empty launch snapshot at the retained NVS sequence floor N. A game
should use `services->save_sequence` even when `save_bytes` is zero; its first
reconstructed or server-restored snapshot then commits as N+1. For older games
that assumed empty bytes meant expected sequence zero, the service translates
only that first recovery request to N. This does not admit any lower or
same-sequence alternate object. A migrated P4LMRK1 identity without its first
authenticated baseline fails closed instead of silently restarting. The empty
floor is available only when current, backup, stage, and journal are all
absent; stale or tampered leftovers fail closed and are never loaded.

This Phase-A seal blocks casual microSD editing, including edits with
recomputed public SHA-256 fields. It is not a hardware trust boundary: secure
boot, flash encryption, encrypted NVS, and eFuse key storage remain disabled,
so physical flash access or modified firmware can recover or misuse the key.
The per-slot NVS anchor blocks ordinary rollback using only SD current, backup,
stage, or journal files. It is still a software anchor in plaintext NVS, not a
hardware monotonic counter. Rolling back the complete NVS namespace together
with the SD state, extracting the key, or running modified firmware remains
outside this tier. Synced saves must retain server-side compare-and-swap and
semantic progression checks.
The key has no Phase-A rotation/export path. Erasing or replacing the NVS
partition creates a new device identity for saves and makes existing local
`P4SAVE2` objects unreadable; recovery must come from a previously accepted
server copy or backup that is restored together with the complete original
`p4_save_seal` namespace, including the registry and every per-slot freshness
record. An existing
malformed key or registry is never silently erased or regenerated.

The `storage` capability currently means a validated read-only `.P4R` payload,
not general storage. Check the capability bit before reading `resource_data`,
then validate the game's inner payload format and version. Games still cannot
list, open, modify, or retain files, and `.P4R` is not a save-data mechanism.

The optional `signal-scan` capability is an OS-owned, non-blocking discovery
service. Call `p4_game_request_signal_scan()` with zero for a general scan or
an opaque previously returned token for focused RSSI updates, then poll
`p4_game_read_signal_scan()` once per update. A snapshot contains at most eight
results. Each result is limited to a sanitized 24-character display label, a
session-scoped salted token, RSSI, channel, and hidden/protected flags. Games
never receive BSSIDs, raw SSID bytes, credentials, sockets, or a radio handle.
They cannot connect, transmit, deauthenticate, or interfere with a network.
Treat RSSI as noisy relative feedback rather than distance or direction.

The SDL game host exposes deterministic fictional scan data for development.
The Waveshare Console OS build has a source-pinned ESP32-P4/ESP32-C6 passive
scan provider and exposes the cartridge callbacks only after its background
transport initialization succeeds. Other boards, an unavailable C6, or any
initialization failure leave the capability absent. Optional games must render
an honest offline state and continue without the service. The provider is
build-tested but remains hardware-unqualified until retained-UART evidence
confirms the exact unit's C6 firmware and live scans.

The optional `multiplayer-session` capability is the transport-neutral game
boundary above P4MP. A launched game can read one sanitized session snapshot,
copy a message of at most 64 bytes to the peer, and poll one copied peer
message at a time. The snapshot exposes only role, local player slot, player
count, a session seed, state, and generation. It never exposes a socket,
route, radio handle, BLE address, UART, account, or peer identity. Console OS
owns discovery, compatibility matching, encryption policy, framing, replay
rejection, timeouts, and disconnect neutralization. Games must treat the
capability as optional and retain a complete same-device mode.

Native games may also declare a bounded networking profile directly in
`game.json`. The short form is intentionally small:

```json
"optional_capabilities": ["multiplayer-session"],
"multiplayer": {
  "schema": 1,
  "style": "turn-based"
}
```

Console OS validates that JSON while packaging, stores its canonical 16-byte
form in the `.P4G` header, and reads it before opening a lobby. `style` is one
of `turn-based`, `realtime`, or `lockstep`. Optional fields are
`min_players`/`max_players` (2..4), `tick_rate_hz` (1..240),
`input_delay_ticks` (0..15 and lockstep-only), `message_bytes` (1..64), and
`protocol` (1..65535). Defaults are 2 players, protocol 1, 64-byte messages,
and respectively 10/30/60 Hz; lockstep defaults to a two-tick input delay.
Unknown fields and impossible combinations fail the build instead of being
silently ignored.

The Tab5 0.53 candidate extends the native-game Local Wi-Fi adapter to four
active players without changing the game ABI. Bluetooth and the USB serial
relay retain their two-player limit. The OS admits each guest, assigns its
slot, and waits for the complete roster before launching. Shared-component
and multi-instance game tests pass; physical four-console acceptance remains
pending. See `docs/MULTIPLAYER.md` for exact-version evidence. Never put `ble`,
`uart`, `usb`, an address, or credentials in this object—the player chooses an
available link and the OS preserves the same game contract on every transport.

At runtime, `p4_game_multiplayer_read_profile()` copies the immutable profile.
The API enforces its per-message budget on both send and receive. Console OS
also binds the canonical profile into lobby compatibility, so two packages
with the same executable but different protocol/timing settings cannot join.
Packages made before this field existed retain the legacy two-player,
realtime, 30 Hz, protocol-1, 64-byte profile. A package with an explicit
profile requires Console OS 0.4.69 or newer because older package parsers
correctly reject nonzero reserved extension bytes.

Turn-based games should prefer host authority: clients send bounded intent,
the host validates it against the current revision, and the host publishes a
complete bounded snapshot after every accepted action. `p4_game_multiplayer_*`
calls are non-blocking; a false send is dropped/degraded and a false receive
means no validated message is currently queued.

The game loop needs no lobby code. Poll the already-sanitized session and keep
the offline path available:

```c
p4_game_multiplayer_status_t net = {0};
if (p4_game_multiplayer_read_status(context, &net) &&
    net.state == P4_GAME_MULTIPLAYER_CONNECTED) {
    if (net.role == P4_GAME_MULTIPLAYER_ROLE_HOST) {
        /* Validate peer intents and publish authoritative snapshots. */
    } else {
        /* Send local intent and apply snapshots received from the host. */
    }
}
```

Console OS includes an installed native cartridge in the Host game list only
when its manifest declares `multiplayer-session`. The first Multiplayer screen
is `HOST` or `JOIN`. Host chooses the game, settings, and link before opening a
room. Join has no separate game picker: its bounded browser lists advertised
rooms across games, and selecting a row resolves the exact local cartridge ID
and payload SHA-256 before connection. Two cartridges with the same display
title but different content cannot join each other. Games never scan, create,
advertise, select, or join rooms themselves.

After the host starts the room, both consoles cross the OS-owned start barrier
and launch the selected cartridge. Its first `game_start` can read the
already-connected session snapshot. Returning to the launcher or losing the
route ends and neutralizes the game-facing session.

Registration is owned entirely by Console OS. During installation or the next
game-catalog scan, the OS copies the validated package ID, launcher ID, payload
digest, title, and multiplayer profile into its bounded in-memory registry.
There is no game-side registration call, first-run prompt, or central table to
edit. A kid-created cartridge only needs the declarative manifest above; by
its first launch it is already available in the Host game list and can appear
in a Join room row when another console advertises it. Invalid, duplicate,
over-capacity, or unsupported-player registrations are rejected before
cartridge code runs.

## OS resource inheritance

Games inherit a stable logical console rather than a board definition:

- Every `.P4G` targets a clipped RGB565 Game API surface: new games request
  native 768x480 with `video-highres` and retain a 320x200 fallback. On
  Waveshare 4.3, the OS viewport is always 768x480 landscape. A game must not
  infer scanout rotation, pin maps, stride layout, or backlight behavior.
- Input is a complete normalized snapshot. Use only the Game API buttons,
  touch points, and standard on-screen controls; never retain a touch pointer
  or talk to GT911/USB directly.
- Lifecycle, timing, launcher return, and audio sessions belong to Console OS.
  A game requests a capability in `game.json`, then uses only the matching
  `p4/` function. It never opens I2S, configures GPIO, owns an audio worker,
  or starts a FreeRTOS task.

### Match the native ANSI/BBS look inside a cartridge

The BBS home page is an 80×30 native terminal. Use
`p4_draw_cp437_glyph()` or `p4_draw_cp437_text()` when a game needs authentic
DOS boxes, arrows, blocks, shading, or text. Select the 8-pixel compact height
for game chrome and the native 16-pixel height for larger art. Both use the
same pinned CP437 font as `components/p4_ansi` on either Game API surface,
while remaining ordinary clipped drawing with no shell, parser, transport, or
terminal-geometry dependency.

## Make motion and effects smooth without an engine

Include `p4/visual.h` and keep the game loop simple:

1. Store slow positions and velocities as signed Q16.16 values. Advance them
   with `p4_q16_step()` using the supplied `elapsed_ms`, then round only when
   drawing. This removes low-speed integer judder without using floating point.
2. Describe one frame in an RGB565 atlas with `p4_sprite_t` and draw it with
   `p4_draw_sprite()`. The helper clips, supports a chroma key, X/Y flipping,
   and integer scale 1–8 without allocating.
3. Select atlas frames with `p4_animation_frame()` and use
   `p4_ease_smoothstep_u16()` for short UI or movement transitions.
4. Add impact polish with deterministic `p4_camera_shake()` and a small
   caller-owned `p4_particle_t` array. Particle updates and draws are bounded;
   there is no heap allocation or hidden task.

For collision-heavy games, retain a fixed-step accumulator for gameplay and
use the measured delta only to feed the accumulator. Cap the number of steps
per update so a late frame cannot create an unbounded catch-up loop.
Render once after the update. These helpers improve presentation while leaving
physics, collision rules, and game state under the cartridge's control.

## Make sound through the host-owned API

Audio is optional and must be original or correctly licensed. Use one of these
two native-API paths:

1. For hops, hits, UI feedback, and other short effects, request `audio-tone`
   and call `p4_game_play_tone()` with a bounded frequency, duration, volume,
   and square or triangle waveform. The host mixes at most eight voices.
2. For original music or more detailed effects, request `audio-stream`, add
   `P4_GAME_CAP_AUDIO_STREAM`, mix signed 16 kHz PCM16 stereo in game-owned
   fixed buffers, and submit 1–256 frames with
   `p4_game_submit_pcm16_stereo()`.

Each accepted stream call copies 1–256 frames of already-mixed signed 16 kHz
PCM16 stereo into a bounded Console OS-owned FIFO; the caller may reuse its
buffer as soon as the call returns. Its current capacity is defined by
`P4_GAME_AUDIO_STREAM_BUFFER_FRAMES` in
[`p4/audio.h`](../components/p4_game_api/include/p4/audio.h); see
[the audio scheduling contract](GAME_PERFORMANCE.md)
for producer timing and buffering behavior. Stream audio and tones are
saturating-mixed before Console OS writes the shared backend. A `false` result means the optional
service is unavailable, the request is invalid, or the FIFO is full. Drop or
degrade that block—never busy-wait inside a game callback. In every game,
`stop` must call `p4_game_stop_audio()` (directly or through the provided game
helper) before returning to the launcher.

On the Waveshare 4.3, Console OS opens the reviewed ES8311 speaker session at
the selected 1–10 master step only while a requesting native game runs. The
current Console OS default is 9/10, and session teardown restores the GPIO53 amplifier-safe
state. Games never own I2S, codec I2C, GPIO53, or the backend. The stream call
was already reserved in API v1, so
activating this bounded implementation does not change the native format or
API version.

### Use the standard original feedback pack

The Console API also includes `p4/audio_pack.h` and `p4/feedback.h` as the
source-of-truth reusable pack for native games. Request both `audio-tone` and
`audio-stream` in `game.json`; declare both capabilities; keep one
`p4_game_audio_effect_player_t` in game-owned state; trigger an action,
impact, reward, or fail effect; then service an already-active effect at most
once per update. The trigger itself queues at most one initial block. If
streaming is unavailable, retain a short `p4_game_play_tone()` fallback. The
pack never busy-waits.

Use `p4_game_feedback_draw_audio_effect()` for event-bound, crop-safe, clipped
original action, impact, reward, or fail overlays. It consumes the shared ImageGen atlas in
`components/p4_game_api/`, not a PNG decoder in the game. Asset provenance,
deterministic conversion commands, and the exact regeneration constraints live
in `components/p4_game_api/README.md`.

When making a new generated sound, use a configured original-sound provider
(for example ElevenLabs Sound Effects) and preserve the selected source WAV in
the owning game or reusable component. Record the provider, date, sharing or
license setting, exact prompt, source filename, SHA-256, resampling gain and
duration before committing its generated PCM include. Request a short dry
effect, no music, no voice, and no copyrighted sounds. Do not use a web rip,
commercial-game sample, or untracked generated media as a runtime source.

## Make animated art with ImageGen

New or materially revised raster animation art **must use ImageGen** as its
source. A game that has no raster animation may use code-drawn primitives;
this requirement applies when adding animation frames, sprites, or a sprite
atlas. Do not substitute copied arcade, console, web, or commercial-game art.

Use this workflow:

1. Ask ImageGen for one crop-safe animation atlas: state the exact frame grid,
   cell size, every animation row, a shared baseline, transparent background,
   coherent palette and no text or cell borders. Choose native-detail painted
   or pixel art according to the game; see [Game art](GAME_ART.md) for materials
   and exact typography. Keep shadows and scenery inside their intended cell.
2. Inspect every frame before committing. Regenerate it when a frame bleeds
   into another cell, loses the shared baseline, has opaque background pixels,
   or cannot be cropped independently.
3. Save the selected PNG at `games/<slug>/assets/`, or beside the owning shared
   API component when multiple games use it. Record the ImageGen prompt,
   purpose, grid, license/provenance, and source filename in the owning README;
   keep source-only art out of runtime dependencies.
4. Commit a deterministic converter beside the owning game or shared component
   that produces a bounded RGB565 include. Record a nearest-neighbor filter
   for pixel art or a quality offline filter for painted materials, and use
   one explicit transparent chroma key. Runtime code must never decode PNGs or
   allocate an image loader.
5. Test regeneration: the converter output must exactly match the committed
   generated include. Account for `width * height * 2` bytes in the game’s
   static flash budget and draw atlas cells with `p4_draw_sprite()`. The older
   `p4_draw_sprite_rgb565()` remains available for a simple unscaled frame.

## Portable-game rules

- Keep hardware access out of `games/`; reusable services belong in
  `components/`.
- Treat input as a snapshot. Never retain touch pointers or assume one contact.
- Bound all state, loops, sprite dimensions, text lengths, and audio requests.
- Use original or correctly licensed code and assets. Do not copy arcade ROMs,
  maps, sprites, fonts, or sounds.
- Record ImageGen animation-art provenance and use a deterministic RGB565
  conversion path for every animated raster asset.
- Do not commit commercial Doom WADs, WAD-bearing firmware artifacts, or
  recovery images.
- Run the changed game's focused host sanitizer tests. A successful build is
  not hardware acceptance or permission to flash.

Maze Chase, Space Invaders, Skyline Leap and Byte Buddy are original examples.
Use `p4/visual.h` and its shared API tests for atlas animation, fixed-point
particles and deterministic camera shake. The current optional reference map
is maintained in [GAME_STARTERS.md](GAME_STARTERS.md); do not reuse identities
reserved in `games/retired.json`.
Byte Buddy demonstrates touch-first virtual-pet care, interaction-driven dragon
growth, coin upgrades, a mini-game, tones, achievements, PixelLab sprite art,
and direct return to the launcher through P4 APIs.
Calculator, Input Test, and AV Test demonstrate removable utility and
diagnostic cartridges under the System hierarchy.

## Dice accessories

Optional `dice-accessory` / `P4_GAME_CAP_DICE_ACCESSORY` exposes an OS-owned
P4MP accessory connection through `p4_game_dice_exchange()`. Games publish a
bounded player label, token, enabled state, held mask, and authoritative dice
faces, and consume one matching Ready/shake completion per token. The API
supports 1–8 dice with 2–255 sides; hardware and BLE stay outside cartridges.
See [DICE_ACCESSORY.md](DICE_ACCESSORY.md) for the full contract, Core2 firmware,
connection lifecycle, and build/acceptance instructions.


Console OS offers **Dice: ON - SHARED CORE2** in Host match settings for native
games declaring `dice-accessory`. In a network match the service is available
only to the host when enabled; clients retain normal controls and receive the
host's game snapshots. Shared-dice games should publish the current player's
label and accept a validated shake for that player through their authoritative
roll path, including remote turns. This does not grant the host controller the
right to act for other players. Off disables scanning and service exposure for
that match. No additional game-specific lobby or hardware ownership is needed.


Dice accessory v2 (`dice_exchange_v2` in the size-checked cartridge host tail)
adds `request.can_hold`, `request.hold_ack`, and status `held_mask`,
`hold_changed`, `hold_sequence`. A game enables hold selection only between
rolls, validates token/current player, applies the absolute mask once, and
returns the selection sequence as `hold_ack` under a new request token.
Acknowledgements also apply to an unchanged mask after a quick keep/undo.
The old dice callback remains null on a v2 host to avoid interpreting old
structure padding as new fields. New games on old consoles retain manual
controls through optional-capability negotiation.


## Cartridge launcher artwork

Console OS 0.45 retains the 0.44 nextgen interface and reads optional native
cartridge artwork from an ELF `.p4icon` section. Existing `.P4G` API/version 1
headers and executable ABI remain unchanged. Titles/subtitles still come from
the validated package header. No title lookup or OS rebuild is needed for a
new icon. Missing/invalid artwork keeps the existing tile fallback.

Set `"launcher_icon": "assets/launcher.p4i"` in `game.json`. Generate the file
from finished art with `python3 scripts/pack-game-icon.py <source.png> --output
<game>/assets/launcher.p4i` (Pillow is an authoring dependency only). The pinned
RISC-V objcopy packs it as a **non-allocated, read-only** ELF section after
stripping. The ordinary cartridge payload SHA-256 covers the icon as well as
the executable. The complete `.P4G` must still fit 512 KiB. A changed `.p4i`
invalidates the cartridge build dependency; it does not change the firmware.

The section is exactly 9,744 bytes: magic `P4ICON1` plus NUL (8 bytes), width
128 and height 72 as little-endian uint16, format 1 as little-endian uint32,
256 little-endian RGB565 palette entries, then 9,216 row-major palette indices.
All pixels are opaque. The catalog validates and copies this bounded data
without executing the game or giving it display/storage handles. Unsupported
geometry, duplicate sections, allocated sections and invalid ELF ranges cannot
become image pointers. Titles and art refresh through the normal post-transfer
catalog rescan. Old cartridges remain usable.

## Six-axis motion snapshots (Tab5 0.50 candidate)

Optional `motion` / `P4_GAME_CAP_MOTION` exposes `p4_game_read_motion()`.
The OS copies the latest sample; no sensor or bus read occurs on the cartridge
thread. `p4_game_motion_t` contains a nonzero sequence, sample age, validity,
three physical-axis acceleration values in mg and three rotation rates in
millidegrees per second. Bounds are +/-4000 mg and +/-2000000 mdps. The API
returns false and zeroes output for unavailable, invalid, out-of-range, or older
than 150 ms samples. Games calibrate a neutral pose and provide ordinary
normalized touch/controller fallback. No automatic screen rotation is implied.

The API v1 cartridge host table appends `read_motion`; `struct_bytes` gates all
access, and a missing callback removes the capability. Old cartridges retain
their ABI. Older firmware's strict package validator will reject a new cartridge
that declares the unknown bit, even when optional, so install a motion-aware OS
before this cartridge. The Tab5 adapter preserves physical axes and uses an
independent IMU timestamp, so a clock or battery update cannot freshen old motion.
Its existing background sampler targets a 20 ms cadence, with battery/RTC reads
and retry/log work kept at their slower cadence. Device latency/axis acceptance
must be measured separately from host tests. See Tide Maze for calibrated
six-axis input and a sensor-free fallback example.
