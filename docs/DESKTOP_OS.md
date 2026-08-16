# Console OS desktop services

Console OS keeps a fixed 768x480 landscape contract on PC and the Waveshare
4.3. Its Program Manager, File Manager, Game Manager, Save Manager, Terminal,
and control panels deliberately use code-rendered Windows 3.1-style chrome so
the UI has no bitmap-theme dependency.

## Current working slice

- File Manager receives a bounded OS-owned catalog, lists file type and binary
  size, and sorts by name, type, or size in either direction.
- Game Manager distinguishes firmware-linked built-ins from validated P4 Cart
  files. Built-ins are read-only and carts remain data until the sandboxed
  cartridge runtime exists.
- Save Manager owns a bounded eight-slot metadata model with total-size and
  write-capability state. No game receives a filesystem handle.
- Terminal has bounded input/scrollback, a touch QWERTY keyboard, a sanitized
  physical-key entry point, and local `HELP`, `STATUS`, `GAMES`, `FILES`,
  `CLEAR`, and `SSH` commands.
- Solitaire is an original native game under `GAMES/PUZZLE`.

The File and Save managers are read-only in the current Waveshare build. That
is intentional: the exact hardware authorization covers the no-format,
read-only SD path, while the existing H1 USB receiver has one separately
reviewed write target for the exact Quake shareware PAK. Disabled controls are
shown as `USB LOCKED` or `READ ONLY` rather than silently attempting a write.

## Storage namespace and transaction contract

The planned writable namespace is fixed and non-recursive at each management
boundary:

```text
/P4/GAMES/<validated-name>.P4CART
/P4/SAVES/<game-id>/<slot>.P4SAVE
/GAMES/QUAKE/ID1/PAK0.PAK
```

Every future mutation must be implemented by an OS service, not by shell or
game code. Import and save operations stage to a reserved non-runnable name,
flush, read back, validate length and SHA-256, then atomically rename. Copy,
replace, delete, and restore must have explicit quotas, path allowlists, and a
recoverable journal. Formatting is never an implicit recovery action.

## USB behavior

H1 is the programming USB-C port behind a CH343 USB-UART bridge. The existing
`scripts/p4-usb-content.py` transfer runs over that serial link; H1 cannot
appear as a normal Finder mass-storage volume through firmware.

H2 is sink/device wired in the reviewed schematic and is not authorized as a
VBUS source or keyboard host. A future drag/drop experience can use either a
desktop/Web Serial companion over H1 or an H2 USB-device protocol, but it must
not mount the live SD filesystem concurrently with Console OS. USB keyboard
host testing requires the documented powered, current-limited, backfeed-safe
fixture.

## SSH boundary

The terminal UI is transport-independent. A real SSH session requires the
ESP32-C6 Wi-Fi transport, DNS/TCP, a bounded SSH implementation, host-key
verification, credential handling, terminal resize rules, and disconnect
cleanup to be qualified as OS services. Games never receive sockets or keys.
Until that stack exists, `SSH` reports the transport as offline and cannot
downgrade to an insecure plaintext shell.
