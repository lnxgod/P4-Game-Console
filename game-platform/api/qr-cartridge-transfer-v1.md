# P4 QR cartridge transfer v1

Status: host encoder, estimator, validator, and reassembler implemented;
graphical QR rendering and Console OS receive UI pending.

P4 QR transfer moves one exact, already-valid `.p4cart` through one or more QR
symbols. It is a transport wrapper, not a reduced game format. A cart keeps its
complete source, assets, license, remix lineage, saves declaration, player
count, and runtime capabilities regardless of how many QR symbols carry it.

The sender validates the P4 Cart, compresses its exact bytes, and divides the
envelope into independently checked text frames. The receiver accepts frames
in any order, ignores identical duplicates, reconstructs the envelope only
when every frame is present, verifies both SHA-256 digests, decompresses to a
bounded buffer, validates the reconstructed P4 Cart, and only then stages it
for installation.

## Product behavior

Every export view must show the transfer cost before generating symbols:

```text
QR Dodge 1.0.0
5,412 cartridge bytes
1,826 compressed transfer bytes
3 QR codes (balanced)
```

One QR and multiple QR sets use the same protocol. Games are never rejected
merely for needing multiple symbols. Console OS may cap one interactive scan
session at 255 symbols; larger carts remain installable through USB, SD, or the
BBS content service. That is a transport usability limit, not a game-runtime
feature limit.

The recommended profiles are:

| Profile | Binary bytes per frame | Intended use |
|---|---:|---|
| `easy` | 384 | easiest scanning and printed cards |
| `balanced` | 640 | normal screen-to-camera exchange |
| `dense` | 900 | fewer symbols with a good camera |

These profiles size the protocol payload. An eventual renderer still chooses
the QR version, physical module size, and error-correction level appropriate
for the display or printer. It must reject a payload that its chosen QR
settings cannot encode instead of silently lowering integrity.

## Compressed envelope

All integers are little endian. The fixed header is 96 bytes:

```text
offset  size  field
0       8     magic = "P4QSET1\0"
8       2     header_size = 96
10      2     version = 1
12      4     flags = 1 (zlib)
16      4     uncompressed P4 Cart bytes, 1..8 MiB
20      4     compressed payload bytes
24      32    SHA-256 of exact uncompressed P4 Cart
56      32    SHA-256 of compressed payload
88      8     reserved = 0
96      ...   zlib payload
```

Decompression is bounded by the declared uncompressed size and the P4 Cart
8 MiB ceiling. Trailing compressed data, output beyond the declared size,
truncated streams, and either digest mismatch are fatal.

The transfer object ID is the uppercase first 16 hexadecimal characters of
SHA-256 over the complete envelope. It groups scans; it is not a trust or
publisher identity. The full P4 Cart content hash remains authoritative.

## QR frame text

Each QR contains one ASCII line:

```text
P4QR1:<OBJECT-ID>:<INDEX>/<TOTAL>:<BYTE-LENGTH>:<CRC32>:<BASE45>
```

- `OBJECT-ID` is exactly 16 uppercase hexadecimal characters.
- `INDEX` is one based and `TOTAL` is 1 through 255.
- `BYTE-LENGTH` is 1 through 1,200. Configured non-final chunks are 128
  through 1,200 bytes; the final frame may be shorter.
- `CRC32` is eight uppercase hexadecimal characters using CRC-32/ISO-HDLC.
- `BASE45` uses the RFC 9285 alphabet so the payload stays compatible with
  QR alphanumeric mode.

The receiver treats all scanned text as untrusted. It bounds fields before
allocating, checks decoded length and CRC before storing a chunk, and rejects
frames whose object ID or total differs from the active transfer. An identical
duplicate is harmless. A conflicting duplicate aborts the set.

Fixed `N of N` collection is the v1 rule. Fountain or parity frames may be
introduced under a new frame prefix later; they are not implied by v1.

## Host commands

```sh
python3 game-platform/scripts/p4qr.py estimate GAME.P4CART
python3 game-platform/scripts/p4qr.py split GAME.P4CART /tmp/game-qr
python3 game-platform/scripts/p4qr.py inspect /tmp/game-qr
python3 game-platform/scripts/p4qr.py join /tmp/game-qr /tmp/GAME.P4CART
```

`estimate --json` is intended for editor, website, and Console OS companion
UIs. `split` writes one `.p4qr` text payload per symbol plus `transfer.json`.
The current tool deliberately does not select or render a QR image; that layer
must make its display/error-correction decision explicitly.

## Installation and trust

Receiving all symbols does not install a game. The eventual Console OS app
must show title, author, license, version, size, capabilities, remix parent,
and content hash from the revalidated P4 Cart. Acceptance stages the complete
cart and uses the same atomic activation boundary as USB/BBS transfers.

QR error correction and per-frame CRC protect transport reliability. They do
not establish authorship. Optional publisher signatures can be added around a
content hash later without changing P4 Cart execution or granting scripts raw
hardware access.
