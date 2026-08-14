# D2.5 corrected direct-audio enable diagnostic

This isolated source app tests the corrected GPIO30 playback sequence without
changing `components/platform_audio`, Doom, or D2.4. It has no flash or runtime
authorization.

The pinned Elecrow V1.0 through V1.2 schematics show GPIO30 `AUDIO_OUT_SD`
driving the populated analog U4 shutdown input and the gate of optional
`Q10 AO3401_NC`. Q10's source is `VDD_3V3`; its drain is `AUDIO_CTRL`.
Optional `R139/R117 100K_NC` then feed the `CTRL` inputs of optional U13/U3
NS4168 amplifiers. The NS4168 datasheet defines 0--0.4 V as shutdown,
0.9--1.15 V as left-channel selection, and 1.5 V through VDD as right-channel
selection. Elecrow's factory playback code drives GPIO30 low immediately before
playback and high afterward. Accordingly, this diagnostic treats high as the
inactive request and low as the bounded active request. GPIO30 low definitely
enables populated analog U4 through R147; it can enable the Q10/NS4168 path
only if its NC parts and I2S routing bridges are populated. The candidate
amplifiers' VDD5 supply is unswitched.

GPIO30 is configured for simultaneous input and output after its safe latch
level is written, making each pinned-IDF pad-level readback meaningful while
preserving latch-high-before-output-enable startup ordering.

After one exact authorization-bound host ARM line, the app establishes a full
six-by-256-frame zero DMA ring and running I2S1 clocks before acquiring its
standalone LDO3/LDO4 rails. It holds GPIO30 high through the five-second
capture-arm interval. It then drives GPIO30 low, checks the GPIO readback, and
holds it low for 350 ms while transmitting zeros. This exceeds U4 NS4263B's
typical 250 ms startup time at 5 V with the board's 1 uF BYPASS/VREF capacitor;
the prior high interval exceeds 100 us, so the sequence requests U4 Class AB
mode. The app then emits one 400 ms 440 Hz stereo triangle wave at a peak of
4096 with 80 ms fades. It writes and drains a full
zero ring for 120 ms, drives and verifies GPIO30 high before I2S teardown, and
only then releases I2S and the rails.

The host-arm nonce is SHA-256 over the ASCII authorization ID followed by
` host-arm v1`. Missing, malformed, duplicate, or late input remains fail
closed. Emergency cleanup requests and verifies GPIO30 high first, then still
writes and drains a full zero ring. If either the inactive readback or the
zero/drain result is uncertain, it retains the I2S handle, clocks, and owned
rails and retries rather than declaring the output safe.
