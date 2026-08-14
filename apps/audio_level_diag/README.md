# D2.4 direct-I2S level diagnostic

This app is a diagnostic-only follow-up to the inaudible D2.3 run. It does not
replace or modify `components/platform_audio`, and therefore does not raise the
audio level used by Doom.

The exact Elecrow V1.0 through V1.2 Lesson 12 sources use I2S1 on GPIO21/22/23,
omit MCLK, multiply PCM samples by ten, and clamp them to full-scale PCM16.
D2.3 used a peak of only 512 (-36.1 dBFS) and its synchronized microphone
capture contained no correlated tone. D2.4 uses a 4096 peak (-18.1 dBFS) for
one 400 ms, 440 Hz triangle wave with 80 ms fades. This remains 18.1 dB below
the vendor example's possible full-scale output.

GPIO30 reaches the alternative analog U4 amplifier path. It is driven high as
the first hardware action, but it is not described as a mute for the candidate
direct digital amplifiers. Direct-path safety instead comes from keeping the
I2S DMA ring filled with zero before the tone, sending and draining a complete
zero ring after the tone, using finite exact writes, and refusing to release
owned resources when cleanup is uncertain.

The image cannot emit its tone on an ordinary boot. With its owned LDO3/LDO4
off, direct output unclocked, and the candidate direct amplifiers still on the
board's unswitched VDD5V rail, it waits for
one exact authorization-bound LF-terminated host ARM frame. Missing, malformed,
or duplicate input enters the safe cleanup loop. All repository flash flags
remain false until the exact build, capture route, and one-shot authorization
are separately frozen and reviewed.
