#!/bin/sh

set -eu

case " $* " in
    *" --unit unit1 "*)
        printf 'This exact diagnostic successor is authorized only for unit2.\n' >&2
        exit 2
        ;;
esac

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.86-activation-diag-20260827-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=b19e84a438e14f5ea3e224294d943385a3decf1969542f60320c970275c67b3a
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-unit2-console-os-0.4.86-activation-diag-cc4336b"
export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-unit2-console-os-0.4.86-install-cc4336b"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-unit2-console-os-0.4.86-install-lock-cc4336b"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.85-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.86-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
