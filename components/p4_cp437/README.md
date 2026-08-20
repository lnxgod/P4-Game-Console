# P4 CP437 font

This allocation-free component owns the platform's single pinned 8×16 IBM PC
Code Page 437 bitmap. Both the native 80×30 ANSI/BBS renderer and the compact
Game API drawing helpers use these exact glyph bytes, so cartridges can match
the Console OS terminal without depending on shell or transport internals.

The font was distributed by
[libansilove](https://github.com/ansilove/libansilove) and pinned from commit
`e22973d569c97c1a521a5246e057d300e865bd9f` on 2026-08-18. libansilove is
Copyright © 2011–2026 Stefan Vogt, Brian Cassidy, and Frederic Cambus. The
font is redistributed under the two-clause BSD terms in
`LICENSE.libansilove`.
