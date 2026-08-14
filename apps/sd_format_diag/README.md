# Destructive J5 microSD FAT32 formatter

This one-shot diagnostic deliberately erases and replaces the partitioning and
filesystem on the microSD card inserted in the Elecrow board's J5 socket. It is
limited to the cross-revision-proven SDMMC0 path: GPIO43 clock, GPIO44 command,
GPIO39 data 0, one-bit SDR, internal pull-ups, and a conservative 1 MHz clock.

The app initializes the exact SDMMC path, explicitly calls FatFs `f_fdisk` to
replace the card with one full-size partition, then calls `f_mkfs` with
`FM_FAT32`, two FATs, and a 16 KiB allocation unit. It mounts the result through
FatFs and requires the reported type to equal `FS_FAT32`. It then mounts through
ESP-IDF's supported VFS path and writes a deterministic 4096-byte
verification file, flushes and `fsync`s it, reads it back byte-for-byte, removes
it, unmounts, remounts with formatting disabled, repeats the verification, and
finally unmounts. The `COMPLETE` marker is unreachable unless every step passes.

This app mutates only the inserted microSD. It does not erase or repartition the
ESP32-P4's external SPI flash. Repository flash authorization remains false
until the exact reproducible image and central app-only gate are reviewed.
