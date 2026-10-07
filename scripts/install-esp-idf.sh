#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

P4_INSTALL_PATH=$(p4_find_idf_path) || \
    P4_INSTALL_PATH="$P4_PROJECT_ROOT/.tools/esp-idf-v$P4_PINNED_IDF_VERSION"
P4_IDF_TAG="$P4_PINNED_IDF_TAG"

if [ ! -f "$P4_INSTALL_PATH/export.sh" ]; then
    if [ -e "$P4_INSTALL_PATH" ] && \
       [ -n "$(find "$P4_INSTALL_PATH" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
        printf 'Refusing to replace incomplete/non-IDF directory: %s\n' "$P4_INSTALL_PATH" >&2
        exit 1
    fi
    mkdir -p "$(dirname -- "$P4_INSTALL_PATH")"
    git clone --branch "$P4_IDF_TAG" --depth 1 --recursive \
        --shallow-submodules --jobs 4 \
        https://github.com/espressif/esp-idf.git "$P4_INSTALL_PATH"
fi

# Check identity and tracked files before updating a reused SDK. Setup may
# initialize missing submodules, but must finish with every pinned gitlink.
p4_verify_idf_checkout "$P4_INSTALL_PATH" --allow-missing-submodules
git -C "$P4_INSTALL_PATH" submodule update --init --recursive --depth 1 --jobs 4
p4_verify_idf_checkout "$P4_INSTALL_PATH"

p4_configure_idf_tools_path
"$P4_INSTALL_PATH/install.sh" esp32p4
printf 'Installed ESP-IDF %s at %s\n' "$P4_PINNED_IDF_VERSION" "$P4_INSTALL_PATH"
