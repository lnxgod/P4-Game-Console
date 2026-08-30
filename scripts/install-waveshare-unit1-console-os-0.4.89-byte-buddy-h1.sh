#!/bin/sh

set -eu

case " $* " in
    *" --unit unit2 "*)
        printf 'This exact successor is authorized only for unit1 pink.\n' >&2
        exit 2
        ;;
esac

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.89-byte-buddy-h1-20260830-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=9cb7110ac3f52a98aab742103ae91a0c5115bb37e3fe3a1e2fe23c031f84f058
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-unit1-console-os-0.4.89-byte-buddy-h1-d49a037"
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-unit1-console-os-0.4.89-byte-buddy-h1-install-d49a037"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-unit1-console-os-0.4.89-byte-buddy-h1-install-lock-d49a037"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.85-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.89-span.bin"
export P4_INSTALL_CONTENT_ARTIFACT_KEY=candidate.byte_buddy_cartridge
export P4_INSTALL_CONTENT_ARTIFACT_LABEL=byte-buddy-cartridge
export P4_INSTALL_CONTENT_ARTIFACT_KEY_2=candidate.byte_buddy_resource
export P4_INSTALL_CONTENT_ARTIFACT_LABEL_2=byte-buddy-resource
export P4_INSTALL_CONTENT_PENDING_MESSAGE='Device remains in loader for retained-UART launch. BYTEBUD.P4R and BYTEBUD.P4G were not yet changed.'

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" \
    "$@" --unit unit1
