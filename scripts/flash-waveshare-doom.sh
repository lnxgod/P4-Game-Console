#!/bin/sh

set -eu

P4_PORT=
P4_WRITE_ENABLED=true
P4_CANDIDATE=doom
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --verify-only) P4_WRITE_ENABLED=false; shift ;;
        --console-os) P4_CANDIDATE=console-os; shift ;;
        --console-os-quake-packdir-fix)
            P4_CANDIDATE=console-os-quake-packdir-fix; shift ;;
        --console-os-quake-stack-fix)
            P4_CANDIDATE=console-os-quake-stack-fix; shift ;;
        --console-os-quake-renderer-fix)
            P4_CANDIDATE=console-os-quake-renderer-fix; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_port "$P4_PORT"
p4_activate_idf

P4_MANIFEST="$P4_PROJECT_ROOT/hardware/backups/manifest.json"
P4_PREDECESSOR=
P4_VERIFY_LIVE_PREDECESSOR=false
case "$P4_CANDIDATE" in
    doom)
        P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-doom-landscape-20260816-exact-unit-authorization.json"
        P4_AUTH_EXPECTED=5ad6e78c12e4d50bf8157c9b3f9bc253f621ac6b65fc0de68292a91874c2e5a8
        P4_BUILD="$P4_PROJECT_ROOT/apps/doom_embedded_touch_audio/build-waveshare-landscape"
        P4_APPLICATION="$P4_BUILD/p4_doom_embedded_touch_audio.bin"
        P4_RESULT_LABEL="Waveshare landscape Doom successor"
        ;;
    console-os)
        P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-usb-content-20260816-exact-unit-authorization.json"
        P4_AUTH_EXPECTED=737b38deea870440efc8bd36341d5d406767c0a7fdb36171ea666b6edb82424f
        P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"
        P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
        P4_RESULT_LABEL="Waveshare full-screen landscape Console OS successor"
        ;;
    console-os-quake-packdir-fix)
        P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-quake-packdir-fix-20260816-exact-unit-authorization.json"
        P4_AUTH_EXPECTED=a035620070589acdd78c5726e9fd0a79b0a4376306e772a8e41174d86cefa067
        P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"
        P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
        P4_RESULT_LABEL="Waveshare Console OS Quake PAK-directory PSRAM successor"
        ;;
    console-os-quake-stack-fix)
        P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-quake-stack-20260816-exact-unit-authorization.json"
        P4_AUTH_EXPECTED=ea78908be39d2660c718e52c9498fb1ddc91491e50e5aa932fbe3a206e69e56d
        P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"
        P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
        P4_RESULT_LABEL="Waveshare Console OS Quake external-stack successor"
        P4_VERIFY_LIVE_PREDECESSOR=true
        ;;
    console-os-quake-renderer-fix)
        P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-quake-renderer-20260816-exact-unit-authorization.json"
        P4_AUTH_EXPECTED=1d271d3b47772bc5b894f1b11290c836cfd9f06e5039b7699cca10ffd1911242
        P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-landscape"
        P4_APPLICATION="$P4_BUILD/p4_console_os.bin"
        P4_RESULT_LABEL="Waveshare Console OS Quake renderer-PSRAM successor"
        P4_VERIFY_LIVE_PREDECESSOR=true
        ;;
    *) printf 'Internal candidate selection error.\n' >&2; exit 2 ;;
esac
P4_BOOTLOADER="$P4_BUILD/bootloader/bootloader.bin"
P4_PARTITIONS="$P4_BUILD/partition_table/partition-table.bin"

[ "$(shasum -a 256 "$P4_AUTH" | awk '{print $1}')" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing to flash: exact-unit authorization changed.\n' >&2; exit 1;
}

P4_BACKUP_REL=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["additional_devices"][0]["backup"]["file"])' "$P4_MANIFEST")
P4_BACKUP_EXPECTED=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["additional_devices"][0]["backup"]["sha256"])' "$P4_MANIFEST")
P4_BACKUP_BYTES=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["additional_devices"][0]["backup"]["bytes"])' "$P4_MANIFEST")
P4_IDENTITY_EXPECTED=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["additional_devices"][0]["device"]["identity"]["sha256"])' "$P4_MANIFEST")
P4_BACKUP="$P4_PROJECT_ROOT/$P4_BACKUP_REL"

p4_authorized_artifact_field() {
    python3 -c '
import json, sys
data = json.load(open(sys.argv[1]))
role, field = sys.argv[2], sys.argv[3]
items = [x for x in data["candidate"]["artifacts"] if x["role"] == role]
if len(items) != 1:
    raise SystemExit("authorization artifact role is not unique")
print(items[0][field])
' "$P4_AUTH" "$1" "$2"
}

p4_check_artifact() {
    P4_CHECK_PATH=$1
    P4_CHECK_ROLE=$2
    P4_CHECK_BYTES=$(p4_authorized_artifact_field "$P4_CHECK_ROLE" bytes)
    P4_CHECK_HASH=$(p4_authorized_artifact_field "$P4_CHECK_ROLE" sha256)
    [ -f "$P4_CHECK_PATH" ] || {
        printf 'Refusing to flash: %s is missing.\n' "$P4_CHECK_ROLE" >&2; exit 1;
    }
    [ "$(wc -c < "$P4_CHECK_PATH" | tr -d ' ')" = "$P4_CHECK_BYTES" ] || {
        printf 'Refusing to flash: %s byte count changed.\n' "$P4_CHECK_ROLE" >&2; exit 1;
    }
    [ "$(shasum -a 256 "$P4_CHECK_PATH" | awk '{print $1}')" = "$P4_CHECK_HASH" ] || {
        printf 'Refusing to flash: %s hash changed.\n' "$P4_CHECK_ROLE" >&2; exit 1;
    }
}

[ -f "$P4_BACKUP" ] || { printf 'Refusing to flash: factory backup is missing.\n' >&2; exit 1; }
[ "$(wc -c < "$P4_BACKUP" | tr -d ' ')" = "$P4_BACKUP_BYTES" ] || {
    printf 'Refusing to flash: factory backup byte count differs.\n' >&2; exit 1;
}
[ "$(shasum -a 256 "$P4_BACKUP" | awk '{print $1}')" = "$P4_BACKUP_EXPECTED" ] || {
    printf 'Refusing to flash: factory backup hash differs.\n' >&2; exit 1;
}
p4_check_artifact "$P4_BOOTLOADER" bootloader
p4_check_artifact "$P4_PARTITIONS" partition-table
p4_check_artifact "$P4_APPLICATION" application

if [ -n "$P4_PREDECESSOR" ]; then
    P4_PREDECESSOR_BYTES=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["predecessor"]["bytes"])' "$P4_AUTH")
    P4_PREDECESSOR_HASH=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["predecessor"]["sha256"])' "$P4_AUTH")
    [ -f "$P4_PREDECESSOR" ] || {
        printf 'Refusing to flash: predecessor artifact is missing.\n' >&2; exit 1;
    }
    [ "$(wc -c < "$P4_PREDECESSOR" | tr -d ' ')" = "$P4_PREDECESSOR_BYTES" ] || {
        printf 'Refusing to flash: predecessor byte count changed.\n' >&2; exit 1;
    }
    [ "$(shasum -a 256 "$P4_PREDECESSOR" | awk '{print $1}')" = "$P4_PREDECESSOR_HASH" ] || {
        printf 'Refusing to flash: predecessor hash changed.\n' >&2; exit 1;
    }
fi

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = 33554432 ] || {
    printf 'Refusing to flash: live flash is not 32 MB.\n' >&2; exit 1;
}
P4_IDENTITY_ACTUAL=$(p4_read_device_identity_hash "$P4_PORT" no_reset)
[ "$P4_IDENTITY_ACTUAL" = "$P4_IDENTITY_EXPECTED" ] || {
    printf 'Refusing to flash: live identity does not match the backed-up unit.\n' >&2; exit 1;
}

P4_BOOT_READBACK=$(mktemp "$P4_PROJECT_ROOT/hardware/backups/.waveshare-boot-readback.XXXXXX")
P4_PART_READBACK=$(mktemp "$P4_PROJECT_ROOT/hardware/backups/.waveshare-part-readback.XXXXXX")
P4_PREDECESSOR_READBACK=$(mktemp "$P4_PROJECT_ROOT/hardware/backups/.waveshare-predecessor-readback.XXXXXX")
P4_KEEP_READBACKS=false
p4_cleanup_readbacks() {
    if [ "$P4_KEEP_READBACKS" = false ]; then
        rm -f "$P4_BOOT_READBACK" "$P4_PART_READBACK" \
            "$P4_PREDECESSOR_READBACK"
    fi
}

p4_verify_flash_digest() {
    P4_VERIFY_SOURCE=$1
    P4_VERIFY_OFFSET=$2
    P4_VERIFY_LABEL=$3
    P4_VERIFY_BYTES=$(wc -c < "$P4_VERIFY_SOURCE" | tr -d ' ')
    P4_VERIFY_EXPECTED=$(shasum -a 256 "$P4_VERIFY_SOURCE" | awk '{print $1}')
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        verify_flash "$P4_VERIFY_OFFSET" "$P4_VERIFY_SOURCE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -20 >&2
        printf '%s digest verification failed.\n' "$P4_VERIFY_LABEL" >&2
        return 1
    }
    printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -12
    printf '%s digest PASS sha256=%s bytes=%s\n' \
        "$P4_VERIFY_LABEL" "$P4_VERIFY_EXPECTED" "$P4_VERIFY_BYTES"
}
trap p4_cleanup_readbacks EXIT HUP INT TERM

p4_read_and_verify() {
    P4_VERIFY_SOURCE=$1
    P4_VERIFY_OFFSET=$2
    P4_VERIFY_OUTPUT=$3
    P4_VERIFY_LABEL=$4
    P4_VERIFY_BYTES=$(wc -c < "$P4_VERIFY_SOURCE" | tr -d ' ')
    P4_VERIFY_EXPECTED=$(shasum -a 256 "$P4_VERIFY_SOURCE" | awk '{print $1}')
    P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        read_flash "$P4_VERIFY_OFFSET" "$P4_VERIFY_BYTES" "$P4_VERIFY_OUTPUT" 2>&1) || {
        printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -20 >&2
        printf '%s readback failed.\n' "$P4_VERIFY_LABEL" >&2
        return 1
    }
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -8
    P4_VERIFY_ACTUAL=$(shasum -a 256 "$P4_VERIFY_OUTPUT" | awk '{print $1}')
    if [ "$P4_VERIFY_ACTUAL" != "$P4_VERIFY_EXPECTED" ]; then
        chmod 600 "$P4_VERIFY_OUTPUT"
        P4_KEEP_READBACKS=true
        printf '%s readback mismatch. Expected %s, got %s.\n' \
            "$P4_VERIFY_LABEL" "$P4_VERIFY_EXPECTED" "$P4_VERIFY_ACTUAL" >&2
        return 1
    fi
    printf '%s readback PASS sha256=%s bytes=%s\n' \
        "$P4_VERIFY_LABEL" "$P4_VERIFY_ACTUAL" "$P4_VERIFY_BYTES"
}

p4_read_and_verify_expected() {
    P4_VERIFY_OFFSET=$1
    P4_VERIFY_BYTES=$2
    P4_VERIFY_EXPECTED=$3
    P4_VERIFY_OUTPUT=$4
    P4_VERIFY_LABEL=$5
    P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        read_flash "$P4_VERIFY_OFFSET" "$P4_VERIFY_BYTES" "$P4_VERIFY_OUTPUT" 2>&1) || {
        printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -20 >&2
        printf '%s readback failed.\n' "$P4_VERIFY_LABEL" >&2
        return 1
    }
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -8
    P4_VERIFY_ACTUAL=$(shasum -a 256 "$P4_VERIFY_OUTPUT" | awk '{print $1}')
    if [ "$P4_VERIFY_ACTUAL" != "$P4_VERIFY_EXPECTED" ]; then
        chmod 600 "$P4_VERIFY_OUTPUT"
        P4_KEEP_READBACKS=true
        printf '%s readback mismatch. Expected %s, got %s.\n' \
            "$P4_VERIFY_LABEL" "$P4_VERIFY_EXPECTED" "$P4_VERIFY_ACTUAL" >&2
        return 1
    fi
    printf '%s readback PASS sha256=%s bytes=%s\n' \
        "$P4_VERIFY_LABEL" "$P4_VERIFY_ACTUAL" "$P4_VERIFY_BYTES"
}

# This successor is app-only. Refuse to mutate unless the installed bootloader
# and partition map are already the exact authorized bytes.
p4_read_and_verify "$P4_BOOTLOADER" 0x2000 "$P4_BOOT_READBACK" bootloader-before-write
p4_read_and_verify "$P4_PARTITIONS" 0x8000 "$P4_PART_READBACK" partition-table-before-write
if [ -n "$P4_PREDECESSOR" ]; then
    p4_read_and_verify "$P4_PREDECESSOR" 0x10000 \
        "$P4_PREDECESSOR_READBACK" application-before-write
fi
if [ "$P4_VERIFY_LIVE_PREDECESSOR" = true ]; then
    P4_PREDECESSOR_BYTES=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["predecessor"]["bytes"])' "$P4_AUTH")
    P4_PREDECESSOR_HASH=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["predecessor"]["sha256"])' "$P4_AUTH")
    p4_read_and_verify_expected 0x10000 "$P4_PREDECESSOR_BYTES" \
        "$P4_PREDECESSOR_HASH" "$P4_PREDECESSOR_READBACK" \
        application-before-write
fi

if [ "$P4_WRITE_ENABLED" = true ]; then
    printf 'Writing exact %s application only.\n' "$P4_RESULT_LABEL"
    P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
        --after no_reset write_flash --flash_mode dio --flash_size 32MB \
        --flash_freq 80m \
        0x10000 "$P4_APPLICATION" 2>&1) || {
        printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -30 >&2
        printf 'Install write failed; preserve power and evaluate recovery.\n' >&2
        exit 1
    }
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -30
else
    printf 'Verify-only resume: no flash write will occur.\n'
fi

p4_verify_flash_digest "$P4_APPLICATION" 0x10000 application

p4_cleanup_readbacks
trap - EXIT HUP INT TERM
printf '%s PASS: base spans and application read back exactly.\n' \
    "$P4_RESULT_LABEL"
