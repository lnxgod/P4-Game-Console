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

The new C implementation adapts the class choices, core battle formulas,
eleven training masters and thresholds, sixteen weapons, sixteen armour
items, representative monsters from every upstream tier, daily forest limits,
bank/inn/healer services, and Red Dragon victory loop. The local realm,
mailbox, PvP transactions, romance model, built-in IGMs, save codec, generated
16-color title art, and code-drawn RIP-style scenes are new bounded
implementations for the P4 port.
UI, state management, input handling, rendering, and tests were written for
P4 Game API v1.

The cartridge does not vendor or load the upstream `lord.js`, `recorddefs.js`,
`lord.ini`, `lordtxt.lrd`, `.ICN` graphics, `.LRD` scripts, or database files.
Their Synchronet/BBS runtime dependencies are consequently not part of the
P4 package.

LORD, *Legend of the Red Dragon*, and related character or place names remain
the property of their respective owners. Keep this provenance note with the
port and preserve the project’s underlying permission record when
redistributing it.
