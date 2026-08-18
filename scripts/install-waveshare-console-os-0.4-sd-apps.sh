#!/bin/sh

set -eu

P4_PORT=
P4_WRITE_ENABLED=true
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --verify-only) P4_WRITE_ENABLED=false; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_port "$P4_PORT"
p4_activate_idf

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-0.4-sd-apps-20260816-exact-unit-authorization.json"
P4_AUTH_EXPECTED=9de8ba02eae5712fc8ac6d9f6f57c949e2ae660294540d09b2afcb5b96f86454
P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing to install: Console OS 0.4 authorization changed.\n' >&2
    exit 1
}

p4_json_field() {
    python3 -c '
import json, sys
value = json.load(open(sys.argv[1]))
for key in sys.argv[2].split("."):
    value = value[key]
print(str(value).lower() if isinstance(value, bool) else value)
' "$1" "$2"
}

p4_check_file() {
    P4_FILE=$1
    P4_BYTES=$2
    P4_HASH=$3
    P4_LABEL=$4
    [ -f "$P4_FILE" ] || {
        printf 'Refusing to install: %s is missing.\n' "$P4_LABEL" >&2
        exit 1
    }
    [ "$(wc -c < "$P4_FILE" | tr -d ' ')" = "$P4_BYTES" ] || {
        printf 'Refusing to install: %s byte count changed.\n' "$P4_LABEL" >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_FILE")" = "$P4_HASH" ] || {
        printf 'Refusing to install: %s digest changed.\n' "$P4_LABEL" >&2
        exit 1
    }
}

P4_SOURCE_COMMIT=$(p4_json_field "$P4_AUTH" candidate.source_commit)
[ "$(git -C "$P4_PROJECT_ROOT" rev-parse HEAD)" = "$P4_SOURCE_COMMIT" ] || {
    printf 'Refusing to install: checkout is not the authorized commit.\n' >&2
    exit 1
}
git -C "$P4_PROJECT_ROOT" diff --quiet HEAD -- || {
    printf 'Refusing to install: tracked source differs from the commit.\n' >&2
    exit 1
}

P4_FACTORY="$P4_PROJECT_ROOT/$(p4_json_field "$P4_AUTH" factory_backup.path)"
p4_check_file "$P4_FACTORY" \
    "$(p4_json_field "$P4_AUTH" factory_backup.bytes)" \
    "$(p4_json_field "$P4_AUTH" factory_backup.sha256)" \
    factory-backup

P4_BOOTLOADER="$P4_BUILD/bootloader/bootloader.bin"
P4_PARTITIONS="$P4_BUILD/partition_table/partition-table.bin"
P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
P4_ELF="$P4_BUILD/p4_console_os.elf"
p4_check_file "$P4_BOOTLOADER" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.sha256)" \
    bootloader
p4_check_file "$P4_PARTITIONS" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.sha256)" \
    partition-table
p4_check_file "$P4_APPLICATION" \
    "$(p4_json_field "$P4_AUTH" candidate.application.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" \
    application
p4_check_file "$P4_ELF" \
    "$(p4_json_field "$P4_AUTH" candidate.elf.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.elf.sha256)" \
    application-elf
python3 "$P4_PROJECT_ROOT/scripts/verify-console-os-waveshare.py" "$P4_BUILD"

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = \
    "$(p4_json_field "$P4_AUTH" board.flash_bytes)" ] || {
    printf 'Refusing to install: live flash geometry differs.\n' >&2
    exit 1
}
[ "$(p4_read_device_identity_hash "$P4_PORT" no_reset)" = \
    "$(p4_json_field "$P4_AUTH" board.device_identity_sha256)" ] || {
    printf 'Refusing to install: live unit differs.\n' >&2
    exit 1
}

p4_verify_live_file() {
    P4_OFFSET=$1
    P4_FILE=$2
    P4_LABEL=$3
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        --after no_reset verify_flash "$P4_OFFSET" "$P4_FILE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -25 >&2
        printf 'Refusing to install: live %s differs.\n' "$P4_LABEL" >&2
        exit 1
    }
    printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -8
}

p4_verify_live_file 0x2000 "$P4_BOOTLOADER" bootloader
p4_verify_live_file 0x8000 "$P4_PARTITIONS" partition-table

P4_PREDECESSOR_TEMP=$(mktemp \
    "$P4_PROJECT_ROOT/hardware/backups/.waveshare-console-os-0.4-predecessor.XXXXXX")
p4_cleanup_predecessor() {
    rm -f "$P4_PREDECESSOR_TEMP"
}
trap p4_cleanup_predecessor EXIT HUP INT TERM
P4_PREDECESSOR_BYTES=$(p4_json_field "$P4_AUTH" predecessor.bytes)
P4_PREDECESSOR_HASH=$(p4_json_field "$P4_AUTH" predecessor.sha256)
P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after no_reset read_flash 0x20000 "$P4_PREDECESSOR_BYTES" \
    "$P4_PREDECESSOR_TEMP" 2>&1) || {
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -25 >&2
    exit 1
}
[ "$(p4_sha256_file "$P4_PREDECESSOR_TEMP")" = "$P4_PREDECESSOR_HASH" ] || {
    printf 'Refusing to install: live predecessor application differs.\n' >&2
    exit 1
}
printf 'Installed predecessor digest PASS sha256=%s bytes=%s\n' \
    "$P4_PREDECESSOR_HASH" "$P4_PREDECESSOR_BYTES"

if [ "$P4_WRITE_ENABLED" = false ]; then
    printf 'Waveshare Console OS 0.4 successor preflight PASS; no write occurred.\n'
    exit 0
fi

P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after no_reset write_flash --flash_mode dio --flash_size 32MB \
    --flash_freq 80m 0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -35 >&2
    exit 1
}
printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -25

P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after hard_reset verify_flash 0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -25 >&2
    exit 1
}
printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -10
printf 'Waveshare Console OS 0.4 successor install PASS.\n'
