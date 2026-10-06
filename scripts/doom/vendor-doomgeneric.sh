#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

P4_DG_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
P4_DG_ROOT=$(CDPATH= cd -- "$P4_DG_SCRIPT_DIR/../.." && pwd)
P4_DG_LOCK="$P4_DG_ROOT/third_party/source-lock.json"
P4_DG_SOURCE_ARG=${1:-}

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

P4_DG_REPOSITORY=$(p4_dg_lock_value repository)
P4_DG_COMMIT=$(p4_dg_lock_value commit)
P4_DG_TREE=$(p4_dg_lock_value tree)
P4_DG_VENDOR_REL=$(p4_dg_lock_value vendor_path)
P4_DG_MANIFEST_REL=$(p4_dg_lock_value vendor_manifest)
P4_DG_MANIFEST_SHA256=$(p4_dg_lock_value vendor_manifest_sha256)

case "$P4_DG_VENDOR_REL:$P4_DG_MANIFEST_REL" in
    third_party/*:third_party/*) ;;
    *) echo "doomgeneric vendor paths must remain under third_party" >&2; exit 1 ;;
esac
case "$P4_DG_VENDOR_REL:$P4_DG_MANIFEST_REL" in
    *..*) echo "doomgeneric vendor paths may not contain '..'" >&2; exit 1 ;;
esac

P4_DG_VENDOR="$P4_DG_ROOT/$P4_DG_VENDOR_REL"
P4_DG_MANIFEST="$P4_DG_ROOT/$P4_DG_MANIFEST_REL"
P4_DG_TEMP_BASE=${TMPDIR:-/tmp}
P4_DG_TEMP_BASE=${P4_DG_TEMP_BASE%/}
P4_DG_TEMP_DIR=$(mktemp -d "$P4_DG_TEMP_BASE/p4-doom-vendor.XXXXXX")
case "$P4_DG_TEMP_DIR" in
    "$P4_DG_TEMP_BASE"/p4-doom-vendor.*) ;;
    *) echo "refusing unsafe temporary directory: $P4_DG_TEMP_DIR" >&2; exit 1 ;;
esac
trap 'rm -rf "$P4_DG_TEMP_DIR"' EXIT HUP INT TERM

if [ -n "$P4_DG_SOURCE_ARG" ]; then
    P4_DG_SOURCE=$(CDPATH= cd -- "$P4_DG_SOURCE_ARG" && pwd)
else
    P4_DG_SOURCE="$P4_DG_TEMP_DIR/source"
    git clone --filter=blob:none --no-checkout "$P4_DG_REPOSITORY" "$P4_DG_SOURCE"
    git -C "$P4_DG_SOURCE" checkout --detach "$P4_DG_COMMIT"
fi

if [ "$(git -C "$P4_DG_SOURCE" rev-parse HEAD)" != "$P4_DG_COMMIT" ]; then
    echo "source checkout is not at locked commit $P4_DG_COMMIT" >&2
    exit 1
fi
if [ "$(git -C "$P4_DG_SOURCE" rev-parse HEAD^{tree})" != "$P4_DG_TREE" ]; then
    echo "source checkout root tree does not match source-lock.json" >&2
    exit 1
fi
if [ -n "$(git -C "$P4_DG_SOURCE" status --porcelain --untracked-files=all)" ]; then
    echo "source checkout is dirty; refusing to vendor" >&2
    exit 1
fi

mkdir -p "$P4_DG_TEMP_DIR/stage"
git -C "$P4_DG_SOURCE" archive "$P4_DG_COMMIT" \
    LICENSE README.TXT README.md doomgeneric | tar -xf - -C "$P4_DG_TEMP_DIR/stage"
git -C "$P4_DG_SOURCE" ls-tree -r "$P4_DG_COMMIT" \
    LICENSE README.TXT README.md doomgeneric >"$P4_DG_TEMP_DIR/manifest"
P4_DG_GENERATED_MANIFEST_SHA256=$(python3 - "$P4_DG_TEMP_DIR/manifest" <<'PY'
import hashlib
import pathlib
import sys

print(hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest())
PY
)
if [ "$P4_DG_GENERATED_MANIFEST_SHA256" != "$P4_DG_MANIFEST_SHA256" ]; then
    echo "generated vendor manifest does not match source-lock.json" >&2
    exit 1
fi

python3 "$P4_DG_SCRIPT_DIR/verify-source-tree.py" "$P4_DG_ROOT" \
    --apply-to "$P4_DG_TEMP_DIR/stage"

if [ -e "$P4_DG_VENDOR" ]; then
    if ! diff -qr "$P4_DG_TEMP_DIR/stage" "$P4_DG_VENDOR" >/dev/null; then
        echo "existing vendor tree diverges; inspect it instead of overwriting it" >&2
        exit 1
    fi
else
    cp -R "$P4_DG_TEMP_DIR/stage" "$P4_DG_VENDOR"
fi

if [ -e "$P4_DG_MANIFEST" ]; then
    if ! cmp -s "$P4_DG_TEMP_DIR/manifest" "$P4_DG_MANIFEST"; then
        echo "existing vendor manifest diverges; inspect it instead of overwriting it" >&2
        exit 1
    fi
else
    cp "$P4_DG_TEMP_DIR/manifest" "$P4_DG_MANIFEST"
fi

"$P4_DG_SCRIPT_DIR/verify-doomgeneric.sh"
echo "doomgeneric vendor copy matches locked upstream plus the pinned P4 patch"
