# Doom Arena checkpoint admission

Arena protocol 8 replaces replay from the beginning of a running match with an exact checkpoint and its canonical suffix. A new or returning guest still uses Multiplayer → Game Changers AI → Local Wi-Fi → Join. The host keeps its current match and does not press Start again. Initial game-data loading remains a separate cost.

The OS advertises protocol 8 explicitly. A protocol 7 room is incompatible with this new Arena launch descriptor. The adapter retains its protocol 7 path for regression coverage; ordinary Doom, Chex, native-game messages, and the existing GCE1 attempt envelope retain their contracts. This is an Arena game-protocol change, not a global P4MP version change.

## Admission and world ownership

A guest first completes authenticated admission for its reserved or newly assigned slot, durably stores its return ticket, and loads the accepted original content. It remains absent from the active lockstep mask. At its first safe engine boundary it requests a checkpoint. The host captures only between engine tics, after the engine has consumed any departure for every waiting guest slot. Session membership alone is insufficient for this decision.

The host performs one bounded capture and SHA-256 for a waiting group, then shares immutable bytes. Capture is synchronous game-thread work; it is not a background or zero-cost operation. `CHECKPOINT_CAPTURE ... boundary_max_us=...` measures the entire service boundary, including codec validation, encoding, and SHA-256. A busy level transition, pending thinker removal, unsupported thinker, or unsuitable membership defers capture and eventually rejects that admission rather than changing the running world.

The guest accepts only metadata and chunks matching its session, peer, route, nonce, content identity, schema, and checkpoint identity. It verifies the complete SHA-256 before destructive restore. The current schema restores gameplay state, typed object references and thinker order, spatial lists, player state, both RNG indices, consistency/input rings, Arena scores/votes/maps, animation translations, and sky selection. The supported engine scope is stable `GS_LEVEL`, no monsters, deathmatch 2, `ticdup=1`, no fast mode, and recognized thinker classes. Unknown states fail closed.

Restore runs only on a cold, preactivation guest. It never rewinds the host. It stops old audio, rebuilds the map, reconstructs the world, rebinds the HUD/status bar, and rebases the guest input queues to the checkpoint's next tic. The guest then consumes the canonical suffix without live input or presentation, acknowledges progress, and joins at a host-selected future activation tic. The host retires that guest's checkpoint lease after activation.

A failed restore or post-load checkpoint admission requests clean guest exit before another input command, engine tic, sound update, or rendered frame. The existing platform exit owner stops workers and restarts Home. Initial content-loading failures retain their existing failure path. There is no rollback to a partially restored world. Delayed receive callbacks reject retired attempts before touching freed buffers.

## Fixed resources and deadlines

| Resource | Bound |
| --- | --- |
| Snapshot | 512 KiB; a larger world cannot join through this codec |
| Host snapshot storage | Two immutable 512 KiB buffers, shared across at most three guests |
| Guest snapshot storage | One 512 KiB buffer, released after activation or exit |
| Codec scratch | Preallocated; approximately 40,984 bytes on the expected 32-bit ABI, requiring device confirmation |
| Host canonical suffix | 4,200 tics / 120 seconds / 168,000 bytes; rolling, never held hostage by a guest |
| Chunk payload | 896 bytes; four-chunk window per guest |
| Global checkpoint send budget | At most two packets per service pass, round robin, after control/live work |
| Capture wait | 1 second from a content-ready request |
| Transfer | 90 seconds total, 30 seconds without valid transfer progress |
| Snapshot/suffix lease | 110 seconds from capture, also retired before suffix eviction |
| Overall admitted attempt | Existing 300-second absolute bound, including content loading |

Snapshot buffers and scratch use PSRAM on ESP-IDF. There is no per-poll or per-capture allocation. The two host buffers allow a later admission while an earlier immutable checkpoint is retained; when both are busy, another request can fail its bounded capture wait. The host continues playing if a guest stalls or exhausts its lease. Match duration no longer consumes an ever-growing admission journal, although fixed integer and attempt-history limits still exist. Existing reserved-seat ticket and 64-attempt replay-protection limits remain unchanged.

Checkpoint data uses dedicated P4MP packet type 13. Generic session receive rejects that type; the checkpoint-specific path validates against a copy of session state and commits sequence/liveness changes only after valid checkpoint processing. Native game messages remain limited to 64 bytes. GCE1 runtime payload limits and nonce validation are unchanged.

## Verification and remaining device acceptance

Host proofs cover actual production C packet/session, rolling journal, transfer/service, lockstep rebase, and adapter code under ASan/UBSan. The full engine proof independently compares exact schema-2 bytes at 25 checkpoints, 33,927 canonical suffix states, and 1,082 selected rendered views under ASan, including moving/firing, departures, item respawn, and map votes. Separate first-local `D_Display` and enabled sound-module fixtures cover usable HUD rebinding and stopped old actor-origin channels. Music restarts the current track; sample-position continuity and exact cosmetic HUD history are not promised.

A deterministic 20 Hz service model with three maximum-size snapshots, lost metadata/chunks/ACKs, and shared send limits completed all transfers in 51.55 seconds; the single-guest case took 34.9 seconds. Those are synthetic worst-cadence test results, not Wi-Fi or device latency. Actual capture pause, restore duration, free/fragmented PSRAM, transport throughput, audio-worker cleanup, and physical first-frame/join acceptance must be recorded on both Tab5 units before release qualification. The real-engine proof and adapter proof are separate; they do not constitute a single physical network session.

Historical replay-from-start evidence remains in [Doom Arena rejoin](DOOM_ARENA_REJOIN.md). A successful host test, build, or flash is not physical gameplay acceptance.
