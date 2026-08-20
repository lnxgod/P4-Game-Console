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

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-first-install-20260820.json"
P4_AUTH_EXPECTED=51ffcce95c5ee2100854ee520e0cf81211a17e44685df716bfb5686ec8641f4a
P4_MANIFEST="$P4_PROJECT_ROOT/hardware/backups/manifest.json"
P4_BUILD="$P4_PROJECT_ROOT/apps/console_os/build-waveshare-usb-host"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing unit 2 install: authorization changed.\n' >&2
    exit 1
}

p4_json_field() {
    python3 -c '
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in sys.argv[2].split("."):
    value = value[key]
print(str(value).lower() if isinstance(value, bool) else value)
' "$1" "$2"
}

p4_artifact_field() {
    python3 -c '
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
matches = [item for item in data["candidate"]["artifacts"]
           if item["role"] == sys.argv[2]]
if len(matches) != 1:
    raise SystemExit("authorization artifact role is not unique")
print(matches[0][sys.argv[3]])
' "$P4_AUTH" "$1" "$2"
}

p4_check_artifact() {
    P4_ROLE=$1
    P4_PATH="$P4_PROJECT_ROOT/$(p4_artifact_field "$P4_ROLE" path)"
    P4_EXPECTED_BYTES=$(p4_artifact_field "$P4_ROLE" bytes)
    P4_EXPECTED_HASH=$(p4_artifact_field "$P4_ROLE" sha256)
    [ -f "$P4_PATH" ] || {
        printf 'Refusing unit 2 install: %s is missing.\n' "$P4_ROLE" >&2
        exit 1
    }
    [ "$(wc -c < "$P4_PATH" | tr -d ' ')" = "$P4_EXPECTED_BYTES" ] || {
        printf 'Refusing unit 2 install: %s byte count changed.\n' \
            "$P4_ROLE" >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_PATH")" = "$P4_EXPECTED_HASH" ] || {
        printf 'Refusing unit 2 install: %s digest changed.\n' "$P4_ROLE" >&2
        exit 1
    }
}

P4_IDENTITY_EXPECTED=$(p4_json_field "$P4_AUTH" board.device_identity_sha256)
python3 - "$P4_MANIFEST" "$P4_AUTH" <<'PY'
import json
import pathlib
import sys

manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
authorization = json.loads(
    pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
)
expected_identity = authorization["board"]["device_identity_sha256"]
matches = [
    entry for entry in manifest.get("additional_devices", [])
    if entry.get("device", {}).get("identity", {}).get("sha256")
       == expected_identity
]
if len(matches) != 1:
    raise SystemExit("unit 2 backup manifest binding is not unique")
recorded = matches[0]
expected_backup = authorization["factory_backup"]
actual_backup = recorded.get("backup", {})
for key in ("path", "bytes", "sha256"):
    manifest_key = "file" if key == "path" else key
    if actual_backup.get(manifest_key) != expected_backup.get(key):
        raise SystemExit(f"unit 2 backup manifest differs for {key}")
if recorded.get("device", {}).get("flash_bytes") != 33_554_432:
    raise SystemExit("unit 2 manifest has the wrong flash geometry")
PY

P4_BACKUP="$P4_PROJECT_ROOT/$(p4_json_field "$P4_AUTH" factory_backup.path)"
[ -f "$P4_BACKUP" ] || {
    printf 'Refusing unit 2 install: factory backup is missing.\n' >&2
    exit 1
}
[ "$(wc -c < "$P4_BACKUP" | tr -d ' ')" = \
    "$(p4_json_field "$P4_AUTH" factory_backup.bytes)" ] || {
    printf 'Refusing unit 2 install: factory backup byte count changed.\n' >&2
    exit 1
}
[ "$(p4_sha256_file "$P4_BACKUP")" = \
    "$(p4_json_field "$P4_AUTH" factory_backup.sha256)" ] || {
    printf 'Refusing unit 2 install: factory backup digest changed.\n' >&2
    exit 1
}

for P4_ROLE in \
    bootloader partition-table ota-data-initial application \
    application-elf-evidence
do
    p4_check_artifact "$P4_ROLE"
done

P4_SOURCE_COMMIT=$(p4_json_field "$P4_AUTH" candidate.source_commit)
git -C "$P4_PROJECT_ROOT" merge-base --is-ancestor \
    "$P4_SOURCE_COMMIT" HEAD || {
    printf 'Refusing unit 2 install: authorized source is not an ancestor.\n' >&2
    exit 1
}

python3 - "$P4_BUILD/flasher_args.json" "$P4_AUTH" <<'PY'
import json
import pathlib
import sys

flasher = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
authorization = json.loads(
    pathlib.Path(sys.argv[2]).read_text(encoding="utf-8")
)
root = pathlib.Path("apps/console_os/build-waveshare-usb-host")
expected = {
    item["offset"]: str(pathlib.Path(item["path"]).relative_to(root))
    for item in authorization["candidate"]["artifacts"]
    if item["offset"] is not None
}
if flasher.get("flash_files") != expected:
    raise SystemExit("flasher layout differs from unit 2 authorization")
if flasher.get("flash_settings") != {
    "flash_mode": "dio", "flash_size": "32MB", "flash_freq": "80m"
}:
    raise SystemExit("flasher settings differ from unit 2 authorization")
PY

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    printf 'Refusing unit 2 install: live flash probe failed.\n' >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers
[ "$(printf '%s\n' "$P4_PROBE" | \
    p4_flash_size_bytes_from_esptool_output)" = "33554432" ] || {
    printf 'Refusing unit 2 install: live flash is not 32 MiB.\n' >&2
    exit 1
}
P4_IDENTITY_ACTUAL=$(p4_read_device_identity_hash "$P4_PORT" no_reset)
[ "$P4_IDENTITY_ACTUAL" = "$P4_IDENTITY_EXPECTED" ] || {
    printf 'Refusing unit 2 install: wrong physical board selected.\n' >&2
    exit 1
}

P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after no_reset get_security_info 2>&1) || {
    printf '%s\n' "$P4_SECURITY" | p4_redact_device_identifiers >&2
    printf 'Refusing unit 2 install: security probe failed.\n' >&2
    exit 1
}
printf '%s\n' "$P4_SECURITY" | p4_redact_device_identifiers
printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null || {
    printf 'Refusing unit 2 install: secure boot is enabled.\n' >&2
    exit 1
}
printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null || {
    printf 'Refusing unit 2 install: flash encryption is enabled.\n' >&2
    exit 1
}

if [ "$P4_WRITE_ENABLED" = false ]; then
    printf 'Waveshare unit 2 first-install preflight PASS; no write occurred.\n'
    exit 0
fi

P4_BOOTLOADER="$P4_BUILD/bootloader/bootloader.bin"
P4_PARTITIONS="$P4_BUILD/partition_table/partition-table.bin"
P4_OTA_DATA="$P4_BUILD/ota_data_initial.bin"
P4_APPLICATION="$P4_BUILD/p4_console_os.bin"

printf 'Writing four exact Console OS spans to Waveshare unit 2.\n'
P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 921600 \
    --after no_reset write_flash --flash_mode dio --flash_size 32MB \
    --flash_freq 80m \
    0x2000 "$P4_BOOTLOADER" \
    0x8000 "$P4_PARTITIONS" \
    0x10000 "$P4_OTA_DATA" \
    0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -40 >&2
    printf 'Unit 2 install failed; the complete factory backup is preserved.\n' >&2
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
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | \
            tail -30 >&2
        printf 'Unit 2 %s verification failed.\n' "$P4_VERIFY_LABEL" >&2
        exit 1
    }
    printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -8
    printf 'Unit 2 %s readback PASS sha256=%s bytes=%s\n' \
        "$P4_VERIFY_LABEL" "$(p4_sha256_file "$P4_VERIFY_FILE")" \
        "$(wc -c < "$P4_VERIFY_FILE" | tr -d ' ')"
}

p4_verify_written 0x2000 "$P4_BOOTLOADER" bootloader no_reset
p4_verify_written 0x8000 "$P4_PARTITIONS" partition-table no_reset
p4_verify_written 0x10000 "$P4_OTA_DATA" ota-data no_reset
p4_verify_written 0x20000 "$P4_APPLICATION" application hard_reset

printf 'Waveshare unit 2 Console OS first install PASS.\n'
