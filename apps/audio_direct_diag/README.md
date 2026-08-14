# Reusable direct-audio diagnostic (D2.3)

This narrow diagnostic exercises `components/platform_audio` itself. It does
not duplicate the I2S driver in app code and does not touch display, storage,
USB, touch, I2C, a codec, or MCLK.

The standalone app temporarily owns LDO3 at 2.5 V and LDO4 at 3.3 V. It keeps
GPIO30 high, creates the muted 16 kHz stereo service, feeds a 400 ms full-scale
440 Hz source tone through the service's quiet attenuation policy, submits a
complete-ring zero postroll, stops and destroys the service, then releases
LDO4 followed by LDO3. Any unconfirmed backend cleanup retains the rails.
The amp remains disabled during a five-second capture-arm window so the host
can attach serial monitoring after verified readback and synchronize a Mac
microphone recording before the one bounded tone.

All flash flags remain false until a reproducible artifact, exact preflash
verifier, readback-before-run sequence, synchronized microphone capture, and
independent review are complete.
