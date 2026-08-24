# LORD visual assets

`title-background-ansi.png` is the checked-in 320×136, 16-color game asset.
`src/generated/lord_title_art.h` is its deterministic RGB565 representation.
Regenerate both with:

```sh
python3 games/lord/tools/build_title_art.py \
  games/lord/assets/source/title-background-generated.png \
  games/lord/assets/title-background-ansi.png \
  games/lord/src/generated/lord_title_art.h
```

The source image was generated for this port with the built-in OpenAI image
generation tool on 2026-08-18. It is new project art and does not copy or
vendor upstream LORD `.ICN`, `.LRD`, or RIP assets.

Final generation prompt:

> A striking red dragon looming over a medieval walled town at night, with a
> tiny lone warrior in the foreground, rendered as premium hand-authored 1990s
> VGA pixel art blended with authentic ANSI/RIP 16-color aesthetics. Use a
> very wide composition, crimson firelight and deep navy night, with no text,
> logo, UI controls, watermark, photorealism, blurry gradients, or modern
> objects.

The conversion script downsamples with a box filter, dithers into the classic
16-color ANSI palette, writes the preview PNG, and emits explicit RGB565 data.

`source/friendship-ansi-concept-imagegen-v1.png` is a second original reference
sheet generated with the built-in OpenAI image tool on 2026-08-23. It is a
2×2, kid-friendly EGA concept board for a moonlit town, Seth/Violet inn
friendship, an adventure-team pledge, and a nonviolent dragon celebration.
It contains no runtime text or copied upstream LORD art and is not decoded by
the cartridge.

Final reference-sheet prompt:

> Create one square 2-by-2 concept sheet for code-rendered 320×200 CP437/ANSI
> scenes: a moonlit medieval town, a welcoming inn where bard Seth and
> innkeeper Violet greet young adventurers as friends, two adventurers making
> a teamwork pledge around a gem, and a nonviolent red-dragon celebration.
> Use authentic late-1980s DOS EGA pixel art, hard edges, a 16-color palette,
> chunky CP437-inspired forms, no readable text, no romance, no kissing, no
> sexualization, no alcohol, no gambling, no gore, and no watermark.

The shipped inn strip, Seth/Violet friendship strips, Dragon Dice table,
recovery panel, and victory celebration were manually redrawn from that
composition reference using the shared pinned CP437 glyphs. This keeps the
runtime deterministic and small while preserving the reference provenance.

The twelve RIP-style location scenes are code-drawn with clipped
Game API rectangles, circles, lines, sprites, ANSI palette colors, and the
platform's pinned CP437 block/shade/symbol glyphs. They do not embed or execute
upstream `.ICN`, `.LRD`, RIPscrip, terminal commands, or file operations.
