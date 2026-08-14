# Storage diagnostic

This app mounts the authorized one-bit SDMMC path at `/sdcard` with
`format_if_mount_failed=false`. It does not create, truncate, rename, delete,
or format anything. It reports card geometry and checks these paths in order:

1. `/sdcard/doom1.wad`
2. `/sdcard/doom/doom1.wad`

An existing candidate must be the exact Doom 1.9 shareware IWAD: 4,196,020
bytes with SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
An invalid first candidate fails closed.

The service exposes only read and inspect operations. ESP-IDF's FAT VFS is
not a hardware-enforced read-only mount, so code outside the service could
still write if it deliberately opened a writable file.

The reproducible build is recorded in
`test-runs/2026-08-12-storage-d1-build.json`.

The exact app-only image was tested on the connected board. Both attempts
timed out on the first card-sector read before the filesystem could be
classified. No SD bytes were written. Do not interpret this as permission to
format: run the planned lower-clock read-only probe first. Runtime evidence is
recorded in `hardware/test-runs/2026-08-12-storage-d1.json`; the diagnostic's
flash authorization has been revoked after the test.

The reviewed artifact can still be verified locally in build-only mode:
all flash flags are false until a separate review explicitly authorizes an
application-only diagnostic run. The exact source and artifact gate is:

```sh
python3 scripts/verify-storage-diag.py apps/storage_diag/build build-only
```

The central flash script invokes the same verifier in `app-flash` mode. That
mode rejects full-project flashing and remains closed until the app-only flag
is explicitly authorized.
