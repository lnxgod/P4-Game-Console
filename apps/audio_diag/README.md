# Audio diagnostic

This frozen historical D2.2 diagnostic exercises the populated speaker
topology proven in Elecrow's V1.0, V1.1, and V1.2 schematic netlists and
resolves a mismatch found in Elecrow's exact 10.1-inch factory source at
commit `c5a4373`:

`ESP32-P4 I2S1 + MCLK -> ES8311 -> differential analog -> NS4263B -> speakers`

The app-local `components/platform_audio` directory is a byte-for-byte archive
of the dual-path component used to build the recorded D2.2 image. It exists only
so the historical source inventory, source hashes, and exact-image verifier keep
resolving to the implementation that actually ran. It is not a reusable backend
and must not be copied into games or new diagnostics. Those consumers use the
repository-root `components/platform_audio` direct-I2S service instead.

The factory BSP selects I2C1, but its `my_codec` control interface only reads
and writes an in-memory register array; it does not program an external codec.
Its speaker path drives I2S1 on GPIO21/22/23 without MCLK, then enables the
active-low GPIO30 amplifier. D2.2 therefore scans I2C1 before selecting a path.
If ES8311 address `0x18` acknowledges, the existing `esp_codec_dev` 1.3.4 path
with MCLK and raw register guards remains mandatory. If it does not, the app
deletes I2C1 and runs one tightly bounded factory-style direct-I2S probe.

The sequence is fail-closed:

1. Set GPIO30 high (amplifier shutdown) before creating any bus or clock.
2. Acquire LDO3 at 2.5 V and LDO4 at 3.3 V, then scan only 7-bit addresses
   `0x08..0x77` on I2C1 with a 10 ms per-address timeout and log every ACK.
3. If `0x18` ACKs, configure and raw-readback guard the ES8311 while GPIO30
   remains high; otherwise delete the scan bus and configure direct I2S1 with
   MCLK unused, matching the exact factory BSP semantics.
4. Submit 256 zero stereo frames before enabling the amplifier, wait 20 ms,
   and play exactly one 400 ms, 440 Hz triangle tone at 512/32767 peak PCM.
5. Submit zero postroll, drive GPIO30 high, tear down all bus/clock ownership,
   release LDO4 then LDO3, and remain silent.

All flash flags remain false. The recorded hardware run passed its serial
lifecycle contract on the factory-direct-I2S path; it did not include operator
or microphone confirmation that the tone was audible.
