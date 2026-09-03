#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.99-highres-20260902-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=a4ea0a2da1fee51867961b50db301912f1d47b260a4945ed3fc2dd92c73277bc
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.99-highres-20260902-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=8c13c163497fe6948567574f9318a8fa46e9c641c219fcb8c17ee7a0dee8e918
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.99-highres-8ea4ee9"

P4_UNIT=
case " $* " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
    *" --unit unit2 "*) P4_UNIT=unit2 ;;
    *)
        printf 'This exact-artifact route requires --unit unit1 or --unit unit2.\n' >&2
        exit 2
        ;;
esac

export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.99-highres-install-8ea4ee9"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.99-highres-install-lock-8ea4ee9"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.98-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.99-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
