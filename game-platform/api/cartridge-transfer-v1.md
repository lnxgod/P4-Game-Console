# P4 cartridge transfer protocol v1

This protocol transfers one opaque `.p4cart` bundle. Web Serial is only one
transport adapter; framing, validation, staging, and atomic activation remain
independent from browser APIs and USB drivers.

The transferred bytes use `cartridge-container-v1.md`. `stage_verify` remains
an integration callback: the storage owner must validate that complete format,
its overall hash, every entry hash, and the manifest before COMMIT. Do not label
an arbitrary archive or native executable `.p4cart`.

## Framing

Frames are COBS encoded and terminated by `0x00`. All integers are little
endian. The decoded frame is:

```text
magic[2]       "P4"
version u8     1
type u8
flags u16      0
payload_len u16
session_id u32
sequence u32
payload[payload_len]
crc32 u32
```

CRC-32/ISO-HDLC uses reflected polynomial `0xedb88320`, initial value
`0xffffffff`, and final XOR `0xffffffff`. It covers the decoded header and
payload, not the CRC field. The check value for `123456789` is `0xcbf43926`.

Limits are 1,024 data bytes per chunk, 1,028 payload bytes, 1,048 decoded bytes,
and 1,054 encoded bytes including the delimiter. The absolute object limit is
16 MiB; a storage backend may advertise less.

Malformed frames and CRC failures are silently dropped. If an encoded frame
overflows its fixed receive buffer, bytes are discarded through the next zero
delimiter so parsing can resynchronize.

## Requests and responses

```text
0x01 HELLO    empty; random nonzero session, sequence 0
0x02 BEGIN    kind u8, options u8, reserved u16, total_size u32, sha256[32]
0x03 DATA     offset u32, data[1..1024]
0x04 END      empty
0x05 COMMIT   empty
0x06 ABORT    empty

0x81 CAPS     max_chunk u16, reserved u16, max_object u32,
         inactivity_ms u32, feature_bits u32
0x82 ACK      request_type u8, state u8, status u16, next_offset u32
0x83 NACK     request_type u8, state u8, status u16, next_offset u32
```

`kind = 1` identifies a v1 `.p4cart`. All options and reserved fields are zero.
The browser keeps one request outstanding and increments sequence for every new
request. A retry repeats the identical sequence and bytes. The device caches
the most recent response: an identical duplicate replays that response without
calling storage again, while reuse of the sequence with different content is
rejected.

State numbers are `UNBOUND=0`, `READY=1`, `RECEIVING=2`, and `VERIFIED=3`.
Status numbers are `NONE=0`, `PROTOCOL=1`, `SESSION=2`, `SEQUENCE=3`,
`SEQUENCE_REUSE=4`, `STATE=5`, `LENGTH=6`, `LIMIT=7`, `OFFSET=8`,
`STORAGE=9`, `VERIFY=10`, and `UNSUPPORTED=11`.

A syntactically valid request with the expected sequence always receives a
cached ACK or NACK and consumes that sequence, including validation and backend
errors. A wrong-session or unexpected/reused sequence NACK is transient: it is
not cached and does not consume the expected sequence. Malformed/CRC-invalid
frames receive no response.

## State and durability

```text
UNBOUND --HELLO--> READY --BEGIN--> RECEIVING
RECEIVING --END + read-back SHA/container validation--> VERIFIED
VERIFIED --COMMIT durable activation--> READY
```

DATA is accepted only at exactly `next_offset`. `END` succeeds only when every
byte is staged and a SHA-256 computed by reading staging storage matches BEGIN.
Only COMMIT may change the active cartridge pointer, and it is acknowledged only
after activation metadata is durable. A write or verification failure
invalidates staging while preserving the active cartridge.

A new valid HELLO, ABORT, disconnect, or the default 30-second inactivity timeout
aborts uncommitted staging. Version 1 deliberately has no cross-reset resume.
The eventual storage adapter should use two cartridge slots and redundant,
generation-tagged activation records so power loss selects either the complete
old cartridge or the complete new cartridge.

The serial channel must be byte-clean. ESP logging may not be interleaved with
protocol bytes; a hardware integration must route logs elsewhere or use a
dedicated USB CDC interface.

The receiver has one serialized owner. `feed`, `tick`, and `disconnect` may not
run concurrently. Backend callbacks are synchronous, and byte/hash pointers
passed to them are valid only until the callback returns. `stage_verify` must
hash bytes read back from staging and validate the container; `stage_commit`
may return success only after the activation record is durable.
