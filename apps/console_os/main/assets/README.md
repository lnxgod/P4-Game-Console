# GameChangers AI boot logo

`gamechangers_ai_logo.rgb565` is the GameChangers AI nonprofit arcade-console
mark used by `https://www.gamechangersai.org/`. It was retrieved from
`https://www.gamechangersai.org/assets/gamechangers-128.png` on 2026-08-17 at
the console owner's request. The upstream RGBA PNG is 128×128 pixels with
SHA-256 `903d20f0b3d52c8b5b785686680cbb5e884ea17a5636fdf381e9752ade92efce`.

The embedded asset is the same mark, composited on black and resampled to
112×112 RGB565 little-endian pixels for the 320×200 Console OS boot surface.
Its SHA-256 is
`48ee7b2a15a744547884ec6ea7f462277ab60e5dde4d0805bf39db9c0b2bd892`.
It is embedded only in this GameChangers-branded Console OS boot experience;
do not reuse it outside authorized brand work.

## GameChangersAI OS 0.42 refresh

`gamechangers_mark_v042.png` is the transparent, high-resolution joystick-mark
refresh made with the built-in ImageGen tool at the owner's request. It retains
the official mark's joystick, green concentric rings/buttons and silver/cyan
arcade deck. The wordmark is rendered by the UI, not baked into the image.
`gamechangers_mark_v042.json` records the full prompt, source and output hashes.
This is a reviewed adaptation, not an unchanged upstream logo.

`pack_boot_mark.py` uses Pillow to resize and pack the PNG into a 384×384
RGB565 little-endian + alpha8 image (442,368 bytes). The firmware CMake file
checks the exact packed hash and size before embedding. The older 112×112
asset above remains available to the compact legacy boot renderer.

The 0.42 wordmark and native UI use a small antialiased ASCII coverage atlas
from the source-pinned, SIL OFL 1.1 Arimo font in `third_party/arimo/`. Regenerate
with `components/console_shell/tools/generate_ui_font.py` using Python/Pillow.
No Microsoft artwork or startup recording is included.
