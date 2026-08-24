# LORD upstream provenance

## Authorized reference

- Project: Synchronet JavaScript port of *Legend of the Red Dragon*
- Original game creator: Seth Able Robinson
- Current LORD rights holder identified by the upstream materials: Gameport
- Synchronet JavaScript port: Deuce and Synchronet contributors
- Repository: `https://gitlab.com/SynchronetBBS/sbbs.git`
- Commit: `25232df05a8ba27a4dd9fcd38b4012c43660fb50`
- Source path: `xtrn/lord/lord.js`
- Git blob: `e3f754eabbb99779bf4ee982bd43be6509bad973`
- Raw source URL:
  `https://gitlab.com/SynchronetBBS/sbbs/-/raw/25232df05a8ba27a4dd9fcd38b4012c43660fb50/xtrn/lord/lord.js`
- SHA-256:
  `a410a37e0b8e39a72cd8527fbbf37e48698cdeccdb7fa9f7ff296b25dfbec645`

The project owner confirmed permission in the P4 development session on
2026-08-18 to make this port from the linked source. The cartridge therefore
uses the manifest license label `LORD-PERMISSION` and source identifier
`LicenseRef-LORD-Permission`; it does not infer an open-source grant from the
absence of a per-file license header.

## What was ported

The C implementation adapts the complete standalone player loop: character
creation, all three skill trees, battle formulas, eleven training masters,
sixteen weapons, sixteen armour items, every one of the 131 `monster_stats`
records, the fifteen forest-event families, town services, the full Red Dragon
Inn interaction set, Red Dragon victory, directory, mail, PvP, social bonding,
adventure teams, and youth mentoring. The source game's mature social and
family mechanics were deliberately redesigned as kid-friendly friendship
and teamwork loops while retaining their progression role.

The bounded add-on loops were adapted from these pinned paths in the same
authorized tree:

- `xtrn/lord/aratime/aratime.js`
- `xtrn/lord/barak/barak.js`
- `xtrn/lord/grabbag/grabbag.js`
- `xtrn/lord/gravyard/gravyard.js`
- `xtrn/lord/oorphans/oorphans.js`
- `xtrn/lord/outhouse/outhouse.js`
- `xtrn/lord/pickle/pickle.js`

`tools/import_upstream_monsters.mjs` refuses any `lord.js` whose SHA-256 does
not match the value above, applies the documented kid-safe display-name and
description map without changing combat stats, and deterministically emits
`src/generated/lord_monsters.h`. UI, state management, persistent local-realm
models, explicit save format, 16-color title art, code-drawn scenes, input,
and tests are new P4 Game API v1 work.

The cartridge does not vendor or load the upstream JavaScript, `recorddefs`,
configuration, `.ICN`, `.LRD`, database, or Synchronet runtime files. The only
mechanically imported source data is the generated C monster table, recorded
with the exact hash above. Synchronet/BBS runtime dependencies are not part of
the P4 package.

LORD, *Legend of the Red Dragon*, and related character or place names remain
the property of their respective owners. Keep this provenance note with the
port and preserve the project’s underlying permission record when
redistributing it.
