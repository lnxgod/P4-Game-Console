# Acceptance roadmap

Each milestone ends with repeatable evidence, not only a successful compile.

Current state: the hashed-identity-bound connected board passed the pin-independent M0 and bounded display-only M1 diagnostics on 2026-08-12. The custom display service now has exact readback, serial-cycle, and direct visual evidence for the EK79007 1024×600 path. The physical product label and PCB revision remain unconfirmed; only the cross-revision display resources are authorized and every other peripheral remains locked. Reproducing from a fresh clone remains a release gate after the initial repository commit.

## M0 — Recovery and toolchain

- Complete factory flash backup exists outside Git; its byte count, SHA-256, and hashed live-device binding are recorded without the raw identifier.
- SDK/component versions are pinned and a clean checkout can run `make setup`, `make verify`, and `make build`.
- Bring-up firmware reports the expected chip, revision, 16 MiB flash, and 32 MiB PSRAM.
- The first diagnostic write used `make flash-app`; a post-write readback confirmed the 64 KiB factory prefix remained byte-identical and the installed app matched its built binary.
- Factory restore command is documented but never run without explicit confirmation.

## D0 — Doom engine host proof (complete, host only)

- The locked doomgeneric commit/tree and all 205 vendored files pass provenance verification.
- The headless native adapter builds and presents eight frames using the ignored, hash-pinned Doom 1.9 shareware development WAD.
- `make check` runs that finite-frame smoke test when the verified local WAD exists and emits an explicit SKIP on clean clones without game data.
- D0 proves only source provenance, native compilation, and headless frame progress. It makes no ESP32-P4, display, SD, audio, USB, or gameplay claim.

## D0.5 — Doom engine ESP32-P4 link proof (complete, build only)

- The pinned ESP-IDF 5.5.3 toolchain cross-compiles the 80-source doomgeneric engine set for ESP32-P4 revision 1.0 through 1.99.
- Whole-archive verification matches all 80 selected sources to all 80 archive members and all 80 members present in the final linker map.
- The build-only seam uses no board pins or display, storage, audio, SD, USB, or WAD data. Repository flash commands reject the Doom app before hardware probing.
- Two separate build directories produced the same 475,776-byte image and ELF; exact hashes and the pinned-compiler warning inventory are recorded in `test-runs/2026-08-12-doom-d05-idf-build.json`.
- D0.5 proves only cross-compilation and link completeness. It was not flashed or executed and makes no boot, runtime, rendering, memory, peripheral, input, or gameplay claim.

## M1 — Board support

- Physical panel identity and PCB revision are recorded.
- **Display baseline complete:** custom firmware shows deterministic vertical, horizontal, and BER patterns with no gross corruption in the captured frame; moving-frame tearing remains a separate qualification.
- Touch test reports calibrated points across all corners.
- SD reads a known file repeatedly and survives removal/reinsert if the socket supports it.
- Audio plays channel-identifiable test tones without underruns.

## M2 — USB gamepad platform

- The next build slice ships the USB Host lifecycle and HID service together; no partial host component is reusable yet.
- The Host library starts with its root port unpowered, and a fixture-ready API is the only path that enables it after external power validation.
- Platform-owned class leases close the client-registration/shutdown race and let the daemon drain devices before uninstalling.
- HID callbacks copy bounded reports to a worker; disconnect uses a guaranteed control path and neutralizes the synchronized snapshot immediately.
- Powered host shim passes VBUS, backfeed, and overcurrent checks before a controller is attached.
- A generic HID pad enumerates, exposes its descriptor, and maps buttons/axes/hat to canonical state.
- Unplugging with every control held publishes neutral state within one game tick.
- Hotplug, rapid reconnect/address reuse, malformed fixtures, and a 100-cycle soak pass.
- Multiple pads and powered hubs are separate qualification cases, not assumed support.

## M3 — Doom on the ESP32-P4

- Legal Freedoom game data loads from SD; no commercial WAD is committed.
- Video is playable on the physical display with frame timing recorded.
- Move, turn, fire, use, pause, and menu work through the shared gamepad API.
- Keyboard fallback remains usable during bring-up.
- Audio mixes reliably, saves persist, and unplug/reconnect does not restart Doom.

## M4 — Badge platform

- A second small game uses the same video/audio/input/storage APIs without copying drivers.
- Clean-build and hardware-smoke procedures are captured as project skills.
- Resource budgets, crash recovery, update strategy, security boundaries, and production PCB USB power design are measured and documented.
