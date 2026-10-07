#!/bin/sh
# SPDX-License-Identifier: MIT
# Keep runtime/source inputs; fetch original art later with sparse-checkout disable.
set -eu

if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
    printf 'Usage: %s <repository-url> <new-directory> [branch-or-tag]\n' "$0" >&2
    exit 2
fi

p4_repository=$1
p4_destination=$2
if [ -e "$p4_destination" ] || [ -L "$p4_destination" ]; then
    printf 'Destination already exists: %s\n' "$p4_destination" >&2
    exit 2
fi

if [ "$#" -eq 3 ]; then
    git clone --depth 1 --filter=blob:none --no-checkout --no-tags --no-local \
        --branch "$3" -- "$p4_repository" "$p4_destination"
else
    git clone --depth 1 --filter=blob:none --no-checkout --no-tags --no-local \
        -- "$p4_repository" "$p4_destination"
fi

# Non-cone patterns are required: entire assets/ directories contain runtime
# icons/resources, and disabled games include protected-lineage build inputs.
git -C "$p4_destination" sparse-checkout set --no-cone --stdin <<'P4_SPARSE'
/*
!/games/*/assets/*.[pP][nN][gG]
!/games/*/assets/**/*.[pP][nN][gG]
!/design/*.[pP][nN][gG]
!/design/**/*.[pP][nN][gG]
!/design/*.[gG][iI][fF]
!/design/**/*.[gG][iI][fF]
P4_SPARSE
git -C "$p4_destination" checkout
printf '\nLite source ready: %s\n' "$p4_destination"
printf 'Original art remains in Git; restore it with git sparse-checkout disable.\n'
printf 'This clone has shallow history; use git fetch --unshallow when needed.\n'
