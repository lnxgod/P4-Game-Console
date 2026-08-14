#!/bin/sh

set -eu

P4_PORT=
P4_OUTPUT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --output) P4_OUTPUT=${2:-}; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_port "$P4_PORT"
p4_activate_idf

if [ -z "$P4_OUTPUT" ]; then
    P4_TIMESTAMP=$(date -u '+%Y%m%dT%H%M%SZ')
    P4_OUTPUT="$P4_PROJECT_ROOT/hardware/backups/factory-$P4_TIMESTAMP.bin"
fi

case "$P4_OUTPUT" in
    /*) ;;
    *) P4_OUTPUT="$P4_PROJECT_ROOT/$P4_OUTPUT" ;;
esac

if [ -e "$P4_OUTPUT" ]; then
    printf 'Refusing to overwrite existing backup: %s\n' "$P4_OUTPUT" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$P4_OUTPUT")"
P4_PARTIAL_OUTPUT=$(mktemp "$(dirname -- "$P4_OUTPUT")/.factory-backup.XXXXXX")
p4_remove_partial_backup() {
    rm -f "$P4_PARTIAL_OUTPUT"
}
trap p4_remove_partial_backup EXIT HUP INT TERM
P4_FLASH_ID_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" flash_id 2>&1) || {
    printf '%s\n' "$P4_FLASH_ID_OUTPUT" | p4_redact_device_identifiers >&2
    printf 'Backup failed: live ESP32-P4 flash probe failed.\n' >&2
    exit 1
}
printf '%s\n' "$P4_FLASH_ID_OUTPUT" | p4_redact_device_identifiers
P4_DETECTED_FLASH_BYTES=$(printf '%s\n' "$P4_FLASH_ID_OUTPUT" | \
    p4_flash_size_bytes_from_esptool_output)

P4_DEVICE_IDENTITY_BEFORE=$(p4_read_device_identity_hash "$P4_PORT")
P4_READ_OUTPUT=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    read_flash --flash_size detect 0x0 ALL "$P4_PARTIAL_OUTPUT" 2>&1) || {
    printf '%s\n' "$P4_READ_OUTPUT" | p4_redact_device_identifiers >&2
    printf 'Backup failed while reading flash.\n' >&2
    exit 1
}
printf '%s\n' "$P4_READ_OUTPUT" | p4_redact_device_identifiers
P4_DEVICE_IDENTITY_AFTER=$(p4_read_device_identity_hash "$P4_PORT")
if [ "$P4_DEVICE_IDENTITY_BEFORE" != "$P4_DEVICE_IDENTITY_AFTER" ]; then
    printf 'Backup failed: device identity changed during capture; do not register this image.\n' >&2
    exit 1
fi
P4_BACKUP_HASH=$(p4_sha256_file "$P4_PARTIAL_OUTPUT")
P4_BACKUP_BYTES=$(wc -c < "$P4_PARTIAL_OUTPUT" | tr -d ' ')

if [ "$P4_BACKUP_BYTES" != "$P4_DETECTED_FLASH_BYTES" ]; then
    printf 'Backup failed: captured %s bytes, but the live probe reported %s bytes.\n' \
        "$P4_BACKUP_BYTES" "$P4_DETECTED_FLASH_BYTES" >&2
    exit 1
fi

chmod 600 "$P4_PARTIAL_OUTPUT"
mv "$P4_PARTIAL_OUTPUT" "$P4_OUTPUT"
trap - EXIT HUP INT TERM
printf 'Backup complete: %s\n' "$P4_OUTPUT"
printf 'Bytes: %s\nSHA-256: %s\n' "$P4_BACKUP_BYTES" "$P4_BACKUP_HASH"
printf 'Device identity SHA-256: %s\n' "$P4_DEVICE_IDENTITY_AFTER"
printf 'Record the file, byte count, backup hash, and identity hash in hardware/backups/manifest.json before flashing.\n'
