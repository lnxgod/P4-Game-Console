# Read-only lower-clock storage probe

This diagnostic exists because the official one-bit, 10 MHz storage test
initialized the card but timed out (`ESP_ERR_TIMEOUT`, `0x107`) reading its
first sector. That is a raw-I/O failure, not evidence of an absent filesystem,
so WAD provisioning and formatting remain blocked.

The probe retries the exact authorized SDMMC0 wiring at 10 MHz, 5 MHz, then
1 MHz. Every profile is one-bit SDR with internal pull-ups; the host's DDR,
4-bit, and 8-bit capabilities are explicitly removed. It records a SHA-256 of
canonical CID fields rather than printing raw identity, checks geometry and
status, reads sector zero eight times, reads other bounded sectors twice, and
requires byte-stable results. It then invokes FatFs through a diagnostic disk
adapter that exposes reads but returns `RES_WRPRT` for every write/trim/format
request. The exact `FRESULT` and underlying SDMMC read error are reported
separately.

No file, sector, partition, erase, or format write API is called. All flash
flags remain false until the exact reproducible build and central gate are
reviewed. A hardware run must still use an app-only write with readback.
