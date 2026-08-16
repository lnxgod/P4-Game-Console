# Doom MUS music test notes

These notes are the repeatable acceptance procedure for the 10 in variant.
They deliberately separate software proof, serial transport proof, and what a
person actually hears. A tone, a nonzero counter, or a prior sound-effects run
must never be substituted for hearing the real Doom music from this image.

## What this image plays

The sound-enabled `doom_embedded_touch_audio` image reads the original MUS
music lumps from the same ignored, hash-pinned Doom v1.9 shareware WAD already
used by the game. A bounded 140 Hz MUS sequencer drives a project-owned
16-voice procedural software synthesizer. It handles note on/off, program,
channel volume, expression, pan, pitch bend, pause/resume, looping, and basic
percussion. The resulting 16 kHz stereo PCM is saturated into the same buffer
as Doom's sound effects and crosses the existing counted `platform_audio`
gateway.

This first synth is intentionally lightweight and retro sounding. It is not a
bit-exact Yamaha OPL2/OPL3 emulator and does not load arbitrary external MIDI
files or a SoundFont. It does play the actual level/title MUS sequences; there
is no canned test melody or hidden fallback tone.

The backend remains in its proven format and pin configuration. Standalone
Doom defaults to step 8/10; Console OS passes its selected 1–10 master step at
handoff. The Doom music menu volume remains independently effective.

## Before building

Run from the repository root:

```sh
shasum -a 256 local-data/doom/doom1.wad
git check-ignore -q local-data/doom/doom1.wad
```

The required WAD is exactly 4,196,020 bytes with SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
Never add the WAD, a WAD-bearing firmware binary, or a recovery image to Git.

## Software and artifact gates

```sh
make doom-audio-host
make doom-touch-audio-host
make build APP=doom_embedded_touch_audio
python3 scripts/verify-doom-embedded-touch-audio-e6.py \
  apps/doom_embedded_touch_audio/build build-only
```

The native WAD test must report exactly 13 valid MUS lumps totaling 245,179
bytes and nonzero E1M1 output. The sanitizer-backed core test must cover MUS
bounds, timing, looping, volume zero, and song-handle lifetime. These checks
prove parser/mixer behavior only; they do not prove real-time performance or
speaker output on the ESP32-P4.

## Guarded install

Normal project and app flash routes remain denied. After the exact artifact,
build evidence, and exact-unit authorization have been resealed, use only the
issued E6 route with a new private recovery directory:

```sh
install -d -m 0700 hardware/local-state/doom-e6-music-YYYYMMDD-HHMMSS
python3 scripts/doom-e6-authorized-route.py \
  --port /dev/cu.wchusbserial10 \
  --artifact apps/doom_embedded_touch_audio/build/p4_doom_embedded_touch_audio.bin \
  --authorization hardware/evidence/doom-embedded-touch-audio-e6-factory-audio-authorization.json \
  --recovery-directory hardware/local-state/doom-e6-music-YYYYMMDD-HHMMSS \
  --capture-seconds 60
```

Resolve the live `/dev/cu.wchusbserial*` name immediately before running. Do
not reuse a recovery directory. The installer binds the exact device and
flash ID, saves the live app span, writes only the app partition, verifies the
complete padded readback, launches once, and restores E5 automatically if its
retained-UART acceptance fails.

## Required serial evidence

The retained capture must include the exact `START`, `SOUND_BOUND`,
`ENGINE_START`, and `SOUND_READY` records plus at least two periodic `STATS`
records. Across those records:

- display frames/submits/completions, touch polls, audio frames, adapter
  writes, backend writes, and music frames/events must increase;
- at least one music song and note must be observed;
- music parse failures, audio/backend write failures, video failures, and
  touch failures must remain zero;
- adapter/backend nonzero-frame counters and peaks must be nonzero;
- the startup record must show the selected backend volume step and the MUS
  procedural-16voice pipeline;
- no `SOUND_DEGRADED`, `AUDIO_SAFETY_FAULT`, `HALT`, reset, panic, or USB
  runtime marker may appear.

Serial proves that bounded music PCM reached the backend. It does not prove a
speaker emitted sound or that the output was clean.

## Human acoustic test — no substitutes

1. Listen on the title screen for the short Doom intro sequence.
2. Use touch to start Episode 1, Mission 1 and wait for the level music.
3. Confirm a changing musical sequence, not a steady tone, click, hiss, or a
   single sound effect.
4. Fire and open a door while the music continues; confirm music and effects
   are audible together without obvious crackling or repeated dropouts.
5. Open Doom's sound-volume menu and change music volume. Confirm the music
   changes independently while the final backend remains capped at step 6/10.
6. Record the observer, exact artifact SHA-256, serial capture path, and a
   plain-language result such as `music heard`, `SFX heard`, `clean`, or the
   precise failure. Do not infer any of those from counters.

If the screen and SFX work but music is silent, preserve the capture and check
`music_songs`, `music_events`, `music_notes`, `music_frames`, and
`music_parse_failures` before changing synthesis or hardware. Do not alter the
I2S pins, GPIO30 policy, PDM side effect, or codec assumptions to diagnose a
software-music failure.
