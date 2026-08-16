# Reproducible workflow

## Environment

`scripts/lib/project-env.sh` resolves the pinned ESP-IDF checkout from an explicit `P4_IDF_PATH`, the repository-local tool directory, the versioned standard user checkout, or a matching ambient `IDF_PATH`, in that order. `scripts/verify-env.sh` rejects an incompatible target, version, Git commit, dirty tracked checkout, or incomplete submodule set.

Install and verify the pinned environment only when needed:

```sh
make setup
make verify
```

`setup` is needed only when the pinned toolchain is absent. After that, run the
smallest existing `*-host` target that covers the changed component and build
only the affected app with `make build APP=<app>`. Documentation and skill-only
changes need no firmware build. Run repo-wide `make check` only for an explicit
request, a deliberate toolchain or dependency-lock migration, or a genuinely
cross-cutting change spanning maintained applications. Do not repeat an
unchanged build, flash, or hardware run.

Do not silently upgrade ESP-IDF or managed components to make a build pass.
Change the lock deliberately, explain the migration, regenerate dependency
locks, and rebuild every maintained app.

## Application structure

- `apps/bringup`: pin-independent chip, flash, PSRAM, heap, and console proof.
- Later peripheral diagnostics: one narrow image per subsystem.
- `apps/doom`: end-to-end game-platform acceptance case, not the owner of board services.
- `components/`: reusable platform APIs consumed by diagnostics and games.

Prefer small diagnostic images because they isolate wiring, clocks, memory, and timing failures. Promote proven code into a component, then have Doom consume that same API.

The Doom D0.5 image is deliberately build-only. Cross-compile it and verify that
every selected engine object reached the final ESP32-P4 linker map with:

```sh
make doom-idf
```

This target verifies pinned doomgeneric provenance, builds under the locked IDF,
and checks the recorded artifact evidence. It does not run the engine or embed a
WAD. Do not bypass `apps/doom/app-metadata.json`: both repository flash modes
remain unauthorized until the on-device D1 storage, video, and memory gates are
implemented and separately reviewed.

## Evidence record

For a hardware run, record:

- date and operator;
- board size, SKU, PCB revision, and chip revision; when a bounded cross-revision authorization is used, record the unresolved revision plus every official revision compared;
- hashed device identity, never the raw base identity;
- Git state and app name;
- ESP-IDF and component lock versions;
- serial port and power/fixture arrangement;
- exact write/readback offsets, byte counts, hashes, acceptance markers, and pass/fail result.

Never record the raw base identity; the stored SHA-256 binding is sufficient for board matching.
