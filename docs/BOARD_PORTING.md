# Port Console OS to another ESP32-P4 board

Board ports are adapter work, not application rewrites. The window manager,
Program Manager, File Manager, Game Manager, native cartridge loader, bundled
games, Doom, and update service stay shared. A port supplies only the board
services below those APIs.

Start by checking the current feature contract and adapter inventory:

```sh
python3 scripts/board-port.py check
python3 scripts/board-port.py matrix
```

Create a source-pinned JSON spec containing the board identity, exact memory,
official repository commit and document hashes, interface facts, available
inputs, content-transfer mode, and the closest adapter template. The
`backend_compatibility` field must explicitly say whether schematic evidence
proves that every reused circuit is compatible. When it is true, bind the
claim to a repository evidence file and its SHA-256.

Then produce a deterministic plan and scaffold:

```sh
python3 scripts/board-port.py plan --spec /absolute/path/board-port.json
python3 scripts/board-port.py scaffold \
  --spec /absolute/path/board-port.json \
  --output-root /absolute/path/board-ports
```

The scaffold contains a normalized write-locked profile, adapter map, safe
starter defaults, and an exact completion checklist. It never overwrites an
existing port and never authorizes a flash. A compatible board reuses the
existing display, storage, input, audio, and dependency adapters; a different
electrical path is reported as `review-or-implement` and belongs behind the
same platform API.

The required parity contract is machine-readable at
`hardware/boards/console-os-port-contract.json`. Touch is the only UI feature
that may be omitted solely because the target has no touch hardware. Input may
instead be gamepad, keyboard, or mouse. Laptop content mutation must use either
the single-owner USB-device MSC workflow or powered-off removable media; the
script rejects an unrecognized concurrency model.

Before the first write to any new board, bind its hashed live-device identity
and review a target-specific flash authorization. Do not create or refresh a
firmware backup as part of flashing; backups run only when the user explicitly
requests that separate operation; backups are never required for flashing.
Recovery can rebuild old source. Preserve existing recovery artifacts and device,
security, partition and artifact checks. A build or inherited adapter
claim does not replace HDMI/panel, storage, audio, input, update/rollback, and
UI hardware acceptance.
