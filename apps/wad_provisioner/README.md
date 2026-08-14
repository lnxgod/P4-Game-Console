# Local Doom shareware WAD provisioner

This is a development-only transport for the ignored local
`local-data/doom/doom1.wad`. The source tree and routine Doom firmware remain
WAD-free. The generated provisioner firmware contains the copyrighted WAD and
is **local, non-redistributable, and unsuitable for release**.

Configuration fails unless the local input is exactly 4,196,020 bytes with
SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
Its custom app partition matches the saved factory partition's 11 MiB capacity,
so ESP-IDF's image-size check rejects an oversized provisioning binary.

Runtime is fail-closed:

- mount with `format_if_mount_failed=false`;
- if `/sdcard/DOOM1.WAD` exists and matches, pass without writing;
- if it exists and differs, refuse;
- otherwise exclusively create `/sdcard/P4WAD.TMP`;
- write, `fsync`, close, and hash-read-back the entire temporary file;
- check the target is still absent, then rename the verified temp file;
- hash-read-back the final file.

It never formats, overwrites, unlinks, or otherwise deletes. A detected failure
before rename deliberately leaves `P4WAD.TMP` for manual inspection and the
next boot refuses to continue. The rename step assumes this one-shot app is the
only writer mounted on the card. The pinned FatFs implementation refuses the
rename when the destination exists, but its FAT directory update is not
power-fail atomic and its own source labels an interruption window that can
cross-link a volume. A power interruption in that window may leave the temp
name, target name, or a damaged filesystem. Use stable power for this one-shot
operation; the final full-file readback detects an unsuccessful result only if
execution continues and cannot make FAT transactional.

The reviewed local artifact is checked with:

```sh
python3 scripts/verify-wad-provisioner.py \
  apps/wad_provisioner/build build-only \
  local-data/doom/doom1.wad apps/wad_provisioner/partitions.csv
```

Do not use a full-project flash for this app. Only an independently reviewed
app-only flash at the saved factory app offset may be authorized.
