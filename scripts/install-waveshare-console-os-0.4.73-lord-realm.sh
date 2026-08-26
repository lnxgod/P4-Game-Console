#!/bin/sh

set -eu

P4_PORT=
P4_UNIT=
P4_CHECK_ONLY=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --unit) P4_UNIT=${2:-}; shift 2 ;;
        --check-only) P4_CHECK_ONLY=1; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-two-unit-console-os-0.4.73-lord-realm-20260825-exact-unit-authorization.json"
P4_AUTH_EXPECTED=64c944ab2b2b6f073ca93ac14e831be8eec2bd5936d4a13f008ef0ed4b031cb4
P4_MANIFEST="$P4_PROJECT_ROOT/hardware/backups/manifest.json"
P4_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142"
P4_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-console-os-0.4.73-install-lock"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing to install: exact-unit authorization changed.\n' >&2
    exit 1
}

p4_json_field() {
    python3 -c '
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in sys.argv[2].split("."):
    value = value[int(key)] if isinstance(value, list) else value[key]
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

p4_board_field() {
    python3 -c '
import json, sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
matches = [item for item in data["boards"]
           if item["unit_label"] == sys.argv[2]]
if len(matches) != 1:
    raise SystemExit("authorized unit label is not unique")
value = matches[0]
for key in sys.argv[3].split("."):
    value = value[key]
print(value)
' "$P4_AUTH" "$1" "$2"
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

p4_relative_file() {
    printf '%s/%s\n' "$P4_PROJECT_ROOT" "$1"
}

p4_check_artifact() {
    P4_ROLE=$1
    P4_PATH=$(p4_relative_file "$(p4_artifact_field "$P4_ROLE" path)")
    p4_check_file "$P4_PATH" \
        "$(p4_artifact_field "$P4_ROLE" bytes)" \
        "$(p4_artifact_field "$P4_ROLE" sha256)" "$P4_ROLE"
    [ "$(stat -f '%Lp' "$P4_PATH")" = 400 ] || {
        printf 'Refusing to install: %s is not sealed mode 0400.\n' \
            "$P4_ROLE" >&2
        exit 1
    }
}

[ "$(p4_json_field "$P4_AUTH" active)" = true ] || {
    printf 'Refusing to install: authorization is inactive.\n' >&2
    exit 1
}
[ -d "$P4_RELEASE" ] || {
    printf 'Refusing to install: immutable release directory is missing.\n' >&2
    exit 1
}

P4_SOURCE_COMMIT=$(p4_json_field "$P4_AUTH" source.commit)
git -C "$P4_PROJECT_ROOT" merge-base --is-ancestor \
    "$P4_SOURCE_COMMIT" HEAD || {
    printf 'Refusing to install: authorized source is not an ancestor.\n' >&2
    exit 1
}

for P4_ROLE in \
    application application-elf-evidence update-package-evidence \
    ota-data-evidence sdkconfig-evidence dependency-lock-evidence \
    lord-cartridge
do
    p4_check_artifact "$P4_ROLE"
done

P4_BOOTLOADER=$(p4_relative_file \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.path)")
P4_PARTITIONS=$(p4_relative_file \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.path)")
p4_check_file "$P4_BOOTLOADER" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.sha256)" \
    bootloader
p4_check_file "$P4_PARTITIONS" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.sha256)" \
    partition-table

python3 -c '
import json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
authorization = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
records = [manifest] + manifest.get("additional_devices", [])
for board in authorization["boards"]:
    identity = board["device_identity_sha256"]
    matches = [record for record in records
               if record.get("device", {}).get("identity", {}).get("sha256")
               == identity]
    if len(matches) != 1:
        raise SystemExit("backup manifest binding for "
                         + board["unit_label"] + " is not unique")
    actual = matches[0].get("backup", {})
    expected = board["factory_backup"]
    for expected_key, manifest_key in (("path", "file"),
                                       ("bytes", "bytes"),
                                       ("sha256", "sha256")):
        if actual.get(manifest_key) != expected[expected_key]:
            raise SystemExit("backup manifest differs for "
                             + board["unit_label"] + " field "
                             + expected_key)
    if matches[0].get("device", {}).get("flash_bytes") != 33_554_432:
        raise SystemExit("backup manifest flash geometry differs for "
                         + board["unit_label"])
' "$P4_MANIFEST" "$P4_AUTH"

P4_BOARD_INDEX=0
while [ "$P4_BOARD_INDEX" -lt 2 ]; do
    P4_BACKUP=$(p4_relative_file \
        "$(p4_json_field "$P4_AUTH" "boards.$P4_BOARD_INDEX.factory_backup.path")")
    p4_check_file "$P4_BACKUP" \
        "$(p4_json_field "$P4_AUTH" "boards.$P4_BOARD_INDEX.factory_backup.bytes")" \
        "$(p4_json_field "$P4_AUTH" "boards.$P4_BOARD_INDEX.factory_backup.sha256")" \
        "unit-$((P4_BOARD_INDEX + 1))-factory-backup"
    P4_BOARD_INDEX=$((P4_BOARD_INDEX + 1))
done

if [ "$P4_CHECK_ONLY" = 1 ]; then
    [ -z "$P4_PORT" ] && [ -z "$P4_UNIT" ] || {
        printf -- '--check-only does not accept --unit or --port.\n' >&2
        exit 2
    }
    printf 'Waveshare Console OS 0.4.73 LORD realm source checks PASS; no device access or write occurred.\n'
    exit 0
fi

[ "$P4_UNIT" = unit1 ] || [ "$P4_UNIT" = unit2 ] || {
    printf 'Use --unit unit1 or --unit unit2.\n' >&2
    exit 2
}
p4_require_port "$P4_PORT"
p4_activate_idf

P4_IDENTITY_EXPECTED=$(p4_board_field "$P4_UNIT" device_identity_sha256)
P4_RECORDED_PORT=$(p4_board_field "$P4_UNIT" recorded_port)
P4_BACKUP=$(p4_relative_file \
    "$(p4_board_field "$P4_UNIT" factory_backup.path)")
P4_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-console-os-0.4.73-lord-realm-fb58142-$P4_UNIT"
P4_PREIMAGE="$P4_RECOVERY/preimage-0.4.70-span.bin"
P4_READBACK="$P4_RECOVERY/readback-0.4.73-span.bin"
P4_LEDGER="$P4_RECOVERY/ledger.json"
P4_APPLICATION=$(p4_relative_file \
    "$(p4_artifact_field application path)")

[ ! -e "$P4_LOCK_DIR" ] || {
    printf 'Refusing to install: another 0.4.73 install route is active.\n' >&2
    exit 1
}
[ ! -e "$P4_RECOVERY" ] || {
    printf 'Refusing to install: %s recovery evidence already exists.\n' \
        "$P4_UNIT" >&2
    exit 1
}
mkdir -m 700 "$P4_LOCK_DIR"
trap 'rmdir "$P4_LOCK_DIR" 2>/dev/null || true' EXIT HUP INT TERM

[ "$P4_PORT" = "$P4_RECORDED_PORT" ] || {
    printf 'Note: %s is using a changed port path; identity binding remains authoritative.\n' \
        "$P4_UNIT"
}

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after no_reset flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers | tail -20
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = \
    "$(p4_json_field "$P4_AUTH" board_profile.flash_bytes)" ] || {
    printf 'Refusing to install: live flash geometry differs.\n' >&2
    exit 1
}
[ "$(p4_read_device_identity_hash "$P4_PORT" no_reset no_reset)" = \
    "$P4_IDENTITY_EXPECTED" ] || {
    printf 'Refusing to install: live unit differs from %s.\n' "$P4_UNIT" >&2
    exit 1
}

P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --before no_reset --after no_reset get_security_info 2>&1) || exit 1
printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null
printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null

p4_verify_live_file() {
    P4_VERIFY_OFFSET=$1
    P4_VERIFY_FILE=$2
    P4_VERIFY_LABEL=$3
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
        --before no_reset --after no_reset verify_flash \
        "$P4_VERIFY_OFFSET" "$P4_VERIFY_FILE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | \
            tail -25 >&2
        printf 'Refusing to install: live %s differs.\n' \
            "$P4_VERIFY_LABEL" >&2
        exit 1
    }
    printf '%s PASS\n' "$P4_VERIFY_LABEL"
}

p4_verify_live_file 0x2000 "$P4_BOOTLOADER" bootloader
p4_verify_live_file 0x8000 "$P4_PARTITIONS" partition-table

mkdir -m 700 "$P4_RECOVERY"
P4_PREIMAGE_BYTES=$(p4_json_field "$P4_AUTH" predecessor.preserve_span_bytes)
P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset read_flash 0x20000 \
    "$P4_PREIMAGE_BYTES" "$P4_PREIMAGE" 2>&1) || {
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -25 >&2
    exit 1
}
chmod 0400 "$P4_PREIMAGE"

P4_PREDECESSOR_BYTES=$(p4_json_field "$P4_AUTH" predecessor.application_bytes)
P4_PREDECESSOR_HASH=$(head -c "$P4_PREDECESSOR_BYTES" "$P4_PREIMAGE" | \
    shasum -a 256 | awk '{print $1}')
[ "$P4_PREDECESSOR_HASH" = \
    "$(p4_json_field "$P4_AUTH" predecessor.application_sha256)" ] || {
    printf 'Refusing to install: live 0.4.70 application differs.\n' >&2
    exit 1
}
P4_PREDECESSOR_SPAN_BYTES=$(p4_json_field \
    "$P4_AUTH" predecessor.installed_span_bytes)
P4_PREDECESSOR_SPAN_HASH=$(head -c "$P4_PREDECESSOR_SPAN_BYTES" \
    "$P4_PREIMAGE" | shasum -a 256 | awk '{print $1}')
[ "$P4_PREDECESSOR_SPAN_HASH" = \
    "$(p4_json_field "$P4_AUTH" predecessor.installed_span_sha256)" ] || {
    printf 'Refusing to install: live 0.4.70 padded span differs.\n' >&2
    exit 1
}
P4_OLD_TAIL_NON_FF=$(tail -c +$((P4_PREDECESSOR_SPAN_BYTES + 1)) \
    "$P4_PREIMAGE" | LC_ALL=C tr -d '\377' | wc -c | tr -d ' ')
[ "$P4_OLD_TAIL_NON_FF" = 0 ] || {
    printf 'Refusing to install: successor-only flash span is not erased.\n' >&2
    exit 1
}
P4_PREIMAGE_HASH=$(p4_sha256_file "$P4_PREIMAGE")

python3 - "$P4_LEDGER" "$P4_AUTH_EXPECTED" "$P4_UNIT" \
    "$P4_IDENTITY_EXPECTED" "$P4_PREIMAGE_HASH" \
    "$(p4_artifact_field application sha256)" <<'PY'
import json
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = {
    "schema": 1,
    "authorization_sha256": sys.argv[2],
    "unit_label": sys.argv[3],
    "device_identity_sha256": sys.argv[4],
    "preimage_sha256": sys.argv[5],
    "candidate_sha256": sys.argv[6],
    "install_write_attempt_count": 0,
    "restore_required": True,
    "automatic_rollback_authorized": False,
    "state": "preimage-preserved-write-pending",
}
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY

P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset write_flash --flash_mode dio \
    --flash_size 32MB --flash_freq 80m 0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -35 >&2
    printf 'Write failed; preserved preimage and restore-required ledger remain.\n' >&2
    exit 1
}
printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -25

p4_verify_live_file 0x20000 "$P4_APPLICATION" application
P4_READBACK_BYTES=$(p4_artifact_field application padded_mutation_bytes)
P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset read_flash 0x20000 \
    "$P4_READBACK_BYTES" "$P4_READBACK" 2>&1) || {
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -25 >&2
    exit 1
}
chmod 0400 "$P4_READBACK"
P4_CANDIDATE_BYTES=$(p4_artifact_field application bytes)
P4_CANDIDATE_READBACK_HASH=$(head -c "$P4_CANDIDATE_BYTES" \
    "$P4_READBACK" | shasum -a 256 | awk '{print $1}')
[ "$P4_CANDIDATE_READBACK_HASH" = \
    "$(p4_artifact_field application sha256)" ] || {
    printf 'Application readback digest differs.\n' >&2
    exit 1
}
P4_TAIL_NON_FF=$(tail -c +$((P4_CANDIDATE_BYTES + 1)) "$P4_READBACK" | \
    LC_ALL=C tr -d '\377' | wc -c | tr -d ' ')
[ "$P4_TAIL_NON_FF" = 0 ] || {
    printf 'Application sector padding is not erased.\n' >&2
    exit 1
}
P4_PADDED_HASH=$(p4_sha256_file "$P4_READBACK")

python3 - "$P4_LEDGER" "$P4_PADDED_HASH" <<'PY'
import json
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
data.update({
    "padded_readback_sha256": sys.argv[2],
    "install_write_attempt_count": 1,
    "restore_required": False,
    "state": "installed-verify-pass-awaiting-retained-uart",
})
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY

printf 'Waveshare Console OS 0.4.73 app-only install PASS for %s.\n' \
    "$P4_UNIT"
printf 'Device remains in loader for retained-UART reset; LORD.P4G was not pushed.\n'
printf 'preimage_sha256=%s padded_readback_sha256=%s\n' \
    "$P4_PREIMAGE_HASH" "$P4_PADDED_HASH"
