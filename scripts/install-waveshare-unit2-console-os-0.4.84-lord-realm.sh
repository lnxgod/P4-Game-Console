#!/bin/sh

set -eu

P4_MODE=${1:-}
[ "$#" -gt 0 ] && shift
P4_PORT=
P4_UNIT=unit2
while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) P4_PORT=${2:-}; shift 2 ;;
        --unit) P4_UNIT=${2:-}; shift 2 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done

case "$P4_MODE" in
    check) [ -z "$P4_PORT" ] || { printf 'check does not accept --port.\n' >&2; exit 2; } ;;
    install|recover|revert|verify) ;;
    *) printf 'Use: %s check [--unit unit1|unit2] | install [--unit unit1|unit2] --port PORT | recover [--unit unit1|unit2] --port PORT | revert [--unit unit1|unit2] --port PORT | verify [--unit unit1|unit2] --port PORT\n' "$0" >&2; exit 2 ;;
esac
[ "$P4_UNIT" = unit1 ] || [ "$P4_UNIT" = unit2 ] || {
    printf 'Use --unit unit1 or --unit unit2.\n' >&2
    exit 2
}

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

if [ "$P4_UNIT" = unit1 ]; then
    P4_AUTH=${P4_INSTALL_AUTH_UNIT1:-"$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.84-lord-realm-20260827-exact-unit-authorization.json"}
    P4_AUTH_EXPECTED=${P4_INSTALL_AUTH_UNIT1_SHA256:-4e1b27796abd6abf1b644c52449f3dce25193e409fa4188fa95c6211908120d6}
else
    P4_AUTH=${P4_INSTALL_AUTH_UNIT2:-"$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.84-lord-realm-20260826-exact-unit-authorization.json"}
    P4_AUTH_EXPECTED=${P4_INSTALL_AUTH_UNIT2_SHA256:-a50c06df5d772c850531ec6e1e840815d52eea43fcfe15192af6bf77376cf208}
fi
P4_RELEASE=${P4_INSTALL_RELEASE:-"$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.84-lord-realm-af784d8"}
P4_RECOVERY=${P4_INSTALL_RECOVERY:-"$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.84-install-af784d8"}
P4_LOCK_DIR=${P4_INSTALL_LOCK_DIR:-"$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.84-install-lock"}
P4_PREIMAGE=${P4_INSTALL_PREIMAGE:-"$P4_RECOVERY/preimage-0.4.83-span.bin"}
P4_READBACK=${P4_INSTALL_READBACK:-"$P4_RECOVERY/readback-0.4.84-span.bin"}
P4_LEDGER="$P4_RECOVERY/ledger.json"
P4_MUTATION_STARTED=0
P4_LOCK_HELD=0

p4_json_field() {
    python3 -c '
import json, sys
value = json.load(open(sys.argv[1], encoding="utf-8"))
for key in sys.argv[2].split("."):
    value = value[int(key)] if isinstance(value, list) else value[key]
print(str(value).lower() if isinstance(value, bool) else value)
' "$1" "$2"
}

p4_ledger_field() {
    p4_json_field "$P4_LEDGER" "$1"
}

P4_CANDIDATE_VERSION=$(p4_json_field "$P4_AUTH" candidate.version)
P4_PREDECESSOR_VERSION=$(p4_json_field "$P4_AUTH" predecessor.version)

p4_relative_file() {
    printf '%s/%s\n' "$P4_PROJECT_ROOT" "$(p4_json_field "$P4_AUTH" "$1.path")"
}

p4_check_file() {
    P4_CHECK_PATH=$1
    P4_CHECK_BYTES=$2
    P4_CHECK_HASH=$3
    P4_CHECK_LABEL=$4
    [ -f "$P4_CHECK_PATH" ] || {
        printf 'Refusing to install: %s is missing.\n' "$P4_CHECK_LABEL" >&2
        exit 1
    }
    [ "$(wc -c < "$P4_CHECK_PATH" | tr -d ' ')" = "$P4_CHECK_BYTES" ] || {
        printf 'Refusing to install: %s byte count changed.\n' "$P4_CHECK_LABEL" >&2
        exit 1
    }
    [ "$(p4_sha256_file "$P4_CHECK_PATH")" = "$P4_CHECK_HASH" ] || {
        printf 'Refusing to install: %s digest changed.\n' "$P4_CHECK_LABEL" >&2
        exit 1
    }
}

p4_check_sealed_artifact() {
    P4_ARTIFACT_KEY=$1
    P4_ARTIFACT_LABEL=$2
    P4_ARTIFACT=$(p4_relative_file "$P4_ARTIFACT_KEY")
    p4_check_file "$P4_ARTIFACT" \
        "$(p4_json_field "$P4_AUTH" "$P4_ARTIFACT_KEY.bytes")" \
        "$(p4_json_field "$P4_AUTH" "$P4_ARTIFACT_KEY.sha256")" \
        "$P4_ARTIFACT_LABEL"
    [ "$(stat -f '%Lp' "$P4_ARTIFACT")" = 400 ] || {
        printf 'Refusing to install: %s is not sealed mode 0400.\n' \
            "$P4_ARTIFACT_LABEL" >&2
        exit 1
    }
}

p4_validate_local_authorization() {
    [ "$(p4_sha256_file "$P4_AUTH")" = "$P4_AUTH_EXPECTED" ] || {
        printf 'Refusing to install: exact-unit authorization changed.\n' >&2
        exit 1
    }
    [ "$(p4_json_field "$P4_AUTH" active)" = true ] || {
        printf 'Refusing to install: authorization is inactive.\n' >&2
        exit 1
    }
    [ "$(p4_json_field "$P4_AUTH" safety.generic_idf_flash_authorized)" = false ]
    [ "$(p4_json_field "$P4_AUTH" safety.full_project_flash_authorized)" = false ]
    [ "$(p4_json_field "$P4_AUTH" safety.erase_authorized)" = false ]
    [ "$(p4_json_field "$P4_AUTH" safety.automatic_rollback_authorized)" = true ]
    [ -d "$P4_RELEASE" ] || {
        printf 'Refusing to install: frozen release directory is missing.\n' >&2
        exit 1
    }
    git -C "$P4_PROJECT_ROOT" merge-base --is-ancestor \
        "$(p4_json_field "$P4_AUTH" source.commit)" HEAD || {
        printf 'Refusing to install: authorized source is not an ancestor.\n' >&2
        exit 1
    }

    P4_FACTORY=$(p4_relative_file factory_backup)
    p4_check_file "$P4_FACTORY" \
        "$(p4_json_field "$P4_AUTH" factory_backup.bytes)" \
        "$(p4_json_field "$P4_AUTH" factory_backup.sha256)" \
        factory-backup

    p4_check_sealed_artifact installed_layout.bootloader bootloader
    p4_check_sealed_artifact installed_layout.partition_table partition-table
    p4_check_sealed_artifact candidate.application application
    p4_check_sealed_artifact candidate.elf application-elf
    p4_check_sealed_artifact candidate.update_package update-package
    p4_check_sealed_artifact candidate.sdkconfig sdkconfig
    p4_check_sealed_artifact candidate.dependencies_lock dependency-lock
    p4_check_sealed_artifact candidate.lord_cartridge lord-cartridge

    python3 - "$P4_PROJECT_ROOT/hardware/backups/manifest.json" "$P4_AUTH" <<'PY'
import json
import pathlib
import sys

manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
authorization = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
identity = authorization["board"]["device_identity_sha256"]
records = [manifest] + manifest.get("additional_devices", [])
matches = [
    record for record in records
    if record.get("device", {}).get("identity", {}).get("sha256") == identity
]
if len(matches) != 1:
    raise SystemExit("factory backup manifest binding is not unique")
actual = matches[0].get("backup", {})
expected = authorization["factory_backup"]
for auth_key, manifest_key in (("path", "file"), ("bytes", "bytes"),
                               ("sha256", "sha256")):
    if actual.get(manifest_key) != expected[auth_key]:
        raise SystemExit("factory backup manifest differs for " + auth_key)
if matches[0].get("device", {}).get("flash_bytes") != authorization["board"]["flash_bytes"]:
    raise SystemExit("factory backup flash geometry differs")
PY
}

p4_verify_live_file() {
    P4_VERIFY_OFFSET=$1
    P4_VERIFY_FILE=$2
    P4_VERIFY_LABEL=$3
    P4_VERIFY=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
        --before no_reset --after no_reset verify_flash \
        "$P4_VERIFY_OFFSET" "$P4_VERIFY_FILE" 2>&1) || {
        printf '%s\n' "$P4_VERIFY" | p4_redact_device_identifiers | tail -25 >&2
        printf 'Live %s verification failed.\n' "$P4_VERIFY_LABEL" >&2
        return 1
    }
    printf '%s PASS\n' "$P4_VERIFY_LABEL"
}

p4_device_preflight() {
    p4_require_port "$P4_PORT"
    p4_activate_idf
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
        printf 'Refusing to install: live unit is not recorded %s.\n' "$P4_UNIT" >&2
        exit 1
    }
    P4_SECURITY=$(esptool.py --chip esp32p4 --port "$P4_PORT" \
        --before no_reset --after no_reset get_security_info 2>&1) || exit 1
    printf '%s\n' "$P4_SECURITY" | grep -F 'Secure Boot: Disabled' >/dev/null
    printf '%s\n' "$P4_SECURITY" | grep -F 'Flash Encryption: Disabled' >/dev/null

    P4_BOOTLOADER=$(p4_relative_file installed_layout.bootloader)
    P4_PARTITIONS=$(p4_relative_file installed_layout.partition_table)
    p4_verify_live_file 0x2000 "$P4_BOOTLOADER" bootloader
    p4_verify_live_file 0x8000 "$P4_PARTITIONS" partition-table
}

p4_update_ledger_restore_result() {
    P4_RESTORE_HASH=$1
    P4_RESTORE_STATE=$2
    python3 - "$P4_LEDGER" "$P4_RESTORE_HASH" "$P4_RESTORE_STATE" <<'PY'
import json
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
data.update({
    "restored_preimage_sha256": sys.argv[2],
    "restore_required": False,
    "state": sys.argv[3],
})
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY
}

p4_restore_preimage() {
    P4_RESTORE_STATE=${1:-restored-predecessor-after-failed-successor}
    [ -f "$P4_LEDGER" ] && [ -f "$P4_PREIMAGE" ] || return 1
    P4_EXPECTED_PREIMAGE=$(p4_ledger_field preimage_sha256)
    [ "$(p4_sha256_file "$P4_PREIMAGE")" = "$P4_EXPECTED_PREIMAGE" ] || {
        printf 'Recovery preimage digest differs; refusing restore.\n' >&2
        return 1
    }
    printf 'Restoring the sealed %s application span...\n' \
        "$P4_PREDECESSOR_VERSION" >&2
    P4_RESTORE=$(esptool.py --chip esp32p4 --port "$P4_PORT" --baud 460800 \
        --before no_reset --after no_reset write_flash --flash_mode dio \
        --flash_size 32MB --flash_freq 80m 0x20000 "$P4_PREIMAGE" 2>&1) || {
        printf '%s\n' "$P4_RESTORE" | p4_redact_device_identifiers | tail -35 >&2
        return 1
    }
    p4_verify_live_file 0x20000 "$P4_PREIMAGE" restored-preimage || return 1
    p4_update_ledger_restore_result "$P4_EXPECTED_PREIMAGE" \
        "$P4_RESTORE_STATE"
    printf 'Exact predecessor restore PASS; device remains in loader.\n' >&2
}

p4_release_lock() {
    if [ "$P4_LOCK_HELD" = 1 ]; then
        rmdir "$P4_LOCK_DIR" 2>/dev/null || true
        P4_LOCK_HELD=0
    fi
}

p4_on_exit() {
    P4_STATUS=$?
    trap - EXIT HUP INT TERM
    if [ "$P4_STATUS" -ne 0 ] && [ "$P4_MUTATION_STARTED" = 1 ]; then
        if p4_restore_preimage; then
            P4_MUTATION_STARTED=0
        else
            printf 'Automatic restore failed; ledger remains restore_required=true.\n' >&2
        fi
    fi
    p4_release_lock
    exit "$P4_STATUS"
}

p4_validate_local_authorization

if [ "$P4_MODE" = check ]; then
    [ ! -e "$P4_RECOVERY" ] || {
        printf 'Recovery directory already exists; inspect its ledger before another install.\n' >&2
        exit 1
    }
    printf 'Console OS %s %s exact-artifact checks PASS; no UART opened and no write occurred.\n' \
        "$P4_CANDIDATE_VERSION" "$P4_UNIT"
    exit 0
fi

[ "$P4_PORT" = "$(p4_json_field "$P4_AUTH" board.recorded_port)" ] || {
    printf 'Note: serial path changed; exact hashed identity remains authoritative.\n'
}
[ ! -e "$P4_LOCK_DIR" ] || {
    printf 'Refusing to continue: another %s route is active.\n' \
        "$P4_CANDIDATE_VERSION" >&2
    exit 1
}
mkdir -m 700 "$P4_LOCK_DIR"
P4_LOCK_HELD=1
trap p4_on_exit EXIT
trap 'exit 130' HUP INT TERM

p4_device_preflight

if [ "$P4_MODE" = verify ]; then
    P4_APPLICATION=$(p4_relative_file candidate.application)
    p4_verify_live_file 0x20000 "$P4_APPLICATION" application
    printf 'Console OS %s %s installed-successor verification PASS; no write occurred.\n' \
        "$P4_CANDIDATE_VERSION" "$P4_UNIT"
    p4_release_lock
    trap - EXIT HUP INT TERM
    exit 0
fi

if [ "$P4_MODE" = recover ] || [ "$P4_MODE" = revert ]; then
    [ -f "$P4_LEDGER" ] || {
        printf 'No %s recovery ledger exists.\n' "$P4_CANDIDATE_VERSION" >&2
        exit 1
    }
    [ "$(p4_ledger_field authorization_sha256)" = "$P4_AUTH_EXPECTED" ] || {
        printf 'Recovery ledger authorization differs.\n' >&2
        exit 1
    }
    [ "$(p4_ledger_field device_identity_sha256)" = \
        "$(p4_json_field "$P4_AUTH" board.device_identity_sha256)" ] || {
        printf 'Recovery ledger unit binding differs.\n' >&2
        exit 1
    }
    if [ "$P4_MODE" = recover ]; then
        [ "$(p4_ledger_field restore_required)" = true ] || {
            printf 'Recovery is not required by the durable ledger.\n' >&2
            exit 1
        }
        p4_restore_preimage
    else
        [ "$(p4_ledger_field restore_required)" = false ] || {
            printf 'Use recover while the durable ledger requires restoration.\n' >&2
            exit 1
        }
        [ "$(p4_ledger_field state)" = \
            installed-readback-pass-awaiting-retained-uart ] || {
            printf 'Refusing explicit revert from an unexpected ledger state.\n' >&2
            exit 1
        }
        P4_APPLICATION=$(p4_relative_file candidate.application)
        p4_verify_live_file 0x20000 "$P4_APPLICATION" rejected-successor
        p4_restore_preimage restored-predecessor-after-rejected-successor
    fi
    p4_release_lock
    trap - EXIT HUP INT TERM
    exit 0
fi

[ ! -e "$P4_RECOVERY" ] || {
    printf 'Refusing to install: recovery evidence already exists; inspect its ledger.\n' >&2
    exit 1
}
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
    printf 'Refusing to install: live predecessor is not Console OS %s.\n' \
        "$P4_PREDECESSOR_VERSION" >&2
    exit 1
}
P4_OLD_TAIL_NON_FF=$(tail -c +$((P4_PREDECESSOR_BYTES + 1)) "$P4_PREIMAGE" | \
    LC_ALL=C tr -d '\377' | wc -c | tr -d ' ')
[ "$P4_OLD_TAIL_NON_FF" = 0 ] || {
    printf 'Refusing to install: predecessor sector padding is not erased.\n' >&2
    exit 1
}
P4_PREIMAGE_HASH=$(p4_sha256_file "$P4_PREIMAGE")

python3 - "$P4_LEDGER" "$P4_AUTH_EXPECTED" \
    "$(p4_json_field "$P4_AUTH" board.device_identity_sha256)" \
    "$P4_PREIMAGE_HASH" \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" <<'PY'
import json
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = {
    "schema": 1,
    "authorization_sha256": sys.argv[2],
    "device_identity_sha256": sys.argv[3],
    "preimage_sha256": sys.argv[4],
    "candidate_sha256": sys.argv[5],
    "install_write_attempt_count": 0,
    "restore_required": True,
    "automatic_rollback_authorized": True,
    "state": "preimage-preserved-write-pending",
}
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY

python3 - "$P4_LEDGER" <<'PY'
import json
import os
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text(encoding="utf-8"))
data.update({"install_write_attempt_count": 1, "state": "successor-write-started"})
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY
P4_MUTATION_STARTED=1

P4_APPLICATION=$(p4_relative_file candidate.application)
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
chmod 0400 "$P4_READBACK"
P4_CANDIDATE_BYTES=$(p4_json_field "$P4_AUTH" candidate.application.bytes)
P4_CANDIDATE_READBACK_HASH=$(head -c "$P4_CANDIDATE_BYTES" "$P4_READBACK" | \
    shasum -a 256 | awk '{print $1}')
[ "$P4_CANDIDATE_READBACK_HASH" = \
    "$(p4_json_field "$P4_AUTH" candidate.application.sha256)" ] || {
    printf 'Application readback digest differs.\n' >&2
    exit 1
}
P4_NEW_TAIL_NON_FF=$(tail -c +$((P4_CANDIDATE_BYTES + 1)) "$P4_READBACK" | \
    LC_ALL=C tr -d '\377' | wc -c | tr -d ' ')
[ "$P4_NEW_TAIL_NON_FF" = 0 ] || {
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
    "restore_required": False,
    "state": "installed-readback-pass-awaiting-retained-uart",
})
temporary = path.with_suffix(".tmp")
temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
os.chmod(temporary, 0o600)
os.replace(temporary, path)
PY
P4_MUTATION_STARTED=0

printf 'Console OS %s %s app-only install/readback PASS.\n' \
    "$P4_CANDIDATE_VERSION" "$P4_UNIT"
printf 'Device remains in loader for retained-UART launch. LORD.P4G was not yet changed.\n'
printf 'preimage_sha256=%s padded_readback_sha256=%s\n' \
    "$P4_PREIMAGE_HASH" "$P4_PADDED_HASH"
p4_release_lock
trap - EXIT HUP INT TERM
