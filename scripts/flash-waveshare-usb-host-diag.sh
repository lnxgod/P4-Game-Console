#!/bin/sh

set -eu

P4_PORT=${1:-}
P4_MODE=${2:-diag}
P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

p4_require_port "$P4_PORT"
p4_activate_idf

P4_MANIFEST="$P4_PROJECT_ROOT/hardware/backups/manifest.json"
P4_DEVICE_FIELDS=$(python3 -c '
import json, sys
data = json.load(open(sys.argv[1]))
matches = [entry for entry in data.get("additional_devices", [])
           if entry.get("board_claim", "").startswith("Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3")]
if len(matches) != 1:
    raise SystemExit("expected exactly one Waveshare 4.3 backup entry")
entry = matches[0]
print(entry["device"]["identity"]["sha256"])
print(entry["device"]["flash_bytes"])
print(entry["backup"]["file"])
print(entry["backup"]["bytes"])
print(entry["backup"]["sha256"])
' "$P4_MANIFEST")
P4_EXPECTED_IDENTITY=$(printf '%s\n' "$P4_DEVICE_FIELDS" | sed -n '1p')
P4_EXPECTED_FLASH_BYTES=$(printf '%s\n' "$P4_DEVICE_FIELDS" | sed -n '2p')
P4_BACKUP_REL=$(printf '%s\n' "$P4_DEVICE_FIELDS" | sed -n '3p')
P4_BACKUP_BYTES=$(printf '%s\n' "$P4_DEVICE_FIELDS" | sed -n '4p')
P4_BACKUP_HASH=$(printf '%s\n' "$P4_DEVICE_FIELDS" | sed -n '5p')
P4_BACKUP_PATH="$P4_PROJECT_ROOT/$P4_BACKUP_REL"

[ -f "$P4_BACKUP_PATH" ] || {
    printf 'Waveshare factory backup is missing: %s\n' "$P4_BACKUP_PATH" >&2
    exit 1
}
[ "$(wc -c < "$P4_BACKUP_PATH" | tr -d ' ')" = "$P4_BACKUP_BYTES" ] || {
    printf 'Waveshare factory backup byte count mismatch.\n' >&2
    exit 1
}
[ "$(p4_sha256_file "$P4_BACKUP_PATH")" = "$P4_BACKUP_HASH" ] || {
    printf 'Waveshare factory backup hash mismatch.\n' >&2
    exit 1
}

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --after no_reset flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = "$P4_EXPECTED_FLASH_BYTES" ] || {
    printf 'Connected flash size does not match the Waveshare backup.\n' >&2
    exit 1
}
P4_LIVE_IDENTITY=$(p4_read_device_identity_hash "$P4_PORT" no_reset no_reset)
[ "$P4_LIVE_IDENTITY" = "$P4_EXPECTED_IDENTITY" ] || {
    printf 'Connected device identity does not match the Waveshare backup.\n' >&2
    exit 1
}

P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --before no_reset \
    --after no_reset get_security_info 2>&1) || exit 1
printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null
printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null

case "$P4_MODE" in
diag)
    "$P4_SCRIPT_DIR/build-waveshare-usb-host-diag.sh"
    P4_BUILD="$P4_PROJECT_ROOT/apps/usb_host_diag/build-waveshare-landscape"
    P4_ARGS="$P4_BUILD/flasher_args.json"
    P4_APP_OFFSET=$(python3 -c '
import json, sys
print(json.load(open(sys.argv[1]))["app"]["offset"])
' "$P4_ARGS")
    P4_APP_FILE=$(python3 -c '
import json, sys
print(json.load(open(sys.argv[1]))["app"]["file"])
' "$P4_ARGS")
    P4_APP_PATH="$P4_BUILD/$P4_APP_FILE"
    P4_LABEL="USB diagnostic"
    ;;
restore-console-os)
    P4_APP_OFFSET=0x20000
    P4_APP_PATH="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-usb-host/p4_console_os.bin"
    P4_LABEL="Console OS 0.4.24"
    [ -f "$P4_APP_PATH" ] || {
        printf 'Console OS 0.4.24 artifact is missing: %s\n' "$P4_APP_PATH" >&2
        exit 1
    }
    [ "$(wc -c < "$P4_APP_PATH" | tr -d ' ')" = 1108080 ] || {
        printf 'Console OS 0.4.24 artifact byte count mismatch.\n' >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_APP_PATH")" = e1417c62a9aa357a200359c06829c91a93c4a3c25082ebf3483fa9a1699cc4a6 ] || {
        printf 'Console OS 0.4.24 artifact hash mismatch.\n' >&2
        exit 1
    }
    ;;
install-console-os)
    P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-usb-host"
    python3 "$P4_PROJECT_ROOT/scripts/verify-console-os-waveshare.py" \
        "$P4_BUILD" >/dev/null
    P4_ARGS="$P4_BUILD/flasher_args.json"
    P4_APP_OFFSET=$(python3 -c '
import json, sys
print(json.load(open(sys.argv[1]))["app"]["offset"])
' "$P4_ARGS")
    P4_APP_FILE=$(python3 -c '
import json, sys
print(json.load(open(sys.argv[1]))["app"]["file"])
' "$P4_ARGS")
    P4_APP_PATH="$P4_BUILD/$P4_APP_FILE"
    P4_LABEL="verified current Console OS"
    ;;
*)
    printf '%s\n' \
        "Unknown mode: $P4_MODE (expected diag, restore-console-os, or install-console-os)" >&2
    exit 2
    ;;
esac
[ "$P4_APP_OFFSET" = "0x20000" ] || {
    printf 'Application offset is not 0x20000: %s\n' "$P4_APP_OFFSET" >&2
    exit 1
}
P4_APP_BYTES=$(wc -c < "$P4_APP_PATH" | tr -d ' ')
P4_APP_HASH=$(p4_sha256_file "$P4_APP_PATH")

P4_READBACK=$(mktemp "${TMPDIR:-/tmp}/p4-waveshare-app-readback.XXXXXX")
trap 'rm -f -- "$P4_READBACK"' EXIT HUP INT TERM

esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset write_flash --flash_mode dio \
    --flash_freq 80m --flash_size 32MB "$P4_APP_OFFSET" "$P4_APP_PATH"
esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset read_flash "$P4_APP_OFFSET" \
    "$P4_APP_BYTES" "$P4_READBACK"
[ "$(p4_sha256_file "$P4_READBACK")" = "$P4_APP_HASH" ] || {
    printf '%s application readback mismatch.\n' "$P4_LABEL" >&2
    exit 1
}

printf 'Waveshare %s installed: offset=%s bytes=%s sha256=%s\n' \
    "$P4_LABEL" "$P4_APP_OFFSET" "$P4_APP_BYTES" "$P4_APP_HASH"
esptool.py --chip esp32p4 --port "$P4_PORT" --before no_reset --after hard_reset run
