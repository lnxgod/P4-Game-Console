# Color Clash

Color Clash is an original shedding-card game for two to four players, with a
classic balanced deck and original branding. Match the discard by color or
symbol, or play a Wild. Skip, Reverse, Draw Two, Wild Draw Four, and the unique
Gamechanger card change the round. The first player to empty their hand wins.

Each human multiplayer participant uses a separate P4 console. Console OS
owns the Host/Join room and selected transport, then launches the cartridge
with an already-sanitized P4MP session. The host validates compact player
intents and broadcasts revisioned, target-tagged messages no larger than 64
bytes. Public
round state uses a compact 27-byte snapshot; private hands use ordered chunks
of at most 54 cards, supporting the full deck without unbounded packets. Each
client applies only messages addressed to its player slot, so its screen shows
its own hand and public opponent card counts. With the current two-console
transport, the authoritative host fills seats three and four with computer
players, producing a four-seat match with two humans and two CPUs. Future
three- or four-console transports automatically leave fewer or no CPU seats.
Only the host advances CPU turns and broadcasts their resulting state.

There is deliberately no pass-and-play mode. A normal launcher start provides
offline practice against one to three deterministic bots, preserving a useful
fallback without exposing alternating human hands on one screen.

## Rules

- The 109-card deck uses purple, gold, red, and Tiffany blue. Each color has
  one 0, two each of 1-9, and two each of Skip, Reverse, and Draw Two. The deck
  also has four Choose Color Wilds, four Wild Draw Fours, and one Gamechanger.
- Match the active color or the top card's symbol. Choose Color Wild is always
  legal.
- Draw Two makes the next player draw two and lose their turn.
- Wild Draw Four lets its player choose red, gold, Tiffany blue, or purple;
  the next player draws four and loses their turn. It is legal only when the
  player has no colored card matching the current active color.
- Reverse changes direction; in a two-player match it acts like Skip.
- If the drawn card is playable, it becomes selected and the player may play
  it or pass. An unplayable drawn card ends the turn. Actions cannot stack.
- Playing down to one card opens an UNO call window. The player may call UNO
  for themself; any other player may call them first, making the caught player
  draw two cards. The window closes when the next normal turn action succeeds.
- The color chooser labels all four choices: RED, GOLD, TIFF, and PURP.
- Color Aid defaults to Symbols, assigning a circle to red, diamond to gold,
  triangle to Tiffany blue, and square to purple. High Contrast adds a dark
  badge and the letters R/G/T/P to every colored card. The same marks appear
  on Wilds, the deck, the active-color status, and the color chooser.
- Playing Gamechanger opens a player chooser. Every hand rotates by the same
  offset so the player who used it receives the chosen player's former hand.
  If that received hand has one or two cards, only the Gamechanger player draws
  up to exactly three. Gamechanger cannot be played as a final card.

## Controls

- On the practice menu, **Left/Right** changes the player count and
  **Up/Down** selects Standard, Symbols, or High Contrast Color Aid. The
  setting stays active when restarting or returning to the menu. Touch users
  can tap the left or right half of the Color Aid row.
- During practice or multiplayer, **Up/Down** can change Color Aid locally at
  any time without affecting the match or another console. Touch users can
  tap the active-color indicator at the top of the table to cycle modes.
- **Left/Right** selects a card and scrolls long hands; **A** plays it.
- Cards that are currently legal to play stay grouped at the left of the hand
  in their original relative order and sit slightly higher than the remaining
  cards. Selection, taps, and scrolling follow that visual order without
  changing the authoritative hand order used by multiplayer.
- **B** or **Start** draws one card. If that card is playable, **A** plays it
  and **B/Start** passes.
- With exactly two cards, **Start** plays the selected legal card and calls
  UNO atomically. The active **UNO+PLAY** touch button does the same thing, so
  multiplayer opponents and bots cannot catch the player between those two
  actions.
- While an UNO call is open, **Start** takes priority over draw/pass: it calls
  UNO for you when you have just played down to one card, or calls out the
  opponent who currently has an unclaimed UNO.
- In the Wild chooser, **Left/Right** selects a color and **A** confirms it.
- **Back** returns from practice to its menu; in network play it exits to the
  launcher and ends the session.
- Swipe the hand left or right to reveal off-screen cards. A swipe never plays
  a card; a stationary tap selects a card, and tapping it again plays it.
- Touch also operates Draw, UNO, and color buttons and uses the persistent
  upper-left Exit control.

The runtime combines original RGB565 drawing primitives, shared font glyphs,
and the reviewed ImageGen frame atlas; it has no copied commercial card art,
radio driver, socket, or raw hardware access.

When Console OS offers the optional `video-highres` capability,
Color Clash renders directly at 768x480 with a native card atlas and scaled
code-drawn UI. Older consoles keep the original 320x200 surface and atlas.
Touch coordinates remain in the Game API's normalized 320x200 input space, so
the same card, swipe, Draw, UNO, and Exit hit regions work in either video
mode.

## Card art and logo provenance

`assets/color_clash_card_frames_imagegen_v3.png` is the selected OpenAI
ImageGen source atlas. It was generated as a strict 3x2 sheet: red, gold, and
Tiffany-blue frames on row one; purple, Wild, and Gamechanger frames on row
two. The selected source is 1024x1536 RGB with SHA-256
`237823be2c22c401a56fe49254ced442ad32788c95b605b63247c2a82ade7ab5`.
The v3 edit preserved five cells and replaced every green ornament in the
bottom-center Wild frame with royal purple, leaving an exact red, gold,
Tiffany-blue, and purple palette. The prompt required blank centers, crop-safe
silhouettes, hard 16-bit pixel edges, no text, symbols, logos, watermarks, or
cell bleed. The earlier v2 source remains beside it as provenance.

The Gamechanger card composites the existing authorized official Game
Changers AI mark from
`apps/console_os/main/assets/gamechangers_ai_logo.rgb565`. That 112x112 RGB565
asset was retrieved from
`https://www.gamechangersai.org/assets/gamechangers-128.png` at the console
owner's request; its repository-pinned SHA-256 is
`48ee7b2a15a744547884ec6ea7f462277ab60e5dde4d0805bf39db9c0b2bd892`.
ImageGen never redraws the logo.

Regenerate both runtime atlases and their QA previews with Pillow:

```sh
python3 games/color_clash/tools/png_to_card_frames.py \
  games/color_clash/assets/color_clash_card_frames_imagegen_v3.png \
  apps/console_os/main/assets/gamechangers_ai_logo.rgb565 \
  games/color_clash/src/generated/color_clash_card_frames.inc \
  games/color_clash/assets/color_clash_card_frames_runtime_preview_v3.png \
  --high-res-preview \
  games/color_clash/assets/color_clash_card_frames_high_res_preview_v3.png
```

The legacy 28x42 and 42x62 sprites use nearest-neighbor reduction to preserve
their established pixel-art appearance. The high-resolution 67x101 and
101x149 sprites use high-quality downsampling plus a light sharpening pass,
which preserves the ornate frame detail instead of enlarging the 320x200
sprites at runtime.

## Host verification

```sh
cmake -S games/color_clash -B build-host/color_clash -G Ninja
cmake --build build-host/color_clash
ctest --test-dir build-host/color_clash --output-on-failure

cmake -S tools/p4-game-host -B build-host/play-color_clash -G Ninja \
  -DP4_GAME=color_clash
cmake --build build-host/play-color_clash
ctest --test-dir build-host/play-color_clash --output-on-failure
```
