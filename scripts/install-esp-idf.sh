#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

P4_INSTALL_PATH=${P4_IDF_PATH:-"$P4_PROJECT_ROOT/.tools/esp-idf-v$P4_PINNED_IDF_VERSION"}
P4_IDF_TAG="$P4_PINNED_IDF_TAG"

if [ ! -f "$P4_INSTALL_PATH/export.sh" ]; then
    if [ -e "$P4_INSTALL_PATH" ] && \
       [ -n "$(find "$P4_INSTALL_PATH" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
        printf 'Refusing to replace incomplete/non-IDF directory: %s\n' "$P4_INSTALL_PATH" >&2
        exit 1
    fi
    mkdir -p "$(dirname -- "$P4_INSTALL_PATH")"
    git clone --branch "$P4_IDF_TAG" --recursive \
        https://github.com/espressif/esp-idf.git "$P4_INSTALL_PATH"
fi

P4_ACTUAL_TAG=$(git -C "$P4_INSTALL_PATH" describe --tags --exact-match 2>/dev/null || true)
if [ "$P4_ACTUAL_TAG" != "$P4_IDF_TAG" ]; then
    printf 'Refusing SDK mismatch: expected %s, found %s at %s\n' \
        "$P4_IDF_TAG" "${P4_ACTUAL_TAG:-unknown}" "$P4_INSTALL_PATH" >&2
    exit 1
fi

git -C "$P4_INSTALL_PATH" submodule update --init --recursive

case "$P4_PINNED_IDF_COMMIT" in
    ''|pending-*) ;;
    *)
        P4_ACTUAL_COMMIT=$(git -C "$P4_INSTALL_PATH" rev-parse HEAD)
        if [ "$P4_ACTUAL_COMMIT" != "$P4_PINNED_IDF_COMMIT" ]; then
            printf 'Refusing SDK commit mismatch: expected %s, found %s at %s\n' \
                "$P4_PINNED_IDF_COMMIT" "$P4_ACTUAL_COMMIT" "$P4_INSTALL_PATH" >&2
            exit 1
        fi
        ;;
esac

"$P4_INSTALL_PATH/install.sh" esp32p4
printf 'Installed ESP-IDF %s at %s\n' "$P4_PINNED_IDF_VERSION" "$P4_INSTALL_PATH"
