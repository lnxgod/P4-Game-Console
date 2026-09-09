#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.5.12-main-20260909-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=84d158f4316de8469b6f57ecc94854cb053a3cda07a8295e0834416be083a99a
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.5.12-main-20260909-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=593678d374d0f46fec497fb460db92dc22f4c6381eba20a25b8504bde6d7bc86
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.5.12-main-85496e0-1b969c8"

P4_UNIT=
case " $* " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
    *" --unit unit2 "*) P4_UNIT=unit2 ;;
    *)
        printf 'This exact-artifact route requires --unit unit1 or --unit unit2.\n' >&2
        exit 2
        ;;
esac

export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.5.12-main-install-1b969c8"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.5.12-main-install-lock-1b969c8"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.101-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.5.12-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
