# P4 Cart container v1

Status: format frozen for host tooling; the Console OS loader is not yet
implemented. A file is a runnable P4 Cart only after it passes this format's
validation and the matching sandboxed Lua runtime exists.

P4 Cart is the open, removable game format for P4 Console OS. It is designed
for original, source-included arcade games that can be copied to an SD card,
opened on a PC, and remixed by a child or an AI assistant. It is not a ROM,
native ESP32 executable, UF2 image, or compatibility layer for another
fantasy console.

## Product rules

- Every v1 cart includes human-readable source and remixable license metadata.
- Executable content is UTF-8 Lua source only. Native code and Lua bytecode are
  forbidden.
- Console OS owns the display, audio device, input devices, storage, network,
  timing, and lifecycle. A cart receives only the bounded P4 Lua API.
- The same source project must preview on PC and run on the Waveshare 4.3 at
  the Console OS 768x480 landscape resolution with 60 fixed updates and at most 30 presented
  frames per second.
- A package hash identifies exact content. Title, author, or filename never
  establishes trust or identity.

## Source project

A source project is an ordinary directory containing `p4.json`. All files to
be packaged are explicitly listed in the manifest, so temporary files,
credentials, editor state, and unrelated assets cannot be included by an
accidental recursive archive.

```text
bounce-lab/
  p4.json
  main.lua
  README.md
  LICENSES/
    NOTICE.txt
```

The manifest schema is
`game-platform/schema/p4-cart-manifest-v1.schema.json`. In addition to that
machine-readable shape, these semantic rules are normative:

- `game.id` is a stable lowercase UUID. A remix gets a new UUID and records
  its parent under `remix.parent`.
- `game.version` is `MAJOR.MINOR.PATCH` with no build metadata.
- Runtime identity is exactly `p4-lua-5.4-v1`; `runtime.entry` names a listed
  `runtime-source` file.
- The screen, update, and presentation values are exactly 768, 480, 60, and
  30. They document the contract rather than negotiate hardware settings.
- `runtime.heap_bytes` is 64 KiB through 512 KiB. The OS may refuse a cart if
  that memory is unavailable; the cart cannot raise the platform maximum.
- `runtime.save_bytes` is 0 through 4 KiB. Saves are namespaced by game UUID
  and never expose paths.
- One through 64 payload files are allowed. Paths are unique printable ASCII,
  at most 63 bytes, use `/`, and contain no empty, `.` or `..` segments.
- Each payload file is at most 2 MiB and the complete packed cart is at most
  8 MiB.
- A `runtime-source` file ends in `.lua`, is valid UTF-8 text, contains no NUL,
  and does not begin with the Lua bytecode signature.
- A v1 cart includes `README.md`, its license notice, and at least one
  `runtime-source` file. `remix.source_included` is always true.
- Code and assets use SPDX identifiers from the schema's remixable-license
  allowlists. `NC`, `ND`, proprietary, unknown, or license-reference-only
  terms are not accepted by the v1 packer.

Passing validation proves format and declared-license compliance only. It does
not prove that the submitter owns the code, art, names, or music. Built-in and
featured carts require a human provenance review.

## Packed bytes

All integers are little endian. All padding and reserved bytes are zero. The
container is deterministic: the manifest uses UTF-8 JSON with sorted keys,
compact separators, and one trailing newline; entries are sorted by path; no
timestamp is stored; and payload starts are aligned to four bytes.

The 128-byte header is:

```text
offset  size  field
0       8     magic = "P4CART1\0"
8       2     header_size = 128
10      2     format_version = 1
12      4     flags = 0
16      4     total_size
20      4     manifest_offset = 128
24      4     manifest_size, 1..16384
28      2     entry_count, 1..64
30      2     entry_size = 112
32      4     payload_offset
36      32    content_sha256
68      60    reserved = 0
```

`content_sha256` is SHA-256 over all `total_size` bytes with header bytes
36..67 replaced by zero. This supports direct SD-card validation. The transfer
protocol additionally hashes bytes read back from staging before activation.

The entry table begins at the first four-byte boundary after the manifest.
Every 112-byte entry is:

```text
offset  size  field
0       64    NUL-terminated canonical path, remaining bytes zero
64      1     kind
65      1     flags = 0
66      2     reserved = 0
68      4     payload offset
72      4     payload length
76      32    payload SHA-256
108     4     reserved = 0
```

Kinds are `1 runtime-source`, `2 runtime-asset`, `3 source-asset`,
`4 documentation`, and `5 license`. Entry ranges are inside the payload,
four-byte aligned, non-overlapping, and ordered exactly as their paths. Bytes
between ranges are zero. Duplicate paths and unknown kinds are rejected.

## Storage and copying

The removable layout is:

```text
/P4/GAMES/<any-name>.p4cart
/P4/SAVES/<game-uuid>.p4save       # OS-owned; never visible as a path to Lua
/P4/INBOX/                         # optional staging, never directly launched
```

Console OS scans only `/P4/GAMES`, does not recurse, and bounds the directory
to 128 candidates. It validates the complete header, manifest, table, overall
hash, and every referenced payload before adding a cart to the Library. A bad
cart gets a readable error tile and cannot run.

Serial or Wi-Fi installs write a new file under `INBOX`, sync it, validate it,
and atomically rename it into `GAMES`. Removing or replacing a cart never
deletes its save automatically. Direct SD copies are discovered on the next
Library refresh or boot.

Host tooling supports:

```sh
python3 game-platform/scripts/p4cart.py validate <source-directory>
python3 game-platform/scripts/p4cart.py pack <source-directory> <game.p4cart>
python3 game-platform/scripts/p4cart.py inspect <game.p4cart>
python3 game-platform/scripts/p4cart.py unpack <game.p4cart> <new-directory>
```

Unpack revalidates the complete object before writing and refuses an existing
destination. That round trip is the basic remix workflow.

## Explicit non-goals for v1

- no native RISC-V, ELF, shared libraries, UF2, WASI, or arbitrary bytecode;
- no compression, encryption, DRM, code signing, online account, or store;
- no direct filesystem, socket, USB, GPIO, display, codec, or ESP-IDF access;
- no arbitrary package dependencies or runtime downloads;
- no promise that ROMs or carts from other consoles are legal or compatible.

Quake and Doom remain separately reviewed native legacy engines. Their game
data is not P4 Cart content and is never bundled into kid-created carts.
