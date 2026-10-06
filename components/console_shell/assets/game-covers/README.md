# Built-in game covers

Chex uses an original ImageGen illustration. Doom uses the editable
`doom-classic.svg` wordmark, with gold and steel-blue lettering appropriate to
the classic game rather than imagery suggesting a modern sequel. Its PNG was
rendered with `rsvg-convert doom-classic.svg -o doom-classic.png`.
Prompts/design intent, SHA-256 values and conversion settings are in
source.json. No artwork or game data was extracted from a WAD.

Run python3 components/console_shell/tools/pack_game_covers.py (Pillow required)
to recreate src/game_covers.inc: each built-in has a 128×72 indexed RGB565 icon
and a 640×360 indexed RGB565 cover. Each cover occupies 230,912 bytes; each icon
9,728 bytes. Only the Tab5 descriptor retains the large cover in embedded builds.
Launcher labels remain sharp, code-rendered UI text outside the images; the
Doom illustration itself is a wordmark.

The converter also records Byte Buddy's existing thumbnail (9,728 bytes) to
recognize its matching legacy 1024×576 hero. A cartridge with new artwork takes
precedence automatically. Covers are static flash data; no PNG decoding, heap
allocation, or extra gameplay drawing is introduced.
