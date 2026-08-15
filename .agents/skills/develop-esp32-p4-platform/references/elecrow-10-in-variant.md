# Elecrow 10 in variant

Use **10 in variant** as the stable project name for the Elecrow CrowPanel Advanced 10.1-inch ESP32-P4 unit. The vendor model/SKU is DHE04310D. The connected unit is ESP32-P4 v1.3 with 16 MiB flash and 32 MiB PSRAM; bind writes to the hashed device identity in `hardware/board-profile.json` and `hardware/backups/manifest.json`.

## Evidence hierarchy

- Treat `hardware/board-profile.json` as the authorization boundary. Global `pin_map_authorized` remains false; use only exact scoped peripheral authorizations.
- Treat the complete factory backup as recovery material, not a pin-map authority.
- Treat commit-pinned Elecrow source and cross-revision schematics as source/topology evidence.
- Treat serial logs, exact readback, photos, measurements, and clearly labeled operator observations as runtime evidence. Never promote a build or a user report into an independently observed hardware claim.
- Preserve exact artifact hashes and use an app-only write unless a reviewed task intentionally changes bootloader or partition table.

## Proven platform baseline

- Display: MIPI-DSI bus 0, two lanes at 900 Mbps, EK79007 1.0.2, 1024x600 RGB565 at 51 MHz, GPIO31 backlight, LDO3 2.5 V then LDO4 3.3 V. Use `platform_display`; never drive GPIO29 or GPIO41.
- Doom video: 320x200 numeric `0x00RRGGBB` to centered 3x RGB565 output with 32-pixel black side margins through `doom_video`.
- Factory recovery: `hardware/backups/elecrow-p4-factory-before-project.bin`, exactly 16 MiB, is bound by `hardware/backups/manifest.json`. Do not erase it or overwrite the manifest.

## Factory touch path

- Use one caller-owned shared I2C1 bus on SDA GPIO45 and SCL GPIO46. `platform_i2c_shared` is the sole bus owner; `platform_touch` borrows its handle.
- Use the pinned GT911 path at 400 kHz, 1024x600, no axis swap or mirroring, maximum five contacts.
- Match Elecrow's address-selection sequence: GPIO40 active-low reset and GPIO42 address latch/input, polling only with no runtime ISR. Try 7-bit address 0x5d first; delete the failed panel IO before trying 0x14.
- The exact managed versions are `espressif/esp_lcd_touch` 1.1.2 and `espressif/esp_lcd_touch_gt911` 1.1.3. Reuse `platform_touch`; do not copy the BSP into a game.
- Use `apps/touch_diag` for isolated visible crosshair/coordinate qualification before claiming contact behavior. Initialization alone does not prove touch coordinates or gestures.

## Factory speaker path

- Elecrow's pinned 10 in factory source initializes PDM RX I2S0 first at 16 kHz mono on GPIO24 clock (1.024 MHz under the pinned DSR_8S configuration) and GPIO26 input, then I2S1 speaker TX as 16 kHz signed PCM16 stereo on LRCLK GPIO21, BCLK GPIO22, DOUT GPIO23, with TX MCLK unused and active-low amplifier control on GPIO30. Its codec-control object is an in-memory shim, not an external ES8311 transaction. Omitting PDM RX is a factory-speaker-TX-compatible subset, not complete factory audio-init equivalence.
- Use `platform_audio_factory` for this factory-direct path. Assert and pad-readback GPIO30 high before I2S setup; preload the complete 1536-frame DMA ring with zeros; enable with one high-to-low transition; keep exact zeros for at least 350 ms; read back low before publishing RUNNING.
- Bound writes and fail closed: on any write/stop error request and verify GPIO30 high first, retain resources if cleanup cannot be proven, and reject restart from FAILED_SAFE.
- Doom audio is 16 kHz PCM. Backend volume step 10/10 is unity gain, not 10 percent; lower steps attenuate proportionally. The exact-unit E6 music build uses step 6/10 and mixes validated WAD MUS through a bounded procedural synth above the platform boundary; it needs no external MIDI hardware, codec transaction, or SoundFont and makes no bit-exact OPL claim. Adapter telemetry is pre-attenuation while factory-backend telemetry is post-attenuation: at step 6, require at least `floor(adapter_peak * 6 / 10)`, cap the backend peak at 19660, and allow integer attenuation to reduce non-zero counts. Independently sampled counter surplus must be bounded by the corresponding frame/sample delta rather than forced equal. The factory UI volume is a separate 0..100 setting and normally attenuates its source.
- Published schematics show an ES8311/NS4263B route and mark the direct NS4168 route NC. Their outputs can converge on the speaker pairs. Prefer a powered-off population/continuity proof before GPIO30-low. For this exact bound unit only, an explicit operator-accepted factory-replay authorization may substitute after recording the known-working factory history, prior nondamaging bounded GPIO30-low run, unresolved risk, exact allowed GPIOs, and rollback. Such an exception is not population proof, amplifier-state proof, or family-wide authorization.

## Doom touch and optional-sound image

- Use `apps/doom_embedded_touch_audio`, never mutate the proven E1/E3 apps in place.
- Keep USB host/HID absent. Touch frames feed `doom_touch_input`; render the visible controls into an app-owned 320x200 XRGB buffer before `doom_video_submit_xrgb8888`.
- Treat simultaneous contacts as a set union of actions. The 1024x600 layout is: lower-left virtual D-pad centered near (180,445); Fire at (900,455), which also accepts menus; Use at (742,486); Run at (770,350); Strafe at (640,430). The top row, left to right, is Map (80,68), Back (178,68), previous weapon (625,68), next weapon (723,68), Accept (840,68), and Pause (945,68). Keep this mapping and the visible overlay synchronized.
- Initialize in this order: runtime authorization, audio safe-high request when audio is enabled, display dark, shared I2C, touch, exact WAD/VFS, video/first black frame, audio bind, backlight, Doom.
- Touch failure must synthesize releases, destroy/release the client when possible, and retry only after cleanup and bus ownership are proven. Audio failure must remain silent and allow Doom/touch to continue. Display, WAD, or video failure halts dark.
- For a hardware-active image bound to this exact unit, retain one exclusive UART descriptor from the final identity/revision/security/16 MiB checks through the app write and complete app readback; separate probe and write processes leave a board-swap window. A RAM-stub write must call `flash_finish(reboot=False)` once as write synchronization, remain in the same stub, and use ordered readback chunks no larger than 512 KiB. Revalidate the target on that same handle after the aggregate SHA-256 matches, assert DTR/RTS inactive, then use one pinned hard reset as the sole application launch. Do not reopen the port, invoke `run`, use a soft reset, or launch before readback. Capture serial afterward without transmitting or resetting.
- Bind this unit's flash by its exact JEDEC RDID low 24 bits, `0x1840c8`, not only by the capacity byte. The pinned ESP32-P4 stub returns the full 32-bit W0 register even though RDID supplies 24 meaningful bits; after a security-info command the unused high byte can be padded as `0xff`. Accept only raw `0x001840c8` or `0xff1840c8`, canonicalize both to `0x1840c8`, and apply that exact check to every physical RDID: initial post-stub, any XMC-startup retry, post-`0x66`/`0x99`, prewrite-policy, immediate write-boundary, and prelaunch. Any other high byte or low-24 identity fails before mutation or launch.
- On 2026-08-13 the corrected guarded E5 touch-only transaction installed the exact 4,898,400-byte image at `0x10000`, verified the complete 4,898,816-byte `0xff`-padded mutation span in ten chunks no larger than 512 KiB (SHA-256 `9c288a83ecfb061cdbec510e679166bb85e2fcf44f3a21f8a2f79e440c1303f9`), and launched it once. A later receive-only capture proved video progress from frame 300 to 600 and successful GT911 polling from 288 to 588 with zero touch/video failures; every periodic record reported gates `1/1/0`, zero audio calls/frames/failures, the firmware-derived `gpio30=untouched` state, and no USB-runtime/fault markers. GPIO30 was not electrically measured. The capture attached after startup and did not test or record contact presence, so touch coordinates, multi-touch mapping, and visible gameplay response remain pending person observation. Audio stayed disabled. The exact record is `hardware/test-runs/2026-08-13-doom-e5-touch-only-persistent.json`.

## USB distinction

- The P4 high-speed USB data peripheral can enumerate low/full/high-speed devices, but that does not make the panel connector a protected host-power source.
- Never use a passive OTG adapter. Native controller work still requires the repository's powered, current-limited, backfeed-safe direct-data fixture authorization. A powered hub is not accepted merely because it lights the controller; review VBUS isolation and topology first.

## Current qualification scope

Select only the commands that cover the changed boundary:

- Touch component: `make platform-touch-host`.
- Doom touch adapter: `make doom-touch-host`.
- Touch diagnostic integration: `make build APP=touch_diag`.
- Combined Doom/touch/audio integration: after its focused host targets, run
  `make build APP=doom_embedded_touch_audio`.

Run `make verify` when the environment has not already been established. Do not
run repo-wide `make check`, every command above, or unrelated peripheral suites
by default. Reserve them for an explicit request, a lock/toolchain migration,
or a genuinely cross-cutting change. Do not repeat an unchanged build or
hardware run.

Run the affected app's verifier before any write, use the guarded app-only
route, and verify the exact aggregate flash readback before one explicit
launch. For touch-only mode, record touch initialization/contact evidence,
monotonically increasing video statistics, zero audio calls, and no GPIO30
access. Only after the separate audio release condition is satisfied may an
audio-enabled image record increasing audio statistics; a human must still
confirm audible output and actual touch response before those claims become
hardware-tested.
