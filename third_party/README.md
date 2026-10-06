# Third-party sources

`source-lock.json` records immutable source identities. `scripts/doom/vendor-doomgeneric.sh` fetches or accepts a clean checkout, verifies the locked commit and root tree, and imports only the listed upstream paths. `doomgeneric.git-tree` records the upstream Git blob ID for every vendored byte. Run `make doom-provenance` to verify the offline copy.

The `third_party/doomgeneric` files are verbatim upstream files. Platform adapters and build integration live outside that directory so upstream provenance remains mechanically verifiable.

`game-data.json` records identification and provenance metadata, never game-data bytes. The current D0 development default is an ignored local Doom v1.9 shareware IWAD whose size and SHA-256 are checked before use. Public/reproducible builds should use the separately supplied Freedoom release recorded in the same manifest.

Never vendor WAD files. Never clone the ESP32P4DOOM reference into this repository's history. Doom engine source belongs under a clearly separated third-party directory; board and game-platform services remain in `components/`.

The Quake engine under `third_party/quakegeneric/` is likewise a verbatim,
manifested subset of the pinned Espressif ESP32-P4 Quake repository. The SDL3
PC adapter and Console OS platform adapter live outside that directory. Quake
PAK files are ignored local/SD inputs and must never be committed or embedded.
