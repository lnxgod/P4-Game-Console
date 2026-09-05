#!/bin/sh

set -eu

P4_SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# shellcheck disable=SC1091
. "$P4_SCRIPT_DIR/lib/project-env.sh"

export P4_INSTALL_AUTH_UNIT1="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit1-console-os-0.4.101-catalog-psram-20260904-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT1_SHA256=16f036205046d49c9e0add2d0c9d0024f01158c5d31d39fecbe8aa88b40b9bde
export P4_INSTALL_AUTH_UNIT2="$P4_PROJECT_ROOT/hardware/evidence/waveshare-unit2-console-os-0.4.101-catalog-psram-20260904-exact-unit-authorization.json"
export P4_INSTALL_AUTH_UNIT2_SHA256=274a4258411e66f7484f35378f3782f57d109fe88f1a823fc00cc40ede5cc1f7
export P4_INSTALL_RELEASE="$P4_PROJECT_ROOT/hardware/local-state/waveshare-two-unit-console-os-0.4.101-catalog-psram-25945b5"

P4_UNIT=
case " $* " in
    *" --unit unit1 "*) P4_UNIT=unit1 ;;
    *" --unit unit2 "*) P4_UNIT=unit2 ;;
    *)
        printf 'This exact-artifact route requires --unit unit1 or --unit unit2.\n' >&2
        exit 2
        ;;
esac

export P4_INSTALL_RECOVERY="$P4_PROJECT_ROOT/hardware/local-state/waveshare-$P4_UNIT-console-os-0.4.101-catalog-psram-install-25945b5"
export P4_INSTALL_LOCK_DIR="$P4_PROJECT_ROOT/hardware/local-state/.waveshare-$P4_UNIT-console-os-0.4.101-catalog-psram-install-lock-25945b5"
export P4_INSTALL_PREIMAGE="$P4_INSTALL_RECOVERY/preimage-0.4.100-span.bin"
export P4_INSTALL_READBACK="$P4_INSTALL_RECOVERY/readback-0.4.101-span.bin"

exec "$P4_SCRIPT_DIR/install-waveshare-unit2-console-os-0.4.84-lord-realm.sh" "$@"
