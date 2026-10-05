# P4MP virtual dice accessory

The dice accessory uses a separate encrypted, bonded BLE connection and does
not consume a P4MP player slot. Console OS owns that connection; games use the
optional `dice-accessory` Game API service. The first hardware target is the
**M5Stack Core2 (ESP32)**. It is not an ESP32-S3/CoreS3 image.

## Play

1. On the Waveshare, open **Multiplayer → Host**, choose **P4 YAHTZEE**, then
   **Match Settings → Dice → ON - SHARED CORE2**. Select Done, open the room,
   and start after the other console joins. Both consoles need Yahtzee 1.3.1
   (protocol 4). Dice defaults to Off. The option appears for compatible games
   when the console has the accessory service.
2. Tap **CONNECT** on the Core2. It advertises for 60 seconds. Activate one
   unpaired dice accessory at a time; the requesting P4 connects to it.
3. The Core2 shows the active player label and the current dice. Yahtzee's
   existing labels are PLAYER 1 through PLAYER 4. The shared API also accepts
   named players from games with their own name entry (19 printable ASCII
   characters maximum).
4. Tap **READY** on the Core2, shake it several times, and let it settle.
   Motion selects recorded dice-shake clips and a separate dice-throw
   recording when the roll lands. The motor gives distinct 70–85 ms pulses
   separated by at least 145 ms off. A shared impact event drives the
   sound, motor pulse and visible dice impulse. Landing uses the recorded
   contact timing for one or two knocks.
5. The game rolls only the unheld dice for that player. Its normal P4MP host
   generates the result and shares the authoritative snapshot. The Core2
   displays those same values. Tap a die on the Core2 to keep it (gold / HELD),
   or tap it again to release it. It briefly shows SAVING while the host
   confirms the selection. Then tap Ready and shake to reroll only the others.
   Held dice can also be released when all five are held. Score the turn on
   the current player's P4. Each new roll requires Ready again.

Touch/Start rolling remains available. Three rolls per turn, held dice,
scoring, and player rotation keep their existing rules. In a local game, pass
one accessory between players. In a network game, one Core2 stays connected
only to the host and is passed to the current player. The host sends that
player's label, held dice and results, and broadcasts each roll to the other
console. Joined consoles never scan for or claim the shared accessory.
Turning the option off grants no accessory service for that network match.
The current Waveshare radio budget is two connections: one game peer plus one
accessory. A BLE controller also needs a connection; use the USB controller
path when using both multiplayer BLE and dice.

**TRY** starts standalone practice on the Core2 without a P4. Tap Ready,
shake, and settle. Practice results are locally generated and never enter a
network game. Exit practice before connecting. Practice is useful for tuning
sound and motor feel without spending a game roll.

Core2 firmware 0.5.0 uses 96 pre-rendered antialiased 3D tumble frames,
plus six stopped and six held faces. Dice jostle inside an elliptical cup,
respond to measured shake direction/strength, collide, bounce, and overlap
with transparent sprites. On landing,
the animation stops on one dominant face and a large value appears beneath
each die, with a persistent ROLLED label until the next Ready action. Held
dice stay gold. Other side counts display the authoritative number beneath
a decorative cube.

The complete UI is buffered in PSRAM, while the 320×126 animated table
is drawn in an explicitly allocated internal-RAM RGB565 canvas. During
shaking, changed 16-pixel tiles inside the 320×126 table are merged into
bounded row runs and transferred through a 10 KiB internal DMA buffer. A
bounded list of old/new sprite-and-shadow rectangles tracks what must be
updated, avoiding a full PSRAM comparison and copy. Complete UI states
are still composed before transfer. Header/buttons update on state changes.
The recorded revision-3.1 Core2 runs at its supported 240 MHz with performance
compilation, a 1 kHz scheduler tick, and a 2 ms motion-loop sleep while shaking.
Core2 0.5.0 runs drawing and display DMA on CPU1; the CPU0 loop samples motion
and schedules feedback without waiting for display transfers. Speaker work is
also pinned to CPU0. A latest-frame mailbox coalesces obsolete animation frames,
while the result handshake is tracked before submission so landing cannot be
lost. The target render interval is 20 ms; actual animation timing is
reported as `P4_DICE PERF`, including rendering and transfer times. These
markers, rather than the target interval, establish measured performance.

Audio is from Kenney Casino Audio 1.1 (CC0). Original recordings, license,
source hashes, conversion details and asset generators are in
`apps/dice_core2/assets/README.md`.

## Reuse in another game

Declare optional `dice-accessory` in game.json and
`P4_GAME_CAP_DICE_ACCESSORY` in the descriptor. Call
`p4_game_dice_exchange(context, &request, &status)` once per update. This
copies bounded data and never waits on BLE. The request contains:

- a nonzero token, player slot 0–3, and enabled flag;
- 1–8 dice, 2–255 sides, a held mask, and current authoritative faces;
- a nonempty printable player name in a 20-byte NUL-terminated array.

Change the token whenever the player, turn, held dice, result, or eligibility
changes. Disable the request in menus, during animation, after the roll
limit, and when the game link is lost. A shared host publishes the current
player, including remote players; a personal accessory accepts only local turns.
Consume `P4_DICE_ROLLED` only once for the matching token and player. Route the
request through the game's existing host-authoritative action path and send
the resulting faces in the next request. The accessory requests an action;
it cannot inject arbitrary dice values into Yahtzee. See
`games/p4_yahtzee/src/p4_yahtzee_network.c` for the integration.

The cartridge host table adds a checked optional tail callback. Older table
sizes remain valid and have the capability stripped before access. Existing
cartridges need no rebuild. Installing a cartridge that declares the new
capability requires the new Console OS package validator first.

## Protocol and lifecycle

The BLE service is `7b0d9f20-6f44-4a0d-9c9e-50344d500010`; its read/write
characteristic ends in `0011`. It carries an 80-byte P4MP datagram: the normal
28-byte header, 48-byte dice payload, and four-byte CRC. Packet type 12 is
reserved for accessories. Player sessions explicitly reject that type. The
accessory uses a random connection session ID and monotonic packet sequences.
P4 peer ID 1 and accessory peer ID 2 are local to this connection.

Payload schema 2: bytes 0/1/2/3 are schema, kind (request=1/status=2), phase,
and action flags (bit 0 roll enabled, bit 1 holds allowed); bytes 4–7 are the little-endian token; bytes 8–11 are player,
count, sides, held mask; bytes 12–19 are eight faces; bytes 20–39 are the
zero-padded name; bytes 40–41 are the little-endian hold acknowledgement
(request) or selection sequence (status); bytes 42–47 are reserved zeros. Invalid lengths, versions,
values, padding, CRCs, session IDs, and replays are rejected. Unused faces
must be zero. ATT MTU must be at least 83.

The P4 worker copies requests through a one-slot mailbox, uses asynchronous
GATT operations, and reads status at approximately 10 Hz. It leaves another
service's active scan alone. BLE starts only when the foreground game asks
for dice; no radio is started by accessory registration at boot. Closing the
game or missing requests for two seconds drops the accessory route. A
four-second missing-response deadline handles lost GATT callbacks after setup;
setup stages have a bounded 15-second deadline renewed by actual progress.
Service and characteristic discovery finish before the next GATT procedure
starts, and immediate errors are logged and disconnected for retry. The Core2
stops feedback and clears Ready on disconnect or stale requests. Pairing uses
bonded encrypted LE Secure Connections with Just Works; it does not provide
MITM-authenticated numeric comparison.

Ready expires after 15 seconds. The portable detector samples acceleration in
milli-g, requires three distinct peaks above 1.8 g separated by at least
80 ms, at least 300 ms of gesture time, and 350 ms of settling. A held high
sample is one peak. It bounds sensor values before squaring and consumes the
gesture once. Moving the device before Ready cannot roll.

The v2 host-table tail `dice_exchange_v2` gates the extended request/status
structures. The old callback stays null: older cartridges retain manual play,
and new cartridges fall back safely on older consoles. A status must echo all
request fields except a bounded held mask and selection sequence, which can
change only while WAITING and explicitly permitted by the game. Each accepted
selection gets a fresh request token and acknowledgement, including keep/undo
sequences ending at the original mask. Pending taps survive an older ack and
are canceled on a new turn, roll context, or disconnect. The game owns dice
values and scoring; the accessory never supplies arbitrary rolled values.

## Build and install

The platform's ESP-IDF 5.5.3 lock remains authoritative. The Core2 app pins
M5Unified 0.2.20 and M5GFX 0.2.27 in its manifest and component lock. M5Stack
owns board autodetection and the PMIC, display, IMU, and speaker setup; game
code never configures pins. No Arduino toolchain is needed.

```sh
sh scripts/build-dice-core2.sh
sh scripts/build.sh console_os waveshare-esp32-p4-wifi6-touch-lcd-4.3-usb-host
python3 scripts/verify-console-os-waveshare.py apps/console_os/build-waveshare-usb-host
```

The Core2 wrapper mirrors only its sources into a checked private `/tmp` build
directory because the upstream legacy component CMake registration mishandles
paths containing spaces. It copies artifacts back into
`apps/dice_core2/build-core2/`; it does not patch pinned dependencies. When checked-in SDK defaults change,
it regenerates its owned staging sdkconfig so features such as PSRAM cannot
remain silently disabled by an older generated configuration.

For the already recorded Core2, use the pinned ESP-IDF Python environment:

```sh
python scripts/install-dice-core2.py --port /dev/cu.EXACT_PORT --install
```

Without `--install` the script only checks the gates. It requires the recorded
full 16 MiB factory backup and hashed identity, exact ESP32 revision and flash
size, unchanged partition table, expected active OTA app0, and matching
artifact digest. It writes only the existing app at 0x10000, reads back the
entire application, and compares the complete lower 64 KiB before resetting.
Each install writes a separate timestamped, artifact-bound receipt and boot
log in `hardware/test-runs/`, preserving earlier evidence.
The firmware stores its M5Stack board detection, radio calibration and BLE
bonds in existing NVS; it never formats or erases NVS on an error. Never use
the generic `idf.py flash` suggestion printed by the build for this unit.
A different Core2 must first have its own complete backup and profile.

The Waveshare needs its board-specific guarded app install at 0x20000 plus
the updated `GAMES/P4_YAHTZEE.P4G` through H1 or the documented SD workflow.
Do not use the Core2 installer for a P4. Backups and generated firmware stay
out of Git.

## Evidence

The original install is recorded in
`hardware/test-runs/2026-09-13-core2-dice-install.json`. The 0.2.0 update is
recorded in
`hardware/test-runs/2026-09-14T040019Z-core2-dice-b5ee88b69761-install.json`:
808,368 bytes, full app readback passed, unchanged boot/partition/NVS prefix
during installation, `imu=ready`, complete 320×240×16 framebuffer, and
4 MiB of PSRAM mapped into the heap. No startup panic was observed. Host tests cover the codec,
CRC, gesture timing, malformed inputs, duplicates, wrong player, held dice,
three-roll limit, restart tokens, and a client accessory roll synchronized
through the game host. The SDL runner was used interactively for roll, hold,
reroll, score, player handoff, and Back. Build success and serial startup do
not establish audible clanking, haptic quality, or an on-device wireless
match; those require the owner's physical check and a named P4/Core2 run.

The owner rejected the first 0.1.0 feedback: sound was too quiet/ticky, the
motor felt weak, and the display flickered with partial redraws and crude
dice. That report is recorded separately from the successful installation.
Version 0.2.0 increases master speaker volume to 255, supplies longer
motor spin-up/contact energy, uses layered 120 ms clanks, and replaces the
renderer. Desktop geometry previews use the actual renderer with a temporary
drawing shim; they check geometry/layout, not physical panel timing or sound.
Physical acceptance of the revised feel and flicker fix remains pending.

The owner also rejected the 0.2.0 audio and animation: the sound still did not
resemble dice, results were hard to read, frame rate was poor, and sustained
vibration felt like a continuous buzz. Version 0.3.0 replaces the audio with
recordings, moves geometry/lighting into an offline atlas, transfers only
the animated region, labels the landed values, and uses real motor-off gaps.
The focused host hardware shim (`scripts/test-dice-core2-feedback.sh`) exercises
the actual feedback implementation
under ASan/UBSan: 13 distinct bursts over three seconds, maximum 85 ms on,
minimum 145 ms off, stopped motion/disarm, and the two-pulse landing pattern
passed. This verifies scheduling, not the motor's physical response.

The 0.3.0 installation is recorded in
`hardware/test-runs/2026-09-14T040821Z-core2-dice-f7d17e3ab1b8-install.json`:
1,392,928 bytes, app SHA-256
`f7d17e3ab1b8eddc6086473449b8a7a8c9980e0cb46ad124cbb3983648b0b22e`.
Full readback and prefix-preservation gates passed. The recorded Core2 booted
with firmware 0.3.0, IMU ready, PSRAM buffer active, no observed startup panic,
and a Ready tap was captured. Physical sound/pulse/graphics acceptance and
measured animation FPS still require the owner's shake test.

The owner's 0.3.0 report was “its pretty awesome!” and “the pulses feel
rpetty good”, with requests for more dynamic cup motion and better
synchronization between sound and vibration. Version 0.4.0 keeps the pulse
envelope but starts each recorded shake clip, motor pulse and visual impulse
from one event. Per-event sound level and motor strength respond to motion.
The ~35 ms recording lead-in is an initial ERM spin-up allowance, not a
calibrated acoustic/mechanical latency measurement.

The cup simulation's sanitizer-backed host checks cover 1–8 dice, strong
forces, bounded timesteps, held dice, and coincident-body separation. The
feedback check verifies one recorded clip per shake pulse, motor-off gaps,
and landing pulses derived from the chosen recording. A 120-frame desktop
preview uses the actual cup simulation and exact RGB565 atlas; text is an
approximation of M5GFX. Physical acceptance of 0.4.0 is pending.

The 0.4.0 run captured 14.9 and 15.7 FPS over full one-second windows (the
short final window was 12.9 FPS), with roughly 25–28 ms drawing plus 24–26 ms
transfer per animation frame. The owner described it as “super laggy”.
Version 0.4.1 removes the old ESP32 PSRAM workaround by setting the minimum
chip revision to the ROM-confirmed 3.1, enables performance compilation,
transfers only changed tile runs, and shortens the active loop delay.
The pinned IDF's `components/esp_psram/esp32/Kconfig.spiram` explicitly excludes
the workaround for minimum revision 3 or later. This image must not be used
on an older ESP32. SDK and M5Stack dependency versions remain unchanged.

The speaker keeps the vendor pin/codec configuration but uses four 128-frame
DMA buffers at 48 kHz, reducing nominal queued capacity from ~42.7 to ~10.7 ms.
This reduces buffering in the path between the shared impact event and the
recorded clack. Actual sound/motor alignment still needs the owner's physical
check. Dirty-region tests cover all changed pixels, unmodified frames, partial
edge bands, and the maximum possible rectangle count under ASan/UBSan.

Version 0.4.2 moves the entire animated table canvas into internal RAM; the
larger full-screen UI buffer and previous-table comparison image remain in
PSRAM. This avoids per-pixel external-memory writes during compositing.
The serial-only `dicebench` command runs a 2.2-second synthetic renderer check
with the real cup physics and sprites, then returns to the normal screen.
It is rejected while connected to a P4, clears Ready, and produces no rolls,
sound, or motor output. Console parsing is bounded and enabled only when
nonblocking input is available. `P4_DICE MEMORY` reports whether the table
is actually internal, and `P4_DICE PERF` provides measured renderer timing.
A renderer-only benchmark must not be presented as combined sound/motor or
wireless-game acceptance.


### Shared-host option and Core2 0.4.3

The native match settings retain their eight-byte P4MP envelope. All zeros
means accessories Off; `01 01 00 00 00 00 00 00` means one shared dice accessory
owned by the host. Unknown encodings and dice settings on a game without the
capability are rejected at launch. These host-owned settings travel in the
offer and accepted match; they do not change the cartridge identity hash.
The reusable registry helper grants the service only to a supported host with
the option enabled. Normal offline use remains supported.

Yahtzee 1.3.0 / protocol 4 carries the shared-accessory hint in snapshot flags.
A validated shake for a remote current player uses the host's normal roll and
snapshot path. Controller actions retain their local-player restriction.
Focused tests cover both players sharing one accessory, stale/duplicate
shakes, held dice, three-roll limits, peer loss, and controller fallback.

Core2 0.4.3 is installed with full application readback and preserved boot/NVS
prefix: `2026-09-14T043311Z-core2-dice-e5f23510e4f3-install.json` in
`hardware/test-runs/`. Its 1,740,832-byte application has SHA-256
`e5f23510e4f33060ed25e952c55e3ae25ce012508a3e46a2102a5b465c8f63a7`.
The recorded synthetic cup benchmark measured 43.6 and 46.3 FPS in its full
windows, with about 8–9 ms drawing and 9–10 ms transfer. This is a hardware
renderer measurement, not a simultaneous BLE/audio/haptics acceptance.
See `2026-09-13-core2-dice-0.4.3-benchmark.json` and its retained serial log.


### Dual-core candidate and roll randomness

Core2 0.5.0 is installed (`2026-09-14T044513Z-core2-dice-96220cf1ce75-install.json`).
Its 1,741,216-byte app SHA-256 is
`96220cf1ce75a590aafe137f22fe70354d993dc052040d97a92df5987d567969`.
Serial confirms renderer CPU1 and motion/audio CPU0. Synthetic renderer windows
measured 46.2 and 49.1 FPS; the final partial interval includes return-to-idle
cost and is not a steady-state rate. No combined BLE/acoustic performance is
claimed from this diagnostic.

Yahtzee's previous LCG alternated parity. Since actual faces consumed every
second output between animation draws, a roll could contain only odd or only
even faces. Actual rolls now use a separate 64-bit PCG XSH-RR stream with
rejection sampling for six faces. Animation uses its own state and cannot
consume real roll values. Network games seed the stream from the OS-generated
session seed; local games use their start-time seed. Core2 practice retains its
hardware RNG and rejection sampling. Natural duplicate results remain valid.
Algorithm reference: https://www.pcg-random.org/using-pcg-c-basic.html .
Tests cover 12,000 rolls, per-position face counts, parity mixing, repeated
whole rolls, zero seed, and independence from animation update cadence.

The P4 accessory worker is pinned to CPU1 alongside the existing NimBLE task;
the native game executes on the main CPU0 task. Game exchange only copies
bounded requests/status through queues and never waits for radio operations.


### Waveshare unit1 installation

The recorded unit1 (identity SHA-256
`c9004de451366bc54158d9d1f3504892c068153610a3f31093785827f1de380d`)
received application `e2a49f9890730dd7649fbb4ba8d70c0a22c69d3214b75ede9b05fd6bf8ea79ef`
(1,884,176 bytes) through the frozen app-only successor route. Full padded
readback passed; bootloader, partitions, OTA data and NVS were preserved.
Serial confirmed the launcher, display, touch, SD catalog, and CPU1 accessory
worker. Boot dial/modem audio reported `ESP_ERR_INVALID_ARG`; this is retained
as a warning, not console acoustic acceptance.

Yahtzee 1.3.0 / protocol 4 was transferred with H1 validation and readback:
42,168 bytes, SHA-256
`63b4a11f2112c0877a13172c7b96078308c6fe800b9253da4b97587a939c5de4`.
Only Yahtzee was copied to the SD card. The full build was made from the frozen
`/tmp/p4-dice-console-candidate` worktree, retaining committed Rummy because
unrelated concurrent Rummy changes failed package validation. Its complete
source manifest and sealed artifacts are under the ignored
`hardware/local-state/waveshare-shared-dice-e2a49f9/` directory. Exact-unit
bindings are in `hardware/evidence/waveshare-unit1-shared-dice-20260913-exact-unit-authorization.json`.

At installation completion, physical Core2 roll/connection and two-console
shared-host acceptance remained pending. Build and host-test success do not
establish those results; see subsequent dated live evidence if present.


### Waveshare unit2 installation

The recorded green unit2 (identity SHA-256
`cb175826408c181592f300640029fd45de66e3e38df9bd1f7d8a3c0b9f055e79`)
received the identical frozen application `e2a49f9890730dd7649fbb4ba8d70c0a22c69d3214b75ede9b05fd6bf8ea79ef`
and Yahtzee cartridge `63b4a11f2112c0877a13172c7b96078308c6fe800b9253da4b97587a939c5de4`.
Its original application and complete mutation span were preserved. Full
application readback, retained-UART startup, CPU1 accessory-worker startup,
BLE host readiness and verified H1 cartridge transfer passed. The same boot
sound `ESP_ERR_INVALID_ARG` warning appeared; console sound acceptance remains
unclaimed. The install receipt and serial evidence are recorded under
`hardware/test-runs/2026-09-13-waveshare-unit2-shared-dice-*`.

`scripts/install-waveshare-shared-dice.sh` now requires an explicit, unique
`--unit unit1` or `--unit unit2`, and selects separately hashed exact-unit
authorizations, locks and recovery directories for the common sealed build.
Missing, invalid and duplicate unit selections are rejected before hardware
access. The already installed Core2 was unplugged to free a USB cable for
unit2; it needs power, but its game connection is BLE and does not require
USB. Two-console shared-roll acceptance remains pending an actual powered
Core2 test; matching builds and cartridge hashes alone do not establish it.


After the unit2 cartridge upload, both the owner's screen report and retained
serial showed only Doom/Chex in the multiplayer list. A retained-UART restart
restored 21 valid cartridges and six native multiplayer registrations (eight
entries with Doom/Chex). Yahtzee registered at protocol 4. See
`hardware/test-runs/2026-09-13-waveshare-unit2-after-yahtzee-boot.log`.
This restores the current device's list; the underlying transient catalog
refresh failure was not isolated and no permanent catalog-refresh fix is
claimed. The owner needs to charge or power the devices before the remaining
shared Core2 connection/roll/handoff test.


### Core2 0.6.0 / Yahtzee 1.3.1 hold and connection candidate

Added Core2 touch hit regions, confirmed hold/unhold selections, all-held
recovery, kept values in practice rerolls, and explicit USE P4 / SAVING / READY
states. Bluetooth discovery now waits for completion callbacks and reports
setup failures rather than silently remaining linked. Core2 `dicestatus` is a
bounded, read-only serial diagnostic; it never arms or rolls dice.

Regression tests cover rapid keep/undo with an older acknowledgement in flight,
held remote-player rerolls, all-held release, stale tokens, modified faces,
invalid masks, v1/v2 ABI tail sizes, and disconnect/turn resets. Desktop
keyboard play confirmed roll, keep, reroll, player handoff, menu and exit.
The desktop automation's coordinate click was observed at a different SDL
window coordinate (the temporary trace was removed); no physical touchscreen
acceptance is claimed from that click. Portable touch-hit tests pass. Updated
firmware installation and actual Core2 connection/hold/shake acceptance must
be recorded separately with their exact artifact hashes.


Both recorded Waveshares now have the readback-verified `fe51c95e9cdb3a811139e64e90149390c458488b1bdcdff3bd5d184fe7c41bca`
application and verified Yahtzee 1.3.1 cartridge
`05a55116d0d1a01f83723b6dfbc21d807d5eb7ae158fb128041f8e51a80b364c`.
Each was restarted after upload and registered six native multiplayer games,
including Yahtzee protocol 4, alongside Doom/Chex. Exact receipts are
`hardware/test-runs/2026-09-13-waveshare-unit1-dice-hold-install.json` and
`hardware/test-runs/2026-09-13-waveshare-unit2-dice-hold-install.json`.

Core2 0.6.0 is built (1,743,968 bytes, SHA-256
`45ef67226f9bcf775b161d0f522015d297fa4a990490f81d862e71421a3b80f0`)
and frozen under `hardware/local-state/core2-dice-0.6.0-45ef6722/`, but its USB
port was absent at this deployment checkpoint. It has NOT yet replaced the
installed 0.5.0 image. The v2 connection/hold test requires this matching Core2
update; do not present the console installs as end-to-end dice acceptance.
