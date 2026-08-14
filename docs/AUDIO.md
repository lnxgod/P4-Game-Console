# Historical alternate codec-backed audio slice

This document records the earlier ES8311-oriented diagnostic branch and its
historical evidence. It is not the E6 Doom runtime contract and must not be
read as superseding the exact pinned factory initializer documented in
`docs/HARDWARE.md`. E6 uses the complete vendor initializer shape (I2S0 PDM RX
GPIO24/GPIO26 followed by I2S1 speaker TX GPIO21/GPIO22/GPIO23), makes no
external codec I2C transaction, and treats only I2S1 TX MCLK as unused.

The 10.1-inch board's populated speaker route is codec-backed, not a direct
I2S amplifier:

`ESP32-P4 I2S1 -> ES8311 DAC -> NS4263B analog PA -> J4/J6 speakers`

The exact Elecrow V1.0, V1.1, and V1.2 Eagle netlists agree on GPIO21 LRCLK,
GPIO22 BCLK, GPIO23 DOUT, GPIO24 MCLK, GPIO45/46 codec control through BSS138
level shifters, and GPIO30 to the NS4263B shutdown pin. Elecrow's playback code
for all three revisions also establishes that GPIO30 is active-low: low enables
the amplifier and high shuts it down. The unpopulated NS4168 paths are not used.
The reproducible proof is in
`hardware/evidence/elecrow-10.1-audio-driver-path.json`.

The control path also depends on ESP32-P4 internal LDO4 at 3.3 V. Across all
three schematics, LDO4 reaches `VDDPST_5` through R109=0R; that rail supplies
the GPIO45/GPIO46-side I2C pull-ups and the Q8/Q9 BSS138 gates. An audio-only
image that omits LDO4 can create I2C0 successfully but cannot communicate with
the ES8311. Every pinned Elecrow Lesson12 revision acquires LDO3 at 2.5 V and
then LDO4 at 3.3 V before audio. The standalone diagnostic follows that exact
pairing, while recording that LDO3 itself feeds the MIPI DPHY rather than the
codec path. A combined display/audio runtime needs one shared, reference-counted
owner for these LDO channels.

## Dependency and address contract

`audio_diag` locks Espressif `esp_codec_dev` exactly at 1.3.4 and records its
Component Registry hash in `dependencies.lock`. The IDF 5.5.3 ES8311 example is
the API reference. The component's `ES8311_CODEC_DEFAULT_ADDR` is the legacy
8-bit value `0x30`; its new IDF I2C control implementation right-shifts that
value and registers the device at the 7-bit address `0x18`. The app disables
the legacy I2C compatibility layer and borrows an IDF 5.5.3 master-bus handle.

## Ownership boundary

`platform_audio` owns I2S1, the ES8311 interfaces, and the GPIO30 amplifier
lifecycle. It borrows but never deletes a shared I2C bus. A game writes bounded
interleaved PCM16 stereo frames through the platform API; it does not receive
I2S or codec handles. The current diagnostic alone creates I2C0 because no
touch or other I2C client is present. A combined badge runtime should replace
that diagnostic ownership with a shared platform I2C service.

The platform intentionally configures Espressif's ES8311 codec with
`pa_pin=-1`. In version 1.3.4, the codec constructor enables a configured PA
pin before the high-level device has been muted. Project-owned GPIO30 control
lets the sequence remain fail-closed:

1. GPIO30 high before any bus or clocks.
2. Acquire LDO3=2.5 V, then LDO4=3.3 V, and wait 20 ms.
3. Create I2C0 and require an acknowledgement from ES8311 address 0x18.
4. MCLK/BCLK/LRCLK active while the external amplifier is still shut down.
5. ES8311 configured, muted, capped at 10% volume, and fed zero preroll.
6. GPIO30 low, 20 ms muted settle, then codec unmute.
7. Codec mute, 10 ms settle, GPIO30 high, destroy codec/I2C, then release
   LDO4 followed by LDO3.

Every error path attempts GPIO30 high before cleanup or halt.

## Diagnostic scope

The corrected build-only diagnostic generates one deterministic 440 Hz triangle tone:
600 ms total, 80 ms linear fades, PCM peak 512 out of 32767, and codec volume
5%. Host tests verify sample bounds, stereo equality, DC balance, fades,
active-low policy, supported sample rates, and the 10% volume ceiling under
AddressSanitizer and UndefinedBehaviorSanitizer.

The prior exact image was flashed and failed safely before tone playback because
it omitted the LDO prerequisite; its ES8311 transactions received no
acknowledgement and GPIO30 remained high. That result is preserved in
`hardware/test-runs/2026-08-12-audio-diag-d2.json`. The corrected artifact has
not yet run. The board profile and every audio flash flag remain false pending
independent app-only review and a monitored test. Build success is not evidence
of sound, safe loudness, pop suppression, or coexistence on GPIO45/GPIO46.
