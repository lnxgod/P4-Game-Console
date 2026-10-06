# Recorded Elecrow Console OS state (2026-08-14)

Read this file before touching the programming UART or interpreting the
recorded tablet result. This is the 2026-08-14 snapshot; inspect later exact-unit
install records before claiming what is currently installed. These facts are
history, not authorization for a different board or future artifact.

## Bound hardware

- Product: Elecrow CrowPanel Advanced 10.1-inch DHE04310D, called the **10 in
  variant** in user-facing text.
- Silicon: ESP32-P4 revision v1.3.
- Flash: 16,777,216 bytes.
- PSRAM: 33,554,432 bytes.
- Hashed device binding:
  `4ea0363808ba7d7788b89a352285396a44d1959fc9f20881c327110e7fbee9f0`.
- Never store or print the raw base identity.
- The serial node was `/dev/cu.wchusbserial10` on 2026-08-14; rediscover it
  after every reconnect instead of assuming the node is stable.

The complete factory backup is
`hardware/backups/elecrow-p4-factory-before-project.bin`, 16,777,216 bytes,
SHA-256
`3e3f5f687f59938963d9040d6c8e9dc1a687baa512790680830422c22bd6ab3c`.
Its binding is recorded in `hardware/backups/manifest.json`. A later complete
pre-E6 live backup is also preserved there with SHA-256
`c567d0a4d960156b59216e4c424d78856e7b8dbe52e05810d47a69a3e58f4217`.
Both binary files are local/ignored.

## Image installed in this record

The recorded installed Console OS was the badge-free folder launcher:

- sealed build path at install time:
  `apps/console_os/build-folder-clean/p4_console_os.bin`
- offset: `0x10000`
- artifact bytes: `4,951,552`
- artifact SHA-256:
  `636ee38221197c0c7b02c5d04ea19ddfac0a3c6dbd9b2719d8bb21dfb1658a7d`
- padded mutation span: `4,964,352` bytes
- installed/readback span SHA-256:
  `ad863f0b1b8377692a22168cad5601b3f3f49fa5b7ab126b12d4df98c83a8f96`

The authoritative record is
`hardware/test-runs/2026-08-14-console-os-folder-clean-install.json`.
The complete successor span read back byte-for-byte in ten chunks, the
partition table stayed unchanged, and no restore was required. Retained-UART
startup passed with seven apps, Game API v1, ordered display/touch markers,
three display submissions and completions, 600 touch polls, zero display/touch
failures, and the amplifier off.

The owner first reported “this is perfect! its running good” while viewing the
new launcher, then explicitly followed with “i did test it all,” “we are
good,” and “all games work.” This accepts the folder-style home screen,
`ALL PROGRAMS`, `GAMES`, and `SYSTEM` presentation, removal of developer
`V/T/A/S` capability letters, nested folder navigation and scrolling, both
native games' visuals/controls/tones/return behavior, Doom SFX and MUS music,
the built-in System pages, and restart-to-launcher on this exact installed
artifact.

Use the completed install record and its sealed recovery directory as the
authority for what remains on the tablet; never infer installed state only from
a mutable build output.

## Historical predecessor candidates

The flat seven-entry Space Invaders image is now the preserved predecessor.
Its artifact SHA-256 was
`9596617fd86d9cd9abd12a539329c6df2d06e696ca4de6814daa644e56090303`,
and its completed record is
`hardware/test-runs/2026-08-14-console-os-space-invaders-install.json`.
The current recovery directory contains its exact full 4,964,352-byte preimage
with SHA-256
`8a4c7be1bd6dab0fbc24d16217c8d44f18e6ec4aca3a3eb5b7bc3259b25b1ffe`.

The earlier folder binary with SHA-256
`861059e3c0c08a901aa2dc30dcde2f07fb43e21f54c0e599a10ce2a6c62a166f`
still contained kid-facing capability badges. It is stale and was never
installed. Its historical software record is
`test-runs/2026-08-14-console-os-folder-hierarchy-build.json`; never confuse it
with the installed badge-free successor.

## Executed route is historical

These files describe the completed badge-free folder installation:

- `scripts/console-os-folder-clean-authorized-route.py`
  - SHA-256
    `d5502231e4781fdff3b3bc41a855c8967cbd2af7940cd53ca2319265c43c9bce`
- `scripts/console-os-folder-clean-install.py`
  - SHA-256
    `5aa5dbc13beeb3ccdc15c81bfd1db561ab91082b0817ee78ac27b6369dff8956`
- `hardware/evidence/console-os-folder-clean-exact-unit-authorization.json`
  - SHA-256
    `af4fa60c211a7e94a48c50c5534582ed2a7d09ec64eb0d99d98219bae0cbfa58`
- `hardware/evidence/console-os-folder-clean-exact-unit-audio-release.json`
  - SHA-256
    `e5f8cf589518766320de4dc50f0de9e74f8e2591de36e43bd0c1c5a1e65e7c0e`

The recovery directory
`hardware/local-state/console-os-folder-clean-install-20260814`
contains the successful immutable ledger and predecessor preimage. Do not delete,
overwrite, or reuse it for another installation. A new build needs new dated
evidence, a new authorization digest, a new frozen outer route, and a unique
recovery directory.

## RDID container-byte evidence

Pinned esptool 4.12 requests 24 bits for command `0x9f` but returns the whole
32-bit W0 register without masking. The exact canonical manufacturer/type/
capacity identity is the low 24 bits `0x1840c8`.

Earlier guarded runs observed raw container values with high bytes `0xab`,
`0xfc`, and `0xff`. Source review and exact-unit evidence narrowly allow high
bytes `0x00`, `0xab`, `0xfc`, and `0xff` while still requiring low 24 bits
`0x1840c8` and rejecting every other value. The badge-free folder install used
`0xff1840c8` at both its write and prelaunch boundaries. See
`hardware/evidence/console-os-folder-clean-jedec-normalization.json`. Do not
generalize this exception to another unit, runtime, stub, or successor without
a new binding.

## Game and asset state

- Native format: `p4-native-static-v1`, statically linked RISC-V code in the
  Console OS ELF/BIN; not UF2.
- Native surface: 320x200 standard RGB565 scaled 3x into a centered 960x600
  viewport.
- Maze Chase is original clean-room code-rendered geometry. It contains no
  external sprite/image asset and no Pac-Man ROM, map, art, or sound.
- Space Invaders is original clean-room code-rendered geometry. It contains no
  arcade ROM, copied sprite/font, original map, artwork, or sound data.
- `p4/draw.h` supports bounded RGB565 sprites for future correctly licensed
  image assets.
- Native game tones and copied 1–256-frame PCM16-stereo blocks use the reviewed
  16 kHz factory path at backend volume step 6/10 through a 512-frame FIFO.
  The installed launcher does not energize either path.
- Doom retains its exclusive handoff with SFX plus WAD MUS software synthesis.
- The bound tablet's factory-compatible audio path is operator-released and
  known working; prior Doom SFX and recognizable MUS music were heard. Do not
  classify audio itself as blocked. The installed Space Invaders artifact has
  its own exact-unit release; any future changed artifact needs another one.

The local Doom input is `local-data/doom/doom1.wad`, 4,196,020 bytes, SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
It and every WAD-bearing firmware artifact must remain ignored and must not be
pushed.

## Current software-evidence caveat

The focused Game API, shell, registry, generated-game, locked ESP-IDF build,
and Console OS verifier passed for the installed artifact. As of 2026-08-14,
the broad historical `make check` also traverses sealed E5 and D0.5 exact-
artifact assertions that predate the current Doom/Console successor state.
Run it only when the parent skill's broader-test scope applies; report its
result without rewriting historical evidence to make that suite green. Distinguish a current candidate failure from a stale historical
binding.

## Latest guarded hardware acceptance

The badge-free folder install used its exact-unit route rather than generic
`idf.py flash`:

1. The initial shell invocation failed before UART open because its Python did
   not contain pinned esptool; it performed no device read or write.
2. The pinned retry captured the complete live 4,964,352-byte flat-launcher
   preimage before writing.
3. It wrote only the factory app partition exactly once and verified the
   complete padded successor span byte-for-byte in ten bounded chunks.
4. It revalidated the unchanged partition table, launched once, and retained
   the same exclusive UART for startup capture.
5. The seven-app launcher proved display completion, GT911 polling, and
   amplifier-off state with no rollback required; the owner manually accepted
   the complete launcher, folder, game, audio, return, and restart checklist.

The exact install record is
`hardware/test-runs/2026-08-14-console-os-folder-clean-install.json`. Its
automated and manual acceptance lists are complete for this exact artifact and
tablet. A changed artifact or different unit requires fresh qualification.

WADs, WAD-bearing firmware binaries, and local recovery images remain local
and must never be pushed to GitHub.
