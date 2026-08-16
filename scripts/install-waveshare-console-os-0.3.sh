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

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-0.3-default-game-20260816-exact-unit-authorization.json"
P4_AUTH_EXPECTED=9fb5ae6cf8abfb739c84cf80277dcc9666ae360cfece74c85dd23ecd2b2f9564
P4_MANIFEST="$P4_PROJECT_ROOT/hardware/backups/manifest.json"
P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing to install: exact-unit authorization changed.\n' >&2
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

p4_artifact_field() {
    python3 -c '
import json, sys
data = json.load(open(sys.argv[1]))
items = [item for item in data["candidate"]["artifacts"]
         if item["role"] == sys.argv[2]]
if len(items) != 1:
    raise SystemExit("authorization artifact role is not unique")
print(items[0][sys.argv[3]])
' "$P4_AUTH" "$1" "$2"
}

p4_check_artifact() {
    P4_ROLE=$1
    P4_PATH="$P4_PROJECT_ROOT/$(p4_artifact_field "$P4_ROLE" path)"
    P4_EXPECTED_BYTES=$(p4_artifact_field "$P4_ROLE" bytes)
    P4_EXPECTED_HASH=$(p4_artifact_field "$P4_ROLE" sha256)
    [ -f "$P4_PATH" ] || {
        printf 'Refusing to install: %s is missing.\n' "$P4_ROLE" >&2
        exit 1
    }
    [ "$(wc -c < "$P4_PATH" | tr -d ' ')" = "$P4_EXPECTED_BYTES" ] || {
        printf 'Refusing to install: %s byte count changed.\n' "$P4_ROLE" >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_PATH")" = "$P4_EXPECTED_HASH" ] || {
        printf 'Refusing to install: %s digest changed.\n' "$P4_ROLE" >&2
        exit 1
    }
}

P4_SOURCE_COMMIT=$(p4_json_field "$P4_AUTH" candidate.source_commit)
git -C "$P4_PROJECT_ROOT" cat-file -e "$P4_SOURCE_COMMIT^{commit}" 2>/dev/null || {
    printf 'Refusing to install: authorized source commit is unavailable.\n' >&2
    exit 1
}
git -C "$P4_PROJECT_ROOT" merge-base --is-ancestor \
    "$P4_SOURCE_COMMIT" HEAD || {
    printf 'Refusing to install: checkout does not contain authorized source.\n' >&2
    exit 1
}

P4_BACKUP_REL=$(p4_json_field "$P4_AUTH" factory_backup.path)
P4_BACKUP="$P4_PROJECT_ROOT/$P4_BACKUP_REL"
P4_BACKUP_BYTES=$(p4_json_field "$P4_AUTH" factory_backup.bytes)
P4_BACKUP_HASH=$(p4_json_field "$P4_AUTH" factory_backup.sha256)
[ -f "$P4_BACKUP" ] || {
    printf 'Refusing to install: preserved factory backup is missing.\n' >&2
    exit 1
}
[ "$(wc -c < "$P4_BACKUP" | tr -d ' ')" = "$P4_BACKUP_BYTES" ] || {
    printf 'Refusing to install: factory backup byte count changed.\n' >&2
    exit 1
}
[ "$(p4_sha256_file "$P4_BACKUP")" = "$P4_BACKUP_HASH" ] || {
    printf 'Refusing to install: factory backup digest changed.\n' >&2
    exit 1
}

for P4_ROLE in bootloader partition-table ota-data-initial application; do
    p4_check_artifact "$P4_ROLE"
done

python3 "$P4_PROJECT_ROOT/scripts/verify-console-os-waveshare.py" "$P4_BUILD"

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers
P4_FLASH_BYTES=$(p4_json_field "$P4_AUTH" board.flash_bytes)
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = \
    "$P4_FLASH_BYTES" ] || {
    printf 'Refusing to install: live flash geometry differs.\n' >&2
    exit 1
}
P4_IDENTITY_EXPECTED=$(p4_json_field "$P4_AUTH" board.device_identity_sha256)
P4_IDENTITY_ACTUAL=$(p4_read_device_identity_hash "$P4_PORT" no_reset)
[ "$P4_IDENTITY_ACTUAL" = "$P4_IDENTITY_EXPECTED" ] || {
    printf 'Refusing to install: live unit does not match the backed-up board.\n' >&2
    exit 1
}

if [ "$P4_WRITE_ENABLED" = false ]; then
    printf 'Waveshare Console OS 0.3 preflight PASS; no flash was read or written.\n'
    exit 0
fi

P4_PREINSTALL_REL=$(p4_json_field "$P4_AUTH" preinstall_backup.path)
P4_PREINSTALL="$P4_PROJECT_ROOT/$P4_PREINSTALL_REL"
P4_PREINSTALL_BYTES=$(p4_json_field "$P4_AUTH" preinstall_backup.bytes)
[ ! -e "$P4_PREINSTALL" ] || {
    printf 'Refusing to install: preinstall backup target already exists.\n' >&2
    exit 1
}
P4_PREINSTALL_TEMP=$(mktemp \
    "$P4_PROJECT_ROOT/hardware/backups/.waveshare-pre-console-os-0.3.XXXXXX")
p4_cleanup_temp() {
    [ -z "$P4_PREINSTALL_TEMP" ] || rm -f "$P4_PREINSTALL_TEMP"
}
trap p4_cleanup_temp EXIT HUP INT TERM

printf 'Preserving the complete live 32 MB flash before layout migration.\n'
P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after no_reset read_flash 0x0 "$P4_PREINSTALL_BYTES" \
    "$P4_PREINSTALL_TEMP" 2>&1) || {
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -30 >&2
    exit 1
}
printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -12
[ "$(wc -c < "$P4_PREINSTALL_TEMP" | tr -d ' ')" = \
    "$P4_PREINSTALL_BYTES" ] || {
    printf 'Preinstall backup has the wrong byte count.\n' >&2
    exit 1
}
chmod 600 "$P4_PREINSTALL_TEMP"
mv "$P4_PREINSTALL_TEMP" "$P4_PREINSTALL"
P4_PREINSTALL_TEMP=
P4_PREINSTALL_HASH=$(p4_sha256_file "$P4_PREINSTALL")
printf 'Complete preinstall backup PASS sha256=%s bytes=%s\n' \
    "$P4_PREINSTALL_HASH" "$P4_PREINSTALL_BYTES"

P4_BOOTLOADER="$P4_BUILD/bootloader/bootloader.bin"
P4_PARTITIONS="$P4_BUILD/partition_table/partition-table.bin"
P4_OTA_DATA="$P4_BUILD/ota_data_initial.bin"
P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
printf 'Writing the exact bootloader, OTA partition map, OTA data, and app.\n'
P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after no_reset write_flash --flash_mode dio --flash_size 32MB \
    --flash_freq 80m \
    0x2000 "$P4_BOOTLOADER" \
    0x8000 "$P4_PARTITIONS" \
    0x10000 "$P4_OTA_DATA" \
    0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -40 >&2
    printf 'Install failed; the complete preinstall backup is preserved at %s.\n' \
        "$P4_PREINSTALL_REL" >&2
    exit 1
}
printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -30

p4_verify_written() {
    P4_VERIFY_OFFSET=$1
    P4_VERIFY_FILE=$2
    P4_VERIFY_LABEL=$3
    P4_VERIFY_AFTER=$4
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        --after "$P4_VERIFY_AFTER" verify_flash \
        "$P4_VERIFY_OFFSET" "$P4_VERIFY_FILE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -30 >&2
        printf '%s on-device verification failed.\n' "$P4_VERIFY_LABEL" >&2
        exit 1
    }
    printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -10
    printf '%s digest PASS sha256=%s bytes=%s\n' \
        "$P4_VERIFY_LABEL" "$(p4_sha256_file "$P4_VERIFY_FILE")" \
        "$(wc -c < "$P4_VERIFY_FILE" | tr -d ' ')"
}

p4_verify_written 0x2000 "$P4_BOOTLOADER" bootloader no_reset
p4_verify_written 0x8000 "$P4_PARTITIONS" partition-table no_reset
p4_verify_written 0x10000 "$P4_OTA_DATA" ota-data no_reset
p4_verify_written 0x20000 "$P4_APPLICATION" application hard_reset

trap - EXIT HUP INT TERM
printf 'Waveshare Console OS 0.3 install PASS; exact written spans verified.\n'
printf 'Preinstall recovery: %s sha256=%s\n' \
    "$P4_PREINSTALL_REL" "$P4_PREINSTALL_HASH"
