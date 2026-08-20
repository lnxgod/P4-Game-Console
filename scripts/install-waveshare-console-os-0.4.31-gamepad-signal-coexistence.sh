#!/bin/sh

set -eu

P4_PORT=
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"
p4_require_port "$P4_PORT"
p4_activate_idf

P4_AUTH="$P4_PROJECT_ROOT/hardware/evidence/waveshare-console-os-0.4.31-gamepad-signal-coexistence-20260819-exact-unit-authorization.json"
P4_AUTH_EXPECTED=870e971602cdc2e7d1b380fa614c4ef75eff30bdf02c140e3659272ff14edaf0
P4_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-console-os-0.4.31-gamepad-signal-coexistence-20260819"
P4_PREIMAGE="$P4_RECOVERY/preimage-0.4.30.bin"
P4_LEDGER="$P4_RECOVERY/ledger.json"
P4_READBACK="$P4_RECOVERY/readback-0.4.31.bin"

[ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
    printf 'Refusing to install: exact-unit authorization changed.\n' >&2
    exit 1
}

p4_json_field() {
    python3 -c '
import json, sys
value = json.load(open(sys.argv[1]))
for key in sys.argv[2].split("."):
    value = value[int(key)] if isinstance(value, list) else value[key]
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

p4_relative_file() {
    printf '%s/%s\n' "$P4_PROJECT_ROOT" "$(p4_json_field "$P4_AUTH" "$1.path")"
}

[ "$(p4_json_field "$P4_AUTH" active)" = true ] || {
    printf 'Refusing to install: authorization is inactive.\n' >&2
    exit 1
}
[ "$(git -C "$P4_PROJECT_ROOT" rev-parse HEAD)" = \
    "$(p4_json_field "$P4_AUTH" candidate.source_base_commit)" ] || {
    printf 'Refusing to install: source base commit differs.\n' >&2
    exit 1
}
[ ! -e "$P4_RECOVERY" ] || {
    printf 'Refusing to install: recovery directory already exists.\n' >&2
    exit 1
}

P4_FACTORY=$(p4_relative_file factory_backup)
P4_BOOTLOADER=$(p4_relative_file installed_layout.bootloader)
P4_PARTITIONS=$(p4_relative_file installed_layout.partition_table)
P4_APPLICATION=$(p4_relative_file candidate.application)
P4_ELF=$(p4_relative_file candidate.elf)
P4_UPDATE=$(p4_relative_file candidate.update_package)
P4_SDKCONFIG=$(p4_relative_file candidate.sdkconfig)
P4_LOCK=$(p4_relative_file candidate.dependencies_lock)

p4_check_file "$P4_FACTORY" \
    "$(p4_json_field "$P4_AUTH" factory_backup.bytes)" \
    "$(p4_json_field "$P4_AUTH" factory_backup.sha256)" factory-backup
p4_check_file "$P4_BOOTLOADER" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.bootloader.sha256)" bootloader
p4_check_file "$P4_PARTITIONS" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.bytes)" \
    "$(p4_json_field "$P4_AUTH" installed_layout.partition_table.sha256)" partition-table
p4_check_file "$P4_APPLICATION" \
    "$(p4_json_field "$P4_AUTH" candidate.application.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" application
p4_check_file "$P4_ELF" \
    "$(p4_json_field "$P4_AUTH" candidate.elf.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.elf.sha256)" application-elf
p4_check_file "$P4_UPDATE" \
    "$(p4_json_field "$P4_AUTH" candidate.update_package.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.update_package.sha256)" update-package
p4_check_file "$P4_SDKCONFIG" \
    "$(p4_json_field "$P4_AUTH" candidate.sdkconfig.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.sdkconfig.sha256)" sdkconfig
p4_check_file "$P4_LOCK" \
    "$(p4_json_field "$P4_AUTH" candidate.dependencies_lock.bytes)" \
    "$(p4_json_field "$P4_AUTH" candidate.dependencies_lock.sha256)" dependency-lock

P4_SOURCE_INDEX=0
while [ "$P4_SOURCE_INDEX" -lt 4 ]; do
    P4_SOURCE_KEY="candidate.source_files.$P4_SOURCE_INDEX"
    p4_check_file "$(p4_relative_file "$P4_SOURCE_KEY")" \
        "$(p4_json_field "$P4_AUTH" "$P4_SOURCE_KEY.bytes")" \
        "$(p4_json_field "$P4_AUTH" "$P4_SOURCE_KEY.sha256")" \
        "source-$P4_SOURCE_INDEX"
    P4_SOURCE_INDEX=$((P4_SOURCE_INDEX + 1))
done

[ "$(stat -f '%Lp' "$P4_APPLICATION")" = 400 ] || {
    printf 'Refusing to install: application artifact is not sealed 0400.\n' >&2
    exit 1
}

python3 "$P4_PROJECT_ROOT/scripts/verify-console-os-waveshare.py" \
    "$P4_PROJECT_ROOT/apps/console_os/build-waveshare-usb-host" >/dev/null

P4_PROBE=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --after no_reset flash_id 2>&1) || {
    printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers >&2
    exit 1
}
printf '%s\n' "$P4_PROBE" | p4_redact_device_identifiers | tail -20
[ "$(printf '%s\n' "$P4_PROBE" | p4_flash_size_bytes_from_esptool_output)" = \
    "$(p4_json_field "$P4_AUTH" board.flash_bytes)" ] || {
    printf 'Refusing to install: live flash geometry differs.\n' >&2
    exit 1
}
[ "$(p4_read_device_identity_hash "$P4_PORT" no_reset no_reset)" = \
    "$(p4_json_field "$P4_AUTH" board.device_identity_sha256)" ] || {
    printf 'Refusing to install: live unit differs.\n' >&2
    exit 1
}

P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
    --before no_reset --after no_reset get_security_info 2>&1) || exit 1
printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null
printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null

p4_verify_live_file() {
    P4_OFFSET=$1
    P4_FILE=$2
    P4_LABEL=$3
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
        --before no_reset --after no_reset verify_flash \
        "$P4_OFFSET" "$P4_FILE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -25 >&2
        printf 'Refusing to install: live %s differs.\n' "$P4_LABEL" >&2
        exit 1
    }
    printf '%s PASS\n' "$P4_LABEL"
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
    printf 'Refusing to install: live 0.4.30 predecessor differs.\n' >&2
    exit 1
}
P4_PREIMAGE_HASH=$(p4_sha256_file "$P4_PREIMAGE")

python3 - "$P4_LEDGER" "$P4_AUTH_EXPECTED" "$P4_PREIMAGE_HASH" \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" <<'PY'
import json, os, pathlib, sys
path = pathlib.Path(sys.argv[1])
data = {
    "schema": 1,
    "authorization_sha256": sys.argv[2],
    "device_identity_sha256": "c9004de451366bc54158d9d1f3504892c068153610a3f31093785827f1de380d",
    "preimage_sha256": sys.argv[3],
    "candidate_sha256": sys.argv[4],
    "install_write_attempt_count": 0,
    "restore_required": True,
    "automatic_rollback_authorized": False,
    "state": "preimage-preserved-write-pending",
}
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY

P4_WRITE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset write_flash --flash_mode dio \
    --flash_size 32MB --flash_freq 80m 0x20000 "$P4_APPLICATION" 2>&1) || {
    printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -35 >&2
    exit 1
}
printf '%s\n' "$P4_WRITE" | p4_redact_device_identifiers | tail -25

p4_verify_live_file 0x20000 "$P4_APPLICATION" application
P4_READBACK_BYTES=$(p4_json_field "$P4_AUTH" candidate.application.padded_mutation_bytes)
P4_READ=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
    --before no_reset --after no_reset read_flash 0x20000 \
    "$P4_READBACK_BYTES" "$P4_READBACK" 2>&1) || {
    printf '%s\n' "$P4_READ" | p4_redact_device_identifiers | tail -25 >&2
    exit 1
}
P4_CANDIDATE_BYTES=$(p4_json_field "$P4_AUTH" candidate.application.bytes)
P4_CANDIDATE_READBACK_HASH=$(head -c "$P4_CANDIDATE_BYTES" "$P4_READBACK" | \
    shasum -a 256 | awk '{print $1}')
[ "$P4_CANDIDATE_READBACK_HASH" = \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" ] || {
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
rm -f -- "$P4_READBACK"

python3 - "$P4_LEDGER" "$P4_PADDED_HASH" <<'PY'
import json, os, pathlib, sys
path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text())
data.update({
    "padded_readback_sha256": sys.argv[2],
    "install_write_attempt_count": 1,
    "restore_required": False,
    "state": "installed-verify-pass-awaiting-retained-uart",
})
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY

printf 'Waveshare Console OS 0.4.31 app-only install PASS; device held in loader for retained-UART reset.\n'
printf 'preimage_sha256=%s padded_readback_sha256=%s\n' \
    "$P4_PREIMAGE_HASH" "$P4_PADDED_HASH"
