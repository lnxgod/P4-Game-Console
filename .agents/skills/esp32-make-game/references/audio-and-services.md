# Audio and optional services

Read this when the game needs PCM music, producer timing, saves, achievements,
resource sidecars or dice accessories. Keep hardware ownership and game callback
lifecycle with Console OS.

## OS-owned audio scheduling

Use both P4 cores through OS-owned services, as described in
`docs/GAME_PERFORMANCE.md`. Games submit bounded PCM/tone commands; the Tab5
shared core-1 audio worker handles mixing/output while the foreground core runs
the game. Never create private tasks or move cartridge callbacks to another
core. Measure producer jitter and backpressure; a successful synth test alone
does not prove continuous device audio.

## Optional service contracts

Use the public APIs for optional saves, achievements, resource sidecars and
dice accessories when the design needs them. See `docs/GAME_STARTERS.md` and
the corresponding Game SDK section before adding a service. Neither `storage`
nor `save` gives a game a filesystem path; missing optional capabilities need
an honest fallback.

For simple sound, request `audio-tone` and call `p4_game_play_tone()`. For a
software mixer, request `audio-stream`, declare
`P4_GAME_CAP_AUDIO_STREAM`, and submit 1–256 frames of signed 16 kHz PCM16
stereo with `p4_game_submit_pcm16_stereo()`. The host copies accepted blocks
into bounded OS-owned buffering. Read
`P4_GAME_AUDIO_STREAM_BUFFER_FRAMES` in
`components/p4_game_api/include/p4/audio.h` and
[the current audio scheduling contract](../../../../docs/GAME_PERFORMANCE.md)
for capacity and producer timing. Drop/degrade a rejected block and never
busy-wait in a callback.
