# Native Tab5 artwork

Four production illustrations generated with the built-in ImageGen tool. Exact prompts, source paths and SHA-256 values are in source.json. Images contain illustration only; all controls and labels are native C rendering.

Run components/console_shell/tools/pack_nextgen.py with Pillow to reproduce src/nextgen_assets.h. The hero retains 1024 × 576 pixels using 256 RGB565 palette entries and byte indices; supporting art is 320 × 180 RGB565. This saves about 589 KB while keeping the firmware inside the existing update-package limit. The transparent brand mark is encoded from the existing reviewed OS logo.

The three antialiased font atlases use the pinned Arimo weight 650 source at 32, 44 and 68 pixels. Source, checksum and SIL OFL 1.1 license are in third_party/arimo. No network or asset decoding is needed at startup.
