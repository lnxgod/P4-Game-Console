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
