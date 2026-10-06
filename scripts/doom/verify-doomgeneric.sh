#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

P4_DG_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_DG_ROOT=$(CDPATH= cd -- "$P4_DG_SCRIPT_DIR/../.." && pwd)
P4_DG_LOCK="$P4_DG_ROOT/third_party/source-lock.json"

p4_dg_lock_value()
{
    python3 - "$P4_DG_LOCK" "$1" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as lock_file:
    value = json.load(lock_file)["sources"]["doomgeneric"][sys.argv[2]]
if not isinstance(value, str) or not value:
    raise SystemExit(f"invalid doomgeneric lock value: {sys.argv[2]}")
print(value)
PY
}

P4_DG_COMMIT=$(p4_dg_lock_value commit)
P4_DG_TREE=$(p4_dg_lock_value tree)
P4_DG_VENDOR_REL=$(p4_dg_lock_value vendor_path)
P4_DG_MANIFEST_REL=$(p4_dg_lock_value vendor_manifest)
P4_DG_MANIFEST_SHA256=$(p4_dg_lock_value vendor_manifest_sha256)

if ! printf '%s\n' "$P4_DG_COMMIT:$P4_DG_TREE" |
     grep -Eq '^[0-9a-f]{40}:[0-9a-f]{40}$'; then
    echo "invalid doomgeneric commit/tree identity in source-lock.json" >&2
    exit 1
fi
if ! printf '%s\n' "$P4_DG_MANIFEST_SHA256" | grep -Eq '^[0-9a-f]{64}$'; then
    echo "invalid doomgeneric vendor manifest hash in source-lock.json" >&2
    exit 1
fi
case "$P4_DG_VENDOR_REL:$P4_DG_MANIFEST_REL" in
    third_party/*:third_party/*) ;;
    *) echo "doomgeneric vendor paths must remain under third_party" >&2; exit 1 ;;
esac
case "$P4_DG_VENDOR_REL:$P4_DG_MANIFEST_REL" in
    *..*) echo "doomgeneric vendor paths may not contain '..'" >&2; exit 1 ;;
esac

P4_DG_VENDOR="$P4_DG_ROOT/$P4_DG_VENDOR_REL"
P4_DG_MANIFEST="$P4_DG_ROOT/$P4_DG_MANIFEST_REL"

if [ ! -d "$P4_DG_VENDOR" ] || [ ! -f "$P4_DG_MANIFEST" ]; then
    echo "vendored doomgeneric source or manifest is missing" >&2
    exit 1
fi
P4_DG_ACTUAL_MANIFEST_SHA256=$(python3 - "$P4_DG_MANIFEST" <<'PY'
import hashlib
import pathlib
import sys

print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
)
if [ "$P4_DG_ACTUAL_MANIFEST_SHA256" != "$P4_DG_MANIFEST_SHA256" ]; then
    echo "doomgeneric vendor manifest hash does not match source-lock.json" >&2
    exit 1
fi
if [ ! -f "$P4_DG_VENDOR/LICENSE" ] || [ ! -f "$P4_DG_VENDOR/README.md" ] ||
   [ ! -f "$P4_DG_VENDOR/README.TXT" ]; then
    echo "upstream doomgeneric license/readmes are missing" >&2
    exit 1
fi

P4_DG_TEMP_BASE=${TMPDIR:-/tmp}
P4_DG_TEMP_BASE=${P4_DG_TEMP_BASE%/}
P4_DG_TEMP_DIR=$(mktemp -d "$P4_DG_TEMP_BASE/p4-doom-provenance.XXXXXX")
case "$P4_DG_TEMP_DIR" in
    "$P4_DG_TEMP_BASE"/p4-doom-provenance.*) ;;
    *) echo "refusing unsafe temporary directory: $P4_DG_TEMP_DIR" >&2; exit 1 ;;
esac
trap 'rm -rf "$P4_DG_TEMP_DIR"' EXIT HUP INT TERM

python3 "$P4_DG_SCRIPT_DIR/verify-source-tree.py" "$P4_DG_ROOT"

P4_DG_TRACKED_WADS=$(git -C "$P4_DG_ROOT" ls-files | awk 'tolower($0) ~ /\.wad$/ { print }')
if [ -n "$P4_DG_TRACKED_WADS" ]; then
    echo "WAD files must never be tracked:" >&2
    printf '%s\n' "$P4_DG_TRACKED_WADS" >&2
    exit 1
fi

find "$P4_DG_ROOT" -type f -iname '*.wad' -not -path "$P4_DG_ROOT/.git/*" \
    -print >"$P4_DG_TEMP_DIR/local-wads"
while IFS= read -r P4_DG_LOCAL_WAD; do
    if ! git -C "$P4_DG_ROOT" check-ignore -q -- "$P4_DG_LOCAL_WAD"; then
        echo "local WAD is not ignored: $P4_DG_LOCAL_WAD" >&2
        exit 1
    fi
done <"$P4_DG_TEMP_DIR/local-wads"

python3 "$P4_DG_SCRIPT_DIR/verify-metadata.py" "$P4_DG_ROOT"

echo "P4_DOOM_D0 PROVENANCE PASS commit=$P4_DG_COMMIT tree=$P4_DG_TREE files=$(wc -l < "$P4_DG_MANIFEST" | tr -d ' ')"
