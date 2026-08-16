# Solitaire

An original clean-room Klondike-style card game for P4 Game API v1. It uses
only code-rendered cards and shared controls/audio services; there are no
borrowed sprites, card images, or proprietary assets.

- D-pad: move the yellow cursor; Up selects deeper face-up runs.
- A: draw, select, or drop cards.
- B: quick-move a top card to its foundation, or cancel a selection.
- Start: deal a new game.
- Back: return to Console OS.

The native-static game renders on the 320x200 compatibility surface, which
Console OS scales into its fixed 768x480 landscape viewport.
