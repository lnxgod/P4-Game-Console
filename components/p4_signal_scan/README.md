# P4 signal scan privacy boundary

`p4_signal_scan` converts driver-owned Wi-Fi observations into the bounded
representation that a Game API title may consume. It does not own a radio and
does not connect to, authenticate with, transmit to, or disrupt any network.

For each observation the OS-side adapter:

- accepts at most 32 SSID bytes and one six-byte BSSID;
- replaces non-printable SSID bytes, trims trailing spaces, and exposes at
  most 24 display characters;
- replaces empty SSIDs with `HIDDEN SIGNAL`;
- derives an opaque 64-bit token with keyed SipHash-2-4 over a domain byte,
  BSSID, and SSID;
- exposes only that token, the sanitized label, bounded RSSI, channel, and
  hidden/protected flags.

The key must be generated and owned by Console OS and must never be given to a
game. Raw BSSIDs stay inside the radio adapter. On the Waveshare build the key
is random and device-local in NVS, so a pet can reject duplicate encounters
after a reboot without ever receiving a BSSID. If NVS is unavailable, the
scanner explicitly degrades to a fresh boot-session key.

The SDL game host supplies deterministic fictional observations for gameplay
and sanitizer testing. The Waveshare Console OS build now includes the
`platform_signal_scan` candidate: `esp_hosted` 1.4.7 and `esp_wifi_remote`
0.14.5 are locked; the official P4-to-C6 four-bit SDIO/reset map is explicit;
the service loads or creates a random device-local key; and a background task runs
passive scans into a fixed 32-record driver buffer before publishing only the
strongest eight sanitized results. Focused requests preserve their opaque
token at the top of the next bounded snapshot when it is still visible.

ESP-Hosted's managed-package constructor is excluded by an exact-source build
check without modifying the pinned package. Console OS starts the hosted radio
task only after the launcher, storage catalog, display, touch, and OTA-valid
checkpoint reach READY.

The cartridge callback table exposes `P4_GAME_CAP_SIGNAL_SCAN` only after the
C6 transport and station scanner report ready. Initialization or scan failure
therefore degrades to the game's honest offline state. No SSID, BSSID, or token
is written to the log. The firmware build is proven, but the exact unit's C6
firmware identity and live scan behavior still require retained-UART hardware
acceptance before this service is called hardware-qualified.

RSSI is noisy and is not distance or direction. A game may use repeated values
as a playful hotter/colder signal, but must not present them as location or
measurement truth.
